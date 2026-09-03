#ifndef ODWD_SIMPLE_H
#define ODWD_SIMPLE_H

/* Optional scalar/global facade for small WebAssembly hosts.
 * Native/Flutter integrations should prefer the reentrant odwd_core.h API. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int od_init(uint32_t seed, uint32_t quality);
uint32_t od_abi_version(void);
int od_init_ex(uint32_t seed, uint32_t rival_count);
int od_init_mode(uint32_t seed, uint32_t quality, uint32_t world_mode);
int od_restart_mode(uint32_t world_mode);
void od_set_quality(uint32_t quality);
uint32_t od_get_quality(void);
void od_set_camera_mode(uint32_t camera_mode);
void od_set_input(float joystick_x, float joystick_y,
                  float camera_dx, float camera_dy,
                  int camera_active, int handbrake);
void od_set_controls(float steering, float throttle, float brake,
                     float camera_dx, float camera_dy, int camera_active,
                     int handbrake, int turbo, int headlights);
void od_set_controls_v2(float steering, float throttle, float brake,
                        float reverse, float camera_dx, float camera_dy,
                        int camera_active, int handbrake, int turbo,
                        int headlights, int jump);
/* headlights: 1 queues a toggle edge, 0 leaves it unchanged, -1 cancels an
 * edge that has not reached a fixed tick yet (web blur/pause neutralization). */
void od_request_respawn(void);
uint32_t od_advance(double elapsed_seconds);
int od_step(void);

uint64_t od_get_tick(void);
uint32_t od_get_event_flags(void);
uint32_t od_get_vehicle_count(void);
uint32_t od_get_player_place(void);
uint32_t od_get_section_index(void);
double od_get_section_progress(void);
double od_get_progress(void);

double od_get_car_x(uint32_t index);
double od_get_car_y(uint32_t index);
double od_get_car_z(uint32_t index);
double od_get_car_yaw(uint32_t index);
double od_get_car_speed(uint32_t index);
double od_get_car_drift(uint32_t index);
double od_get_car_slip(uint32_t index);
double od_get_car_steer(uint32_t index);
double od_get_car_lateral_speed(uint32_t index);
double od_get_car_road_lateral(uint32_t index);
double od_get_car_velocity_y(uint32_t index);

double od_get_camera_x(void);
double od_get_camera_y(void);
double od_get_camera_z(void);
double od_get_camera_target_x(void);
double od_get_camera_target_y(void);
double od_get_camera_target_z(void);
double od_get_camera_fov(void);
double od_get_camera_roll(void);

uint32_t od_get_road_node_count(void);
double od_get_road_x(uint32_t index);
double od_get_road_y(uint32_t index);
double od_get_road_z(uint32_t index);
double od_get_road_width(uint32_t index);
double od_get_road_alt_x(uint32_t index);
double od_get_road_alt_y(uint32_t index);
double od_get_road_alt_z(uint32_t index);
double od_get_road_progress(uint32_t index);
double od_get_road_curvature(uint32_t index);
int64_t od_get_road_global_index(uint32_t index);
uint32_t od_get_road_flags(uint32_t index);

/* HUD/read-only aliases.  Presentation reads engine facts; it does not
 * re-simulate them in JavaScript or Dart. */
double od_speed_kph(void);
double od_drift_score(void);
double od_drift_chain(void);
double od_sector_progress(void);
uint32_t od_sector_index(void);
uint32_t od_race_position(void);
uint32_t od_racer_count(void);
uint32_t od_checkpoint_index(void);
double od_surface_grip(void);
double od_slip_angle(void);
double od_turbo_level(void);
uint32_t od_turbo_active(void);
double od_night_amount(void);
double od_music_energy(void);
double od_music_beat(void);
double od_music_bass(void);
double od_music_mid(void);
double od_music_high(void);
double od_music_flux(void);
double od_music_pulse(void);
double od_music_beat_phase(void);
uint32_t od_world_mode(void);
uint32_t od_headlights_on(void);
uint32_t od_camera_mode(void);
uint32_t od_activity_zone(void);
uint32_t od_football_score_left(void);
uint32_t od_football_score_right(void);
uint32_t od_survival_alive_count(void);
uint32_t od_survival_player_eliminated(void);
uint32_t od_survival_final_place(void);
uint32_t od_survival_sector_family(void);
double od_survival_sector_time(void);
double od_music_survival_health(void);
double od_music_survival_score(void);
uint32_t od_music_hazard_type(void);
uint32_t od_music_hazard_phase(void);
uint32_t od_music_hazard_level(void);
uint32_t od_music_enemy_pressure(void);
double od_music_hazard_time(void);

uint32_t od_music_buffer_ptr(void);
uint32_t od_music_buffer_capacity(void);
int od_music_submit(uint32_t frame_count, uint32_t channels,
                    uint32_t sample_rate, double playback_time_s);
void od_reset_checkpoint(void);
void od_pause(int paused);

/* Internal adapter hook used by the C renderer. Not part of the FFI surface. */
const void *od_internal_storage(void);

#ifdef __cplusplus
}
#endif

#endif /* ODWD_SIMPLE_H */
