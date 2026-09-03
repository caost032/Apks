#include "odpar_module.h"

#include "odwd_core.h"
#include "odwd_render.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WL_MAGIC UINT64_C(0x574c444d4f44554c)
#define WL_POINTERS 16u
#define WL_FIXED_DT (1.0 / (double)ODWD_TICK_HZ)
#define WL_COLOR(r,g,b) ((uint32_t)(r) | ((uint32_t)(g) << 8u) | ((uint32_t)(b) << 16u) | UINT32_C(0xff000000))

static const uint32_t WL_WHITE = WL_COLOR(248,248,245);
static const uint32_t WL_BLACK = WL_COLOR(10,11,13);
static const uint32_t WL_GREY = WL_COLOR(106,110,116);
static const uint32_t WL_WARNING = WL_COLOR(244,177,48);

typedef enum WlUiState {
    WL_UI_MENU = 0,
    WL_UI_RUNNING = 1,
    WL_UI_PAUSED = 2
} WlUiState;

typedef enum WlPointerRole {
    WL_ROLE_NONE = 0,
    WL_ROLE_LEFT,
    WL_ROLE_RIGHT,
    WL_ROLE_DRIFT,
    WL_ROLE_TURBO,
    WL_ROLE_JUMP,
    WL_ROLE_REVERSE,
    WL_ROLE_BRAKE,
    WL_ROLE_THROTTLE,
    WL_ROLE_CAMERA
} WlPointerRole;

typedef struct WlPointer {
    int32_t id;
    uint8_t active;
    uint8_t role;
    float last_x;
    float last_y;
} WlPointer;

typedef struct WlModule {
    uint64_t magic;
    odwd_storage storage;
    odwd_input input;
    odwd_frame frame;
    double accumulator;
    uint32_t seed;
    uint32_t selected_mode;
    uint32_t current_mode;
    uint32_t camera_mode;
    uint32_t quality;
    uint32_t portrait;
    uint32_t autodrive;
    uint32_t wants_exit;
    uint32_t lifecycle_paused;
    uint32_t ui_state;
    uint32_t render_context;
    uint32_t view_width;
    uint32_t view_height;
    uint8_t jump_host_level;
    uint8_t jump_sim_level;
    uint8_t jump_queue_bits;
    uint8_t jump_queue_count;
    uint8_t headlight_edge;
    WlPointer pointers[WL_POINTERS];
} WlModule;

static WlModule g_module;

static int wl_valid(const WlModule *m) {
    return m != NULL && m->magic == WL_MAGIC;
}

static float wl_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void wl_queue_jump(WlModule *m, uint8_t level) {
    uint32_t shift;
    if (level == m->jump_host_level) return;
    m->jump_host_level = level;
    if (m->jump_queue_count >= 8u) {
        m->jump_queue_bits >>= 1u;
        m->jump_queue_count = 7u;
    }
    shift = m->jump_queue_count;
    if (level) m->jump_queue_bits |= (uint8_t)(1u << shift);
    else m->jump_queue_bits &= (uint8_t)~(1u << shift);
    ++m->jump_queue_count;
}

static uint8_t wl_next_jump(WlModule *m) {
    uint8_t level;
    if (m->jump_queue_count == 0u) return m->jump_host_level;
    level = m->jump_queue_bits & 1u;
    m->jump_queue_bits >>= 1u;
    --m->jump_queue_count;
    return level;
}

static int wl_reset_engine(WlModule *m, uint32_t mode) {
    odwd_config cfg;
    memset(&m->storage, 0, sizeof(m->storage));
    odwd_config_defaults(&cfg);
    cfg.seed = m->seed;
    cfg.rival_count = mode == ODWD_MODE_SURVIVAL ? 5u : 5u;
    cfg.section_length_m = 1536.0;
    cfg.checkpoint_spacing_m = 480.0;
    cfg.world_mode = mode;
    if (odwd_engine_init(m->storage.bytes, sizeof(m->storage.bytes), &cfg) != ODWD_OK)
        return -1;
    odwd_input_neutral(&m->input);
    m->input.camera_mode = ODWD_CAMERA_CHASE;
    m->camera_mode = ODWD_CAMERA_CHASE;
    m->current_mode = mode;
    m->accumulator = 0.0;
    m->jump_host_level = 0u;
    m->jump_sim_level = 0u;
    m->jump_queue_bits = 0u;
    m->jump_queue_count = 0u;
    m->headlight_edge = 0u;
    memset(m->pointers, 0, sizeof(m->pointers));
    if (odwd_engine_read_frame(m->storage.bytes, &m->frame) != ODWD_OK)
        return -1;
    return 0;
}

static WlPointer *wl_pointer(WlModule *m, int32_t id, int create) {
    uint32_t i;
    WlPointer *free_slot = NULL;
    for (i = 0u; i < WL_POINTERS; ++i) {
        if (m->pointers[i].active && m->pointers[i].id == id) return &m->pointers[i];
        if (!m->pointers[i].active && free_slot == NULL) free_slot = &m->pointers[i];
    }
    if (!create || free_slot == NULL) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->active = 1u;
    free_slot->id = id;
    return free_slot;
}

static int wl_any_role(const WlModule *m, WlPointerRole role) {
    uint32_t i;
    for (i = 0u; i < WL_POINTERS; ++i)
        if (m->pointers[i].active && m->pointers[i].role == (uint8_t)role) return 1;
    return 0;
}

static void wl_rebuild_controls(WlModule *m) {
    double steering = 0.0;
    uint32_t buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    if (wl_any_role(m, WL_ROLE_LEFT)) steering -= 1.0;
    if (wl_any_role(m, WL_ROLE_RIGHT)) steering += 1.0;
    m->input.joystick_x = steering;
    m->input.joystick_y = 0.0;
    m->input.throttle = wl_any_role(m, WL_ROLE_THROTTLE) ? 1.0 : 0.0;
    m->input.brake = wl_any_role(m, WL_ROLE_BRAKE) ? 1.0 : 0.0;
    m->input.reverse = wl_any_role(m, WL_ROLE_REVERSE) ? 1.0 : 0.0;
    if (wl_any_role(m, WL_ROLE_DRIFT)) buttons |= ODWD_BUTTON_HANDBRAKE;
    if (wl_any_role(m, WL_ROLE_TURBO)) buttons |= ODWD_BUTTON_TURBO;
    if (wl_any_role(m, WL_ROLE_CAMERA)) buttons |= ODWD_BUTTON_CAMERA_HOLD;
    if (m->autodrive) buttons |= ODWD_BUTTON_AUTODRIVE;
    if (m->headlight_edge) buttons |= ODWD_BUTTON_HEADLIGHTS;
    if (m->jump_sim_level) buttons |= ODWD_BUTTON_JUMP;
    m->input.buttons = buttons;
    m->input.camera_mode = m->camera_mode;
}

static WlPointerRole wl_role_for(float x, float y, uint32_t mode) {
    if (y > 0.72f) {
        if (x < 0.18f) return WL_ROLE_LEFT;
        if (x < 0.34f) return WL_ROLE_RIGHT;
        if (x > 0.84f) return WL_ROLE_THROTTLE;
        if (x > 0.72f) return WL_ROLE_BRAKE;
        if (x > 0.61f) return WL_ROLE_REVERSE;
    }
    if (y > 0.55f && y <= 0.72f) {
        if (x > 0.84f && mode == ODWD_MODE_SURVIVAL) return WL_ROLE_JUMP;
        if (x > 0.74f) return WL_ROLE_TURBO;
        if (x > 0.64f) return WL_ROLE_DRIFT;
    }
    if (x >= 0.34f) return WL_ROLE_CAMERA;
    return WL_ROLE_NONE;
}

static uint32_t wl_mix(uint32_t a, uint32_t b, unsigned alpha) {
    unsigned ar = a & 255u, ag = (a >> 8u) & 255u, ab = (a >> 16u) & 255u;
    unsigned br = b & 255u, bg = (b >> 8u) & 255u, bb = (b >> 16u) & 255u;
    unsigned inv = 255u - alpha;
    return WL_COLOR((ar * inv + br * alpha) / 255u,
                    (ag * inv + bg * alpha) / 255u,
                    (ab * inv + bb * alpha) / 255u);
}

static void wl_fill(uint32_t *px, uint32_t w, uint32_t h,
                    int x0, int y0, int x1, int y1,
                    uint32_t color, unsigned alpha) {
    int x, y;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > (int)w) x1 = (int)w; if (y1 > (int)h) y1 = (int)h;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) {
            uint32_t *p = &px[(uint32_t)y * w + (uint32_t)x];
            *p = alpha >= 255u ? color : wl_mix(*p, color, alpha);
        }
}

static void wl_border(uint32_t *px, uint32_t w, uint32_t h,
                      int x0, int y0, int x1, int y1, uint32_t color, int t) {
    wl_fill(px,w,h,x0,y0,x1,y0+t,color,255u);
    wl_fill(px,w,h,x0,y1-t,x1,y1,color,255u);
    wl_fill(px,w,h,x0,y0,x0+t,y1,color,255u);
    wl_fill(px,w,h,x1-t,y0,x1,y1,color,255u);
}

static uint8_t wl_glyph(char c, unsigned col) {
    static const uint8_t digits[10][5] = {
        {31,17,17,17,31},{0,18,31,16,0},{29,21,21,21,23},{17,21,21,21,31},{7,4,4,4,31},
        {23,21,21,21,29},{31,21,21,21,29},{1,1,1,1,31},{31,21,21,21,31},{23,21,21,21,31}
    };
    static const uint8_t letters[26][5] = {
        {30,5,5,5,30},{31,21,21,21,10},{14,17,17,17,17},{31,17,17,17,14},{31,21,21,21,17},
        {31,5,5,5,1},{14,17,21,21,29},{31,4,4,4,31},{17,17,31,17,17},{8,16,16,16,15},
        {31,4,10,17,0},{31,16,16,16,16},{31,2,4,2,31},{31,2,4,8,31},{14,17,17,17,14},
        {31,5,5,5,2},{14,17,25,17,30},{31,5,13,21,18},{18,21,21,21,9},{1,1,31,1,1},
        {15,16,16,16,15},{7,8,16,8,7},{31,8,4,8,31},{17,10,4,10,17},{1,2,28,2,1},{25,21,21,21,19}
    };
    if (col >= 5u) return 0u;
    if (c >= '0' && c <= '9') return digits[(unsigned)(c-'0')][col];
    if (c >= 'A' && c <= 'Z') return letters[(unsigned)(c-'A')][col];
    if (c == '-') return col == 2u ? 4u : 4u;
    if (c == '/') return (uint8_t)(1u << (4u-col));
    if (c == ':') return col == 2u ? 10u : 0u;
    if (c == '.') return col == 2u ? 16u : 0u;
    return 0u;
}

static void wl_text(uint32_t *px, uint32_t w, uint32_t h,
                    int x, int y, int scale, const char *s, uint32_t color) {
    int cx = x;
    while (*s) {
        char c = *s++;
        unsigned col, row;
        if (c == ' ') { cx += 4 * scale; continue; }
        for (col = 0u; col < 5u; ++col) {
            uint8_t bits = wl_glyph(c, col);
            for (row = 0u; row < 5u; ++row)
                if (bits & (1u << row))
                    wl_fill(px,w,h,cx+(int)col*scale,y+(int)row*scale,
                            cx+(int)(col+1u)*scale,y+(int)(row+1u)*scale,color,255u);
        }
        cx += 6 * scale;
    }
}

static const char *wl_mode_name(uint32_t mode) {
    switch (mode) {
        case ODWD_MODE_OPEN_FIELD: return "OPEN FIELD";
        case ODWD_MODE_SURVIVAL: return "BLOCKDASH";
        case ODWD_MODE_MUSIC_SURVIVAL: return "MUSIC SURVIVAL";
        default: return "ENDLESS";
    }
}

static void wl_overlay_menu(WlModule *m, uint32_t *px, uint32_t w, uint32_t h) {
    uint32_t i;
    int left = (int)(w * 0.18f), right = (int)(w * 0.82f);
    wl_fill(px,w,h,0,0,(int)w,(int)h,WL_BLACK,205u);
    wl_text(px,w,h,(int)(w*0.19f),(int)(h*0.10f),3,"WHITE//LINE DRIFT",WL_WHITE);
    wl_text(px,w,h,(int)(w*0.19f),(int)(h*0.18f),1,"ODPAR  NATIVE C",WL_GREY);
    for (i = 0u; i < 4u; ++i) {
        int y0 = (int)(h * (0.29f + 0.105f*(float)i));
        int y1 = y0 + (int)(h * 0.075f);
        uint32_t bg = i == m->selected_mode ? WL_WHITE : WL_BLACK;
        uint32_t fg = i == m->selected_mode ? WL_BLACK : WL_WHITE;
        wl_fill(px,w,h,left,y0,right,y1,bg,235u);
        wl_border(px,w,h,left,y0,right,y1,WL_WHITE,1);
        wl_text(px,w,h,left+14,y0+10,2,wl_mode_name(i),fg);
    }
    {
        int y0 = (int)(h * 0.76f), y1 = (int)(h * 0.88f);
        wl_fill(px,w,h,left,y0,right,y1,WL_WHITE,255u);
        wl_text(px,w,h,(int)(w*0.42f),y0+14,3,"PLAY",WL_BLACK);
    }
}

static void wl_overlay_pause(WlModule *m, uint32_t *px, uint32_t w, uint32_t h) {
    (void)m;
    wl_fill(px,w,h,0,0,(int)w,(int)h,WL_BLACK,190u);
    wl_text(px,w,h,(int)(w*0.40f),(int)(h*0.25f),3,"PAUSED",WL_WHITE);
    wl_fill(px,w,h,(int)(w*0.28f),(int)(h*0.43f),(int)(w*0.72f),(int)(h*0.55f),WL_WHITE,255u);
    wl_text(px,w,h,(int)(w*0.39f),(int)(h*0.46f),2,"RESUME",WL_BLACK);
    wl_border(px,w,h,(int)(w*0.28f),(int)(h*0.61f),(int)(w*0.72f),(int)(h*0.73f),WL_WHITE,2);
    wl_text(px,w,h,(int)(w*0.38f),(int)(h*0.64f),2,"EXIT RUN",WL_WHITE);
}

static void wl_overlay_game(WlModule *m, uint32_t *px, uint32_t w, uint32_t h) {
    odwd_vehicle_snapshot v;
    char buf[64];
    int active;
    if (odwd_engine_read_vehicle(m->storage.bytes, 0u, &v) == ODWD_OK) {
        snprintf(buf,sizeof(buf),"%03d KMH",(int)(v.speed_mps*3.6));
        wl_fill(px,w,h,8,8,155,34,WL_BLACK,150u);
        wl_text(px,w,h,16,14,2,buf,WL_WHITE);
    }
    wl_text(px,w,h,12,44,1,wl_mode_name(m->current_mode),WL_BLACK);
    if (m->autodrive) wl_text(px,w,h,12,58,1,"AUTO",WL_WARNING);

#define BUTTON(X0,Y0,X1,Y1,LABEL,ROLE) do { \
    active = wl_any_role(m,(ROLE)); \
    wl_fill(px,w,h,(int)(w*(X0)),(int)(h*(Y0)),(int)(w*(X1)),(int)(h*(Y1)), \
            active ? WL_WHITE : WL_BLACK, active ? 230u : 105u); \
    wl_border(px,w,h,(int)(w*(X0)),(int)(h*(Y0)),(int)(w*(X1)),(int)(h*(Y1)),WL_WHITE,1); \
    wl_text(px,w,h,(int)(w*(X0))+5,(int)(h*(Y0))+5,1,(LABEL),active?WL_BLACK:WL_WHITE); \
} while(0)
    BUTTON(0.03f,0.76f,0.16f,0.93f,"LEFT",WL_ROLE_LEFT);
    BUTTON(0.18f,0.76f,0.31f,0.93f,"RIGHT",WL_ROLE_RIGHT);
    BUTTON(0.62f,0.76f,0.71f,0.93f,"REV",WL_ROLE_REVERSE);
    BUTTON(0.72f,0.76f,0.83f,0.93f,"BRAKE",WL_ROLE_BRAKE);
    BUTTON(0.84f,0.73f,0.98f,0.94f,"GO",WL_ROLE_THROTTLE);
    BUTTON(0.65f,0.58f,0.73f,0.71f,"DRIFT",WL_ROLE_DRIFT);
    BUTTON(0.75f,0.58f,0.83f,0.71f,"BOOST",WL_ROLE_TURBO);
    if (m->current_mode == ODWD_MODE_SURVIVAL)
        BUTTON(0.85f,0.58f,0.97f,0.71f,"JUMP",WL_ROLE_JUMP);
#undef BUTTON
    wl_border(px,w,h,8,78,68,108,WL_BLACK,1); wl_text(px,w,h,14,88,1,"LIGHT",WL_BLACK);
    wl_border(px,w,h,74,78,124,108,WL_BLACK,1); wl_text(px,w,h,82,88,1,"CAM",WL_BLACK);
    wl_border(px,w,h,130,78,190,108,m->autodrive?WL_WARNING:WL_BLACK,1); wl_text(px,w,h,138,88,1,"AUTO",m->autodrive?WL_WARNING:WL_BLACK);
    wl_border(px,w,h,(int)w-55,8,(int)w-8,42,WL_BLACK,1); wl_text(px,w,h,(int)w-43,18,2,"II",WL_BLACK);
}

static int wl_menu_touch(WlModule *m, float x, float y) {
    uint32_t i;
    if (x >= 0.18f && x <= 0.82f) {
        for (i = 0u; i < 4u; ++i) {
            float y0 = 0.29f + 0.105f * (float)i;
            if (y >= y0 && y <= y0 + 0.075f) {
                m->selected_mode = i;
                return 1;
            }
        }
        if (y >= 0.76f && y <= 0.88f) {
            if (wl_reset_engine(m,m->selected_mode) == 0) {
                m->ui_state = WL_UI_RUNNING;
            }
            return 1;
        }
    }
    return 0;
}

static int wl_pause_touch(WlModule *m, float x, float y) {
    if (x >= 0.28f && x <= 0.72f && y >= 0.43f && y <= 0.55f) {
        m->ui_state = WL_UI_RUNNING; return 1;
    }
    if (x >= 0.28f && x <= 0.72f && y >= 0.61f && y <= 0.73f) {
        m->ui_state = WL_UI_MENU; return 1;
    }
    return 0;
}

static void *wl_create(const OdparModuleCreateInfo *info) {
    WlModule *m = &g_module;
    if (m->magic == WL_MAGIC) return NULL;
    memset(m,0,sizeof(*m));
    m->magic = WL_MAGIC;
    m->seed = info && info->seed ? info->seed : UINT32_C(0x574c4431);
    m->view_width = info ? info->view_width : 1280u;
    m->view_height = info ? info->view_height : 720u;
    m->portrait = m->view_height > m->view_width;
    m->selected_mode = ODWD_MODE_ENDLESS;
    m->quality = 1u;
    m->render_context = 1u;
    m->ui_state = WL_UI_MENU;
    m->lifecycle_paused = 0u;
    if (wl_reset_engine(m,m->selected_mode) != 0) {
        memset(m,0,sizeof(*m));
        return NULL;
    }
    m->ui_state = WL_UI_MENU;
    return m;
}

static void wl_destroy(void *instance) {
    WlModule *m = (WlModule *)instance;
    if (!wl_valid(m)) return;
    memset(m,0,sizeof(*m));
}

static int wl_event(void *instance, const OdparEvent *event) {
    WlModule *m = (WlModule *)instance;
    WlPointer *p;
    if (!wl_valid(m) || !event || event->struct_size < sizeof(*event)) return -1;
    if (event->type == ODPAR_EVENT_RESIZE) {
        if (event->width && event->height) {
            m->view_width = event->width; m->view_height = event->height;
            m->portrait = event->height > event->width;
        }
        return 0;
    }
    if (event->type == ODPAR_EVENT_PAUSE) { m->lifecycle_paused = 1u; return 0; }
    if (event->type == ODPAR_EVENT_RESUME) { m->lifecycle_paused = 0u; return 0; }
    if (event->type == ODPAR_EVENT_BACK) {
        if (m->ui_state == WL_UI_RUNNING) { m->ui_state = WL_UI_PAUSED; }
        else if (m->ui_state == WL_UI_PAUSED) { m->ui_state = WL_UI_RUNNING; }
        else m->wants_exit = 1u;
        return 0;
    }
    if (event->type == ODPAR_EVENT_POINTER_DOWN) {
        float x = wl_clampf(event->x01,0.0f,1.0f), y = wl_clampf(event->y01,0.0f,1.0f);
        if (m->ui_state == WL_UI_MENU) return wl_menu_touch(m,x,y) ? 0 : 0;
        if (m->ui_state == WL_UI_PAUSED) return wl_pause_touch(m,x,y) ? 0 : 0;
        if (x > 0.90f && y < 0.16f) { m->ui_state = WL_UI_PAUSED; return 0; }
        if (y >= 0.18f && y <= 0.33f) {
            if (x < 0.12f) { m->headlight_edge = 1u; return 0; }
            if (x < 0.24f) { m->camera_mode = (m->camera_mode + 1u) % ODWD_CAMERA_MODE_COUNT; return 0; }
            if (x < 0.38f) { m->autodrive = !m->autodrive; return 0; }
        }
        p = wl_pointer(m,event->pointer_id,1);
        if (!p) return -1;
        p->last_x = x; p->last_y = y; p->role = (uint8_t)wl_role_for(x,y,m->current_mode);
        if (p->role == WL_ROLE_JUMP) wl_queue_jump(m,1u);
        wl_rebuild_controls(m);
        return 0;
    }
    if (event->type == ODPAR_EVENT_POINTER_MOVE) {
        p = wl_pointer(m,event->pointer_id,0);
        if (!p) return 0;
        if (p->role == WL_ROLE_CAMERA) {
            m->input.look_dx += (double)event->dx01;
            m->input.look_dy += (double)event->dy01;
        }
        p->last_x = event->x01; p->last_y = event->y01;
        return 0;
    }
    if (event->type == ODPAR_EVENT_POINTER_UP || event->type == ODPAR_EVENT_POINTER_CANCEL) {
        p = wl_pointer(m,event->pointer_id,0);
        if (!p) return 0;
        if (p->role == WL_ROLE_JUMP) wl_queue_jump(m,0u);
        memset(p,0,sizeof(*p));
        wl_rebuild_controls(m);
        return 0;
    }
    return 0;
}

static uint32_t wl_advance(void *instance, double elapsed_seconds) {
    WlModule *m = (WlModule *)instance;
    uint32_t steps = 0u, events = 0u;
    if (!wl_valid(m) || m->lifecycle_paused || m->ui_state != WL_UI_RUNNING) return 0u;
    if (!(elapsed_seconds > 0.0) || elapsed_seconds != elapsed_seconds) return 0u;
    if (elapsed_seconds > 0.25) elapsed_seconds = 0.25;
    m->accumulator += elapsed_seconds;
    while (m->accumulator + 1.0e-12 >= WL_FIXED_DT && steps < 30u) {
        m->jump_sim_level = wl_next_jump(m);
        wl_rebuild_controls(m);
        if (odwd_engine_step(m->storage.bytes,&m->input,&m->frame) != ODWD_OK) break;
        events |= m->frame.event_flags;
        m->input.look_dx = 0.0;
        m->input.look_dy = 0.0;
        m->headlight_edge = 0u;
        m->accumulator -= WL_FIXED_DT;
        ++steps;
    }
    m->frame.event_flags = events;
    return steps;
}

static int wl_render(void *instance, OdparFrame *out) {
    WlModule *m = (WlModule *)instance;
    uint32_t *pixels;
    uint32_t w,h;
    if (!wl_valid(m) || !out || out->struct_size < sizeof(*out)) return -1;
    od_renderer_set_context(m->render_context);
    od_set_view_orientation(m->portrait);
    (void)od_renderer_render_storage(m->storage.bytes,m->quality);
    w = od_framebuffer_width(); h = od_framebuffer_height();
    pixels = (uint32_t *)(uintptr_t)od_framebuffer_data();
    if (m->ui_state == WL_UI_MENU) wl_overlay_menu(m,pixels,w,h);
    else if (m->ui_state == WL_UI_PAUSED) wl_overlay_pause(m,pixels,w,h);
    else wl_overlay_game(m,pixels,w,h);
    memset(out,0,sizeof(*out));
    out->struct_size = (uint32_t)sizeof(*out);
    out->format = ODPAR_FRAME_RGBA8888;
    out->pixels = pixels;
    out->width = w;
    out->height = h;
    out->stride_bytes = w * 4u;
    out->tick = m->frame.tick;
    return 0;
}

static int wl_wants_exit(const void *instance) {
    const WlModule *m = (const WlModule *)instance;
    return wl_valid(m) ? (int)m->wants_exit : 1;
}

static const OdparModuleApi g_api = {
    sizeof(OdparModuleApi),
    ODPAR_MODULE_ABI,
    "odpar.whiteline",
    "WhiteLine Drift",
    "0.7.0-native-rebuild",
    wl_create,
    wl_destroy,
    wl_event,
    wl_advance,
    wl_render,
    wl_wants_exit
};

ODPAR_MODULE_EXPORT const OdparModuleApi *odpar_module_get_api(uint32_t host_abi) {
    if (host_abi != ODPAR_MODULE_ABI) return NULL;
    return &g_api;
}
