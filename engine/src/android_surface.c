#include "odg_internal.h"

#ifdef __ANDROID__
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <string.h>

void odg_android_attach_surface(OdgEngineService *service, void *surface) {
    ANativeWindow *window = (ANativeWindow *)surface;
    if (service == NULL) return;
    (void)pthread_mutex_lock(&service->surface_mu);
    if (service->native_window != NULL) {
        ANativeWindow_release((ANativeWindow *)service->native_window);
    }
    service->native_window = window;
    service->surface_width = 0u;
    service->surface_height = 0u;
    (void)pthread_mutex_unlock(&service->surface_mu);
}

void odg_android_detach_surface(OdgEngineService *service) {
    if (service == NULL) return;
    (void)pthread_mutex_lock(&service->surface_mu);
    if (service->native_window != NULL) {
        ANativeWindow_release((ANativeWindow *)service->native_window);
        service->native_window = NULL;
    }
    service->surface_width = 0u;
    service->surface_height = 0u;
    (void)pthread_mutex_unlock(&service->surface_mu);
}

void odg_android_present(OdgEngineService *service, const OdgRaster *raster) {
    ANativeWindow *window;
    ANativeWindow_Buffer buffer;
    uint32_t y;
    if (service == NULL || raster == NULL || raster->pixels == NULL) return;
    (void)pthread_mutex_lock(&service->surface_mu);
    window = (ANativeWindow *)service->native_window;
    if (window == NULL) {
        (void)pthread_mutex_unlock(&service->surface_mu);
        return;
    }
    if (service->surface_width != raster->width || service->surface_height != raster->height) {
        if (ANativeWindow_setBuffersGeometry(window, (int32_t)raster->width, (int32_t)raster->height,
                                             WINDOW_FORMAT_RGBA_8888) != 0) {
            (void)pthread_mutex_unlock(&service->surface_mu);
            return;
        }
        service->surface_width = raster->width;
        service->surface_height = raster->height;
    }
    if (ANativeWindow_lock(window, &buffer, NULL) == 0) {
        uint32_t rows = raster->height < (uint32_t)buffer.height ? raster->height : (uint32_t)buffer.height;
        uint32_t cols = raster->width < (uint32_t)buffer.width ? raster->width : (uint32_t)buffer.width;
        for (y = 0u; y < rows; ++y) {
            uint8_t *dst = (uint8_t *)buffer.bits + (size_t)y * (size_t)buffer.stride * sizeof(uint32_t);
            const uint8_t *src = (const uint8_t *)(raster->pixels + (size_t)y * (size_t)raster->width);
            (void)memcpy(dst, src, (size_t)cols * sizeof(uint32_t));
        }
        (void)ANativeWindow_unlockAndPost(window);
    }
    (void)pthread_mutex_unlock(&service->surface_mu);
}

JNIEXPORT void JNICALL
Java_com_odpar_territorial_1domain_greenfield_NativeRenderPlugin_nativeAttachSurface(
    JNIEnv *env, jobject thiz, jlong handle, jobject surface) {
    OdgEngineService *service = (OdgEngineService *)(uintptr_t)handle;
    ANativeWindow *window;
    (void)thiz;
    if (service == NULL || surface == NULL) return;
    window = ANativeWindow_fromSurface(env, surface);
    if (window == NULL) return;
    odg_android_attach_surface(service, window);
}

JNIEXPORT void JNICALL
Java_com_odpar_territorial_1domain_greenfield_NativeRenderPlugin_nativeDetachSurface(
    JNIEnv *env, jobject thiz, jlong handle) {
    OdgEngineService *service = (OdgEngineService *)(uintptr_t)handle;
    (void)env;
    (void)thiz;
    odg_android_detach_surface(service);
}

JNIEXPORT void JNICALL
Java_com_odpar_territorial_1domain_greenfield_NativeRenderPlugin_nativeRetainService(
    JNIEnv *env, jobject thiz, jlong handle) {
    OdgEngineService *service = (OdgEngineService *)(uintptr_t)handle;
    (void)env;
    (void)thiz;
    odg_service_retain_reference(service);
}

JNIEXPORT void JNICALL
Java_com_odpar_territorial_1domain_greenfield_NativeRenderPlugin_nativeReleaseService(
    JNIEnv *env, jobject thiz, jlong handle) {
    OdgEngineService *service = (OdgEngineService *)(uintptr_t)handle;
    (void)env;
    (void)thiz;
    odg_service_release_reference(service);
}

#else

void odg_android_attach_surface(OdgEngineService *service, void *surface) {
    (void)service;
    (void)surface;
}

void odg_android_detach_surface(OdgEngineService *service) {
    (void)service;
}

void odg_android_present(OdgEngineService *service, const OdgRaster *raster) {
    (void)service;
    (void)raster;
}

#endif
