#ifndef ODPAR_GREENFIELD_INTERNAL_H
#define ODPAR_GREENFIELD_INTERNAL_H

#include "odpar/odg_host.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

#define ODG_SIM_HZ 60u
#define ODG_SIM_STEP_NS UINT64_C(16666667)
#define ODG_INPUT_STALE_NS UINT64_C(250000000)
#define ODG_RENDER_MIN_W 180u
#define ODG_RENDER_MIN_H 180u
#define ODG_RENDER_MAX_W 640u
#define ODG_RENDER_MAX_H 640u
#define ODG_DEFAULT_RENDER_W 360u
#define ODG_DEFAULT_RENDER_H 640u
#define ODG_PERF_RING_CAP 256u
#define ODG_SIM_SPIKE_BUDGET_US 5000u
#define ODG_RENDER_SPIKE_BUDGET_US 16667u
#define ODG_PI_F 3.14159265358979323846f

typedef struct {
    float x;
    float y;
    float z;
} OdgVec3;

typedef struct {
    OdgVec3 min;
    OdgVec3 max;
    uint32_t color_rgba;
} OdgWorldBox;

typedef struct {
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
    float facing_yaw;
    float camera_yaw;
    float camera_pitch;
    float camera_distance;
    float camera_distance_velocity;
    float gait_distance;
    uint32_t grounded;
} OdgPlayerState;

typedef struct {
    OdgPlayerState player;
    uint64_t simulation_step;
    uint32_t overload_count;
} OdgEngine;

typedef struct {
    float move_x;
    float move_forward;
    float look_yaw_delta;
    float look_pitch_delta;
    uint32_t buttons_pressed;
    uint32_t buttons_held;
    uint64_t submitted_ns;
    uint64_t sequence;
} OdgConsumedInput;

typedef struct {
    float move_x;
    float move_forward;
    float look_yaw_accum;
    float look_pitch_accum;
    uint32_t buttons_pressed;
    uint32_t buttons_held;
    uint64_t submitted_ns;
    uint64_t sequence;
} OdgInputMailbox;

typedef struct {
    uint64_t sequence;
    uint64_t simulation_step;
    OdgPlayerState player;
} OdgRenderSnapshot;

typedef struct {
    uint32_t samples[ODG_PERF_RING_CAP];
    uint32_t count;
    uint32_t cursor;
} OdgPerfRing;

typedef struct {
    uint32_t *pixels;
    float *depth;
    uint32_t width;
    uint32_t height;
} OdgRaster;

struct OdgEngineService {
    OdgEngine engine;
    pthread_t simulation_thread;
    pthread_t render_thread;
    atomic_bool running;
    atomic_bool stop_requested;
    atomic_uint ref_count;

    pthread_mutex_t input_mu;
    OdgInputMailbox input;

    pthread_mutex_t snapshot_mu;
    OdgUiSnapshot ui_snapshot;
    OdgRenderSnapshot render_snapshot;

    pthread_mutex_t perf_mu;
    OdgPerfRing sim_perf;
    OdgPerfRing render_perf;
    uint32_t sim_max_us;
    uint32_t render_max_us;
    uint32_t sim_spikes_over_5ms;
    uint32_t render_spikes_over_16ms;

    pthread_mutex_t render_cfg_mu;
    uint32_t requested_render_width;
    uint32_t requested_render_height;
    OdgRaster raster;

    pthread_mutex_t surface_mu;
    void *native_window;
    uint32_t surface_width;
    uint32_t surface_height;
};

uint64_t odg_monotonic_ns(void);
void odg_sleep_until_ns(uint64_t deadline_ns);
float odg_clampf(float v, float lo, float hi);
float odg_wrap_pi(float v);
float odg_approach(float current, float target, float max_delta);
float odg_angle_approach(float current, float target, float max_delta);
void odg_perf_push(OdgPerfRing *ring, uint32_t sample_us);
uint32_t odg_perf_quantile(const OdgPerfRing *ring, uint32_t percentile);

void odg_engine_init(OdgEngine *engine);
void odg_engine_step(OdgEngine *engine, const OdgConsumedInput *input, float dt);
void odg_engine_make_render_snapshot(const OdgEngine *engine, uint64_t sequence, OdgRenderSnapshot *out);

const OdgWorldBox *odg_world_boxes(uint32_t *out_count);
int odg_world_player_position_valid(float x, float y, float z);
float odg_world_ground_height(float x, float z);
float odg_world_camera_distance(const OdgPlayerState *player, float desired_distance);

int odg_raster_init(OdgRaster *raster, uint32_t width, uint32_t height);
int odg_raster_resize(OdgRaster *raster, uint32_t width, uint32_t height);
void odg_raster_destroy(OdgRaster *raster);
void odg_raster_render(OdgRaster *raster, const OdgRenderSnapshot *snapshot);
uint64_t odg_raster_hash(const OdgRaster *raster);

void odg_android_attach_surface(OdgEngineService *service, void *surface);
void odg_android_detach_surface(OdgEngineService *service);
void odg_android_present(OdgEngineService *service, const OdgRaster *raster);

OdgEngineService *odg_service_create_internal(const OdgServiceConfig *config);
void odg_service_destroy_internal(OdgEngineService *service);
uint32_t odg_service_start_internal(OdgEngineService *service);
void odg_service_stop_internal(OdgEngineService *service);
uint32_t odg_service_submit_input_internal(OdgEngineService *service, const OdgInputFrame *frame);
uint32_t odg_service_copy_ui_snapshot_internal(OdgEngineService *service, OdgUiSnapshot *out_snapshot);
uint32_t odg_service_set_render_extent_internal(OdgEngineService *service, uint32_t width, uint32_t height);
void odg_service_retain_reference(OdgEngineService *service);
void odg_service_release_reference(OdgEngineService *service);

#endif
