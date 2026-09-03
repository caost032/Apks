#ifndef ODWD_CORE_H
#define ODWD_CORE_H

/*
 * ODPAR WhiteLine Drift -- portable simulation ABI.
 *
 * This header deliberately exposes no renderer, browser, Flutter, or OS type.
 * The same state transition function is intended to be compiled as native C11
 * and as WebAssembly.  UI layers only translate touch/audio/file events.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODWD_ABI_VERSION          UINT32_C(0x00060000)
#define ODWD_TICK_HZ              UINT32_C(120)
#define ODWD_MAX_VEHICLES         UINT32_C(8)
#define ODWD_MAX_WORLD_PROPS      UINT32_C(320)
#define ODWD_MAX_TURBO_PICKUPS    UINT32_C(8)
#define ODWD_OPEN_GROUND_CELL_M   14.0
#define ODWD_ENGINE_STORAGE_BYTES ((size_t)131072)

enum {
    ODWD_OK = 0,
    ODWD_E_ARGUMENT = -1,
    ODWD_E_STORAGE = -2,
    ODWD_E_ABI = -3,
    ODWD_E_INDEX = -4,
    ODWD_E_STATE = -5
};

enum {
    ODWD_BUTTON_RESPAWN = 1u << 0,
    ODWD_BUTTON_HANDBRAKE = 1u << 1,
    /* Keeps a manually chosen camera heading while the right pointer is held. */
    ODWD_BUTTON_CAMERA_HOLD = 1u << 2,
    ODWD_BUTTON_TURBO = 1u << 3,
    ODWD_BUTTON_HEADLIGHTS = 1u << 4,
    /* Selects steering + pedals instead of the legacy 2D drive vector. */
    ODWD_BUTTON_EXPLICIT_PEDALS = 1u << 5,
    /* Level input: request powered reverse once forward speed is arrested. */
    ODWD_BUTTON_REVERSE = 1u << 6,
    /* Manual jump is authoritative only in SURVIVAL mode. */
    ODWD_BUTTON_JUMP = 1u << 7,
    /* Level input. The core owns every autonomous driving decision. */
    ODWD_BUTTON_AUTODRIVE = 1u << 8
};

enum {
    ODWD_EVENT_NONE = 0u,
    ODWD_EVENT_CHECKPOINT = 1u << 0,
    ODWD_EVENT_PLAYER_RESPAWN = 1u << 1,
    ODWD_EVENT_COLLISION = 1u << 2,
    ODWD_EVENT_STREAM_SHIFT = 1u << 3,
    ODWD_EVENT_SECTION = 1u << 4,
    ODWD_EVENT_TURBO_PICKUP = 1u << 5,
    ODWD_EVENT_JUMP = 1u << 6,
    ODWD_EVENT_LAND = 1u << 7,
    ODWD_EVENT_MUSIC_BEAT = 1u << 8,
    ODWD_EVENT_BALL_HIT = 1u << 9,
    ODWD_EVENT_GOAL = 1u << 10,
    ODWD_EVENT_ACTIVITY_ZONE = 1u << 11,
    ODWD_EVENT_TRAMPOLINE = 1u << 12,
    ODWD_EVENT_SURVIVAL_ELIMINATED = 1u << 13,
    ODWD_EVENT_SURVIVAL_SECTOR = 1u << 14,
    ODWD_EVENT_SURVIVAL_FINISH = 1u << 15,
    ODWD_EVENT_MUSIC_HAZARD_WARNING = 1u << 16,
    ODWD_EVENT_MUSIC_HAZARD_ACTIVE = 1u << 17,
    ODWD_EVENT_MUSIC_DAMAGE = 1u << 18,
    ODWD_EVENT_MUSIC_ENEMY_SPAWN = 1u << 19,
    ODWD_EVENT_MUSIC_FINISH = 1u << 20
};

enum {
    ODWD_ROAD_RIDGE = 1u << 0,
    ODWD_ROAD_GORGE = 1u << 1,
    ODWD_ROAD_HAIRPIN = 1u << 2,
    ODWD_ROAD_TUNNEL = 1u << 3,
    ODWD_ROAD_ALT_ROUTE = 1u << 4,
    ODWD_ROAD_RAMP = 1u << 5,
    ODWD_ROAD_GAP = 1u << 6,
    ODWD_ROAD_VIADUCT = 1u << 7,
    ODWD_ROAD_SPEED_SECTION = 1u << 8
};

enum {
    ODWD_MODE_ENDLESS = 0u,
    ODWD_MODE_OPEN_FIELD = 1u,
    ODWD_MODE_SURVIVAL = 2u,
    ODWD_MODE_MUSIC_SURVIVAL = 3u
};

enum {
    ODWD_CAMERA_CHASE = 0u,
    ODWD_CAMERA_CINEMATIC = 1u,
    ODWD_CAMERA_LOW_ACTION = 2u,
    ODWD_CAMERA_ORBIT = 3u,
    ODWD_CAMERA_LEFT_RIG = 4u,
    ODWD_CAMERA_RIGHT_RIG = 5u,
    ODWD_CAMERA_ROOF = 6u,
    ODWD_CAMERA_DIRECTOR = 7u,
    ODWD_CAMERA_MODE_COUNT = 8u
};

enum {
    ODWD_ACTIVITY_EXPLORE = 0u,
    ODWD_ACTIVITY_DRIFT_GARDEN = 1u,
    ODWD_ACTIVITY_JUMP_PARK = 2u,
    ODWD_ACTIVITY_FOOTBALL = 3u,
    ODWD_ACTIVITY_BUMPER_ARENA = 4u,
    ODWD_ACTIVITY_SKY_PEAK = 5u,
    ODWD_ACTIVITY_VILLAGE = 6u
};

enum {
    ODWD_PROP_TREE = 1u,
    ODWD_PROP_SHRUB = 2u,
    ODWD_PROP_ROCK = 3u,
    ODWD_PROP_SCULPTURE = 4u,
    ODWD_PROP_HOUSE = 5u,
    ODWD_PROP_FLOWER = 6u,
    ODWD_PROP_BIRD_GROUND = 7u,
    ODWD_PROP_BIRD_FLYING = 8u,
    ODWD_PROP_RAMP = 9u,
    ODWD_PROP_RAMP_LARGE = 10u,
    ODWD_PROP_TRAMPOLINE = 11u,
    ODWD_PROP_GOAL = 12u,
    ODWD_PROP_BARRIER = 13u,
    ODWD_PROP_BALL = 14u,
    ODWD_PROP_SURVIVAL_PLATFORM = 15u,
    ODWD_PROP_SURVIVAL_WALL = 16u,
    ODWD_PROP_SURVIVAL_COLUMN = 17u,
    ODWD_PROP_SURVIVAL_SWEEPER = 18u,
    ODWD_PROP_SURVIVAL_SAFE_TILE = 19u,
    ODWD_PROP_SURVIVAL_RAMP = 20u,
    ODWD_PROP_SURVIVAL_GATE = 21u,
    ODWD_PROP_MUSIC_PLATFORM = 22u,
    ODWD_PROP_MUSIC_WARNING = 23u,
    ODWD_PROP_MUSIC_LAVA = 24u,
    ODWD_PROP_MUSIC_METEOR = 25u,
    ODWD_PROP_MUSIC_SHELTER = 26u,
    ODWD_PROP_MUSIC_SAFE_PAD = 27u,
    ODWD_PROP_MUSIC_HOLE = 28u,
    ODWD_PROP_BUMPER_FLOOR = 29u,
    ODWD_PROP_MUSIC_BOUNDARY = 30u
};

enum {
    ODWD_PICKUP_TURBO = 1u
};

typedef struct odwd_config {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t seed;
    uint32_t rival_count;       /* 0..7 */
    double section_length_m;    /* progress loops visually; the race does not end */
    double checkpoint_spacing_m;
    uint32_t world_mode;
    uint32_t reserved_u32[7];
    double reserved_f64[8];
} odwd_config;

/*
 * Touch adapter convention:
 *   joystick_x: left/right in [-1,+1]
 *   joystick_y: backward/forward in [-1,+1]
 *   look_dx/dy: normalized per-tick drag delta from the right half of screen.
 * Camera-relative intent is resolved inside the core, not in JavaScript/Dart.
 */
typedef struct odwd_input {
    uint32_t struct_size;
    uint32_t buttons;
    double joystick_x;
    double joystick_y;
    double look_dx;
    double look_dy;
    double throttle;
    double brake;
    double reverse;
    double music_energy;
    double music_beat;
    uint32_t camera_mode;
    uint32_t reserved_u32[3];
} odwd_input;

typedef struct odwd_vehicle_snapshot {
    uint32_t struct_size;
    uint32_t vehicle_id;
    uint32_t is_player;
    uint32_t place;
    double position_x;
    double position_y;
    double position_z;
    double velocity_x;
    double velocity_y;
    double velocity_z;
    double heading_rad;
    double steering_rad;
    double speed_mps;
    double longitudinal_speed_mps;
    double lateral_speed_mps;
    double slip_angle_rad;
    double drift_intensity;
    double road_progress_m;
    double road_lateral_m;
    double checkpoint_progress_m;
    uint32_t respawn_count;
    uint32_t collision_count;
    uint32_t headlights_on;
    uint32_t airborne;
    uint32_t turbo_active;
    uint32_t reserved_u32[3];
    double turbo_01;
    double air_time_s;
    double traveled_distance_m;
    double body_pitch_rad;
    double body_roll_rad;
    double reserved_f64[1];
} odwd_vehicle_snapshot;

typedef struct odwd_camera_snapshot {
    uint32_t struct_size;
    uint32_t camera_mode;
    double position_x;
    double position_y;
    double position_z;
    double target_x;
    double target_y;
    double target_z;
    double forward_yaw_rad;
    double pitch_rad;
    double roll_rad;
    double distance_m;
    double vertical_fov_rad;
    double speed_response;
    double collision_impulse;
    double reserved_f64[4];
} odwd_camera_snapshot;

/* One center-line sample.  The alternate route is valid when ALT_ROUTE is set. */
typedef struct odwd_road_node {
    uint32_t struct_size;
    uint32_t flags;
    int64_t global_node_index;
    double progress_m;
    double center_x;
    double center_y;
    double center_z;
    double tangent_x;
    double tangent_y;
    double tangent_z;
    double half_width_m;
    double alternate_x;
    double alternate_y;
    double alternate_z;
    double alternate_half_width_m;
    double curvature_per_m;
    double alternate_tangent_x;
    double alternate_tangent_y;
    double alternate_tangent_z;
    double surface_grip_01;
} odwd_road_node;

typedef struct odwd_world_prop_snapshot {
    uint32_t struct_size;
    uint32_t prop_id;
    uint32_t type;
    uint32_t collidable;
    double position_x;
    double position_y;
    double position_z;
    double radius_m;
    double scale;
    double rotation_rad;
    uint32_t variant;
    uint32_t reserved_u32[3];
    double extent_x_m;
    double extent_y_m;
    double extent_z_m;
} odwd_world_prop_snapshot;

typedef struct odwd_pickup_snapshot {
    uint32_t struct_size;
    uint32_t pickup_id;
    uint32_t type;
    uint32_t active;
    double position_x;
    double position_y;
    double position_z;
    double progress_m;
    double amount_01;
    uint32_t reserved_u32[4];
    double reserved_f64[2];
} odwd_pickup_snapshot;

typedef struct odwd_frame {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t tick;
    uint32_t event_flags;
    uint32_t vehicle_count;
    uint32_t player_place;
    uint32_t section_index;
    uint32_t world_mode;
    uint32_t sector_style;
    uint32_t headlights_on;
    uint32_t activity_zone;
    uint32_t football_score_left;
    uint32_t football_score_right;
    double simulation_time_s;
    double section_progress_01;
    double endless_progress_m;
    double last_checkpoint_m;
    double drift_score;
    double drift_chain;
    uint64_t deterministic_state_hash;
    double day_phase_01;
    double night_amount_01;
    double music_energy_01;
    double music_beat_01;
    uint32_t survival_alive_count;
    uint32_t survival_player_eliminated;
    uint32_t survival_final_place;
    uint32_t survival_sector_family;
    double survival_sector_time_01;
    double music_bass_01;
    double music_mid_01;
    double music_high_01;
    double music_flux_01;
    double music_pulse_01;
    double music_beat_phase_01;
    double music_survival_health_01;
    double music_survival_score;
    uint32_t music_hazard_type;
    uint32_t music_hazard_phase;
    uint32_t music_hazard_level;
    uint32_t music_enemy_pressure;
    uint32_t music_survival_finished;
    uint32_t autodrive_active;
    double music_hazard_time_01;
} odwd_frame;

/* Convenient correctly aligned caller-owned storage; using it is optional. */
typedef union odwd_storage {
    max_align_t alignment;
    unsigned char bytes[ODWD_ENGINE_STORAGE_BYTES];
} odwd_storage;

void odwd_config_defaults(odwd_config *config);
void odwd_input_neutral(odwd_input *input);

size_t odwd_engine_required_bytes(void);
uint32_t odwd_engine_required_alignment(void);

int odwd_engine_init(void *storage, size_t storage_bytes,
                     const odwd_config *config);
int odwd_engine_step(void *storage, const odwd_input *input,
                     odwd_frame *frame_out);
int odwd_engine_read_frame(const void *storage, odwd_frame *frame_out);
int odwd_engine_read_vehicle(const void *storage, uint32_t vehicle_index,
                             odwd_vehicle_snapshot *vehicle_out);
int odwd_engine_read_camera(const void *storage,
                            odwd_camera_snapshot *camera_out);

uint32_t odwd_engine_road_node_count(const void *storage);
int odwd_engine_read_road_node(const void *storage, uint32_t node_index,
                               odwd_road_node *node_out);

uint32_t odwd_engine_world_prop_count(const void *storage);
int odwd_engine_read_world_prop(const void *storage, uint32_t prop_index,
                                odwd_world_prop_snapshot *prop_out);
uint32_t odwd_engine_pickup_count(const void *storage);
int odwd_engine_read_pickup(const void *storage, uint32_t pickup_index,
                            odwd_pickup_snapshot *pickup_out);
double odwd_engine_ground_height(const void *storage, double x, double z);
double odwd_engine_base_ground_height(const void *storage, double x, double z);

/* Canonical PCM analysis lives in C. Hosts decode media and submit interleaved
 * samples aligned to their local playback clock; they do not decide beats. */
int odwd_engine_submit_music_pcm(void *storage, const float *interleaved,
                                 uint32_t frame_count, uint32_t channels,
                                 uint32_t sample_rate,
                                 double playback_time_s);

uint64_t odwd_engine_state_hash(const void *storage);

#ifdef __cplusplus
}
#endif

#endif /* ODWD_CORE_H */
