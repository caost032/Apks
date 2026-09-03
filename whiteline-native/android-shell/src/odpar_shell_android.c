#include "android_native_app_glue.h"
#include "odpar_module.h"

#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "ODPAR-NATIVE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define MAX_TOUCH 16

typedef struct TouchTrack {
    int32_t id;
    int active;
    float x;
    float y;
} TouchTrack;

typedef struct Shell {
    struct android_app *app;
    void *module_so;
    const OdparModuleApi *api;
    void *game;
    int has_window;
    int running;
    TouchTrack touch[MAX_TOUCH];
} Shell;

static void send_simple_event(Shell *s, uint32_t type);

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static TouchTrack *touch_find(Shell *s, int32_t id, int create) {
    int i;
    TouchTrack *free_slot = NULL;
    for (i = 0; i < MAX_TOUCH; ++i) {
        if (s->touch[i].active && s->touch[i].id == id) return &s->touch[i];
        if (!s->touch[i].active && free_slot == NULL) free_slot = &s->touch[i];
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->id = id;
    free_slot->active = 1;
    return free_slot;
}

static void module_event(Shell *s, uint32_t type, int32_t id,
                         float x, float y, float dx, float dy) {
    OdparEvent e;
    int w = s->app->window ? ANativeWindow_getWidth(s->app->window) : 1;
    int h = s->app->window ? ANativeWindow_getHeight(s->app->window) : 1;
    if (!s->api || !s->game || !s->api->event) return;
    memset(&e, 0, sizeof(e));
    e.struct_size = sizeof(e);
    e.type = type;
    e.pointer_id = id;
    e.x01 = w > 0 ? x / (float)w : 0.0f;
    e.y01 = h > 0 ? y / (float)h : 0.0f;
    e.dx01 = w > 0 ? dx / (float)w : 0.0f;
    e.dy01 = h > 0 ? dy / (float)h : 0.0f;
    e.width = (uint32_t)(w > 0 ? w : 1);
    e.height = (uint32_t)(h > 0 ? h : 1);
    s->api->event(s->game, &e);
}

static int32_t on_input(struct android_app *app, AInputEvent *event) {
    Shell *s = (Shell *)app->userData;
    int32_t source;
    int32_t action;
    int32_t masked;
    size_t count;
    size_t i;
    if (!s) return 0;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK &&
            AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN) {
            send_simple_event(s, ODPAR_EVENT_BACK);
            return 1;
        }
        return 0;
    }
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
    source = AInputEvent_getSource(event);
    if ((source & AINPUT_SOURCE_TOUCHSCREEN) != AINPUT_SOURCE_TOUCHSCREEN) return 0;
    action = AMotionEvent_getAction(event);
    masked = action & AMOTION_EVENT_ACTION_MASK;
    count = AMotionEvent_getPointerCount(event);

    if (masked == AMOTION_EVENT_ACTION_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        size_t index = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        int32_t id = AMotionEvent_getPointerId(event, index);
        float x = AMotionEvent_getX(event, index), y = AMotionEvent_getY(event, index);
        TouchTrack *t = touch_find(s, id, 1);
        if (t) { t->x = x; t->y = y; }
        module_event(s, ODPAR_EVENT_POINTER_DOWN, id, x, y, 0.0f, 0.0f);
        return 1;
    }
    if (masked == AMOTION_EVENT_ACTION_MOVE) {
        for (i = 0; i < count; ++i) {
            int32_t id = AMotionEvent_getPointerId(event, i);
            float x = AMotionEvent_getX(event, i), y = AMotionEvent_getY(event, i);
            TouchTrack *t = touch_find(s, id, 1);
            float dx = t ? x - t->x : 0.0f;
            float dy = t ? y - t->y : 0.0f;
            if (t) { t->x = x; t->y = y; }
            module_event(s, ODPAR_EVENT_POINTER_MOVE, id, x, y, dx, dy);
        }
        return 1;
    }
    if (masked == AMOTION_EVENT_ACTION_UP || masked == AMOTION_EVENT_ACTION_POINTER_UP) {
        size_t index = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        int32_t id = AMotionEvent_getPointerId(event, index);
        float x = AMotionEvent_getX(event, index), y = AMotionEvent_getY(event, index);
        module_event(s, ODPAR_EVENT_POINTER_UP, id, x, y, 0.0f, 0.0f);
        { TouchTrack *t = touch_find(s, id, 0); if (t) memset(t, 0, sizeof(*t)); }
        return 1;
    }
    if (masked == AMOTION_EVENT_ACTION_CANCEL) {
        for (i = 0; i < MAX_TOUCH; ++i) {
            if (s->touch[i].active) {
                module_event(s, ODPAR_EVENT_POINTER_CANCEL, s->touch[i].id,
                             s->touch[i].x, s->touch[i].y, 0.0f, 0.0f);
                memset(&s->touch[i], 0, sizeof(s->touch[i]));
            }
        }
        return 1;
    }
    return 0;
}

static void send_simple_event(Shell *s, uint32_t type) {
    OdparEvent e;
    if (!s->api || !s->game || !s->api->event) return;
    memset(&e, 0, sizeof(e));
    e.struct_size = sizeof(e);
    e.type = type;
    if (s->app->window) {
        e.width = (uint32_t)ANativeWindow_getWidth(s->app->window);
        e.height = (uint32_t)ANativeWindow_getHeight(s->app->window);
    }
    s->api->event(s->game, &e);
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    Shell *s = (Shell *)app->userData;
    if (!s) return;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            s->has_window = app->window != NULL;
            send_simple_event(s, ODPAR_EVENT_RESIZE);
            break;
        case APP_CMD_TERM_WINDOW:
            s->has_window = 0;
            break;
        case APP_CMD_PAUSE:
            send_simple_event(s, ODPAR_EVENT_PAUSE);
            break;
        case APP_CMD_RESUME:
            send_simple_event(s, ODPAR_EVENT_RESUME);
            break;
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_WINDOW_RESIZED:
            send_simple_event(s, ODPAR_EVENT_RESIZE);
            break;
        default:
            break;
    }
}

static int load_module(Shell *s) {
    OdparModuleGetApiFn get_api = NULL;
    OdparModuleCreateInfo ci;
    s->module_so = dlopen("libodpar_whiteline.so", RTLD_NOW | RTLD_LOCAL);
    if (!s->module_so) {
        LOGE("dlopen WhiteLine failed: %s", dlerror());
        return -1;
    }
    *(void **)(&get_api) = dlsym(s->module_so, "odpar_module_get_api");
    if (!get_api) {
        LOGE("odpar_module_get_api missing");
        return -1;
    }
    s->api = get_api(ODPAR_MODULE_ABI);
    if (!s->api) {
        LOGE("WhiteLine refused ODPAR module ABI %08x", ODPAR_MODULE_ABI);
        return -1;
    }
    memset(&ci, 0, sizeof(ci));
    ci.struct_size = sizeof(ci);
    ci.view_width = s->app->window ? (uint32_t)ANativeWindow_getWidth(s->app->window) : 1280u;
    ci.view_height = s->app->window ? (uint32_t)ANativeWindow_getHeight(s->app->window) : 720u;
    ci.seed = UINT32_C(0x574c4431);
    ci.density = 1.0f;
    ci.data_path = s->app->activity->internalDataPath;
    ci.save_path = s->app->activity->internalDataPath;
    s->game = s->api->create(&ci);
    if (!s->game) {
        LOGE("WhiteLine create failed");
        return -1;
    }
    LOGI("Loaded %s %s", s->api->name, s->api->version);
    return 0;
}

static void unload_module(Shell *s) {
    if (s->api && s->game && s->api->destroy) s->api->destroy(s->game);
    s->game = NULL;
    s->api = NULL;
    if (s->module_so) dlclose(s->module_so);
    s->module_so = NULL;
}

static void present(Shell *s) {
    OdparFrame f;
    ANativeWindow_Buffer b;
    uint32_t y;
    uint32_t copy_w;
    if (!s->has_window || !s->app->window || !s->api || !s->game) return;
    memset(&f, 0, sizeof(f));
    f.struct_size = sizeof(f);
    if (s->api->render(s->game, &f) != 0 || !f.pixels || !f.width || !f.height) return;
    ANativeWindow_setBuffersGeometry(s->app->window, (int32_t)f.width, (int32_t)f.height, WINDOW_FORMAT_RGBA_8888);
    if (ANativeWindow_lock(s->app->window, &b, NULL) != 0) return;
    copy_w = f.width < (uint32_t)b.width ? f.width : (uint32_t)b.width;
    for (y = 0; y < f.height && y < (uint32_t)b.height; ++y) {
        const uint8_t *src = (const uint8_t *)f.pixels + (size_t)y * f.stride_bytes;
        uint8_t *dst = (uint8_t *)b.bits + (size_t)y * (size_t)b.stride * 4u;
        memcpy(dst, src, (size_t)copy_w * 4u);
    }
    ANativeWindow_unlockAndPost(s->app->window);
}

void android_main(struct android_app *app) {
    Shell s;
    double previous;
    memset(&s, 0, sizeof(s));
    s.app = app;
    s.running = 1;
    app->userData = &s;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;

    if (load_module(&s) != 0) {
        unload_module(&s);
        ANativeActivity_finish(app->activity);
        return;
    }

    previous = now_seconds();
    while (s.running) {
        int events;
        struct android_poll_source *source;
        int ident;
        while ((ident = ALooper_pollOnce(s.has_window ? 0 : -1, NULL, &events, (void **)&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) { s.running = 0; break; }
            if (ident == ALOOPER_POLL_TIMEOUT) break;
        }
        if (!s.running) break;
        if (s.has_window && s.api && s.game) {
            double now = now_seconds();
            double dt = now - previous;
            previous = now;
            if (dt > 0.05) dt = 0.05;
            s.api->advance(s.game, dt);
            present(&s);
            if (s.api->wants_exit && s.api->wants_exit(s.game)) {
                ANativeActivity_finish(app->activity);
                s.running = 0;
            }
        } else {
            previous = now_seconds();
        }
    }
    unload_module(&s);
}
