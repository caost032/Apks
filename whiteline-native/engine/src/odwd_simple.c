#include "odwd_simple.h"
#include "odwd_core.h"

#include <string.h>

static odwd_storage bridge_storage;
static odwd_input bridge_input;
static odwd_frame bridge_frame;
static odwd_camera_snapshot bridge_camera;
static odwd_vehicle_snapshot bridge_vehicles[ODWD_MAX_VEHICLES];
static double bridge_accumulator;
static int bridge_ready;
static int bridge_paused;
static uint32_t bridge_quality = 2u;
static uint32_t bridge_seed = UINT32_C(0x574c4431);
static uint32_t bridge_rival_count = 5u;
static uint32_t bridge_world_mode = ODWD_MODE_ENDLESS;
static float bridge_music_buffer[8192];

static double bridge_abs(double value) { return value < 0.0 ? -value : value; }
static double bridge_clamp(double value, double low, double high) {
    return value < low ? low : (value > high ? high : value);
}

static int bridge_refresh(void) {
    uint32_t i;
    if (!bridge_ready) return ODWD_E_STATE;
    if (odwd_engine_read_frame(bridge_storage.bytes, &bridge_frame) != ODWD_OK)
        return ODWD_E_STATE;
    if (odwd_engine_read_camera(bridge_storage.bytes, &bridge_camera) != ODWD_OK)
        return ODWD_E_STATE;
    for (i = 0; i < bridge_frame.vehicle_count; ++i) {
        if (odwd_engine_read_vehicle(bridge_storage.bytes, i,
                                     &bridge_vehicles[i]) != ODWD_OK)
            return ODWD_E_STATE;
    }
    return ODWD_OK;
}

int od_init(uint32_t seed, uint32_t quality) {
    return od_init_mode(seed, quality, ODWD_MODE_ENDLESS);
}

uint32_t od_abi_version(void) { return ODWD_ABI_VERSION; }

void od_set_quality(uint32_t quality) {
    /* Physics is invariant across presentation quality tiers. */
    bridge_quality = quality > 2u ? 2u : quality;
}

uint32_t od_get_quality(void) { return bridge_quality; }

void od_set_camera_mode(uint32_t camera_mode) {
    bridge_input.camera_mode = camera_mode < ODWD_CAMERA_MODE_COUNT ?
                               camera_mode : ODWD_CAMERA_CHASE;
}

int od_init_ex(uint32_t seed, uint32_t rival_count) {
    bridge_seed = seed;
    bridge_rival_count = rival_count;
    bridge_world_mode = ODWD_MODE_ENDLESS;
    return od_init_mode(seed, bridge_quality, ODWD_MODE_ENDLESS);
}

int od_init_mode(uint32_t seed, uint32_t quality, uint32_t world_mode) {
    odwd_config config;
    int result;
    od_set_quality(quality);
    odwd_config_defaults(&config);
    config.seed = seed;
    /* The browser SURVIVAL preset uses the full eight-car core capacity. */
    config.rival_count = world_mode == ODWD_MODE_SURVIVAL ?
                         ODWD_MAX_VEHICLES - 1u : bridge_rival_count;
    config.section_length_m = 1536.0;
    config.checkpoint_spacing_m = 480.0;
    config.world_mode = world_mode;
    memset(&bridge_storage, 0, sizeof(bridge_storage));
    odwd_input_neutral(&bridge_input);
    bridge_accumulator = 0.0;
    bridge_paused = 0;
    bridge_ready = 0;
    bridge_seed = seed;
    bridge_world_mode = world_mode;
    result = odwd_engine_init(bridge_storage.bytes, sizeof(bridge_storage.bytes),
                              &config);
    if (result != ODWD_OK) return result;
    bridge_ready = 1;
    return bridge_refresh();
}

int od_restart_mode(uint32_t world_mode) {
    return od_init_mode(bridge_seed, bridge_quality, world_mode);
}

void od_set_input(float joystick_x, float joystick_y,
                  float camera_dx, float camera_dy,
                  int camera_active, int handbrake) {
    uint32_t pending_edges = bridge_input.buttons &
                             (ODWD_BUTTON_RESPAWN |
                              ODWD_BUTTON_HEADLIGHTS);
    bridge_input.joystick_x = (double)joystick_x;
    bridge_input.joystick_y = (double)joystick_y;
    /* Drag is an accumulated gesture; advance consumes it once. */
    bridge_input.look_dx += (double)camera_dx;
    bridge_input.look_dy += (double)camera_dy;
    bridge_input.buttons = pending_edges |
                           (camera_active ? ODWD_BUTTON_CAMERA_HOLD : 0u);
    if (handbrake) bridge_input.buttons |= ODWD_BUTTON_HANDBRAKE;
}

void od_set_controls(float steering, float throttle, float brake,
                     float camera_dx, float camera_dy, int camera_active,
                     int handbrake, int turbo, int headlights) {
    od_set_controls_v2(steering, throttle, brake, 0.0f,
                       camera_dx, camera_dy, camera_active,
                       handbrake, turbo, headlights, 0);
}

void od_set_controls_v2(float steering, float throttle, float brake,
                        float reverse, float camera_dx, float camera_dy,
                        int camera_active, int handbrake, int turbo,
                        int headlights, int jump) {
    uint32_t pending_edges = bridge_input.buttons &
                             (ODWD_BUTTON_RESPAWN |
                              ODWD_BUTTON_HEADLIGHTS);
    bridge_input.joystick_x = (double)steering;
    bridge_input.joystick_y = 0.0;
    bridge_input.throttle = (double)throttle;
    bridge_input.brake = (double)brake;
    bridge_input.reverse = (double)reverse;
    bridge_input.look_dx += (double)camera_dx;
    bridge_input.look_dy += (double)camera_dy;
    if (headlights < 0) pending_edges &= ~ODWD_BUTTON_HEADLIGHTS;
    bridge_input.buttons = pending_edges | ODWD_BUTTON_EXPLICIT_PEDALS |
                           (camera_active ? ODWD_BUTTON_CAMERA_HOLD : 0u);
    if (handbrake) bridge_input.buttons |= ODWD_BUTTON_HANDBRAKE;
    if (turbo) bridge_input.buttons |= ODWD_BUTTON_TURBO;
    if (headlights > 0) bridge_input.buttons |= ODWD_BUTTON_HEADLIGHTS;
    if (jump) bridge_input.buttons |= ODWD_BUTTON_JUMP;
}
void od_request_respawn(void) { bridge_input.buttons |= ODWD_BUTTON_RESPAWN; }

int od_step(void) {
    int result;
    const odwd_vehicle_snapshot *player;
    if (!bridge_ready) return ODWD_E_STATE;
    result = odwd_engine_step(bridge_storage.bytes, &bridge_input, &bridge_frame);
    bridge_input.look_dx = 0.0;
    bridge_input.look_dy = 0.0;
    /* Respawn/headlights are edges; driving controls remain level inputs. */
    bridge_input.buttons &= (ODWD_BUTTON_HANDBRAKE |
                             ODWD_BUTTON_CAMERA_HOLD |
                             ODWD_BUTTON_TURBO |
                             ODWD_BUTTON_EXPLICIT_PEDALS |
                             ODWD_BUTTON_JUMP);
    if (result != ODWD_OK) return result;
    result = bridge_refresh();
    if (result != ODWD_OK) return result;
    (void)player;
    return ODWD_OK;
}

uint32_t od_advance(double elapsed_seconds) {
    const double fixed_dt = 1.0 / (double)ODWD_TICK_HZ;
    uint32_t steps = 0u;
    uint32_t accumulated_events = 0u;
    if (!bridge_ready || bridge_paused ||
        elapsed_seconds != elapsed_seconds || elapsed_seconds <= 0.0)
        return 0u;
    if (elapsed_seconds > 0.25) elapsed_seconds = 0.25;
    bridge_frame.event_flags = 0u;
    bridge_accumulator += elapsed_seconds;
    while (bridge_accumulator + 1.0e-12 >= fixed_dt && steps < 30u) {
        if (od_step() != ODWD_OK) break;
        accumulated_events |= bridge_frame.event_flags;
        bridge_accumulator -= fixed_dt;
        ++steps;
    }
    bridge_frame.event_flags = accumulated_events;
    return steps;
}

uint64_t od_get_tick(void) { return bridge_ready ? bridge_frame.tick : 0u; }
uint32_t od_get_event_flags(void) { return bridge_ready ? bridge_frame.event_flags : 0u; }
uint32_t od_get_vehicle_count(void) { return bridge_ready ? bridge_frame.vehicle_count : 0u; }
uint32_t od_get_player_place(void) { return bridge_ready ? bridge_frame.player_place : 0u; }
uint32_t od_get_section_index(void) { return bridge_ready ? bridge_frame.section_index : 0u; }
double od_get_section_progress(void) { return bridge_ready ? bridge_frame.section_progress_01 : 0.0; }
double od_get_progress(void) { return bridge_ready ? bridge_frame.endless_progress_m : 0.0; }

static const odwd_vehicle_snapshot *bridge_vehicle(uint32_t index) {
    if (!bridge_ready || index >= bridge_frame.vehicle_count) return NULL;
    return &bridge_vehicles[index];
}

double od_get_car_x(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->position_x : 0.0; }
double od_get_car_y(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->position_y : 0.0; }
double od_get_car_z(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->position_z : 0.0; }
double od_get_car_yaw(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->heading_rad : 0.0; }
double od_get_car_speed(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->speed_mps : 0.0; }
double od_get_car_drift(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->drift_intensity : 0.0; }
double od_get_car_slip(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->slip_angle_rad : 0.0; }
double od_get_car_steer(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->steering_rad : 0.0; }
double od_get_car_lateral_speed(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->lateral_speed_mps : 0.0; }
double od_get_car_road_lateral(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->road_lateral_m : 0.0; }
double od_get_car_velocity_y(uint32_t index) { const odwd_vehicle_snapshot *v = bridge_vehicle(index); return v ? v->velocity_y : 0.0; }

double od_get_camera_x(void) { return bridge_ready ? bridge_camera.position_x : 0.0; }
double od_get_camera_y(void) { return bridge_ready ? bridge_camera.position_y : 0.0; }
double od_get_camera_z(void) { return bridge_ready ? bridge_camera.position_z : 0.0; }
double od_get_camera_target_x(void) { return bridge_ready ? bridge_camera.target_x : 0.0; }
double od_get_camera_target_y(void) { return bridge_ready ? bridge_camera.target_y : 0.0; }
double od_get_camera_target_z(void) { return bridge_ready ? bridge_camera.target_z : 0.0; }
double od_get_camera_fov(void) { return bridge_ready ? bridge_camera.vertical_fov_rad : 0.0; }
double od_get_camera_roll(void) { return bridge_ready ? bridge_camera.roll_rad : 0.0; }

uint32_t od_get_road_node_count(void) {
    return bridge_ready ? odwd_engine_road_node_count(bridge_storage.bytes) : 0u;
}

static int bridge_road(uint32_t index, odwd_road_node *node) {
    if (!bridge_ready) return 0;
    return odwd_engine_read_road_node(bridge_storage.bytes, index, node) == ODWD_OK;
}

double od_get_road_x(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.center_x : 0.0; }
double od_get_road_y(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.center_y : 0.0; }
double od_get_road_z(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.center_z : 0.0; }
double od_get_road_width(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.half_width_m : 0.0; }
double od_get_road_alt_x(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.alternate_x : 0.0; }
double od_get_road_alt_y(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.alternate_y : 0.0; }
double od_get_road_alt_z(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.alternate_z : 0.0; }
double od_get_road_progress(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.progress_m : 0.0; }
double od_get_road_curvature(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.curvature_per_m : 0.0; }
int64_t od_get_road_global_index(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.global_node_index : 0; }
uint32_t od_get_road_flags(uint32_t index) { odwd_road_node n; return bridge_road(index, &n) ? n.flags : 0u; }

double od_speed_kph(void) { return od_get_car_speed(0u) * 3.6; }
double od_drift_score(void) { return bridge_ready ? bridge_frame.drift_score : 0.0; }
double od_drift_chain(void) { return bridge_ready ? bridge_frame.drift_chain : 1.0; }
double od_sector_progress(void) { return od_get_section_progress(); }
uint32_t od_sector_index(void) { return od_get_section_index() + 1u; }
uint32_t od_race_position(void) { return od_get_player_place(); }
uint32_t od_racer_count(void) { return od_get_vehicle_count(); }
uint32_t od_checkpoint_index(void) {
    return bridge_ready ? (uint32_t)(bridge_frame.last_checkpoint_m / 480.0) : 0u;
}
double od_surface_grip(void) {
    const odwd_vehicle_snapshot *v = bridge_vehicle(0u);
    double edge;
    if (!v) return 1.0;
    edge = bridge_clamp((bridge_abs(v->road_lateral_m) - 6.0) / 5.0, 0.0, 1.0);
    return bridge_clamp(1.0 - v->drift_intensity * 0.34 - edge * 0.52,
                        0.12, 1.0);
}
double od_slip_angle(void) {
    return od_get_car_slip(0u) * 57.29577951308232;
}
double od_turbo_level(void) {
    const odwd_vehicle_snapshot *v = bridge_vehicle(0u);
    return v ? v->turbo_01 : 0.0;
}
uint32_t od_turbo_active(void) {
    const odwd_vehicle_snapshot *v = bridge_vehicle(0u);
    return v ? v->turbo_active : 0u;
}
double od_night_amount(void) {
    return bridge_ready ? bridge_frame.night_amount_01 : 0.0;
}
double od_music_energy(void) {
    return bridge_ready ? bridge_frame.music_energy_01 : 0.0;
}
double od_music_beat(void) {
    return bridge_ready ? bridge_frame.music_beat_01 : 0.0;
}
double od_music_bass(void) {
    return bridge_ready ? bridge_frame.music_bass_01 : 0.0;
}
double od_music_mid(void) {
    return bridge_ready ? bridge_frame.music_mid_01 : 0.0;
}
double od_music_high(void) {
    return bridge_ready ? bridge_frame.music_high_01 : 0.0;
}
double od_music_flux(void) {
    return bridge_ready ? bridge_frame.music_flux_01 : 0.0;
}
double od_music_pulse(void) {
    return bridge_ready ? bridge_frame.music_pulse_01 : 0.0;
}
double od_music_beat_phase(void) {
    return bridge_ready ? bridge_frame.music_beat_phase_01 : 0.0;
}
uint32_t od_world_mode(void) {
    return bridge_ready ? bridge_frame.world_mode : bridge_world_mode;
}
uint32_t od_headlights_on(void) {
    return bridge_ready ? bridge_frame.headlights_on : 0u;
}
uint32_t od_camera_mode(void) {
    return bridge_ready ? bridge_camera.camera_mode : ODWD_CAMERA_CHASE;
}
uint32_t od_activity_zone(void) {
    return bridge_ready ? bridge_frame.activity_zone : ODWD_ACTIVITY_EXPLORE;
}
uint32_t od_football_score_left(void) {
    return bridge_ready ? bridge_frame.football_score_left : 0u;
}
uint32_t od_football_score_right(void) {
    return bridge_ready ? bridge_frame.football_score_right : 0u;
}
uint32_t od_survival_alive_count(void) {
    return bridge_ready ? bridge_frame.survival_alive_count : 0u;
}
uint32_t od_survival_player_eliminated(void) {
    return bridge_ready ? bridge_frame.survival_player_eliminated : 0u;
}
uint32_t od_survival_final_place(void) {
    return bridge_ready ? bridge_frame.survival_final_place : 0u;
}
uint32_t od_survival_sector_family(void) {
    return bridge_ready ? bridge_frame.survival_sector_family : 0u;
}
double od_survival_sector_time(void) {
    return bridge_ready ? bridge_frame.survival_sector_time_01 : 0.0;
}
double od_music_survival_health(void) {
    return bridge_ready ? bridge_frame.music_survival_health_01 : 0.0;
}
double od_music_survival_score(void) {
    return bridge_ready ? bridge_frame.music_survival_score : 0.0;
}
uint32_t od_music_hazard_type(void) {
    return bridge_ready ? bridge_frame.music_hazard_type : 0u;
}
uint32_t od_music_hazard_phase(void) {
    return bridge_ready ? bridge_frame.music_hazard_phase : 0u;
}
uint32_t od_music_hazard_level(void) {
    return bridge_ready ? bridge_frame.music_hazard_level : 0u;
}
uint32_t od_music_enemy_pressure(void) {
    return bridge_ready ? bridge_frame.music_enemy_pressure : 0u;
}
double od_music_hazard_time(void) {
    return bridge_ready ? bridge_frame.music_hazard_time_01 : 0.0;
}
uint32_t od_music_buffer_ptr(void) {
    return (uint32_t)(uintptr_t)bridge_music_buffer;
}
uint32_t od_music_buffer_capacity(void) { return 8192u; }
int od_music_submit(uint32_t frame_count, uint32_t channels,
                    uint32_t sample_rate, double playback_time_s) {
    if (!bridge_ready || channels == 0u ||
        frame_count > od_music_buffer_capacity() / channels)
        return ODWD_E_ARGUMENT;
    return odwd_engine_submit_music_pcm(bridge_storage.bytes,
                                        bridge_music_buffer, frame_count,
                                        channels, sample_rate,
                                        playback_time_s);
}
void od_reset_checkpoint(void) { od_request_respawn(); }
void od_pause(int paused) { bridge_paused = paused != 0; }
const void *od_internal_storage(void) {
    return bridge_ready ? (const void *)bridge_storage.bytes : NULL;
}
