/* GENERATED from host/schema/host_api.json. DO NOT EDIT. */
#ifndef ODPAR_GREENFIELD_ODG_HOST_H
#define ODPAR_GREENFIELD_ODG_HOST_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#define ODG_EXPORT __declspec(dllexport)
#else
#define ODG_EXPORT __attribute__((visibility("default")))
#endif
#define ODG_HOST_ABI_VERSION UINT32_C(1)
#define ODG_STATUS_OK UINT32_C(0)
#define ODG_STATUS_INVALID_ARGUMENT UINT32_C(1)
#define ODG_STATUS_STATE UINT32_C(2)
#define ODG_STATUS_UNSUPPORTED UINT32_C(3)
#define ODG_BUTTON_JUMP UINT32_C(1)

typedef struct OdgEngineService OdgEngineService;

typedef struct OdgServiceConfig {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t render_width;
    uint32_t render_height;
} OdgServiceConfig;
_Static_assert(sizeof(OdgServiceConfig) == 16u, "OdgServiceConfig size");
_Static_assert(offsetof(OdgServiceConfig, struct_size) == 0u, "OdgServiceConfig.struct_size offset");
_Static_assert(offsetof(OdgServiceConfig, abi_version) == 4u, "OdgServiceConfig.abi_version offset");
_Static_assert(offsetof(OdgServiceConfig, render_width) == 8u, "OdgServiceConfig.render_width offset");
_Static_assert(offsetof(OdgServiceConfig, render_height) == 12u, "OdgServiceConfig.render_height offset");

typedef struct OdgInputFrame {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t sequence;
    int16_t move_x_q15;
    int16_t move_forward_q15;
    int16_t look_yaw_q15;
    int16_t look_pitch_q15;
    uint32_t buttons_pressed;
    uint32_t buttons_held;
} OdgInputFrame;
_Static_assert(sizeof(OdgInputFrame) == 32u, "OdgInputFrame size");
_Static_assert(offsetof(OdgInputFrame, struct_size) == 0u, "OdgInputFrame.struct_size offset");
_Static_assert(offsetof(OdgInputFrame, abi_version) == 4u, "OdgInputFrame.abi_version offset");
_Static_assert(offsetof(OdgInputFrame, sequence) == 8u, "OdgInputFrame.sequence offset");
_Static_assert(offsetof(OdgInputFrame, move_x_q15) == 16u, "OdgInputFrame.move_x_q15 offset");
_Static_assert(offsetof(OdgInputFrame, move_forward_q15) == 18u, "OdgInputFrame.move_forward_q15 offset");
_Static_assert(offsetof(OdgInputFrame, look_yaw_q15) == 20u, "OdgInputFrame.look_yaw_q15 offset");
_Static_assert(offsetof(OdgInputFrame, look_pitch_q15) == 22u, "OdgInputFrame.look_pitch_q15 offset");
_Static_assert(offsetof(OdgInputFrame, buttons_pressed) == 24u, "OdgInputFrame.buttons_pressed offset");
_Static_assert(offsetof(OdgInputFrame, buttons_held) == 28u, "OdgInputFrame.buttons_held offset");

typedef struct OdgUiSnapshot {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t sequence;
    uint64_t simulation_step;
    uint64_t published_ns;
    float player_x;
    float player_y;
    float player_z;
    float player_speed;
    float player_facing_yaw;
    float camera_yaw;
    float camera_pitch;
    float camera_distance;
    uint32_t grounded;
    uint32_t overload_count;
    uint32_t sim_p50_us;
    uint32_t sim_p95_us;
    uint32_t sim_p99_us;
    uint32_t sim_max_us;
    uint32_t sim_spikes_over_5ms;
    uint32_t render_p50_us;
    uint32_t render_p95_us;
    uint32_t render_p99_us;
    uint32_t render_max_us;
    uint32_t render_spikes_over_16ms;
    uint32_t input_age_us;
    uint32_t render_width;
    uint32_t render_height;
} OdgUiSnapshot;
_Static_assert(sizeof(OdgUiSnapshot) == 128u, "OdgUiSnapshot size");
_Static_assert(offsetof(OdgUiSnapshot, struct_size) == 0u, "OdgUiSnapshot.struct_size offset");
_Static_assert(offsetof(OdgUiSnapshot, abi_version) == 4u, "OdgUiSnapshot.abi_version offset");
_Static_assert(offsetof(OdgUiSnapshot, sequence) == 8u, "OdgUiSnapshot.sequence offset");
_Static_assert(offsetof(OdgUiSnapshot, simulation_step) == 16u, "OdgUiSnapshot.simulation_step offset");
_Static_assert(offsetof(OdgUiSnapshot, published_ns) == 24u, "OdgUiSnapshot.published_ns offset");
_Static_assert(offsetof(OdgUiSnapshot, player_x) == 32u, "OdgUiSnapshot.player_x offset");
_Static_assert(offsetof(OdgUiSnapshot, player_y) == 36u, "OdgUiSnapshot.player_y offset");
_Static_assert(offsetof(OdgUiSnapshot, player_z) == 40u, "OdgUiSnapshot.player_z offset");
_Static_assert(offsetof(OdgUiSnapshot, player_speed) == 44u, "OdgUiSnapshot.player_speed offset");
_Static_assert(offsetof(OdgUiSnapshot, player_facing_yaw) == 48u, "OdgUiSnapshot.player_facing_yaw offset");
_Static_assert(offsetof(OdgUiSnapshot, camera_yaw) == 52u, "OdgUiSnapshot.camera_yaw offset");
_Static_assert(offsetof(OdgUiSnapshot, camera_pitch) == 56u, "OdgUiSnapshot.camera_pitch offset");
_Static_assert(offsetof(OdgUiSnapshot, camera_distance) == 60u, "OdgUiSnapshot.camera_distance offset");
_Static_assert(offsetof(OdgUiSnapshot, grounded) == 64u, "OdgUiSnapshot.grounded offset");
_Static_assert(offsetof(OdgUiSnapshot, overload_count) == 68u, "OdgUiSnapshot.overload_count offset");
_Static_assert(offsetof(OdgUiSnapshot, sim_p50_us) == 72u, "OdgUiSnapshot.sim_p50_us offset");
_Static_assert(offsetof(OdgUiSnapshot, sim_p95_us) == 76u, "OdgUiSnapshot.sim_p95_us offset");
_Static_assert(offsetof(OdgUiSnapshot, sim_p99_us) == 80u, "OdgUiSnapshot.sim_p99_us offset");
_Static_assert(offsetof(OdgUiSnapshot, sim_max_us) == 84u, "OdgUiSnapshot.sim_max_us offset");
_Static_assert(offsetof(OdgUiSnapshot, sim_spikes_over_5ms) == 88u, "OdgUiSnapshot.sim_spikes_over_5ms offset");
_Static_assert(offsetof(OdgUiSnapshot, render_p50_us) == 92u, "OdgUiSnapshot.render_p50_us offset");
_Static_assert(offsetof(OdgUiSnapshot, render_p95_us) == 96u, "OdgUiSnapshot.render_p95_us offset");
_Static_assert(offsetof(OdgUiSnapshot, render_p99_us) == 100u, "OdgUiSnapshot.render_p99_us offset");
_Static_assert(offsetof(OdgUiSnapshot, render_max_us) == 104u, "OdgUiSnapshot.render_max_us offset");
_Static_assert(offsetof(OdgUiSnapshot, render_spikes_over_16ms) == 108u, "OdgUiSnapshot.render_spikes_over_16ms offset");
_Static_assert(offsetof(OdgUiSnapshot, input_age_us) == 112u, "OdgUiSnapshot.input_age_us offset");
_Static_assert(offsetof(OdgUiSnapshot, render_width) == 116u, "OdgUiSnapshot.render_width offset");
_Static_assert(offsetof(OdgUiSnapshot, render_height) == 120u, "OdgUiSnapshot.render_height offset");

ODG_EXPORT uint32_t odg_host_abi_version(void);
ODG_EXPORT OdgEngineService *odg_service_create(const OdgServiceConfig *config);
ODG_EXPORT void odg_service_destroy(OdgEngineService *service);
ODG_EXPORT uint32_t odg_service_start(OdgEngineService *service);
ODG_EXPORT void odg_service_stop(OdgEngineService *service);
ODG_EXPORT uint32_t odg_service_submit_input(OdgEngineService *service, const OdgInputFrame *frame);
ODG_EXPORT uint32_t odg_service_copy_ui_snapshot(OdgEngineService *service, OdgUiSnapshot *out_snapshot);
ODG_EXPORT uint32_t odg_service_set_render_extent(OdgEngineService *service, uint32_t width, uint32_t height);

#endif
