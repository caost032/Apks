#ifndef ODPAR_MODULE_H
#define ODPAR_MODULE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODPAR_MODULE_ABI UINT32_C(0x00010000)
#define ODPAR_FRAME_RGBA8888 UINT32_C(1)

#if defined(_WIN32)
#  define ODPAR_MODULE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define ODPAR_MODULE_EXPORT __attribute__((visibility("default")))
#else
#  define ODPAR_MODULE_EXPORT
#endif

typedef enum OdparEventType {
    ODPAR_EVENT_NONE = 0,
    ODPAR_EVENT_POINTER_DOWN = 1,
    ODPAR_EVENT_POINTER_MOVE = 2,
    ODPAR_EVENT_POINTER_UP = 3,
    ODPAR_EVENT_POINTER_CANCEL = 4,
    ODPAR_EVENT_BACK = 5,
    ODPAR_EVENT_PAUSE = 6,
    ODPAR_EVENT_RESUME = 7,
    ODPAR_EVENT_RESIZE = 8
} OdparEventType;

typedef struct OdparModuleCreateInfo {
    uint32_t struct_size;
    uint32_t view_width;
    uint32_t view_height;
    uint32_t seed;
    float density;
    const char *data_path;
    const char *save_path;
} OdparModuleCreateInfo;

typedef struct OdparEvent {
    uint32_t struct_size;
    uint32_t type;
    int32_t pointer_id;
    float x01;
    float y01;
    float dx01;
    float dy01;
    uint32_t width;
    uint32_t height;
} OdparEvent;

typedef struct OdparFrame {
    uint32_t struct_size;
    uint32_t format;
    const void *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint64_t tick;
} OdparFrame;

typedef struct OdparModuleApi {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *module_id;
    const char *name;
    const char *version;

    void *(*create)(const OdparModuleCreateInfo *info);
    void (*destroy)(void *instance);
    int (*event)(void *instance, const OdparEvent *event);
    uint32_t (*advance)(void *instance, double elapsed_seconds);
    int (*render)(void *instance, OdparFrame *frame_out);
    int (*wants_exit)(const void *instance);
} OdparModuleApi;

typedef const OdparModuleApi *(*OdparModuleGetApiFn)(uint32_t host_abi);

ODPAR_MODULE_EXPORT const OdparModuleApi *odpar_module_get_api(uint32_t host_abi);

#ifdef __cplusplus
}
#endif

#endif
