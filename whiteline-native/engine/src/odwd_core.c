#include "odwd_core.h"

#include <string.h>

#define ODWD_MAGIC UINT64_C(0x4f445744434f5245)
#define ROAD_CAP 288u
#define ROAD_NODE_SPACING 12.0
#define ROAD_CHUNK_NODES 16u
#define ROAD_SHIFT 48u
#define ROAD_SHIFT_AT 104u
#define OPEN_PROP_CELL_M 34.0
#define DAY_CYCLE_SECONDS 168.0
#define OPEN_FOOTBALL_X 160.0
#define OPEN_FOOTBALL_Z 92.0
#define OPEN_BUMPER_X -170.0
#define OPEN_BUMPER_Z 112.0
#define OPEN_PEAK_X 54.0
#define OPEN_PEAK_Z 350.0
#define OPEN_GOAL_PLANE_HALF 54.0
#define OPEN_GOAL_MOUTH_HALF 5.8
#define OPEN_GOAL_HEIGHT 4.6
#define OPEN_GOAL_POST_RADIUS 0.28
#define OPEN_BALL_RADIUS 0.92
#define OPEN_FOOTBALL_TOUCHLINE_HALF 35.0
#define OPEN_BARRIER_HALF_LENGTH 6.2
#define OPEN_BARRIER_HALF_DEPTH 0.82
#define VEHICLE_PROP_RADIUS 1.12
#define VEHICLE_ENVELOPE_HALF_LENGTH 2.37
#define VEHICLE_ENVELOPE_HALF_WIDTH 1.38
#define SURVIVAL_ARENA_Y 22.0
#define SURVIVAL_ARENA_HALF_W 21.0
#define SURVIVAL_ARENA_HALF_D 15.0
#define SURVIVAL_MAX_OBSTACLES 48u
#define SURVIVAL_MAX_PLATFORMS 18u
#define SURVIVAL_HISTORY 6u
#define SURVIVAL_FAMILY_COUNT 17u
#define MUSIC_ARENA_Y 18.0
#define MUSIC_HAZARD_MAX 8u
#define MUSIC_SAFE_MAX 3u
#define MUSIC_EVENT_COOLDOWN 0u
#define MUSIC_EVENT_WARNING 1u
#define MUSIC_EVENT_ACTIVE 2u
/* Includes the car body plus the measured maximum node/segment tangent
 * difference, so the visible road edge and collision edge never disagree. */
#define ROAD_EDGE_CLEARANCE 1.54
#define PI 3.14159265358979323846264338327950288
#define HALF_PI 1.57079632679489661923132169163975144
#define TWO_PI 6.28318530717958647692528676655900576
#define DT (1.0 / 120.0)

typedef struct road_internal {
    int64_t global_index;
    double s;
    double x;
    double y;
    double z;
    double heading;
    double curvature;
    double half_width;
    double alt_offset;
    double alt_height;
    uint32_t flags;
} road_internal;

typedef struct projection {
    uint32_t segment;
    uint32_t alternate;
    double t;
    double s;
    double cx;
    double cy;
    double cz;
    double tx;
    double ty;
    double tz;
    double rx;
    double rz;
    double lateral;
    double half_width;
    double distance_sq;
    uint32_t flags;
} projection;

typedef struct prop_internal {
    double x;
    double y;
    double z;
    double radius;
    double scale;
    double rotation;
    double extent_x;
    double extent_y;
    double extent_z;
    uint32_t id;
    uint32_t type;
    uint32_t collidable;
    uint32_t variant;
} prop_internal;

typedef struct pickup_internal {
    double x;
    double y;
    double z;
    double progress;
    double amount;
    uint32_t id;
    uint32_t generation;
    uint32_t active;
} pickup_internal;

typedef struct ball_internal {
    double x;
    double y;
    double z;
    double vx;
    double vy;
    double vz;
    double roll_angle;
    double roll_axis_x;
    double roll_axis_z;
    double reset_timer;
    double shot_cooldown;
} ball_internal;

typedef struct open_ramp_definition {
    double x;
    double z;
    double yaw;
    double length;
    double width;
    double height;
    double launch_speed;
    uint32_t prop_type;
} open_ramp_definition;

static const open_ramp_definition open_ramps[] = {
    { 72.0,  -94.0,  0.00, 18.0,  9.0,  4.6,  8.8, ODWD_PROP_RAMP },
    {-38.0, -132.0,  HALF_PI, 16.0,  8.0,  4.1,  8.2, ODWD_PROP_RAMP },
    {118.0, -154.0, -0.62, 20.0,  9.5,  5.2,  9.4, ODWD_PROP_RAMP },
    /* Keep the authored mountain launch as its own strong, already-proven
     * landmark. Regular ramps below/elsewhere use the improved launch path. */
    { 54.0,  430.0,  0.00, 34.0, 13.0, 10.5, 16.8, ODWD_PROP_RAMP_LARGE },
    {-154.0,  108.0,  0.82, 22.0,  9.0,  5.6, 10.2, ODWD_PROP_RAMP },
    { 242.0,  -68.0, -1.20, 20.0,  8.5,  4.8,  9.8, ODWD_PROP_RAMP },
    {-278.0,  -46.0,  0.15, 24.0, 10.0,  6.0, 10.8, ODWD_PROP_RAMP },
    { 305.0,  225.0,  2.35, 22.0,  9.0,  5.4, 10.4, ODWD_PROP_RAMP },
    {-320.0,  285.0, -0.75, 26.0, 10.0,  6.8, 11.5, ODWD_PROP_RAMP },
    { 170.0,  330.0,  1.55, 18.0,  8.5,  4.5,  9.5, ODWD_PROP_RAMP }
};

static const double open_trampoline_pads[][2] = {
    { 24.0,  -88.0}, { 98.0, -112.0}, { -8.0, -166.0},
    {-112.0,  176.0}, {214.0, 172.0}, {-246.0, 218.0},
    { 286.0, -246.0}, { 12.0,  282.0}, {-352.0, -118.0}
};

/* Far-field authored skill lines. They keep the expanded open world from
 * becoming scenery-only after the central activity cluster. */
static const double open_stunt_barriers[][3] = {
    { 246.0,  286.0,  0.12}, { 255.0,  300.0, -0.10},
    { 239.0,  314.0,  0.18}, { 258.0,  328.0, -0.16},
    {-286.0,  148.0,  1.55}, {-301.0,  158.0,  1.48},
    {-286.0,  170.0,  1.63}, {-302.0,  181.0,  1.51},
    { 315.0, -154.0, -0.76}, { 328.0, -143.0, -0.70},
    { 314.0, -130.0, -0.82}, { 330.0, -118.0, -0.72}
};

typedef struct vehicle_gameplay_envelope {
    double half_length;
    double half_width;
    double half_height;
    double max_useful_speed;
    double acceleration;
    double braking;
    double lateral_capacity;
    double turn_radius;
    double jump_impulse;
    double jump_distance;
} vehicle_gameplay_envelope;

typedef struct survival_obstacle {
    double x, y, z;
    double half_x, half_y, half_z;
    double yaw;
    double vx, vz;
    double angular_speed;
    double origin_x, origin_z;
    double amplitude;
    uint32_t type;
    uint32_t active;
    uint32_t variant;
} survival_obstacle;

typedef struct survival_platform {
    double x, y, z;
    double half_x, half_z;
    double vx, vz;
    uint32_t active;
    uint32_t variant;
} survival_platform;

typedef struct survival_state {
    vehicle_gameplay_envelope envelope;
    survival_obstacle obstacles[SURVIVAL_MAX_OBSTACLES];
    survival_platform platforms[SURVIVAL_MAX_PLATFORMS];
    uint64_t elimination_tick[ODWD_MAX_VEHICLES];
    uint32_t eliminated[ODWD_MAX_VEHICLES];
    uint32_t elimination_place[ODWD_MAX_VEHICLES];
    uint32_t obstacle_count;
    uint32_t platform_count;
    uint32_t sector_index;
    uint32_t family;
    uint32_t history[SURVIVAL_HISTORY];
    uint32_t history_count;
    uint32_t alive_count;
    uint32_t player_final_place;
    uint32_t finished;
    uint32_t solution_count;
    double solution_x[3];
    double sector_elapsed;
    double sector_duration;
    double warning_time;
    double difficulty;
    double obstacle_speed;
    uint32_t requires_jump;
} survival_state;

typedef struct music_hazard_zone {
    double x, z;
    double radius;
    double strength;
    uint32_t variant;
} music_hazard_zone;

typedef struct music_safe_zone {
    double x, z;
    double radius;
    double height;
    uint32_t level;
    uint32_t shelter;
} music_safe_zone;

typedef struct music_survival_state {
    music_hazard_zone hazards[MUSIC_HAZARD_MAX];
    music_safe_zone safe[MUSIC_SAFE_MAX];
    double half_w, half_d;
    double health;
    double score;
    double elapsed;
    double phase_elapsed;
    double phase_duration;
    double damage_cooldown;
    double enemy_spawn_cooldown;
    uint32_t event_index;
    uint32_t map_variant;
    uint32_t hazard_type;
    uint32_t hazard_phase;
    uint32_t hazard_level;
    uint32_t hazard_count;
    uint32_t safe_count;
    uint32_t enemy_pressure;
    uint32_t finished;
} music_survival_state;

enum {
    MUSIC_HAZARD_NONE = 0u,
    MUSIC_HAZARD_HOLES = 1u,
    MUSIC_HAZARD_LAVA_RAIN = 2u,
    MUSIC_HAZARD_QUAKE = 3u,
    MUSIC_HAZARD_METEORS = 4u,
    MUSIC_HAZARD_LAVA_FLOOD = 5u,
    MUSIC_HAZARD_WIND = 6u,
    MUSIC_HAZARD_SHOCKWAVE = 7u,
    MUSIC_HAZARD_LIGHTNING = 8u
};

enum {
    MUSIC_ENEMY_NONE = 0u,
    MUSIC_ENEMY_STRIKER = 1u,
    MUSIC_ENEMY_CHASER = 2u,
    MUSIC_ENEMY_PUSHER = 3u
};

enum {
    SURVIVAL_WALL_GAP = 0u,
    SURVIVAL_DOUBLE_WALL = 1u,
    SURVIVAL_MOVING_GAP = 2u,
    SURVIVAL_GATE = 3u,
    SURVIVAL_LOW_WALL = 4u,
    SURVIVAL_ELEVATED = 5u,
    SURVIVAL_FRAGMENTED = 6u,
    SURVIVAL_BRIDGE = 7u,
    SURVIVAL_ISLANDS = 8u,
    SURVIVAL_RAMP_GAP = 9u,
    SURVIVAL_SLALOM = 10u,
    SURVIVAL_SWEEPER = 11u,
    SURVIVAL_ROTATOR = 12u,
    SURVIVAL_ROUTE_CHOICE = 13u,
    SURVIVAL_FUNNEL = 14u,
    SURVIVAL_MOVING_FLOOR = 15u,
    SURVIVAL_PRECISION = 16u
};

typedef struct vehicle_internal {
    double x;
    double y;
    double z;
    double vx;
    double vy;
    double vz;
    double yaw;
    double yaw_rate;
    double steer;
    double longitudinal;
    double lateral_speed;
    double slip_angle;
    double drift;
    double progress;
    double road_lateral;
    double checkpoint;
    double checkpoint_x;
    double checkpoint_z;
    double checkpoint_yaw;
    double offroad_time;
    double stuck_time;
    double collision_cooldown;
    double last_collision_impulse;
    double turbo;
    double air_time;
    double traveled_distance;
    double previous_x;
    double previous_z;
    double body_pitch;
    double body_roll;
    double pitch_rate;
    double roll_rate;
    double jump_cooldown;
    double ai_lane;
    double ai_skill;
    double ai_timer;
    double ai_target_x;
    double ai_target_z;
    uint32_t ai_archetype;
    uint32_t ai_state;
    uint32_t road_segment;
    uint32_t route_choice;
    uint32_t respawns;
    uint32_t collisions;
    uint32_t place;
    uint32_t is_player;
    uint32_t headlights;
    uint32_t turbo_active;
    uint32_t airborne;
} vehicle_internal;

typedef struct camera_internal {
    double x;
    double y;
    double z;
    double target_x;
    double target_y;
    double target_z;
    double focus_x;
    double focus_y;
    double focus_z;
    double focus_vx;
    double focus_vy;
    double focus_vz;
    double view_yaw;
    double view_yaw_velocity;
    double pitch;
    double pitch_target;
    double pitch_velocity;
    double roll;
    double roll_velocity;
    double distance;
    double distance_velocity;
    double fov;
    double fov_velocity;
    double manual_idle;
    double showcase_idle;
    double collision_impulse;
    uint32_t mode;
} camera_internal;

typedef struct odwd_engine_internal {
    uint64_t magic;
    odwd_config config;
    uint64_t tick;
    uint32_t event_flags;
    uint32_t vehicle_count;
    uint32_t section_index;
    uint32_t player_jump_down;
    uint32_t autodrive_active;
    road_internal road[ROAD_CAP];
    uint32_t road_count;
    int64_t road_global_first;
    vehicle_internal vehicles[ODWD_MAX_VEHICLES];
    camera_internal camera;
    prop_internal props[ODWD_MAX_WORLD_PROPS];
    pickup_internal pickups[ODWD_MAX_TURBO_PICKUPS];
    ball_internal ball;
    uint32_t prop_count;
    uint32_t music_beat_pending;
    uint32_t activity_zone;
    uint32_t football_score_left;
    uint32_t football_score_right;
    int32_t prop_cell_x;
    int32_t prop_cell_z;
    double music_energy;
    double music_bass;
    double music_lowpass;
    double music_mid_lowpass;
    double music_mid;
    double music_high;
    double music_flux;
    double music_pulse;
    double music_prev_abs;
    double music_onset_age;
    double music_beat_interval;
    double music_beat_phase;
    double music_envelope;
    double music_beat;
    double music_playback_time;
    double drift_score;
    double drift_chain;
    double drift_idle;
    double last_checkpoint;
    survival_state survival;
    music_survival_state music_survival;
} odwd_engine_internal;

_Static_assert(sizeof(odwd_engine_internal) <= ODWD_ENGINE_STORAGE_BYTES,
               "ODWD_ENGINE_STORAGE_BYTES is too small");

static double dabs(double x) { return x < 0.0 ? -x : x; }
static double dmin(double a, double b) { return a < b ? a : b; }
static double dmax(double a, double b) { return a > b ? a : b; }
static double clampd(double x, double lo, double hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static double sign_nonzero(double x) { return x < 0.0 ? -1.0 : 1.0; }

static int finite_control(double x) {
    return x == x && x > -1000000.0 && x < 1000000.0;
}

static double dsqrt(double x) {
    double g;
    int i;
    if (x <= 0.0) return 0.0;
    g = x >= 1.0 ? x * 0.5 : 1.0;
    for (i = 0; i < 18; ++i) g = 0.5 * (g + x / g);
    return g;
}

static double wrap_angle(double x) {
    /* Constant-time reduction matters for an endless simulation: camera shake
     * feeds this helper an absolute tick phase. */
    int64_t turns;
    if (x > 5.0e19 || x < -5.0e19) return 0.0;
    turns = (int64_t)(x / TWO_PI);
    x -= (double)turns * TWO_PI;
    if (x > PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;
    return x;
}

static double dsin(double x) {
    double x2;
    x = wrap_angle(x);
    if (x > HALF_PI) x = PI - x;
    else if (x < -HALF_PI) x = -PI - x;
    x2 = x * x;
    return x * (1.0 + x2 * (-1.0 / 6.0 + x2 *
           (1.0 / 120.0 + x2 * (-1.0 / 5040.0 + x2 / 362880.0))));
}

static double dcos(double x) { return dsin(x + HALF_PI); }

static double datan_unit(double x) {
    /* Max error is small enough for steering/slip while remaining libm-free. */
    return x * (0.7853981633974483 + 0.273 * (1.0 - x));
}

static double datan2(double y, double x) {
    double ax = dabs(x), ay = dabs(y), a;
    if (ax + ay < 1.0e-15) return 0.0;
    if (ax >= ay) a = datan_unit(ay / ax);
    else a = HALF_PI - datan_unit(ax / ay);
    if (x < 0.0) a = PI - a;
    if (y < 0.0) a = -a;
    return a;
}

static double smooth01(double x) {
    x = clampd(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

static int64_t floor_i64_nonnegative(double x) {
    if (x <= 0.0) return 0;
    return (int64_t)x;
}

static int32_t floor_i32_signed(double x) {
    int32_t value = (int32_t)x;
    if (x < 0.0 && (double)value != x) --value;
    return value;
}

static uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
    x *= UINT32_C(0x846ca68b);
    x ^= x >> 16;
    return x;
}

static double hash_signed(uint32_t seed, int64_t index, uint32_t salt) {
    uint32_t lo = (uint32_t)(uint64_t)index;
    uint32_t hi = (uint32_t)((uint64_t)index >> 32);
    uint32_t h = hash32(seed ^ hash32(lo + salt) ^ hash32(hi + salt * 17u));
    return ((double)(h & UINT32_C(0x00ffffff)) / 8388607.5) - 1.0;
}

static uint32_t chunk_hash(const odwd_engine_internal *e, int64_t chunk) {
    uint32_t lo = (uint32_t)(uint64_t)chunk;
    uint32_t hi = (uint32_t)((uint64_t)chunk >> 32);
    return hash32(e->config.seed ^ lo ^ hash32(hi + UINT32_C(0x9e3779b9)));
}

static double chunk_curvature(const odwd_engine_internal *e, int64_t chunk) {
    uint32_t h = chunk_hash(e, chunk);
    double c = hash_signed(e->config.seed, chunk, UINT32_C(0x51ed270b));
    double magnitude = 0.0020 + dabs(c) * 0.0052;
    if ((h % 11u) == 0u) magnitude = 0.0100 + dabs(c) * 0.0032;
    if ((h % 7u) == 1u) magnitude *= 0.35;
    return sign_nonzero(c) * magnitude;
}

static double chunk_grade(const odwd_engine_internal *e, int64_t chunk) {
    double a = hash_signed(e->config.seed, chunk, UINT32_C(0xb5297a4d));
    double b = hash_signed(e->config.seed, chunk + 1, UINT32_C(0x68e31da4));
    return clampd(a * 0.045 + b * 0.022, -0.075, 0.075);
}

static uint32_t chunk_flags(const odwd_engine_internal *e, int64_t chunk) {
    uint32_t h = chunk_hash(e, chunk);
    uint32_t flags;
    switch ((h >> 4) % 4u) {
        case 0: flags = ODWD_ROAD_RIDGE; break;
        case 1: flags = ODWD_ROAD_GORGE; break;
        case 2: flags = ODWD_ROAD_TUNNEL; break;
        default: flags = 0u; break;
    }
    if (dabs(chunk_curvature(e, chunk)) > 0.009) flags |= ODWD_ROAD_HAIRPIN;
    return flags;
}

static uint32_t road_sector_style(const odwd_engine_internal *e,
                                  int64_t global_index);
static uint32_t road_action_pattern(const odwd_engine_internal *e,
                                    int64_t global_index);

static int chunk_has_branch(const odwd_engine_internal *e, int64_t chunk) {
    uint32_t h = chunk_hash(e, chunk);
    (void)e;
    return chunk > 1 && (h % 9u) == 3u;
}

static uint32_t road_sector_style(const odwd_engine_internal *e,
                                  int64_t global_index) {
    double progress = (double)global_index * ROAD_NODE_SPACING;
    uint32_t section = (uint32_t)floor_i64_nonnegative(
        progress / e->config.section_length_m);
    /* Themes no longer repeat in a visible six-sector loop. They remain six
     * renderer-compatible palettes, but their order is seed/section-derived. */
    return hash32(e->config.seed ^ section * UINT32_C(0x9e3779b9) ^
                  UINT32_C(0x726f6164)) % 6u;
}

static uint32_t road_action_pattern(const odwd_engine_internal *e,
                                    int64_t global_index) {
    double progress = (double)global_index * ROAD_NODE_SPACING;
    uint32_t section = (uint32_t)floor_i64_nonnegative(
        progress / e->config.section_length_m);
    /* Twelve deterministic action families avoid the old short stunt cycle.
     * The renderer still consumes the same road flags, so variety lives in
     * the authoritative road generator rather than in host/UI scripts. */
    return hash32(e->config.seed ^ section * UINT32_C(0x85ebca6b) ^
                  UINT32_C(0x7374756e)) % 12u;
}

static uint32_t road_action_start(const odwd_engine_internal *e,
                                  int64_t global_index) {
    double progress = (double)global_index * ROAD_NODE_SPACING;
    uint32_t section = (uint32_t)floor_i64_nonnegative(
        progress / e->config.section_length_m);
    uint32_t h = hash32(e->config.seed ^ section * UINT32_C(0xc2b2ae35));
    return 30u + (h % 26u);
}

static uint32_t road_section_local_node(const odwd_engine_internal *e,
                                        int64_t global_index) {
    uint32_t nodes = (uint32_t)(e->config.section_length_m /
                                ROAD_NODE_SPACING);
    if (nodes < 64u) nodes = 64u;
    return (uint32_t)((uint64_t)global_index % nodes);
}

static double road_action_height(const odwd_engine_internal *e,
                                 int64_t global_index) {
    uint32_t local;
    uint32_t pattern;
    uint32_t start;
    double height;
    if (global_index < 0) return 0.0;
    if (chunk_has_branch(e, global_index / (int64_t)ROAD_CHUNK_NODES))
        return 0.0;
    local = road_section_local_node(e, global_index);
    pattern = road_action_pattern(e, global_index);
    start = road_action_start(e, global_index);
    height = pattern == 6u ? 5.15 : (pattern == 1u ? 2.55 : 3.65);
    if (pattern == 0u || pattern == 1u || pattern == 6u) {
        if (local >= start && local <= start + 7u)
            return height * smooth01((double)(local - start) / 7.0);
        if (local > start + 7u && local <= start + 10u)
            return height;
        if (local > start + 10u && local <= start + 18u)
            return height * (1.0 - smooth01((double)(local - start - 10u) / 8.0));
    }
    if (pattern == 4u) {
        uint32_t second = start + 27u;
        if (local >= start && local <= start + 6u)
            return 1.75 * smooth01((double)(local - start) / 6.0);
        if (local > start + 6u && local <= start + 12u)
            return 1.75 * (1.0 - smooth01((double)(local - start - 6u) / 6.0));
        if (local >= second && local <= second + 6u)
            return 2.20 * smooth01((double)(local - second) / 6.0);
        if (local > second + 6u && local <= second + 12u)
            return 2.20 * (1.0 - smooth01((double)(local - second - 6u) / 6.0));
    }
    if (pattern == 5u) {
        /* Roller section: three broad crests, never a collision wall. */
        uint32_t crest;
        for (crest = 0u; crest < 3u; ++crest) {
            uint32_t base = start + crest * 14u;
            double crest_height = 1.05 + (double)crest * 0.38;
            if (local >= base && local <= base + 6u)
                return crest_height * smooth01((double)(local - base) / 6.0);
            if (local > base + 6u && local <= base + 12u)
                return crest_height * (1.0 - smooth01((double)(local - base - 6u) / 6.0));
        }
    }
    if (pattern == 8u) {
        /* Elevated causeway: climb, long readable plateau, descend. */
        const double plateau = 3.15;
        if (local >= start && local <= start + 9u)
            return plateau * smooth01((double)(local - start) / 9.0);
        if (local > start + 9u && local <= start + 31u)
            return plateau;
        if (local > start + 31u && local <= start + 41u)
            return plateau * (1.0 - smooth01((double)(local - start - 31u) / 10.0));
    }
    if (pattern == 10u) {
        /* One high launch followed later by a small recovery crest. */
        uint32_t second = start + 31u;
        if (local >= start && local <= start + 8u)
            return 4.35 * smooth01((double)(local - start) / 8.0);
        if (local > start + 8u && local <= start + 11u)
            return 4.35;
        if (local > start + 11u && local <= start + 20u)
            return 4.35 * (1.0 - smooth01((double)(local - start - 11u) / 9.0));
        if (local >= second && local <= second + 6u)
            return 1.35 * smooth01((double)(local - second) / 6.0);
        if (local > second + 6u && local <= second + 12u)
            return 1.35 * (1.0 - smooth01((double)(local - second - 6u) / 6.0));
    }
    return 0.0;
}

static uint32_t road_action_flags(const odwd_engine_internal *e,
                                  int64_t global_index) {
    uint32_t local;
    uint32_t pattern;
    uint32_t start;
    if (global_index < 0) return 0u;
    if (chunk_has_branch(e, global_index / (int64_t)ROAD_CHUNK_NODES))
        return 0u;
    local = road_section_local_node(e, global_index);
    pattern = road_action_pattern(e, global_index);
    start = road_action_start(e, global_index);
    if (pattern == 0u || pattern == 6u) {
        if (local >= start && local <= start + 7u)
            return ODWD_ROAD_RAMP | ODWD_ROAD_SPEED_SECTION;
        if (local > start + 7u && local <= start + 10u)
            return ODWD_ROAD_GAP;
        if (local > start + 10u && local <= start + 24u)
            return ODWD_ROAD_VIADUCT;
    } else if (pattern == 1u || pattern == 4u) {
        if ((local >= start && local <= start + 18u) ||
            (pattern == 4u && local >= start + 27u && local <= start + 39u))
            return ODWD_ROAD_RAMP | ODWD_ROAD_SPEED_SECTION;
    } else if (pattern == 2u) {
        if (local >= start - 8u && local <= start + 42u)
            return ODWD_ROAD_VIADUCT | ODWD_ROAD_RIDGE;
    } else if (pattern == 3u) {
        if (local >= start - 5u && local <= start + 36u)
            return ODWD_ROAD_SPEED_SECTION;
    } else if (pattern == 5u) {
        if (local >= start && local <= start + 40u)
            return ODWD_ROAD_RAMP | ODWD_ROAD_RIDGE;
    } else if (pattern == 7u) {
        if (local >= start - 7u && local <= start + 43u)
            return ODWD_ROAD_TUNNEL | ODWD_ROAD_SPEED_SECTION;
    } else if (pattern == 8u) {
        if (local >= start && local <= start + 41u)
            return ODWD_ROAD_VIADUCT | ODWD_ROAD_RIDGE |
                   ((local <= start + 9u || local > start + 31u) ?
                    ODWD_ROAD_RAMP : 0u);
    } else if (pattern == 9u) {
        if ((local >= start && local <= start + 10u) ||
            (local >= start + 20u && local <= start + 31u) ||
            (local >= start + 39u && local <= start + 49u))
            return ODWD_ROAD_SPEED_SECTION;
    } else if (pattern == 10u) {
        if (local >= start && local <= start + 8u)
            return ODWD_ROAD_RAMP | ODWD_ROAD_SPEED_SECTION;
        if (local > start + 8u && local <= start + 11u)
            return ODWD_ROAD_GAP;
        if (local > start + 11u && local <= start + 26u)
            return ODWD_ROAD_VIADUCT | ODWD_ROAD_GORGE;
        if (local >= start + 31u && local <= start + 43u)
            return ODWD_ROAD_RAMP;
    } else if (pattern == 11u) {
        if (local >= start - 6u && local <= start + 22u)
            return ODWD_ROAD_GORGE;
        if (local > start + 22u && local <= start + 49u)
            return ODWD_ROAD_TUNNEL;
    }
    return 0u;
}

static double open_radial_blend(double x, double z,
                                double cx, double cz,
                                double inner, double outer) {
    double dx = x - cx;
    double dz = z - cz;
    double distance = dsqrt(dx * dx + dz * dz);
    if (distance <= inner) return 1.0;
    if (distance >= outer) return 0.0;
    return 1.0 - smooth01((distance - inner) / (outer - inner));
}

static double open_rect_blend(double x, double z,
                              double cx, double cz,
                              double half_x, double half_z,
                              double feather) {
    double dx = dabs(x - cx) - half_x;
    double dz = dabs(z - cz) - half_z;
    double outside = dmax(dx, dz);
    if (outside <= 0.0) return 1.0;
    if (outside >= feather) return 0.0;
    return 1.0 - smooth01(outside / feather);
}

static double open_macro_relief(double x, double z) {
    /* Infinite deterministic macro-terrain.  Broad cells generate mountain
     * shoulders/ridges rather than tiny noise, so travelling far from spawn
     * keeps revealing a landscape while local driving remains readable. */
    const double cell = 280.0;
    int32_t base_x = floor_i32_signed(x / cell);
    int32_t base_z = floor_i32_signed(z / cell);
    double height = 0.0;
    int dz;
    for (dz = -1; dz <= 1; ++dz) {
        int dx;
        for (dx = -1; dx <= 1; ++dx) {
            int32_t gx = base_x + dx;
            int32_t gz = base_z + dz;
            uint32_t h = hash32((uint32_t)gx * UINT32_C(0x9e3779b1) ^
                                (uint32_t)gz * UINT32_C(0x85ebca77) ^
                                UINT32_C(0x4f445750));
            double ox;
            double oz;
            double radius;
            double amplitude;
            double mx;
            double mz;
            double qx;
            double qz;
            double distance;
            double influence;
            if ((h % 7u) >= 5u) continue;
            ox = 0.20 + (double)((h >> 5u) & 1023u) / 1023.0 * 0.60;
            oz = 0.20 + (double)((h >> 15u) & 1023u) / 1023.0 * 0.60;
            radius = 112.0 + (double)((h >> 9u) & 255u) / 255.0 * 96.0;
            amplitude = 18.0 + (double)((h >> 21u) & 255u) / 255.0 * 48.0;
            mx = ((double)gx + ox) * cell;
            mz = ((double)gz + oz) * cell;
            /* Elliptical ridges avoid a field of obvious round cones. */
            qx = (x - mx) / radius;
            qz = (z - mz) / (radius * (0.62 + (double)(h & 63u) / 180.0));
            distance = dsqrt(qx * qx + qz * qz);
            if (distance >= 1.0) continue;
            influence = 1.0 - smooth01(distance);
            height += amplitude * influence * influence;
            if (((h >> 29u) & 1u) != 0u)
                height += dsin((x + z) * 0.018 + (double)(h & 255u)) *
                          influence * 2.1;
        }
    }
    return height;
}

static double open_ground_height_analytic(double x, double z) {
    double height = dsin(x * 0.012) * 1.65 + dcos(z * 0.010) * 1.30 +
                    dsin((x + z) * 0.006) * 1.15 +
                    dcos((x - z) * 0.0043) * 0.72;
    double peak = open_radial_blend(x, z, OPEN_PEAK_X, OPEN_PEAK_Z,
                                    20.0, 205.0);
    double west_hill = open_radial_blend(x, z, -285.0, -160.0,
                                         12.0, 150.0);
    double east_hill = open_radial_blend(x, z, 310.0, -205.0,
                                         18.0, 165.0);
    double blend;
    height += open_macro_relief(x, z);
    height += peak * peak * 57.0;
    height += west_hill * west_hill * 18.0;
    height += east_hill * east_hill * 15.0;

    /* Authored play spaces are level enough for predictable collisions while
     * the terrain around them stays visibly rolling. */
    blend = open_radial_blend(x, z, 0.0, 0.0, 38.0, 66.0);
    height = height * (1.0 - blend) + 0.35 * blend;
    blend = open_rect_blend(x, z, OPEN_FOOTBALL_X, OPEN_FOOTBALL_Z,
                            57.0, 35.0, 15.0);
    height = height * (1.0 - blend) + 1.15 * blend;
    blend = open_radial_blend(x, z, OPEN_BUMPER_X, OPEN_BUMPER_Z,
                              48.0, 67.0);
    height = height * (1.0 - blend) - 1.6 * blend;
    blend = open_rect_blend(x, z, -168.0, -188.0,
                            70.0, 55.0, 22.0);
    height = height * (1.0 - blend) + 2.35 * blend;
    return height;
}

static double open_ground_height(double x, double z) {
    const double cell = ODWD_OPEN_GROUND_CELL_M;
    double x0 = (double)floor_i32_signed(x / cell) * cell;
    double z0 = (double)floor_i32_signed(z / cell) * cell;
    double u = clampd((x - x0) / cell, 0.0, 1.0);
    double v = clampd((z - z0) / cell, 0.0, 1.0);
    double a = open_ground_height_analytic(x0, z0);
    double b = open_ground_height_analytic(x0, z0 + cell);
    double c = open_ground_height_analytic(x0 + cell, z0 + cell);
    double d = open_ground_height_analytic(x0 + cell, z0);
    /* Match od_draw_quad(a,b,c,d): diagonal a-c. Simulation, props,
     * shadows and the rasterized terrain now share exactly one surface. */
    if (v >= u) return a * (1.0 - v) + b * (v - u) + c * u;
    return a * (1.0 - u) + c * v + d * (u - v);
}

static uint32_t open_activity_zone_for(double x, double z) {
    double dx;
    double dz;
    if (open_rect_blend(x, z, OPEN_FOOTBALL_X, OPEN_FOOTBALL_Z,
                        62.0, 40.0, 0.001) > 0.5)
        return ODWD_ACTIVITY_FOOTBALL;
    dx = x - OPEN_BUMPER_X;
    dz = z - OPEN_BUMPER_Z;
    if (dx * dx + dz * dz < 64.0 * 64.0)
        return ODWD_ACTIVITY_BUMPER_ARENA;
    dx = x - 55.0;
    dz = z + 120.0;
    if (dx * dx + dz * dz < 92.0 * 92.0)
        return ODWD_ACTIVITY_JUMP_PARK;
    dx = x - OPEN_PEAK_X;
    dz = z - OPEN_PEAK_Z;
    if (dx * dx + dz * dz < 220.0 * 220.0)
        return ODWD_ACTIVITY_SKY_PEAK;
    if (open_rect_blend(x, z, -168.0, -188.0,
                        82.0, 68.0, 0.001) > 0.5)
        return ODWD_ACTIVITY_VILLAGE;
    if (x * x + z * z < 82.0 * 82.0)
        return ODWD_ACTIVITY_DRIFT_GARDEN;
    return ODWD_ACTIVITY_EXPLORE;
}

static void open_ramp_local(const open_ramp_definition *ramp,
                            double x, double z,
                            double *side, double *forward) {
    double dx = x - ramp->x;
    double dz = z - ramp->z;
    double sine = dsin(ramp->yaw);
    double cosine = dcos(ramp->yaw);
    *side = dx * cosine - dz * sine;
    *forward = dx * sine + dz * cosine;
}

static int open_ramp_surface(double x, double z, uint32_t *ramp_index,
                             double *height, double *forward_out) {
    uint32_t index;
    for (index = 0u; index < (uint32_t)(sizeof(open_ramps) /
                                        sizeof(open_ramps[0])); ++index) {
        const open_ramp_definition *ramp = &open_ramps[index];
        double side;
        double forward;
        double start_x;
        double start_z;
        double t;
        open_ramp_local(ramp, x, z, &side, &forward);
        if (dabs(side) > ramp->width * 0.5 ||
            forward < -ramp->length * 0.5 ||
            forward > ramp->length * 0.5) continue;
        start_x = ramp->x - dsin(ramp->yaw) * ramp->length * 0.5;
        start_z = ramp->z - dcos(ramp->yaw) * ramp->length * 0.5;
        t = (forward + ramp->length * 0.5) / ramp->length;
        if (ramp_index) *ramp_index = index;
        /* The collision surface is the exact same plane rendered by
         * od_render_ramp. Keeping one linear profile prevents wheels and
         * shadows from floating above (or cutting through) the mesh. */
        if (height) *height = open_ground_height(start_x, start_z) +
                              0.10 + t * ramp->height;
        if (forward_out) *forward_out = forward;
        return 1;
    }
    return 0;
}

static int open_ramp_launch_crossed(double previous_x, double previous_z,
                                    double x, double z,
                                    uint32_t *ramp_index) {
    uint32_t index;
    for (index = 0u; index < (uint32_t)(sizeof(open_ramps) /
                                        sizeof(open_ramps[0])); ++index) {
        const open_ramp_definition *ramp = &open_ramps[index];
        double previous_side;
        double previous_forward;
        double side;
        double forward;
        double tip = ramp->length * 0.5 - 0.72;
        open_ramp_local(ramp, previous_x, previous_z,
                        &previous_side, &previous_forward);
        open_ramp_local(ramp, x, z, &side, &forward);
        if (dabs(side) <= ramp->width * 0.52 &&
            dabs(previous_side) <= ramp->width * 0.58 &&
            previous_forward <= tip && forward > tip) {
            if (ramp_index) *ramp_index = index;
            return 1;
        }
    }
    return 0;
}

static int open_trampoline_at(double x, double z, uint32_t *pad_index) {
    uint32_t index;
    for (index = 0u; index < (uint32_t)(sizeof(open_trampoline_pads) /
                                        sizeof(open_trampoline_pads[0])); ++index) {
        double dx = x - open_trampoline_pads[index][0];
        double dz = z - open_trampoline_pads[index][1];
        if (dx * dx + dz * dz <= 5.7 * 5.7) {
            if (pad_index) *pad_index = index;
            return 1;
        }
    }
    return 0;
}

static int open_authored_play_clear(double x, double z) {
    uint32_t index;
    for (index = 0u; index < (uint32_t)(sizeof(open_ramps) /
                                        sizeof(open_ramps[0])); ++index) {
        double side;
        double forward;
        open_ramp_local(&open_ramps[index], x, z, &side, &forward);
        if (dabs(side) < open_ramps[index].width * 0.5 + 6.0 &&
            dabs(forward) < open_ramps[index].length * 0.5 + 9.0)
            return 1;
    }
    for (index = 0u; index < (uint32_t)(sizeof(open_trampoline_pads) /
                                        sizeof(open_trampoline_pads[0])); ++index) {
        double dx = x - open_trampoline_pads[index][0];
        double dz = z - open_trampoline_pads[index][1];
        if (dx * dx + dz * dz < 13.0 * 13.0) return 1;
    }
    for (index = 0u; index < (uint32_t)(sizeof(open_stunt_barriers) /
                                        sizeof(open_stunt_barriers[0])); ++index) {
        double dx = x - open_stunt_barriers[index][0];
        double dz = z - open_stunt_barriers[index][1];
        if (dx * dx + dz * dz < 8.5 * 8.5) return 1;
    }
    return 0;
}

static void fill_road_node(odwd_engine_internal *e, uint32_t dst,
                           int64_t global_index) {
    road_internal *n = &e->road[dst];
    const road_internal *p = dst > 0 ? &e->road[dst - 1u] : NULL;
    int64_t chunk = global_index / (int64_t)ROAD_CHUNK_NODES;
    uint32_t local = (uint32_t)(global_index % (int64_t)ROAD_CHUNK_NODES);
    double t = (double)local / (double)ROAD_CHUNK_NODES;
    double blend = smooth01(t);
    double curve0 = chunk_curvature(e, chunk);
    double curve1 = chunk_curvature(e, chunk + 1);
    double grade0 = chunk_grade(e, chunk);
    double grade1 = chunk_grade(e, chunk + 1);
    double grade = grade0 + (grade1 - grade0) * blend;
    double curvature = curve0 + (curve1 - curve0) * blend;
    double branch_shape = 0.0;
    double branch_side = 1.0;
    uint32_t h = chunk_hash(e, chunk);

    memset(n, 0, sizeof(*n));
    n->global_index = global_index;
    n->curvature = curvature;
    n->half_width = 7.2 + ((double)((h >> 10) & 7u) * 0.18);
    n->flags = chunk_flags(e, chunk) |
               road_action_flags(e, global_index);

    if (p) {
        double horizontal = ROAD_NODE_SPACING / dsqrt(1.0 + grade * grade);
        n->s = p->s + ROAD_NODE_SPACING;
        n->heading = wrap_angle(p->heading + curvature * ROAD_NODE_SPACING);
        /* Keep the streamed route generally advancing along its global axis.
         * This deterministic envelope prevents accidental same-level loops
         * while preserving hairpins, S-curves and branches. */
        n->heading = clampd(n->heading, -1.18, 1.18);
        n->curvature = wrap_angle(n->heading - p->heading) /
                       ROAD_NODE_SPACING;
        n->x = p->x + dsin(n->heading) * horizontal;
        n->z = p->z + dcos(n->heading) * horizontal;
        n->y = p->y + grade * horizontal +
               road_action_height(e, global_index) -
               road_action_height(e, global_index - 1);
    } else {
        n->s = 0.0;
        n->heading = 0.0;
    }

    if (chunk_has_branch(e, chunk)) {
        double phase = PI * t;
        branch_shape = dsin(phase);
        branch_shape *= branch_shape;
        branch_side = (h & 1u) ? 1.0 : -1.0;
        n->alt_offset = branch_side * (19.0 + (double)((h >> 16) & 7u)) * branch_shape;
        n->alt_height = 2.2 * dsin(phase) * (0.25 + 0.75 * branch_shape);
        n->flags |= ODWD_ROAD_ALT_ROUTE;
    }
}

static void road_initialize(odwd_engine_internal *e) {
    uint32_t i;
    e->road_count = ROAD_CAP;
    e->road_global_first = 0;
    for (i = 0; i < ROAD_CAP; ++i) fill_road_node(e, i, (int64_t)i);
}

static void road_stream_shift(odwd_engine_internal *e) {
    uint32_t i;
    int64_t next_global;
    memmove(e->road, e->road + ROAD_SHIFT,
            (ROAD_CAP - ROAD_SHIFT) * sizeof(e->road[0]));
    next_global = e->road[ROAD_CAP - ROAD_SHIFT - 1u].global_index + 1;
    for (i = ROAD_CAP - ROAD_SHIFT; i < ROAD_CAP; ++i) {
        fill_road_node(e, i, next_global++);
    }
    e->road_global_first += (int64_t)ROAD_SHIFT;
    for (i = 0; i < e->vehicle_count; ++i) {
        if (e->vehicles[i].road_segment >= ROAD_SHIFT)
            e->vehicles[i].road_segment -= ROAD_SHIFT;
        else
            e->vehicles[i].road_segment = 0u;
    }
    e->event_flags |= ODWD_EVENT_STREAM_SHIFT;
}

static double road_alt_half_width(const road_internal *n);

static void road_point_components(const road_internal *n, uint32_t alternate,
                                  double *x, double *y, double *z) {
    double rx = dcos(n->heading);
    double rz = -dsin(n->heading);
    if (alternate && (n->flags & ODWD_ROAD_ALT_ROUTE)) {
        double width = road_alt_half_width(n);
        double authored_offset = n->alt_offset;
        double offset = 0.0;
        double separation = smooth01(width / VEHICLE_PROP_RADIUS);
        /* Keep the narrow connector flush with the main deck. Height is
         * introduced only after a full vehicle-width split exists, turning
         * the following cells into a drivable ramp instead of a step. */
        /* Across every authored branch profile the first node wide enough
         * for a car is at most 5.307 m. Starting lift just beyond that bound
         * guarantees a flush, driveable connector on both split and merge;
         * elevation then arrives on subsequent fully-open nodes. */
        double lift = smooth01((width - 5.32) / 0.75);
        if (dabs(authored_offset) > 1.0e-9) {
            double side = authored_offset < 0.0 ? -1.0 : 1.0;
            /* The branch is the boundary of one connected road union: its
             * zero-width endpoint sits exactly on the main road edge, then
             * opens with a small shoulder clearance. Once fully open this
             * equals the authored offset by construction. */
            offset = width < VEHICLE_PROP_RADIUS ?
                     side * (n->half_width + width + 0.42 * separation) :
                     authored_offset;
        }
        *x = n->x + rx * offset;
        /* A zero-width split point lies on the main deck, then gains height
         * only as the branch becomes wide enough to support a car. */
        *y = n->y + n->alt_height * lift;
        *z = n->z + rz * offset;
    } else {
        *x = n->x;
        *y = n->y;
        *z = n->z;
    }
}

static double road_alt_half_width(const road_internal *n) {
    double available;
    if (!(n->flags & ODWD_ROAD_ALT_ROUTE)) return 0.0;
    available = dabs(n->alt_offset) - n->half_width - 0.42;
    return clampd(available, 0.0, n->half_width * 0.72);
}

static void projection_try(const odwd_engine_internal *e, projection *best,
                           uint32_t segment, uint32_t alternate,
                           double px, double py, double pz,
                           uint32_t hint, uint32_t preferred_alternate) {
    const road_internal *a = &e->road[segment];
    const road_internal *b = &e->road[segment + 1u];
    double ax, ay, az, bx, by, bz, dx, dz, qx, qz, len2, raw_t, t;
    double cx, cy, cz, dsq, score, vertical, continuity;
    double inv, tx, tz, rx, rz, lateral;
    double alt_width_a = road_alt_half_width(a);
    double alt_width_b = road_alt_half_width(b);
    double candidate_half_width;
    int valid_alt = (a->flags & ODWD_ROAD_ALT_ROUTE) &&
                    (b->flags & ODWD_ROAD_ALT_ROUTE) &&
                    dmax(alt_width_a, alt_width_b) > ROAD_EDGE_CLEARANCE;
    if (alternate && !valid_alt) return;
    road_point_components(a, alternate, &ax, &ay, &az);
    road_point_components(b, alternate, &bx, &by, &bz);
    dx = bx - ax;
    dz = bz - az;
    qx = px - ax;
    qz = pz - az;
    len2 = dx * dx + dz * dz;
    if (len2 < 1.0e-9) return;
    raw_t = (qx * dx + qz * dz) / len2;
    /* Main-road segments may still serve their endpoint during stream
     * transitions. An alternate route must never capture the car through a
     * clamped endpoint belonging to a neighbouring taper. */
    if (alternate && (raw_t < -1.0e-6 || raw_t > 1.0 + 1.0e-6)) return;
    t = clampd(raw_t, 0.0, 1.0);
    candidate_half_width = alternate ?
                           alt_width_a + (alt_width_b - alt_width_a) * t :
                           dmin(a->half_width, b->half_width);
    if (alternate && candidate_half_width < ROAD_EDGE_CLEARANCE) return;
    cx = ax + dx * t;
    cy = ay + (by - ay) * t;
    cz = az + dz * t;
    qx = px - cx;
    qz = pz - cz;
    inv = 1.0 / dsqrt(len2);
    tx = dx * inv;
    tz = dz * inv;
    rx = tz;
    rz = -tx;
    lateral = qx * rx + qz * rz;
    /* A taper is visible before it can physically contain the car. Even
     * after it reaches vehicle width, only a centre that actually fits in
     * the usable corridor may select it; this prevents route snaps from the
     * neighbouring main deck. */
    if (alternate &&
        dabs(lateral) > candidate_half_width - ROAD_EDGE_CLEARANCE + 0.02)
        return;
    dsq = qx * qx + qz * qz;
    vertical = py - (cy + 0.58);
    continuity = segment > hint ? (double)(segment - hint) :
                                   (double)(hint - segment);
    continuity = continuity > 6.0 ? continuity - 6.0 : 0.0;
    score = dsq + vertical * vertical * 1.65 +
            continuity * continuity * 0.42;
    if (alternate != preferred_alternate) score += 0.42;
    if (score >= best->distance_sq) return;
    best->segment = segment;
    best->alternate = alternate;
    best->t = t;
    best->s = a->s + (b->s - a->s) * t;
    best->cx = cx;
    best->cy = cy;
    best->cz = cz;
    best->tx = tx;
    best->ty = (by - ay) * inv;
    best->tz = tz;
    best->rx = rx;
    best->rz = rz;
    best->lateral = lateral;
    best->half_width = candidate_half_width;
    best->distance_sq = score;
    best->flags = a->flags | b->flags;
}

static projection road_project(const odwd_engine_internal *e,
                               double px, double py, double pz, uint32_t hint,
                               uint32_t preferred_alternate) {
    projection best;
    uint32_t begin, end, i;
    memset(&best, 0, sizeof(best));
    best.distance_sq = 1.0e300;
    begin = hint > 14u ? hint - 14u : 0u;
    end = hint + 24u;
    if (end > e->road_count - 1u) end = e->road_count - 1u;
    for (i = begin; i < end; ++i) {
        projection_try(e, &best, i, 0u, px, py, pz, hint,
                       preferred_alternate);
        projection_try(e, &best, i, 1u, px, py, pz, hint,
                       preferred_alternate);
    }
    if (best.distance_sq >= 1.0e299) {
        for (i = 0; i + 1u < e->road_count; ++i)
            projection_try(e, &best, i, 0u, px, py, pz, hint,
                           preferred_alternate);
    }
    return best;
}

static projection road_sample_progress(const odwd_engine_internal *e,
                                       double s, uint32_t alternate) {
    projection p;
    uint32_t i;
    memset(&p, 0, sizeof(p));
    if (s <= e->road[0].s) i = 0u;
    else {
        i = 0u;
        while (i + 2u < e->road_count && e->road[i + 1u].s < s) ++i;
    }
    if (i + 1u >= e->road_count) i = e->road_count - 2u;
    {
        const road_internal *a = &e->road[i];
        const road_internal *b = &e->road[i + 1u];
        double ax, ay, az, bx, by, bz, dx, dz, len;
        double alt_width_a = road_alt_half_width(a);
        double alt_width_b = road_alt_half_width(b);
        double sample_alt_width;
        int alt_ok;
        p.t = clampd((s - a->s) / (b->s - a->s), 0.0, 1.0);
        sample_alt_width = alt_width_a +
                           (alt_width_b - alt_width_a) * p.t;
        alt_ok = alternate && (a->flags & ODWD_ROAD_ALT_ROUTE) &&
                 (b->flags & ODWD_ROAD_ALT_ROUTE) &&
                 dmax(alt_width_a, alt_width_b) > ROAD_EDGE_CLEARANCE &&
                 sample_alt_width >= ROAD_EDGE_CLEARANCE;
        road_point_components(a, (uint32_t)alt_ok, &ax, &ay, &az);
        road_point_components(b, (uint32_t)alt_ok, &bx, &by, &bz);
        p.cx = ax + (bx - ax) * p.t;
        p.cy = ay + (by - ay) * p.t;
        p.cz = az + (bz - az) * p.t;
        dx = bx - ax;
        dz = bz - az;
        len = dsqrt(dx * dx + dz * dz);
        if (len < 1.0e-9) len = 1.0;
        p.tx = dx / len;
        p.ty = (by - ay) / len;
        p.tz = dz / len;
        p.rx = p.tz;
        p.rz = -p.tx;
        p.s = a->s + (b->s - a->s) * p.t;
        p.segment = i;
        p.alternate = (uint32_t)alt_ok;
        p.half_width = alt_ok ? sample_alt_width
                              : dmin(a->half_width, b->half_width);
        p.flags = a->flags | b->flags;
    }
    return p;
}

static int road_main_branch_exit_open(const odwd_engine_internal *e,
                                      const projection *p) {
    const road_internal *a;
    const road_internal *b;
    double ax, ay, az, bx, by, bz;
    double alt_x, alt_y, alt_z, branch_side;
    double width;
    if (!e || !p || p->alternate || p->segment + 1u >= e->road_count)
        return 0;
    a = &e->road[p->segment];
    b = &e->road[p->segment + 1u];
    if (!(a->flags & ODWD_ROAD_ALT_ROUTE) ||
        !(b->flags & ODWD_ROAD_ALT_ROUTE)) return 0;
    width = road_alt_half_width(a) +
            (road_alt_half_width(b) - road_alt_half_width(a)) * p->t;
    /* Open only the tapered join. A flat but fully separated branch is still
     * bounded; otherwise the car could drive through the empty median. */
    if (width <= 0.02 || width > 2.40) return 0;
    road_point_components(a, 1u, &ax, &ay, &az);
    road_point_components(b, 1u, &bx, &by, &bz);
    alt_x = ax + (bx - ax) * p->t;
    alt_y = ay + (by - ay) * p->t;
    alt_z = az + (bz - az) * p->t;
    /* Only the flush connector opens the main-road edge. Once the branch is
     * a raised, separate deck, crossing its side remains a real barrier. */
    if (dabs(alt_y - p->cy) > 0.50) return 0;
    branch_side = (alt_x - p->cx) * p->rx +
                  (alt_z - p->cz) * p->rz;
    return p->lateral * branch_side > 0.0;
}

static void prop_assign(prop_internal *prop, uint32_t id, uint32_t type,
                        double x, double y, double z, double radius,
                        double scale, double rotation) {
    memset(prop, 0, sizeof(*prop));
    prop->id = id;
    prop->type = type;
    prop->collidable = type == ODWD_PROP_TREE || type == ODWD_PROP_ROCK ||
                       type == ODWD_PROP_SCULPTURE ||
                       type == ODWD_PROP_HOUSE ||
                       type == ODWD_PROP_BARRIER ||
                       type == ODWD_PROP_GOAL ||
                       type == ODWD_PROP_SURVIVAL_WALL ||
                       type == ODWD_PROP_SURVIVAL_COLUMN ||
                       type == ODWD_PROP_SURVIVAL_SWEEPER ||
                       type == ODWD_PROP_SURVIVAL_GATE;
    prop->x = x;
    prop->y = y;
    prop->z = z;
    prop->radius = radius;
    prop->scale = scale;
    prop->rotation = rotation;
    prop->variant = (type == ODWD_PROP_TREE || type == ODWD_PROP_SHRUB ||
                     type == ODWD_PROP_FLOWER || type == ODWD_PROP_ROCK ||
                     type == ODWD_PROP_SCULPTURE) ? hash32(id) % 5u : 0u;
}

static void prop_push(odwd_engine_internal *e, uint32_t id, uint32_t type,
                      double x, double z, double radius, double scale,
                      double rotation) {
    if (e->prop_count >= ODWD_MAX_WORLD_PROPS) return;
    prop_assign(&e->props[e->prop_count++], id, type,
                x, open_ground_height(x, z), z, radius, scale, rotation);
}

static double survival_bot_anchor_x(uint32_t vehicle_index) {
    static const double x[ODWD_MAX_VEHICLES] = {
        0.0, -10.5, -7.0, -3.5, 3.5, 7.0, 10.5, 0.0
    };
    return x[vehicle_index < ODWD_MAX_VEHICLES ? vehicle_index : 0u];
}

static double survival_bot_anchor_z(uint32_t vehicle_index) {
    /* Rival rows are deliberately separated along the obstacle travel axis.
     * A single 6 m opening cannot physically contain seven 2.76 m-wide cars
     * at the same instant. Staggered rows let the same moving gate reach each
     * rival at a different time, eliminating the old same-train/same-death
     * behaviour without changing collisions or granting immunity. */
    uint32_t rank = vehicle_index > 0u ? vehicle_index - 1u : 0u;
    if (rank > 6u) rank = 6u;
    return -8.2 + (double)rank * 2.55;
}

static double survival_projected_half_width(const vehicle_gameplay_envelope *env,
                                            double angle) {
    return dabs(dcos(angle)) * env->half_width +
           dabs(dsin(angle)) * env->half_length;
}

static double survival_gap_width(const survival_state *s, uint32_t category) {
    /* A gap is derived from the oriented vehicle envelope, not from a magic
     * wall prefab.  Easier categories intentionally tolerate a larger arrival
     * angle; precision categories reward entering nearly straight. */
    double tolerated_angle = category == 0u ? 0.22 :
                             category == 1u ? 0.14 :
                             category == 2u ? 0.075 : 0.030;
    double projected = survival_projected_half_width(&s->envelope,
                                                      tolerated_angle) * 2.0;
    double difficulty = s->difficulty;
    /* The old precision margins were mathematically passable but visually and
     * tactically too close to the 2.76 m body width.  These margins leave real
     * steering/latency room while difficulty comes from motion/patterns. */
    if (category == 0u) return projected + 2.10 - difficulty * 0.22;
    if (category == 1u) return projected + 1.38 - difficulty * 0.18;
    if (category == 2u) return projected + 0.92 - difficulty * 0.12;
    return projected + 0.58;
}

static double survival_reachable_followup_gap(const survival_state *s,
                                              double previous_x,
                                              double desired_x,
                                              double spacing,
                                              double obstacle_speed) {
    double travel_time = spacing / dmax(obstacle_speed, 0.5);
    /* A car cannot translate sideways at its theoretical acceleration for the
     * whole interval: it must first yaw, build lateral motion, then settle into
     * the next opening.  Keep sequential gates inside a conservative playable
     * budget derived from the actual gameplay envelope. */
    double max_shift = s->envelope.lateral_capacity * travel_time * 0.42;
    max_shift = clampd(max_shift, 2.6, 6.0);
    return clampd(desired_x, previous_x - max_shift, previous_x + max_shift);
}

static void survival_add_platform(survival_state *s, double x, double z,
                                  double half_x, double half_z,
                                  uint32_t variant) {
    survival_platform *platform;
    if (s->platform_count >= SURVIVAL_MAX_PLATFORMS) return;
    platform = &s->platforms[s->platform_count++];
    memset(platform, 0, sizeof(*platform));
    platform->x = x;
    platform->y = SURVIVAL_ARENA_Y;
    platform->z = z;
    platform->half_x = half_x;
    platform->half_z = half_z;
    platform->active = 1u;
    platform->variant = variant;
}

static void survival_add_obstacle(survival_state *s, uint32_t type,
                                  double x, double z,
                                  double half_x, double half_y, double half_z,
                                  double yaw, double vx, double vz,
                                  uint32_t variant) {
    survival_obstacle *o;
    if (s->obstacle_count >= SURVIVAL_MAX_OBSTACLES) return;
    o = &s->obstacles[s->obstacle_count++];
    memset(o, 0, sizeof(*o));
    o->type = type;
    o->x = x;
    o->y = SURVIVAL_ARENA_Y + half_y;
    o->z = z;
    o->origin_x = x;
    o->origin_z = z;
    o->half_x = half_x;
    o->half_y = half_y;
    o->half_z = half_z;
    o->yaw = yaw;
    o->vx = vx;
    o->vz = vz;
    o->active = 1u;
    o->variant = variant;
}

static void survival_add_wall_gap(survival_state *s, double z,
                                  double gap_x, double gap_width,
                                  double speed, uint32_t variant) {
    double left_edge = gap_x - gap_width * 0.5;
    double right_edge = gap_x + gap_width * 0.5;
    double left_half = (left_edge + SURVIVAL_ARENA_HALF_W) * 0.5;
    double right_half = (SURVIVAL_ARENA_HALF_W - right_edge) * 0.5;
    if (left_half > 0.05)
        survival_add_obstacle(s, ODWD_PROP_SURVIVAL_WALL,
            -SURVIVAL_ARENA_HALF_W + left_half, z,
            left_half, 2.15, 0.72, 0.0, 0.0, -speed, variant);
    if (right_half > 0.05)
        survival_add_obstacle(s, ODWD_PROP_SURVIVAL_WALL,
            SURVIVAL_ARENA_HALF_W - right_half, z,
            right_half, 2.15, 0.72, 0.0, 0.0, -speed, variant);
}

static int survival_family_recent(const survival_state *s, uint32_t family) {
    uint32_t i;
    for (i = 0u; i < s->history_count; ++i)
        if (s->history[i] == family) return 1;
    return 0;
}

static uint32_t survival_choose_family(const odwd_engine_internal *e,
                                       uint32_t sector_index) {
    const survival_state *s = &e->survival;
    uint32_t attempt;
    uint32_t raw = hash32(e->config.seed ^
                          hash32(sector_index * UINT32_C(0x9e3779b9)));
    uint32_t unlocked = 8u + sector_index / 2u;
    static const uint32_t intro_families[4] = {
        SURVIVAL_WALL_GAP, SURVIVAL_GATE, SURVIVAL_LOW_WALL,
        SURVIVAL_FRAGMENTED
    };
    /* The opening sector must teach the core BlockDash language before it
     * asks for repeated lateral reversals. SLALOM remains fully available in
     * later sectors; it is only excluded from sector zero. */
    if (sector_index == 0u)
        return intro_families[raw % 4u];
    if (unlocked > SURVIVAL_FAMILY_COUNT) unlocked = SURVIVAL_FAMILY_COUNT;
    for (attempt = 0u; attempt < SURVIVAL_FAMILY_COUNT; ++attempt) {
        uint32_t family = (raw + attempt * 7u + attempt * attempt * 3u) % unlocked;
        /* ELEVATED combines a moving gap with mandatory jump timing. Keep it
         * out of the first follow-up wave so a fresh match cannot be decided
         * by two expert checks before the player has settled into BlockDash. */
        if (sector_index < 2u && family == SURVIVAL_ELEVATED) continue;
        if (!survival_family_recent(s, family)) return family;
    }
    return raw % unlocked;
}

static void survival_remember_family(survival_state *s, uint32_t family) {
    uint32_t i;
    if (s->history_count < SURVIVAL_HISTORY) {
        s->history[s->history_count++] = family;
        return;
    }
    for (i = 1u; i < SURVIVAL_HISTORY; ++i)
        s->history[i - 1u] = s->history[i];
    s->history[SURVIVAL_HISTORY - 1u] = family;
}

/* Feasibility is checked in vehicle-space, not prefab-space.  This is a
 * conservative gate: a generated sector may be hard, but it cannot rely on a
 * center point fitting where the oriented body does not. */
static int survival_sector_feasible(const survival_state *s) {
    double straight_width = s->envelope.half_width * 2.0;
    double reaction_distance = s->envelope.max_useful_speed * s->warning_time;
    uint32_t i;
    if (s->solution_count == 0u || s->solution_count > 3u) return 0;
    if (s->warning_time < 1.25 || s->sector_duration <= s->warning_time + 2.0)
        return 0;
    if (reaction_distance < s->envelope.turn_radius * 2.0) return 0;
    for (i = 0u; i < s->solution_count; ++i) {
        if (dabs(s->solution_x[i]) >
            SURVIVAL_ARENA_HALF_W - s->envelope.half_width - 0.15) return 0;
    }
    if (s->family == SURVIVAL_WALL_GAP ||
        s->family == SURVIVAL_DOUBLE_WALL ||
        s->family == SURVIVAL_MOVING_GAP ||
        s->family == SURVIVAL_GATE ||
        s->family == SURVIVAL_ELEVATED ||
        s->family == SURVIVAL_ROUTE_CHOICE ||
        s->family == SURVIVAL_PRECISION) {
        /* The most precise generated opening is still slightly wider than the
         * straight body envelope. Orientation is what spends that margin. */
        if (survival_gap_width(s, 3u) <= straight_width + 0.10) return 0;
    }
    if (s->requires_jump) {
        if (s->envelope.jump_impulse <= 0.0 ||
            s->envelope.jump_distance < s->envelope.half_length * 1.6) return 0;
    }
    if ((s->family == SURVIVAL_FRAGMENTED ||
         s->family == SURVIVAL_BRIDGE ||
         s->family == SURVIVAL_ISLANDS ||
         s->family == SURVIVAL_RAMP_GAP ||
         s->family == SURVIVAL_MOVING_FLOOR) && s->platform_count == 0u)
        return 0;
    for (i = 0u; i < s->platform_count; ++i) {
        const survival_platform *p = &s->platforms[i];
        if (p->half_x < s->envelope.half_width + 0.05 ||
            p->half_z < 1.0) return 0;
    }
    return 1;
}

static int survival_transition_feasible(
    const survival_state *s, const double previous_solutions[3],
    uint32_t previous_count) {
    uint32_t i, j;
    double lateral_budget;
    if (previous_count == 0u) return 1;
    /* This is intentionally conservative: the declared lateral capability is
     * a recalibratable gameplay contract, not an assumption about the current
     * steering bug/feel. The warning interval belongs to the new sector. */
    lateral_budget = s->envelope.lateral_capacity * s->warning_time * 0.82 +
                     s->envelope.half_width * 0.55;
    for (i = 0u; i < previous_count; ++i)
        for (j = 0u; j < s->solution_count; ++j)
            if (dabs(previous_solutions[i] - s->solution_x[j]) <= lateral_budget)
                return 1;
    return 0;
}

static double survival_nearest_previous_solution(
    const double previous_solutions[3], uint32_t previous_count) {
    double best = 0.0;
    uint32_t i;
    if (previous_count == 0u) return 0.0;
    best = previous_solutions[0];
    for (i = 1u; i < previous_count; ++i)
        if (dabs(previous_solutions[i]) < dabs(best)) best = previous_solutions[i];
    return best;
}

static void survival_make_safe_fallback(survival_state *s, double center_x) {
    double gap = survival_gap_width(s, 0u);
    memset(s->obstacles, 0, sizeof(s->obstacles));
    memset(s->platforms, 0, sizeof(s->platforms));
    s->obstacle_count = 0u;
    s->platform_count = 0u;
    s->family = SURVIVAL_WALL_GAP;
    s->solution_count = 1u;
    center_x = clampd(center_x, -SURVIVAL_ARENA_HALF_W + gap * 0.5 + 0.35,
                      SURVIVAL_ARENA_HALF_W - gap * 0.5 - 0.35);
    s->solution_x[0] = center_x;
    s->solution_x[1] = center_x;
    s->solution_x[2] = center_x;
    s->requires_jump = 0u;
    survival_add_platform(s, 0.0, 0.0, SURVIVAL_ARENA_HALF_W,
                          SURVIVAL_ARENA_HALF_D, 0u);
    survival_add_wall_gap(s, 27.0, center_x, gap, s->obstacle_speed, 0u);
}

static void survival_generate_sector(odwd_engine_internal *e,
                                     uint32_t sector_index) {
    survival_state *s = &e->survival;
    uint32_t family = survival_choose_family(e, sector_index);
    double previous_solutions[3] = {0.0, 0.0, 0.0};
    uint32_t previous_count = sector_index == 0u ? 0u : s->solution_count;
    uint32_t h = hash32(e->config.seed ^ sector_index * UINT32_C(0x85ebca6b));
    double signed_a = ((double)(h & 65535u) / 32767.5) - 1.0;
    double signed_b = ((double)((h >> 16u) & 65535u) / 32767.5) - 1.0;
    double speed;
    double gap;
    double gap_x;
    uint32_t i;
    if (previous_count > 3u) previous_count = 3u;
    for (i = 0u; i < previous_count; ++i)
        previous_solutions[i] = s->solution_x[i];
    memset(s->obstacles, 0, sizeof(s->obstacles));
    memset(s->platforms, 0, sizeof(s->platforms));
    s->obstacle_count = 0u;
    s->platform_count = 0u;
    s->sector_index = sector_index;
    s->family = family;
    s->sector_elapsed = 0.0;
    s->difficulty = clampd((double)sector_index / 30.0, 0.0, 1.0);
    s->warning_time = 2.25 - s->difficulty * 0.80 +
                      (sector_index == 0u ? 0.48 : 0.0);
    s->sector_duration = 7.8 - s->difficulty * 1.35;
    s->obstacle_speed = 6.0 + s->difficulty * 3.3;
    s->solution_count = 1u;
    s->solution_x[0] = 0.0;
    s->solution_x[1] = 0.0;
    s->solution_x[2] = 0.0;
    s->requires_jump = 0u;
    speed = s->obstacle_speed;
    gap = survival_gap_width(s, family == SURVIVAL_PRECISION ? 3u :
                                (sector_index < 4u ? 0u :
                                 sector_index < 10u ? 1u : 2u));
    gap_x = clampd(signed_a * 11.8, -14.0, 14.0);

    if (family != SURVIVAL_FRAGMENTED && family != SURVIVAL_BRIDGE &&
        family != SURVIVAL_ISLANDS && family != SURVIVAL_RAMP_GAP &&
        family != SURVIVAL_MOVING_FLOOR)
        survival_add_platform(s, 0.0, 0.0, SURVIVAL_ARENA_HALF_W,
                              SURVIVAL_ARENA_HALF_D, 0u);

    switch (family) {
        case SURVIVAL_WALL_GAP:
            s->solution_x[0] = gap_x;
            survival_add_wall_gap(s, 27.0, gap_x, gap, speed, 0u);
            break;
        case SURVIVAL_DOUBLE_WALL: {
            double followup;
            s->solution_x[0] = gap_x;
            survival_add_wall_gap(s, 27.0, gap_x, gap + 0.25, speed, 0u);
            followup = survival_reachable_followup_gap(
                s, gap_x,
                clampd(-gap_x * 0.42 + signed_b * 2.2, -9.0, 9.0),
                9.5, speed);
            survival_add_wall_gap(s, 36.5, followup,
                                  gap + 0.38, speed, 1u);
            s->sector_duration += 1.1;
            break;
        }
        case SURVIVAL_MOVING_GAP:
            s->solution_x[0] = gap_x;
            survival_add_wall_gap(s, 27.0, gap_x, gap + 0.28, speed, 2u);
            for (i = 0u; i < s->obstacle_count; ++i) {
                s->obstacles[i].amplitude = 4.8 + s->difficulty * 2.8;
                s->obstacles[i].variant = 2u;
            }
            break;
        case SURVIVAL_GATE:
            s->solution_x[0] = gap_x;
            survival_add_wall_gap(s, 27.0, gap_x, gap + 0.8, speed, 3u);
            for (i = 0u; i < s->obstacle_count; ++i)
                s->obstacles[i].variant = 3u;
            break;
        case SURVIVAL_LOW_WALL:
            s->requires_jump = 1u;
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_WALL, 0.0, 27.0,
                                  SURVIVAL_ARENA_HALF_W, 0.50, 0.75,
                                  0.0, 0.0, -speed, 4u);
            break;
        case SURVIVAL_ELEVATED:
            s->requires_jump = 1u;
            s->solution_x[0] = gap_x;
            survival_add_wall_gap(s, 29.0, gap_x, gap + 0.65, speed, 5u);
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_WALL,
                                  gap_x, 25.5, gap * 0.50, 0.38, 0.70,
                                  0.0, 0.0, -speed, 5u);
            break;
        case SURVIVAL_FRAGMENTED:
            s->solution_count = 3u;
            s->solution_x[0] = -11.0;
            s->solution_x[1] = 0.0;
            s->solution_x[2] = 11.0;
            for (i = 0u; i < 3u; ++i) {
                double x = s->solution_x[i];
                survival_add_platform(s, x, -7.5, 4.1, 6.3, 10u + i);
                survival_add_platform(s, x + (i == 1u ? 3.0 : -2.2), 7.4,
                                      4.1, 6.3, 14u + i);
            }
            s->sector_duration += 1.0;
            break;
        case SURVIVAL_BRIDGE:
            s->solution_x[0] = gap_x * 0.35;
            survival_add_platform(s, s->solution_x[0], 0.0,
                                  3.35 + (1.0 - s->difficulty) * 0.65,
                                  SURVIVAL_ARENA_HALF_D, 20u);
            break;
        case SURVIVAL_ISLANDS:
            s->requires_jump = 1u;
            s->solution_x[0] = 0.0;
            survival_add_platform(s, 0.0, -10.2, 8.2, 4.0, 30u);
            survival_add_platform(s, signed_a * 3.2, 0.0, 7.2, 3.4, 31u);
            survival_add_platform(s, signed_b * 3.2, 10.1, 8.2, 4.0, 32u);
            s->sector_duration += 1.2;
            break;
        case SURVIVAL_RAMP_GAP:
            s->requires_jump = 1u;
            s->solution_x[0] = gap_x * 0.42;
            survival_add_platform(s, 0.0, -7.9, SURVIVAL_ARENA_HALF_W,
                                  7.0, 40u);
            survival_add_platform(s, 0.0, 8.5, SURVIVAL_ARENA_HALF_W,
                                  6.5, 41u);
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_RAMP,
                                  s->solution_x[0], -1.9, 3.2, 0.8, 3.6,
                                  0.0, 0.0, 0.0, 9u);
            break;
        case SURVIVAL_SLALOM:
            s->solution_x[0] = 0.0;
            for (i = 0u; i < 5u; ++i) {
                double x = (i & 1u) ? 7.6 : -7.6;
                survival_add_obstacle(s, ODWD_PROP_SURVIVAL_COLUMN,
                                      x + signed_a * 1.6,
                                      24.0 + (double)i * 6.4,
                                      2.5, 2.4, 2.5, 0.0,
                                      0.0, -speed, 10u + i);
            }
            s->sector_duration += 1.5;
            break;
        case SURVIVAL_SWEEPER:
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_SWEEPER,
                                  0.0, 2.5, 13.5, 0.62, 0.65,
                                  0.0, 0.0, 0.0, 11u);
            s->obstacles[0].angular_speed = 0.86 + s->difficulty * 0.62;
            break;
        case SURVIVAL_ROTATOR:
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_SWEEPER,
                                  -6.7, 3.0, 8.5, 0.68, 0.62,
                                  0.3, 0.0, 0.0, 12u);
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_SWEEPER,
                                  7.4, -3.0, 8.5, 0.68, 0.62,
                                  -0.4, 0.0, 0.0, 13u);
            s->obstacles[0].angular_speed = 1.08;
            s->obstacles[1].angular_speed = -0.92;
            break;
        case SURVIVAL_ROUTE_CHOICE: {
            double left_gap = -9.8;
            double right_gap = 9.5;
            double narrow = survival_gap_width(s, 2u);
            double edge = SURVIVAL_ARENA_HALF_W;
            s->solution_count = 2u;
            s->solution_x[0] = left_gap;
            s->solution_x[1] = right_gap;
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_WALL,
                                  (-edge + left_gap - narrow * 0.5) * 0.5,
                                  27.0,
                                  (left_gap - narrow * 0.5 + edge) * 0.5,
                                  2.15, 0.72, 0.0, 0.0, -speed, 13u);
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_WALL,
                                  (left_gap + narrow * 0.5 + right_gap -
                                   narrow * 0.5) * 0.5, 27.0,
                                  (right_gap - left_gap - narrow) * 0.5,
                                  2.15, 0.72, 0.0, 0.0, -speed, 13u);
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_WALL,
                                  (right_gap + narrow * 0.5 + edge) * 0.5,
                                  27.0,
                                  (edge - right_gap - narrow * 0.5) * 0.5,
                                  2.15, 0.72, 0.0, 0.0, -speed, 13u);
            break;
        }
        case SURVIVAL_FUNNEL:
            s->solution_x[0] = 0.0;
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_WALL,
                                  -12.5, 3.5, 8.5, 1.8, 0.65,
                                  -0.52, 0.0, 0.0, 14u);
            survival_add_obstacle(s, ODWD_PROP_SURVIVAL_WALL,
                                  12.5, 3.5, 8.5, 1.8, 0.65,
                                  0.52, 0.0, 0.0, 14u);
            break;
        case SURVIVAL_MOVING_FLOOR:
            s->solution_count = 2u;
            s->solution_x[0] = -7.0;
            s->solution_x[1] = 7.0;
            survival_add_platform(s, -7.0, 0.0, 5.0, SURVIVAL_ARENA_HALF_D, 50u);
            survival_add_platform(s, 7.0, 0.0, 5.0, SURVIVAL_ARENA_HALF_D, 51u);
            s->platforms[0].vx = 2.2;
            s->platforms[1].vx = -2.2;
            break;
        case SURVIVAL_PRECISION:
        default: {
            double second_gap, third_gap;
            s->solution_x[0] = clampd(gap_x * 0.52, -6.2, 6.2);
            gap = survival_gap_width(s, 3u);
            second_gap = survival_reachable_followup_gap(
                s, s->solution_x[0],
                clampd(-s->solution_x[0] * 0.42, -5.2, 5.2),
                7.5, speed);
            third_gap = survival_reachable_followup_gap(
                s, second_gap, s->solution_x[0] * 0.35,
                7.5, speed);
            survival_add_wall_gap(s, 27.0, s->solution_x[0], gap, speed, 16u);
            survival_add_wall_gap(s, 34.5, second_gap,
                                  gap + 0.12, speed, 16u);
            survival_add_wall_gap(s, 42.0, third_gap,
                                  gap + 0.20, speed, 16u);
            s->sector_duration += 2.2;
            break;
        }
    }
    if (!survival_sector_feasible(s) ||
        !survival_transition_feasible(s, previous_solutions, previous_count))
        survival_make_safe_fallback(
            s, survival_nearest_previous_solution(previous_solutions, previous_count));
    /* Do not despawn a moving obstacle while it is still on the arena.  The
     * previous fixed sector clock could replace a wall around z=-6 while the
     * playable floor continues to z=-15, making one safe corner an exploit. */
    {
        double required = s->sector_duration;
        for (i = 0u; i < s->obstacle_count; ++i) {
            const survival_obstacle *o = &s->obstacles[i];
            if (o->vz < -0.05) {
                double travel = (o->origin_z + SURVIVAL_ARENA_HALF_D +
                                 o->half_z + 4.5) / (-o->vz);
                required = dmax(required, s->warning_time + travel + 0.60);
            }
        }
        s->sector_duration = required;
    }
    survival_remember_family(s, s->family);
}

static void survival_initialize(odwd_engine_internal *e) {
    survival_state *s = &e->survival;
    uint32_t i;
    memset(s, 0, sizeof(*s));
    s->envelope.half_length = VEHICLE_ENVELOPE_HALF_LENGTH;
    s->envelope.half_width = VEHICLE_ENVELOPE_HALF_WIDTH;
    s->envelope.half_height = 0.80;
    s->envelope.max_useful_speed = 38.0;
    s->envelope.acceleration = 8.5;
    s->envelope.braking = 10.5;
    s->envelope.lateral_capacity = 8.8;
    s->envelope.turn_radius = 5.2;
    /* BlockDash uses a dedicated dodge hop: quick takeoff, generous hang-time
     * and a short re-arm. It intentionally does not inherit the heavier stunt
     * jump tuning from the open-world car. */
    s->envelope.jump_impulse = 9.6;
    s->envelope.jump_distance = 10.6;
    s->alive_count = e->vehicle_count;
    for (i = 0u; i < ODWD_MAX_VEHICLES; ++i)
        s->elimination_tick[i] = UINT64_MAX;
    survival_generate_sector(e, 0u);
}

static void survival_update_geometry(odwd_engine_internal *e) {
    survival_state *s = &e->survival;
    double active_t = s->sector_elapsed - s->warning_time;
    uint32_t i;
    if (active_t < 0.0) active_t = 0.0;
    for (i = 0u; i < s->obstacle_count; ++i) {
        survival_obstacle *o = &s->obstacles[i];
        o->x = o->origin_x + o->vx * active_t;
        o->z = o->origin_z + o->vz * active_t;
        if (o->variant == 2u && active_t > 0.0)
            o->x += dsin(active_t * 1.22) * o->amplitude;
        if (o->variant == 3u && active_t > 0.0) {
            double open = 0.45 + 0.55 * dsin(active_t * 2.15);
            o->x += sign_nonzero(o->origin_x) * open * 2.2;
        }
        if (o->angular_speed != 0.0 && active_t > 0.0)
            o->yaw = wrap_angle(o->yaw + o->angular_speed * DT);
    }
    if (s->family == SURVIVAL_MOVING_FLOOR && active_t > 0.0) {
        for (i = 0u; i < s->platform_count; ++i) {
            survival_platform *p = &s->platforms[i];
            double base = i == 0u ? -7.0 : 7.0;
            p->x = base + dsin(active_t * 0.82 + (double)i * PI) * 3.2;
        }
    }
}

static int survival_platform_contains(const survival_platform *p,
                                      double x, double z, double margin) {
    return p->active && dabs(x - p->x) <= p->half_x + margin &&
           dabs(z - p->z) <= p->half_z + margin;
}

static int survival_vehicle_supported(const odwd_engine_internal *e,
                                      const vehicle_internal *v) {
    const survival_state *s = &e->survival;
    double fwd_x = dsin(v->yaw), fwd_z = dcos(v->yaw);
    double right_x = fwd_z, right_z = -fwd_x;
    double support_x[5];
    double support_z[5];
    uint32_t count = 0u;
    uint32_t i, j;
    if (s->sector_elapsed < s->warning_time &&
        (s->family == SURVIVAL_FRAGMENTED || s->family == SURVIVAL_BRIDGE ||
         s->family == SURVIVAL_ISLANDS || s->family == SURVIVAL_RAMP_GAP ||
         s->family == SURVIVAL_MOVING_FLOOR))
        return dabs(v->x) <= SURVIVAL_ARENA_HALF_W + 0.3 &&
               dabs(v->z) <= SURVIVAL_ARENA_HALF_D + 0.3;
    support_x[0] = v->x;
    support_z[0] = v->z;
    for (i = 0u; i < 4u; ++i) {
        double fore = (i < 2u ? 1.0 : -1.0) * 1.72;
        double side = (i & 1u ? 1.0 : -1.0) * 0.92;
        support_x[i + 1u] = v->x + fwd_x * fore + right_x * side;
        support_z[i + 1u] = v->z + fwd_z * fore + right_z * side;
    }
    for (i = 0u; i < 5u; ++i) {
        for (j = 0u; j < s->platform_count; ++j) {
            if (survival_platform_contains(&s->platforms[j],
                                           support_x[i], support_z[i], 0.04)) {
                ++count;
                break;
            }
        }
    }
    return count >= 2u;
}

static double survival_ground_height(const odwd_engine_internal *e,
                                     const vehicle_internal *v,
                                     int *supported) {
    int ok = survival_vehicle_supported(e, v);
    if (supported) *supported = ok;
    return SURVIVAL_ARENA_Y;
}

static int survival_obb_contact(const vehicle_internal *v,
                                const survival_obstacle *o,
                                double *normal_x, double *normal_z,
                                double *penetration) {
    double car_axes[4][2];
    double obs_right_x = dcos(o->yaw), obs_right_z = -dsin(o->yaw);
    double obs_fwd_x = dsin(o->yaw), obs_fwd_z = dcos(o->yaw);
    double car_right_x = dcos(v->yaw), car_right_z = -dsin(v->yaw);
    double car_fwd_x = dsin(v->yaw), car_fwd_z = dcos(v->yaw);
    double dx = v->x - o->x;
    double dz = v->z - o->z;
    double best = 1.0e300;
    double best_x = 0.0, best_z = 0.0;
    uint32_t i;
    if (v->y - 0.74 >= o->y + o->half_y ||
        v->y + 0.72 <= o->y - o->half_y) return 0;
    car_axes[0][0] = car_right_x; car_axes[0][1] = car_right_z;
    car_axes[1][0] = car_fwd_x; car_axes[1][1] = car_fwd_z;
    car_axes[2][0] = obs_right_x; car_axes[2][1] = obs_right_z;
    car_axes[3][0] = obs_fwd_x; car_axes[3][1] = obs_fwd_z;
    for (i = 0u; i < 4u; ++i) {
        double ax = car_axes[i][0];
        double az = car_axes[i][1];
        double center = dabs(dx * ax + dz * az);
        double car_r = VEHICLE_ENVELOPE_HALF_WIDTH *
                       dabs(car_right_x * ax + car_right_z * az) +
                       VEHICLE_ENVELOPE_HALF_LENGTH *
                       dabs(car_fwd_x * ax + car_fwd_z * az);
        double obs_r = o->half_x * dabs(obs_right_x * ax + obs_right_z * az) +
                       o->half_z * dabs(obs_fwd_x * ax + obs_fwd_z * az);
        double overlap = car_r + obs_r - center;
        if (overlap <= 0.0) return 0;
        if (overlap < best) {
            double sign = (dx * ax + dz * az) < 0.0 ? -1.0 : 1.0;
            best = overlap;
            best_x = ax * sign;
            best_z = az * sign;
        }
    }
    if (normal_x) *normal_x = best_x;
    if (normal_z) *normal_z = best_z;
    if (penetration) *penetration = best;
    return 1;
}

static int survival_ramp_launch_crossed(const survival_state *s,
                                         double old_x, double old_z,
                                         double new_x, double new_z,
                                         double *ramp_yaw) {
    uint32_t i;
    for (i = 0u; i < s->obstacle_count; ++i) {
        const survival_obstacle *o = &s->obstacles[i];
        double right_x, right_z, fwd_x, fwd_z;
        double old_side, new_side, old_forward, new_forward;
        double lip;
        if (!o->active || o->type != ODWD_PROP_SURVIVAL_RAMP) continue;
        right_x = dcos(o->yaw); right_z = -dsin(o->yaw);
        fwd_x = dsin(o->yaw); fwd_z = dcos(o->yaw);
        old_side = (old_x - o->x) * right_x + (old_z - o->z) * right_z;
        new_side = (new_x - o->x) * right_x + (new_z - o->z) * right_z;
        old_forward = (old_x - o->x) * fwd_x + (old_z - o->z) * fwd_z;
        new_forward = (new_x - o->x) * fwd_x + (new_z - o->z) * fwd_z;
        lip = o->half_z - 0.28;
        if (dabs(old_side) <= o->half_x - 0.18 &&
            dabs(new_side) <= o->half_x - 0.18 &&
            old_forward < lip && new_forward >= lip) {
            if (ramp_yaw) *ramp_yaw = o->yaw;
            return 1;
        }
    }
    return 0;
}

static void survival_collide_obstacles(odwd_engine_internal *e,
                                       vehicle_internal *v) {
    survival_state *s = &e->survival;
    uint32_t i;
    if (s->sector_elapsed < s->warning_time) return;
    for (i = 0u; i < s->obstacle_count; ++i) {
        survival_obstacle *o = &s->obstacles[i];
        double nx, nz, penetration;
        double relative;
        double kick;
        if (!o->active || o->type == ODWD_PROP_SURVIVAL_RAMP) continue;
        if (!survival_obb_contact(v, o, &nx, &nz, &penetration)) continue;
        v->x += nx * (penetration + 0.012);
        v->z += nz * (penetration + 0.012);
        relative = (v->vx - o->vx) * nx + (v->vz - o->vz) * nz;
        kick = o->type == ODWD_PROP_SURVIVAL_SWEEPER ? 1.72 : 1.34;
        if (relative < 0.0) {
            v->vx -= nx * relative * kick;
            v->vz -= nz * relative * kick;
        }
        if (o->type == ODWD_PROP_SURVIVAL_SWEEPER) {
            double tangent_x = dcos(o->yaw);
            double tangent_z = -dsin(o->yaw);
            double rotational = o->angular_speed *
                                dsqrt((v->x - o->x) * (v->x - o->x) +
                                      (v->z - o->z) * (v->z - o->z));
            v->vx += tangent_x * rotational * 0.18;
            v->vz += tangent_z * rotational * 0.18;
        }
        v->last_collision_impulse = dmax(v->last_collision_impulse,
                                          dabs(relative) + penetration * 5.0);
        if (v->collision_cooldown <= 0.0) {
            ++v->collisions;
            v->collision_cooldown = 0.20;
        }
        e->event_flags |= ODWD_EVENT_COLLISION;
    }
}

static void survival_try_jump(odwd_engine_internal *e,
                              vehicle_internal *v, int requested) {
    survival_state *s = &e->survival;
    if (!requested || e->config.world_mode != ODWD_MODE_SURVIVAL ||
        s->eliminated[(uint32_t)(v - e->vehicles)] || v->airborne ||
        v->jump_cooldown > 0.0 || !survival_vehicle_supported(e, v)) return;
    v->airborne = 1u;
    v->air_time = 0.0;
    v->vy = s->envelope.jump_impulse;
    v->pitch_rate = -0.16;
    v->jump_cooldown = 0.24;
    e->event_flags |= ODWD_EVENT_JUMP;
}

static void survival_eliminate(odwd_engine_internal *e, uint32_t index) {
    survival_state *s = &e->survival;
    vehicle_internal *v = &e->vehicles[index];
    if (index >= e->vehicle_count || s->eliminated[index]) return;
    s->eliminated[index] = 1u;
    s->elimination_tick[index] = e->tick;
    s->elimination_place[index] = s->alive_count;
    v->place = s->alive_count;
    if (s->alive_count > 0u) --s->alive_count;
    v->vx *= 0.35;
    v->vz *= 0.35;
    if (index == 0u) {
        s->player_final_place = s->alive_count + 1u;
        e->event_flags |= ODWD_EVENT_SURVIVAL_ELIMINATED;
    }
    if (s->alive_count <= 1u) {
        uint32_t i;
        for (i = 0u; i < e->vehicle_count; ++i) {
            if (!s->eliminated[i]) {
                e->vehicles[i].place = 1u;
                if (i == 0u) s->player_final_place = 1u;
                break;
            }
        }
        s->finished = 1u;
        e->event_flags |= ODWD_EVENT_SURVIVAL_FINISH;
    }
}

static void survival_update_eliminations(odwd_engine_internal *e) {
    survival_state *s = &e->survival;
    uint32_t candidate[ODWD_MAX_VEHICLES];
    double failure_depth[ODWD_MAX_VEHICLES];
    uint32_t count = 0u;
    uint32_t i, j;
    for (i = 0u; i < e->vehicle_count; ++i) {
        vehicle_internal *v = &e->vehicles[i];
        double depth = 0.0;
        int failed = 0;
        if (s->eliminated[i]) continue;
        if (v->y < SURVIVAL_ARENA_Y - 12.0) {
            depth = (SURVIVAL_ARENA_Y - 12.0) - v->y;
            failed = 1;
        }
        if (dabs(v->x) > SURVIVAL_ARENA_HALF_W + 16.0) {
            depth = dmax(depth, dabs(v->x) - (SURVIVAL_ARENA_HALF_W + 16.0));
            failed = 1;
        }
        if (dabs(v->z) > SURVIVAL_ARENA_HALF_D + 18.0) {
            depth = dmax(depth, dabs(v->z) - (SURVIVAL_ARENA_HALF_D + 18.0));
            failed = 1;
        }
        if (!failed) continue;
        candidate[count] = i;
        failure_depth[count] = depth;
        ++count;
    }
    /* Same-tick eliminations are ranked by how far beyond the recovery limit
     * the car travelled, then by stable vehicle id. This is deterministic and
     * independent of accidental array traversal order. */
    for (i = 1u; i < count; ++i) {
        uint32_t ci = candidate[i];
        double di = failure_depth[i];
        j = i;
        while (j > 0u && (di > failure_depth[j - 1u] + 1.0e-9 ||
               (dabs(di - failure_depth[j - 1u]) <= 1.0e-9 &&
                ci < candidate[j - 1u]))) {
            candidate[j] = candidate[j - 1u];
            failure_depth[j] = failure_depth[j - 1u];
            --j;
        }
        candidate[j] = ci;
        failure_depth[j] = di;
    }
    for (i = 0u; i < count &&
         (e->vehicle_count == 1u || s->alive_count > 1u); ++i)
        survival_eliminate(e, candidate[i]);
}
static void survival_tick(odwd_engine_internal *e) {
    survival_state *s = &e->survival;
    if (e->config.world_mode != ODWD_MODE_SURVIVAL || s->finished) return;
    s->sector_elapsed += DT;
    survival_update_geometry(e);
    survival_update_eliminations(e);
    if (s->sector_elapsed >= s->sector_duration) {
        survival_generate_sector(e, s->sector_index + 1u);
        e->event_flags |= ODWD_EVENT_SURVIVAL_SECTOR;
    }
}

static int music_map_contains(const music_survival_state *m,
                              double x, double z) {
    double ax = dabs(x) / dmax(m->half_w, 1.0);
    double az = dabs(z) / dmax(m->half_d, 1.0);
    if (m->map_variant == 1u)
        return ax * ax + az * az <= 1.0;
    if (m->map_variant == 2u)
        return ax + az * 0.88 <= 1.13;
    if (ax > 1.0 || az > 1.0) return 0;
    if (ax <= 0.80 || az <= 0.80) return 1;
    ax = (ax - 0.80) / 0.20;
    az = (az - 0.80) / 0.20;
    return ax * ax + az * az <= 1.0;
}

static double music_ground_height_at(const odwd_engine_internal *e,
                                     double x, double z) {
    const music_survival_state *m = &e->music_survival;
    double h = MUSIC_ARENA_Y;
    uint32_t i;
    if (m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD &&
        m->hazard_phase != MUSIC_EVENT_COOLDOWN) {
        for (i = 0u; i < m->safe_count; ++i) {
            const music_safe_zone *safe = &m->safe[i];
            double dx, dz, distance, t, hill;
            if (safe->shelter) continue;
            dx = x - safe->x;
            dz = z - safe->z;
            distance = dsqrt(dx * dx + dz * dz);
            if (distance >= safe->radius) continue;
            t = 1.0 - distance / dmax(safe->radius, 0.1);
            hill = safe->height * smooth01(t);
            h = dmax(h, MUSIC_ARENA_Y + hill);
        }
    }
    return h;
}

static int music_hole_contains(const music_survival_state *m,
                               double x, double z) {
    uint32_t i;
    if (m->hazard_type != MUSIC_HAZARD_HOLES ||
        m->hazard_phase != MUSIC_EVENT_ACTIVE) return 0;
    for (i = 0u; i < m->hazard_count; ++i) {
        double dx = x - m->hazards[i].x;
        double dz = z - m->hazards[i].z;
        double radius = m->hazards[i].radius;
        if (dx * dx + dz * dz < radius * radius) return 1;
    }
    return 0;
}

static int music_vehicle_supported_at(const odwd_engine_internal *e,
                                      double x, double z) {
    const music_survival_state *m = &e->music_survival;
    if (!music_map_contains(m, x, z)) return 0;
    if (music_hole_contains(m, x, z)) return 0;
    return 1;
}

static void music_random_point(const odwd_engine_internal *e, uint32_t salt,
                               double margin, double *x, double *z) {
    const music_survival_state *m = &e->music_survival;
    uint32_t k;
    double px = 0.0, pz = 0.0;
    for (k = 0u; k < 12u; ++k) {
        uint32_t h = hash32(e->config.seed ^ salt ^ k * UINT32_C(0x9e3779b9));
        px = hash_signed(h, (int64_t)k, UINT32_C(0xa511e9b3)) *
             dmax(4.0, m->half_w - margin);
        pz = hash_signed(h ^ UINT32_C(0x63d83595), (int64_t)k,
                         UINT32_C(0x7f4a7c15)) *
             dmax(4.0, m->half_d - margin);
        if (music_map_contains(m, px, pz)) break;
    }
    *x = px;
    *z = pz;
}

static double music_segment_point_distance_sq(double ax, double az,
                                              double bx, double bz,
                                              double px, double pz) {
    double dx = bx - ax;
    double dz = bz - az;
    double len2 = dx * dx + dz * dz;
    double t;
    double qx, qz;
    if (len2 < 1.0e-9) {
        dx = px - ax;
        dz = pz - az;
        return dx * dx + dz * dz;
    }
    t = ((px - ax) * dx + (pz - az) * dz) / len2;
    t = clampd(t, 0.0, 1.0);
    qx = ax + dx * t;
    qz = az + dz * t;
    dx = px - qx;
    dz = pz - qz;
    return dx * dx + dz * dz;
}

/* A STRIKER is intentionally simple to read: it commits to one high-speed
 * chord and never homes in on the player.  The route is chosen before spawn
 * and must have physical clearance from shelters/hills and announced holes,
 * so the archetype cannot look "stupid" by ploughing into the event scenery. */
static int music_straight_route_clear(const music_survival_state *m,
                                      double ax, double az,
                                      double bx, double bz) {
    uint32_t i;
    for (i = 0u; i < m->safe_count; ++i) {
        const music_safe_zone *safe = &m->safe[i];
        double clearance = safe->radius + VEHICLE_ENVELOPE_HALF_WIDTH + 1.15;
        if (music_segment_point_distance_sq(ax, az, bx, bz,
                                            safe->x, safe->z) <
            clearance * clearance)
            return 0;
    }
    if (m->hazard_type == MUSIC_HAZARD_HOLES &&
        m->hazard_phase != MUSIC_EVENT_COOLDOWN) {
        for (i = 0u; i < m->hazard_count; ++i) {
            const music_hazard_zone *hole = &m->hazards[i];
            double clearance = hole->radius + VEHICLE_ENVELOPE_HALF_WIDTH + 0.85;
            if (music_segment_point_distance_sq(ax, az, bx, bz,
                                                hole->x, hole->z) <
                clearance * clearance)
                return 0;
        }
    }
    return 1;
}

/* Intersect a deterministic lane with the current procedural arena using the
 * same authoritative map predicate as collision/support.  Sampling is safe
 * here because all current map variants are convex and spawn planning happens
 * only a few times per second, never in the inner vehicle-physics loop. */
static int music_lane_segment(const music_survival_state *m,
                              double angle, double lateral_offset,
                              uint32_t reverse,
                              double *start_x, double *start_z,
                              double *inside_exit_x, double *inside_exit_z,
                              double *target_x, double *target_z) {
    const uint32_t samples = 192u;
    double dx = dsin(angle);
    double dz = dcos(angle);
    double nx = dz;
    double nz = -dx;
    double cx = nx * lateral_offset;
    double cz = nz * lateral_offset;
    double limit = dsqrt(m->half_w * m->half_w +
                         m->half_d * m->half_d) + 16.0;
    double first = 0.0, last = 0.0;
    double entry_t, exit_t;
    uint32_t found = 0u;
    uint32_t i;
    for (i = 0u; i <= samples; ++i) {
        double t = -limit + (2.0 * limit * (double)i) / (double)samples;
        double x = cx + dx * t;
        double z = cz + dz * t;
        if (music_map_contains(m, x, z)) {
            if (!found) first = t;
            last = t;
            found = 1u;
        }
    }
    if (!found || last - first < 16.0) return 0;
    entry_t = first + 2.8;
    exit_t = last - 2.8;
    if (exit_t - entry_t < 10.0) return 0;
    if (!reverse) {
        *start_x = cx + dx * entry_t;
        *start_z = cz + dz * entry_t;
        *inside_exit_x = cx + dx * exit_t;
        *inside_exit_z = cz + dz * exit_t;
        *target_x = *inside_exit_x + dx * 22.0;
        *target_z = *inside_exit_z + dz * 22.0;
    } else {
        *start_x = cx + dx * exit_t;
        *start_z = cz + dz * exit_t;
        *inside_exit_x = cx + dx * entry_t;
        *inside_exit_z = cz + dz * entry_t;
        *target_x = *inside_exit_x - dx * 22.0;
        *target_z = *inside_exit_z - dz * 22.0;
    }
    return music_map_contains(m, *start_x, *start_z) &&
           music_map_contains(m, *inside_exit_x, *inside_exit_z);
}

static int music_pick_spawn_lane(const odwd_engine_internal *e,
                                 uint32_t index, uint32_t h,
                                 int require_clear,
                                 double *start_x, double *start_z,
                                 double *target_x, double *target_z) {
    const music_survival_state *m = &e->music_survival;
    double minimum_half = dmin(m->half_w, m->half_d);
    uint32_t attempt;
    for (attempt = 0u; attempt < 64u; ++attempt) {
        uint32_t lane_hash = hash32(h ^ attempt * UINT32_C(0x9e3779b9) ^
                                    index * UINT32_C(0x7f4a7c15));
        double angle = ((double)(lane_hash & 65535u) / 65535.0) * PI;
        double lateral = hash_signed(lane_hash ^ UINT32_C(0x85ebca6b),
                                     (int64_t)attempt,
                                     UINT32_C(0xc2b2ae35)) *
                         minimum_half * 0.62;
        double inside_exit_x, inside_exit_z;
        uint32_t reverse = (lane_hash >> 19u) & 1u;
        if (!music_lane_segment(m, angle, lateral, reverse,
                                start_x, start_z,
                                &inside_exit_x, &inside_exit_z,
                                target_x, target_z))
            continue;
        if (!require_clear ||
            music_straight_route_clear(m, *start_x, *start_z,
                                       inside_exit_x, inside_exit_z))
            return 1;
    }
    /* A STRIKER may never violate its readable straight-line contract. If an
     * event layout leaves no safe chord, the caller downgrades this spawn to a
     * steering archetype instead of sending it through a shelter/hill. */
    if (require_clear) return 0;

    /* Steering archetypes can always use a centre-biased deterministic
     * fallback because their per-tick AI is allowed to bend around scenery. */
    for (attempt = 0u; attempt < 12u; ++attempt) {
        uint32_t lane_hash = hash32(h ^ attempt * UINT32_C(0x27d4eb2d));
        double angle = ((double)(lane_hash & 65535u) / 65535.0) * PI;
        double inside_exit_x, inside_exit_z;
        if (music_lane_segment(m, angle, 0.0, (lane_hash >> 23u) & 1u,
                               start_x, start_z,
                               &inside_exit_x, &inside_exit_z,
                               target_x, target_z))
            return 1;
    }
    return 0;
}

static void music_generate_event(odwd_engine_internal *e) {
    music_survival_state *m = &e->music_survival;
    uint32_t seed = hash32(e->config.seed ^
                           (m->event_index + 1u) * UINT32_C(0x85ebca6b));
    uint32_t band_bias = (uint32_t)clampd(e->music_bass * 5.0 +
                                          e->music_mid * 3.0 +
                                          e->music_high * 4.0 +
                                          e->music_flux * 6.0, 0.0, 18.0);
    uint32_t previous = m->hazard_type;
    uint32_t i;
    double flood_warning = 0.0;
    m->hazard_type = 1u + ((seed ^ band_bias * 131u) % 8u);
    if (m->hazard_type == previous)
        m->hazard_type = 1u + (m->hazard_type % 8u);
    ++m->event_index;
    m->hazard_level = 1u + ((seed >> 8u) % 3u);
    m->hazard_count = 0u;
    m->safe_count = 0u;
    for (i = 0u; i < MUSIC_HAZARD_MAX; ++i)
        memset(&m->hazards[i], 0, sizeof(m->hazards[i]));
    for (i = 0u; i < MUSIC_SAFE_MAX; ++i)
        memset(&m->safe[i], 0, sizeof(m->safe[i]));

    if (m->hazard_type == MUSIC_HAZARD_HOLES ||
        m->hazard_type == MUSIC_HAZARD_LAVA_RAIN ||
        m->hazard_type == MUSIC_HAZARD_METEORS ||
        m->hazard_type == MUSIC_HAZARD_LIGHTNING) {
        uint32_t count = m->hazard_type == MUSIC_HAZARD_HOLES ?
                         3u + (seed % 3u) :
                         m->hazard_type == MUSIC_HAZARD_LIGHTNING ?
                         5u + (seed % 4u) : 4u + (seed % 4u);
        if (count > MUSIC_HAZARD_MAX) count = MUSIC_HAZARD_MAX;
        m->hazard_count = count;
        for (i = 0u; i < count; ++i) {
            music_hazard_zone *h = &m->hazards[i];
            music_random_point(e, seed ^ i * 977u, 4.5, &h->x, &h->z);
            h->radius = (m->hazard_type == MUSIC_HAZARD_HOLES ? 3.8 :
                         m->hazard_type == MUSIC_HAZARD_LIGHTNING ? 2.7 : 3.0) +
                        (double)((hash32(seed + i * 71u) >> 8u) & 255u) /
                        255.0 * (m->hazard_type == MUSIC_HAZARD_HOLES ? 3.8 :
                                 m->hazard_type == MUSIC_HAZARD_LIGHTNING ? 2.5 : 4.2);
            h->strength = m->hazard_type == MUSIC_HAZARD_LIGHTNING ?
                          0.52 + (double)i * 0.53 : 0.42 + (double)i * 0.47;
            h->variant = hash32(seed ^ i * UINT32_C(0xc2b2ae35)) & 7u;
        }
    }

    if (m->hazard_type == MUSIC_HAZARD_SHOCKWAVE) {
        music_hazard_zone *h = &m->hazards[0];
        m->hazard_count = 1u;
        music_random_point(e, seed ^ UINT32_C(0xe17a1465), 10.0, &h->x, &h->z);
        h->radius = 3.1;
        h->strength = dsqrt(m->half_w * m->half_w +
                            m->half_d * m->half_d) * 1.18;
        h->variant = seed & 7u;
    }
    if (m->hazard_type == MUSIC_HAZARD_METEORS ||
        m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD) {
        m->safe_count = MUSIC_SAFE_MAX;
        for (i = 0u; i < m->safe_count; ++i) {
            music_safe_zone *safe = &m->safe[i];
            uint32_t h = hash32(seed ^ i * UINT32_C(0x27d4eb2d));
            music_random_point(e, h, 8.0, &safe->x, &safe->z);
            safe->radius = m->hazard_type == MUSIC_HAZARD_METEORS ?
                           5.8 + (double)(h & 255u) / 255.0 * 2.4 :
                           7.0 + (double)(h & 255u) / 255.0 * 2.2;
            safe->level = 1u + ((h >> 9u) % 3u);
            safe->height = 1.8 + (double)safe->level * 2.25;
            safe->shelter = m->hazard_type == MUSIC_HAZARD_METEORS ? 1u : 0u;
        }
        /* Flood survival is deliberately learnable rather than random damage:
         * exactly one refuge reaches the selected lava level. Every other hill
         * is strictly lower, so choosing a convincing-but-wrong peak can still
         * fail at level 2/3. Recompute height after the level assignment so
         * rendering, collision/ground height and damage all share one fact. */
        if (m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD) {
            uint32_t winner = (seed >> 18u) % m->safe_count;
            vehicle_internal *player = &e->vehicles[0];
            double winner_distance = 34.0;
            uint32_t attempt;
            for (i = 0u; i < m->safe_count; ++i) {
                music_safe_zone *safe = &m->safe[i];
                uint32_t h = hash32(seed ^ i * UINT32_C(0x165667b1));
                if (i == winner) safe->level = m->hazard_level;
                else if (m->hazard_level > 1u)
                    safe->level = h % m->hazard_level;
                else safe->level = 0u;
                safe->height = 1.8 + (double)safe->level * 2.25;
            }
            /* Put the one actually-safe refuge at a fair, readable travel
             * distance from the player. This removes seeds where the correct
             * answer spawned trivially close or effectively across the arena. */
            for (attempt = 0u; attempt < 18u; ++attempt) {
                uint32_t h = hash32(seed ^ attempt * UINT32_C(0x9e3779b9));
                double angle = (double)(h & 65535u) / 65535.0 * TWO_PI;
                double radius = 26.0 + (double)((h >> 16u) & 255u) / 255.0 * 17.0;
                double x = player->x + dsin(angle) * radius;
                double z = player->z + dcos(angle) * radius;
                if (music_map_contains(m, x, z) &&
                    music_map_contains(m, x + 4.0, z) &&
                    music_map_contains(m, x - 4.0, z) &&
                    music_map_contains(m, x, z + 4.0) &&
                    music_map_contains(m, x, z - 4.0)) {
                    double dx = x - player->x, dz = z - player->z;
                    m->safe[winner].x = x;
                    m->safe[winner].z = z;
                    winner_distance = dsqrt(dx * dx + dz * dz);
                    break;
                }
            }
            /* More travel distance gets more warning, but the window stays
             * bounded and energetic. */
            flood_warning = clampd(3.55 + (winner_distance - 26.0) / 17.0 * 1.10,
                                   3.55, 4.65);
        }
    }
    m->hazard_phase = MUSIC_EVENT_WARNING;
    m->phase_elapsed = 0.0;
    m->phase_duration = flood_warning > 0.0 ? flood_warning :
                        clampd(2.65 - e->music_pulse * 0.55, 1.75, 2.65);
    e->event_flags |= ODWD_EVENT_MUSIC_HAZARD_WARNING;
}

static void music_enemy_spawn(odwd_engine_internal *e, uint32_t index,
                              uint32_t archetype) {
    music_survival_state *m = &e->music_survival;
    vehicle_internal *v = &e->vehicles[index];
    uint32_t h = hash32(e->config.seed ^ index * 7919u ^
                        (m->event_index + v->ai_state + 1u) * 6151u);
    double start_x = 0.0, start_z = 0.0;
    double target_x = 0.0, target_z = 0.0;
    v->ai_archetype = archetype;
    ++v->ai_state;

    /* Every archetype starts on a mathematically valid arena edge lane.
     * STRIKER additionally requires a clear straight corridor because it is
     * the readable, non-homing high-speed threat. Chaser/Pusher can steer. */
    if (!music_pick_spawn_lane(e, index, h,
                               archetype == MUSIC_ENEMY_STRIKER,
                               &start_x, &start_z, &target_x, &target_z)) {
        if (archetype == MUSIC_ENEMY_STRIKER) {
            /* No clear high-speed chord exists in this event layout. Preserve
             * the STRIKER promise by spawning a steerable CHASER instead. */
            archetype = MUSIC_ENEMY_CHASER;
            v->ai_archetype = archetype;
        }
        if (!music_pick_spawn_lane(e, index, h, 0,
                                   &start_x, &start_z,
                                   &target_x, &target_z)) {
            music_random_point(e, h, 5.0, &start_x, &start_z);
            target_x = -start_x;
            target_z = -start_z;
        }
    }
    v->x = start_x;
    v->z = start_z;
    v->ai_target_x = target_x;
    v->ai_target_z = target_z;
    v->y = music_ground_height_at(e, v->x, v->z) + 0.58;
    v->yaw = datan2(v->ai_target_x - v->x, v->ai_target_z - v->z);
    v->vx = v->vz = v->vy = 0.0;
    v->yaw_rate = v->steer = 0.0;
    v->airborne = 0u;
    v->body_pitch = v->body_roll = 0.0;
    v->pitch_rate = v->roll_rate = 0.0;
    v->ai_timer = archetype == MUSIC_ENEMY_STRIKER ?
                  7.0 : 4.8 + (double)((h >> 14u) & 255u) / 255.0 * 2.4;
    e->event_flags |= ODWD_EVENT_MUSIC_ENEMY_SPAWN;
}
static void music_survival_initialize(odwd_engine_internal *e) {
    music_survival_state *m = &e->music_survival;
    uint32_t seed = hash32(e->config.seed ^ UINT32_C(0x7f4a7c15));
    uint32_t i;
    memset(m, 0, sizeof(*m));
    /* Survival needs room for multiple simultaneous, telegraphed events.
     * The arena is intentionally large but remains finite and readable. */
    m->half_w = 64.0 + (double)(seed & 255u) / 255.0 * 18.0;
    m->half_d = 54.0 + (double)((seed >> 8u) & 255u) / 255.0 * 16.0;
    m->map_variant = (seed >> 17u) % 3u;
    m->health = 1.0;
    m->phase_duration = 3.2;
    m->enemy_spawn_cooldown = 0.8;
    for (i = 1u; i < e->vehicle_count; ++i) {
        vehicle_internal *v = &e->vehicles[i];
        v->ai_archetype = 1u + ((i - 1u) % 3u);
        v->ai_timer = 0.55 + (double)i * 0.52;
        v->y = MUSIC_ARENA_Y - 40.0;
    }
}

static int music_inside_safe(const music_survival_state *m,
                             double x, double z, int shelters_only) {
    uint32_t i;
    for (i = 0u; i < m->safe_count; ++i) {
        const music_safe_zone *safe = &m->safe[i];
        double dx, dz;
        if (shelters_only && !safe->shelter) continue;
        dx = x - safe->x;
        dz = z - safe->z;
        if (dx * dx + dz * dz <= safe->radius * safe->radius) return 1;
    }
    return 0;
}

static void music_damage_player(odwd_engine_internal *e, double amount) {
    music_survival_state *m = &e->music_survival;
    if (m->finished || amount <= 0.0) return;
    m->health = dmax(0.0, m->health - amount);
    if (m->damage_cooldown <= 0.0) {
        e->event_flags |= ODWD_EVENT_MUSIC_DAMAGE;
        m->damage_cooldown = 0.16;
    }
    if (m->health <= 0.0 && !m->finished) {
        m->finished = 1u;
        e->event_flags |= ODWD_EVENT_MUSIC_FINISH;
    }
}

static void music_recover_vehicle(odwd_engine_internal *e,
                                  vehicle_internal *v, int player) {
    double recover_x = player ? 0.0 : v->x * 0.2;
    double recover_z = player ? 0.0 : v->z * 0.2;
    if (player && !music_vehicle_supported_at(e, recover_x, recover_z)) {
        uint32_t attempt;
        /* A hole may legitimately cover the center. Respawning into the same
         * active hole would create an unavoidable damage loop, so choose a
         * deterministic supported recovery point inside the same generated
         * arena instead of granting temporary collision immunity. */
        for (attempt = 0u; attempt < 32u; ++attempt) {
            double x, z;
            music_random_point(e, UINT32_C(0x6d757369) ^
                               e->music_survival.event_index * UINT32_C(0x9e3779b9) ^
                               attempt * UINT32_C(0x85ebca6b),
                               8.0, &x, &z);
            if (!music_vehicle_supported_at(e, x, z)) continue;
            recover_x = x;
            recover_z = z;
            break;
        }
    }
    v->x = recover_x;
    v->z = recover_z;
    v->y = music_ground_height_at(e, v->x, v->z) + 0.58;
    v->vx = v->vy = v->vz = 0.0;
    v->yaw_rate = v->steer = 0.0;
    v->airborne = 0u;
    v->body_pitch = v->body_roll = 0.0;
    v->pitch_rate = v->roll_rate = 0.0;
    if (player) {
        ++v->respawns;
        e->event_flags |= ODWD_EVENT_PLAYER_RESPAWN;
    }
}

static void music_survival_tick(odwd_engine_internal *e) {
    music_survival_state *m = &e->music_survival;
    vehicle_internal *player = &e->vehicles[0];
    uint32_t i;
    if (e->config.world_mode != ODWD_MODE_MUSIC_SURVIVAL) return;
    m->elapsed += DT;
    if (!m->finished) m->score += DT * (1.0 + e->music_energy * 0.45 +
                                        e->music_pulse * 0.18);
    if (m->damage_cooldown > 0.0) m->damage_cooldown -= DT;
    m->phase_elapsed += DT;
    if (m->hazard_phase == MUSIC_EVENT_COOLDOWN) {
        if (m->phase_elapsed >= m->phase_duration) music_generate_event(e);
    } else if (m->hazard_phase == MUSIC_EVENT_WARNING) {
        if (m->phase_elapsed >= m->phase_duration) {
            m->hazard_phase = MUSIC_EVENT_ACTIVE;
            m->phase_elapsed = 0.0;
            m->phase_duration = m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD ? 7.4 :
                                m->hazard_type == MUSIC_HAZARD_HOLES ? 6.0 :
                                m->hazard_type == MUSIC_HAZARD_QUAKE ? 5.2 :
                                m->hazard_type == MUSIC_HAZARD_METEORS ? 5.5 :
                                m->hazard_type == MUSIC_HAZARD_SHOCKWAVE ? 5.1 :
                                m->hazard_type == MUSIC_HAZARD_LIGHTNING ? 5.6 :
                                4.8 + e->music_energy * 1.4;
            e->event_flags |= ODWD_EVENT_MUSIC_HAZARD_ACTIVE;
        }
    } else if (m->hazard_phase == MUSIC_EVENT_ACTIVE) {
        if (m->hazard_type == MUSIC_HAZARD_LAVA_RAIN) {
            for (i = 0u; i < m->hazard_count; ++i) {
                double dx = player->x - m->hazards[i].x;
                double dz = player->z - m->hazards[i].z;
                if (dx * dx + dz * dz < m->hazards[i].radius * m->hazards[i].radius)
                    music_damage_player(e, DT * (0.095 + 0.025 * m->hazard_level));
            }
        } else if (m->hazard_type == MUSIC_HAZARD_METEORS) {
            if (!music_inside_safe(m, player->x, player->z, 1)) {
                for (i = 0u; i < m->hazard_count; ++i) {
                    const music_hazard_zone *h = &m->hazards[i];
                    double impact = h->strength + 0.75;
                    double dx = player->x - h->x;
                    double dz = player->z - h->z;
                    if (dabs(m->phase_elapsed - impact) < 0.24 &&
                        dx * dx + dz * dz < h->radius * h->radius)
                        music_damage_player(e, 0.0065 + DT * 0.80);
                }
            }
        } else if (m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD) {
            double flood_h = MUSIC_ARENA_Y + 0.8 + (double)m->hazard_level * 2.05;
            if (music_ground_height_at(e, player->x, player->z) + 0.50 < flood_h)
                music_damage_player(e, DT * (0.11 + 0.026 * m->hazard_level));
        } else if (m->hazard_type == MUSIC_HAZARD_QUAKE) {
            double force = (1.7 + (double)m->hazard_level * 0.65) *
                           (0.55 + e->music_bass * 0.70);
            player->vx += dsin(m->phase_elapsed * 13.3) * force * DT;
            player->vz += dcos(m->phase_elapsed * 10.7) * force * DT;
            player->yaw_rate += dsin(m->phase_elapsed * 8.1) * 0.22 * DT;
        } else if (m->hazard_type == MUSIC_HAZARD_WIND) {
            uint32_t h = hash32(e->config.seed ^ m->event_index * 991u);
            double angle = (double)(h & 65535u) / 65535.0 * TWO_PI;
            double force = 4.4 + (double)m->hazard_level * 1.6 + e->music_high * 3.2;
            player->vx += dsin(angle) * force * DT;
            player->vz += dcos(angle) * force * DT;
        } else if (m->hazard_type == MUSIC_HAZARD_SHOCKWAVE &&
                   m->hazard_count > 0u) {
            const music_hazard_zone *h = &m->hazards[0];
            double wave = clampd(m->phase_elapsed / dmax(m->phase_duration, 0.001),
                                 0.0, 1.0) * h->strength;
            double dx = player->x - h->x;
            double dz = player->z - h->z;
            double distance = dsqrt(dx * dx + dz * dz);
            double band = 2.25 + (double)m->hazard_level * 0.38;
            if (dabs(distance - wave) < band) {
                double inv = distance > 0.25 ? 1.0 / distance : 0.0;
                double push = (7.5 + (double)m->hazard_level * 2.4) *
                              (0.72 + e->music_bass * 0.58);
                player->vx += dx * inv * push * DT;
                player->vz += dz * inv * push * DT;
                music_damage_player(e, DT * (0.055 + 0.020 * m->hazard_level));
            }
        } else if (m->hazard_type == MUSIC_HAZARD_LIGHTNING) {
            for (i = 0u; i < m->hazard_count; ++i) {
                const music_hazard_zone *h = &m->hazards[i];
                double impact = h->strength;
                double dx = player->x - h->x;
                double dz = player->z - h->z;
                if (dabs(m->phase_elapsed - impact) < 0.18 &&
                    dx * dx + dz * dz < h->radius * h->radius) {
                    double distance = dsqrt(dx * dx + dz * dz);
                    double inv = distance > 0.20 ? 1.0 / distance : 0.0;
                    double kick = 4.2 + (double)m->hazard_level * 1.25 +
                                  e->music_flux * 3.0;
                    player->vx += dx * inv * kick;
                    player->vz += dz * inv * kick;
                    player->yaw_rate += hash_signed(e->config.seed,
                        (int64_t)(m->event_index * 17u + i), UINT32_C(0x91e10da5)) * 0.28;
                    music_damage_player(e, 0.055 + 0.018 * m->hazard_level);
                }
            }
        }
        if (m->phase_elapsed >= m->phase_duration) {
            m->hazard_phase = MUSIC_EVENT_COOLDOWN;
            m->phase_elapsed = 0.0;
            m->phase_duration = clampd(2.25 - e->music_energy * 0.55, 1.35, 2.25);
        }
    }

    /* The procedural map predicate is the authoritative boundary.  Some map
     * variants intentionally extend beyond half_w/half_d on one axis, so an
     * old rectangular emergency bound could respawn a perfectly supported
     * car.  Real missing-floor failures are detected by the vertical fall. */
    if (player->y < MUSIC_ARENA_Y - 8.0) {
        music_damage_player(e, 0.20);
        music_recover_vehicle(e, player, 1);
    }
    /* Global spawn pacing keeps the arena readable. Difficulty increases
     * through a small, bounded active-car cap instead of burst-spawning every
     * dormant slot on the same beat. */
    if (m->enemy_spawn_cooldown > 0.0)
        m->enemy_spawn_cooldown = dmax(0.0, m->enemy_spawn_cooldown - DT);
    m->enemy_pressure = 0u;
    for (i = 1u; i < e->vehicle_count; ++i)
        if (e->vehicles[i].y > MUSIC_ARENA_Y - 5.0) ++m->enemy_pressure;
    if (!m->finished && m->enemy_spawn_cooldown <= 0.0) {
        uint32_t cap = 1u;
        uint32_t spawn_index = 0u;
        /* Global shelter/refuge events already demand a long committed route.
         * Do not add fresh traffic beyond one active car while the warning or
         * event is live; cars that already exist remain real hazards. */
        if (!((m->hazard_type == MUSIC_HAZARD_METEORS ||
               m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD) &&
              m->hazard_phase != MUSIC_EVENT_COOLDOWN)) {
            if (m->elapsed > 22.0) ++cap;
            if (m->elapsed > 58.0 && (e->music_energy > 0.42 || m->hazard_level >= 2u)) ++cap;
            if (m->elapsed > 105.0 && e->music_energy > 0.70) ++cap;
            if (cap > 4u) cap = 4u;
        }
        if (m->enemy_pressure < cap) {
            for (i = 1u; i < e->vehicle_count; ++i) {
                if (e->vehicles[i].y < MUSIC_ARENA_Y - 10.0 &&
                    e->vehicles[i].ai_timer <= 0.0) {
                    spawn_index = i;
                    break;
                }
            }
        }
        if (spawn_index != 0u) {
            vehicle_internal *v = &e->vehicles[spawn_index];
            uint32_t archetype = 1u + (hash32(e->config.seed ^
                spawn_index * 313u ^
                (m->event_index + v->ai_state) * 977u) % 3u);
            music_enemy_spawn(e, spawn_index, archetype);
            ++m->enemy_pressure;
            m->enemy_spawn_cooldown = clampd(2.65 - e->music_energy * 0.75 -
                e->music_pulse * 0.28, 1.35, 2.65);
        }
    }
}

static void props_refresh_music_survival(odwd_engine_internal *e) {
    music_survival_state *m = &e->music_survival;
    const double tile = 10.5;
    int gz, gx;
    int max_gx = (int)(m->half_w / tile) + 1;
    int max_gz = (int)(m->half_d / tile) + 1;
    uint32_t i;
    e->prop_count = 0u;
    for (gz = -max_gz; gz <= max_gz && e->prop_count < ODWD_MAX_WORLD_PROPS; ++gz) {
        for (gx = -max_gx; gx <= max_gx && e->prop_count < ODWD_MAX_WORLD_PROPS; ++gx) {
            double x = (double)gx * tile;
            double z = (double)gz * tile;
            prop_internal *prop;
            if (!music_map_contains(m, x, z)) continue;
            prop = &e->props[e->prop_count++];
            prop_assign(prop, UINT32_C(0x90000000) +
                        (uint32_t)((gz + max_gz) * (max_gx * 2 + 1) +
                                   gx + max_gx),
                        ODWD_PROP_MUSIC_PLATFORM, x,
                        music_ground_height_at(e, x, z) - 0.42, z,
                        0.0, 1.0, 0.0);
            prop->collidable = 0u;
            prop->extent_x = tile + 0.08;
            prop->extent_y = 0.84;
            prop->extent_z = tile + 0.08;
            prop->variant = (uint32_t)((gx + gz) & 1);
        }
    }
    /* Sample the actual procedural boundary by radial binary search. This
     * gives all three map shapes a visible perimeter without duplicating the
     * containment formula in the renderer. */
    for (i = 0u; i < 28u && e->prop_count < ODWD_MAX_WORLD_PROPS; ++i) {
        double angle = (double)i / 28.0 * TWO_PI;
        double lo = 0.0;
        double hi = dmax(m->half_w, m->half_d) * 1.55;
        uint32_t iter;
        prop_internal *prop;
        for (iter = 0u; iter < 14u; ++iter) {
            double r = (lo + hi) * 0.5;
            double x = dsin(angle) * r;
            double z = dcos(angle) * r;
            if (music_map_contains(m, x, z)) lo = r; else hi = r;
        }
        prop = &e->props[e->prop_count++];
        prop_assign(prop, UINT32_C(0x90500000) + i,
                    ODWD_PROP_MUSIC_BOUNDARY,
                    dsin(angle) * lo, MUSIC_ARENA_Y, dcos(angle) * lo,
                    0.8, 1.0, angle);
        prop->collidable = 0u;
        prop->extent_x = 2.2;
        prop->extent_y = 2.7;
        prop->extent_z = 0.55;
        prop->variant = i & 1u;
    }
    /* Global events need a visible telegraph too. Quake and wind do not own
     * point hazards, and flood telegraphs its future arena-wide danger before
     * the lava plane exists. Emit one authoritative world-space marker so the
     * renderer never has to infer an event from HUD state alone. */
    if (m->hazard_phase != MUSIC_EVENT_COOLDOWN &&
        (m->hazard_type == MUSIC_HAZARD_QUAKE ||
         m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD ||
         m->hazard_type == MUSIC_HAZARD_WIND) &&
        e->prop_count < ODWD_MAX_WORLD_PROPS) {
        prop_internal *prop = &e->props[e->prop_count++];
        double radius = m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD ?
                        dmin(m->half_w, m->half_d) * 0.72 :
                        m->hazard_type == MUSIC_HAZARD_QUAKE ?
                        16.0 + (double)m->hazard_level * 4.5 :
                        21.0 + (double)m->hazard_level * 3.0;
        double rotation = 0.0;
        if (m->hazard_type == MUSIC_HAZARD_WIND) {
            uint32_t h = hash32(e->config.seed ^ m->event_index * 991u);
            rotation = (double)(h & 65535u) / 65535.0 * TWO_PI;
        }
        prop_assign(prop, UINT32_C(0x90600000), ODWD_PROP_MUSIC_WARNING,
                    0.0, MUSIC_ARENA_Y + 0.04, 0.0,
                    radius, 1.0, rotation);
        prop->collidable = 0u;
        prop->extent_x = radius * 2.0;
        prop->extent_y = 0.18;
        prop->extent_z = radius * 2.0;
        prop->variant = (m->hazard_type << 8u) | (m->hazard_level & 255u);
    }
    for (i = 0u; i < m->hazard_count && e->prop_count < ODWD_MAX_WORLD_PROPS; ++i) {
        const music_hazard_zone *h = &m->hazards[i];
        prop_internal *prop = &e->props[e->prop_count++];
        uint32_t type = ODWD_PROP_MUSIC_WARNING;
        if (m->hazard_phase == MUSIC_EVENT_ACTIVE) {
            if (m->hazard_type == MUSIC_HAZARD_HOLES) type = ODWD_PROP_MUSIC_HOLE;
            else if (m->hazard_type == MUSIC_HAZARD_LAVA_RAIN) type = ODWD_PROP_MUSIC_LAVA;
            else if (m->hazard_type == MUSIC_HAZARD_METEORS) type = ODWD_PROP_MUSIC_METEOR;
        }
        prop_assign(prop, UINT32_C(0x91000000) + i, type,
                    h->x, MUSIC_ARENA_Y + 0.04, h->z, h->radius,
                    h->radius, 0.0);
        prop->collidable = 0u;
        prop->extent_x = m->hazard_type == MUSIC_HAZARD_SHOCKWAVE ?
                         h->strength * 2.0 : h->radius * 2.0;
        prop->extent_y = type == ODWD_PROP_MUSIC_METEOR ? 10.0 : 0.18;
        prop->extent_z = h->radius * 2.0;
        prop->variant = h->variant | (m->hazard_type << 8u);
    }
    for (i = 0u; i < m->safe_count && e->prop_count < ODWD_MAX_WORLD_PROPS; ++i) {
        const music_safe_zone *safe = &m->safe[i];
        prop_internal *prop = &e->props[e->prop_count++];
        prop_assign(prop, UINT32_C(0x92000000) + i,
                    safe->shelter ? ODWD_PROP_MUSIC_SHELTER : ODWD_PROP_MUSIC_SAFE_PAD,
                    safe->x, MUSIC_ARENA_Y, safe->z, safe->radius,
                    safe->radius, 0.0);
        prop->collidable = 0u;
        prop->extent_x = safe->radius * 1.55;
        prop->extent_y = safe->shelter ? 4.7 : safe->height;
        prop->extent_z = safe->radius * 1.55;
        prop->variant = safe->level;
    }
    if (m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD &&
        m->hazard_phase == MUSIC_EVENT_ACTIVE &&
        e->prop_count < ODWD_MAX_WORLD_PROPS) {
        prop_internal *prop = &e->props[e->prop_count++];
        double flood_h = MUSIC_ARENA_Y + 0.8 +
                         (double)m->hazard_level * 2.05;
        const double lava_thickness = 0.22;
        /* The rendered lava top is exactly the gameplay danger height. This
         * prevents invisible damage while the car still looks above the lava. */
        prop_assign(prop, UINT32_C(0x93000000), ODWD_PROP_MUSIC_LAVA,
                    0.0, flood_h - lava_thickness,
                    0.0, dmax(m->half_w, m->half_d), 1.0, 0.0);
        prop->collidable = 0u;
        prop->extent_x = m->half_w * 2.0;
        prop->extent_y = lava_thickness;
        prop->extent_z = m->half_d * 2.0;
        prop->variant = UINT32_C(0x10000) | m->hazard_level;
    }
}

static void props_refresh_survival(odwd_engine_internal *e) {
    survival_state *s = &e->survival;
    uint32_t i;
    e->prop_count = 0u;
    for (i = 0u; i < s->platform_count && e->prop_count < ODWD_MAX_WORLD_PROPS; ++i) {
        const survival_platform *p = &s->platforms[i];
        prop_internal *prop = &e->props[e->prop_count++];
        prop_assign(prop, UINT32_C(0x81000000) + i,
                    ODWD_PROP_SURVIVAL_PLATFORM,
                    p->x, p->y - 0.45, p->z, 0.35, p->half_z * 2.0, 0.0);
        prop->collidable = 0u;
        prop->extent_x = p->half_x * 2.0;
        prop->extent_y = 0.45;
        prop->extent_z = p->half_z * 2.0;
        prop->variant = p->variant;
    }
    for (i = 0u; i < s->obstacle_count && e->prop_count < ODWD_MAX_WORLD_PROPS; ++i) {
        const survival_obstacle *o = &s->obstacles[i];
        prop_internal *prop = &e->props[e->prop_count++];
        prop_assign(prop, UINT32_C(0x82000000) + i, o->type,
                    o->x, o->y - o->half_y, o->z,
                    o->half_y, o->half_z * 2.0, o->yaw);
        prop->extent_x = o->half_x * 2.0;
        prop->extent_y = o->half_y * 2.0;
        prop->extent_z = o->half_z * 2.0;
        prop->variant = o->variant;
        /* Survival collision uses the oriented envelope/SAT path, not the
         * legacy circular prop collider. */
        prop->collidable = 0u;
    }
}

static void props_add_open_authored(odwd_engine_internal *e) {
    static const double houses[][3] = {
        {-218.0, -226.0, 0.12}, {-194.0, -226.0, -0.08},
        {-166.0, -226.0, 0.06}, {-136.0, -226.0, -0.10},
        {-112.0, -205.0, 1.54}, {-112.0, -174.0, 1.60},
        {-132.0, -148.0, 3.08}, {-162.0, -148.0, 3.16},
        {-194.0, -148.0, 3.05}, {-221.0, -164.0, -1.58},
        {-221.0, -195.0, -1.50}, {-160.0, -188.0, 0.02}
    };
    static const double flowers[][2] = {
        {-20.0, 28.0}, {-12.0, 34.0}, {-3.0, 37.0}, {8.0, 35.0},
        {18.0, 29.0}, {26.0, 20.0}, {-30.0, 16.0}, {-33.0, 5.0},
        {-205.0, -207.0}, {-184.0, -208.0}, {-151.0, -207.0},
        {-124.0, -188.0}, {-128.0, -158.0}, {-151.0, -158.0},
        {-182.0, -158.0}, {-210.0, -179.0},
        {128.0, 62.0}, {145.0, 61.0}, {164.0, 61.0},
        {183.0, 61.0}, {198.0, 68.0}, {202.0, 116.0},
        {180.0, 123.0}, {143.0, 124.0}
    };
    static const double ground_birds[][2] = {
        {-27.0, 44.0}, {31.0, 42.0}, {-185.0, -184.0},
        {-143.0, -180.0}, {132.0, 128.0}, {196.0, 54.0}
    };
    static const double flying_birds[][2] = {
        {-255.0, 28.0}, {272.0, 180.0}, {18.0, 292.0}
    };
    uint32_t index;

    for (index = 0u; index < (uint32_t)(sizeof(open_ramps) /
                                        sizeof(open_ramps[0])); ++index) {
        const open_ramp_definition *ramp = &open_ramps[index];
        prop_push(e, UINT32_C(0x71000000) + index, ramp->prop_type,
                  ramp->x, ramp->z, 0.0,
                  ramp->length / 18.0, ramp->yaw);
    }
    for (index = 0u; index < (uint32_t)(sizeof(open_trampoline_pads) /
                                        sizeof(open_trampoline_pads[0])); ++index)
        prop_push(e, UINT32_C(0x72000000) + index, ODWD_PROP_TRAMPOLINE,
                  open_trampoline_pads[index][0], open_trampoline_pads[index][1],
                  0.0, 1.0 + (double)(index % 3u) * 0.06,
                  (double)index * 0.37);

    for (index = 0u; index < (uint32_t)(sizeof(open_stunt_barriers) /
                                        sizeof(open_stunt_barriers[0])); ++index)
        prop_push(e, UINT32_C(0x72500000) + index, ODWD_PROP_BARRIER,
                  open_stunt_barriers[index][0], open_stunt_barriers[index][1],
                  1.35, 0.78 + (double)(index % 3u) * 0.09,
                  open_stunt_barriers[index][2]);

    prop_push(e, UINT32_C(0x73000000), ODWD_PROP_GOAL,
              OPEN_FOOTBALL_X - OPEN_GOAL_PLANE_HALF, OPEN_FOOTBALL_Z,
              0.0, 1.0, -HALF_PI);
    prop_push(e, UINT32_C(0x73000001), ODWD_PROP_GOAL,
              OPEN_FOOTBALL_X + OPEN_GOAL_PLANE_HALF, OPEN_FOOTBALL_Z,
              0.0, 1.0, HALF_PI);

    /* A dedicated non-colliding floor makes the bumper-car activity legible
     * before the player reaches the wall ring. It is a semantic world prop so
     * every host/backend receives the same arena marker. */
    {
        uint32_t before = e->prop_count;
        prop_push(e, UINT32_C(0x743fffff), ODWD_PROP_BUMPER_FLOOR,
                  OPEN_BUMPER_X, OPEN_BUMPER_Z, 48.5, 1.0, 0.0);
        if (e->prop_count > before) {
            prop_internal *floor = &e->props[e->prop_count - 1u];
            floor->collidable = 0u;
            floor->y += 0.035;
            floor->extent_x = 97.0;
            floor->extent_y = 0.07;
            floor->extent_z = 97.0;
        }
    }

    for (index = 0u; index < 22u; ++index) {
        double angle = (double)index / 22.0 * TWO_PI;
        /* Two deliberate openings make the arena readable and enterable. */
        if (index == 5u || index == 6u || index == 16u || index == 17u)
            continue;
        prop_push(e, UINT32_C(0x74000000) + index, ODWD_PROP_BARRIER,
                  OPEN_BUMPER_X + dsin(angle) * 52.0,
                  OPEN_BUMPER_Z + dcos(angle) * 52.0,
                  1.65, 1.0, angle);
    }

    /* Tall non-colliding entrance pylons make the bumper arena readable from
     * the exploration camera instead of looking like an invisible activity. */
    for (index = 0u; index < 8u; ++index) {
        double angle = (double)index / 8.0 * TWO_PI + PI * 0.125;
        uint32_t before = e->prop_count;
        prop_push(e, UINT32_C(0x74400000) + index, ODWD_PROP_SCULPTURE,
                  OPEN_BUMPER_X + dsin(angle) * 58.5,
                  OPEN_BUMPER_Z + dcos(angle) * 58.5,
                  0.0, 2.35 + (double)(index & 1u) * 0.42, angle);
        if (e->prop_count > before) {
            e->props[e->prop_count - 1u].collidable = 0u;
            e->props[e->prop_count - 1u].variant = 4u + (index & 1u);
        }
    }

    for (index = 0u; index < (uint32_t)(sizeof(houses) /
                                        sizeof(houses[0])); ++index)
        prop_push(e, UINT32_C(0x75000000) + index, ODWD_PROP_HOUSE,
                  houses[index][0], houses[index][1],
                  4.0 + (double)(index % 3u) * 0.55,
                  0.86 + (double)(index % 4u) * 0.11,
                  houses[index][2]);

    for (index = 0u; index < (uint32_t)(sizeof(flowers) /
                                        sizeof(flowers[0])); ++index)
        prop_push(e, UINT32_C(0x76000000) + index, ODWD_PROP_FLOWER,
                  flowers[index][0], flowers[index][1], 0.0,
                  0.75 + (double)(index % 4u) * 0.12,
                  (double)index * 0.73);

    for (index = 0u; index < (uint32_t)(sizeof(ground_birds) /
                                        sizeof(ground_birds[0])); ++index)
        prop_push(e, UINT32_C(0x77000000) + index, ODWD_PROP_BIRD_GROUND,
                  ground_birds[index][0], ground_birds[index][1], 0.0,
                  0.78 + (double)(index % 3u) * 0.10,
                  (double)index * 0.91);
    for (index = 0u; index < (uint32_t)(sizeof(flying_birds) /
                                        sizeof(flying_birds[0])); ++index) {
        prop_push(e, UINT32_C(0x78000000) + index, ODWD_PROP_BIRD_FLYING,
                  flying_birds[index][0], flying_birds[index][1], 0.0,
                  0.92 + (double)index * 0.08,
                  (double)index * 2.04);
        e->props[e->prop_count - 1u].y += 18.0 + (double)index * 6.0;
    }
}

static void props_refresh_endless(odwd_engine_internal *e) {
    uint32_t index;
    e->prop_count = 0u;
    for (index = 8u; index + 2u < e->road_count &&
         e->prop_count + 1u < ODWD_MAX_WORLD_PROPS; index += 6u) {
        const road_internal *node = &e->road[index];
        uint32_t seed = hash32((uint32_t)node->global_index ^ e->config.seed);
        uint32_t style = road_sector_style(e, node->global_index);
        uint32_t side_index;
        if (node->flags & ODWD_ROAD_GAP) continue;
        for (side_index = 0u; side_index < 2u &&
             e->prop_count < ODWD_MAX_WORLD_PROPS; ++side_index) {
            double side = side_index == 0u ? -1.0 : 1.0;
            double random = (double)(hash32(seed + side_index * 977u) &
                            UINT32_C(0xffff)) / 65535.0;
            double distance = node->half_width + 8.0 + random * 20.0;
            double rx = dcos(node->heading);
            double rz = -dsin(node->heading);
            uint32_t type;
            double scale = 0.76 + random * 0.74;
            double radius;
            if (style == 0u || style == 2u)
                type = (seed + side_index) % 5u == 0u ?
                       ODWD_PROP_SHRUB : ODWD_PROP_TREE;
            else if (style == 1u)
                type = (seed + side_index) % 3u == 0u ?
                       ODWD_PROP_TREE : ODWD_PROP_ROCK;
            else if (style == 4u)
                type = (seed + side_index) % 3u == 0u ?
                       ODWD_PROP_SCULPTURE : ODWD_PROP_SHRUB;
            else
                type = (seed + side_index) % 2u == 0u ?
                       ODWD_PROP_TREE : ODWD_PROP_SHRUB;
            radius = type == ODWD_PROP_TREE ? 1.05 * scale :
                     type == ODWD_PROP_ROCK ? 1.45 * scale :
                     type == ODWD_PROP_SCULPTURE ? 1.70 * scale : 0.0;
            prop_assign(&e->props[e->prop_count++],
                        hash32(seed ^ (side_index * UINT32_C(0x9e3779b9))),
                        type,
                        node->x + rx * side * distance,
                        node->y,
                        node->z + rz * side * distance,
                        radius, scale,
                        (double)(seed & UINT32_C(0xffff)) / 65535.0 * TWO_PI);
        }
    }
}

static void props_refresh_open(odwd_engine_internal *e, int force) {
    const vehicle_internal *player = &e->vehicles[0];
    int32_t cell_x = floor_i32_signed(player->x / OPEN_PROP_CELL_M);
    int32_t cell_z = floor_i32_signed(player->z / OPEN_PROP_CELL_M);
    int32_t dz;
    if (!force && cell_x == e->prop_cell_x && cell_z == e->prop_cell_z) return;
    e->prop_cell_x = cell_x;
    e->prop_cell_z = cell_z;
    e->prop_count = 0u;
    props_add_open_authored(e);
    /* The procedural ring extends beyond the rendered ground horizon. Props
     * therefore enter/leave while culled, never when the player crosses the
     * former 187 m streaming edge. Authored props consume ~70 of 320 slots. */
    for (dz = -7; dz <= 7; ++dz) {
        int32_t dx;
        for (dx = -7; dx <= 7; ++dx) {
            int32_t gx = cell_x + dx;
            int32_t gz = cell_z + dz;
            uint32_t seed;
            double jitter_x;
            double jitter_z;
            double x;
            double z;
            double scale;
            uint32_t type;
            if (e->prop_count >= ODWD_MAX_WORLD_PROPS) break;
            seed = hash32(e->config.seed ^ hash32((uint32_t)gx) ^
                          hash32((uint32_t)gz + UINT32_C(0x6a09e667)));
            jitter_x = hash_signed(e->config.seed, gx,
                                   (uint32_t)gz ^ UINT32_C(0x18a5)) * 10.5;
            jitter_z = hash_signed(e->config.seed, gz,
                                   (uint32_t)gx ^ UINT32_C(0xb73f)) * 10.5;
            x = ((double)gx + 0.5) * OPEN_PROP_CELL_M + jitter_x;
            z = ((double)gz + 0.5) * OPEN_PROP_CELL_M + jitter_z;
            /* Clearing is world-authored, never relative to the moving
             * player. Crossing a streaming cell therefore cannot delete a
             * nearby tree or swap an obstacle under the car. */
            if (x * x + z * z < 58.0 * 58.0 ||
                open_rect_blend(x, z, OPEN_FOOTBALL_X, OPEN_FOOTBALL_Z,
                                67.0, 44.0, 0.001) > 0.5 ||
                open_radial_blend(x, z, OPEN_BUMPER_X, OPEN_BUMPER_Z,
                                  0.0, 72.0) > 0.0 ||
                open_rect_blend(x, z, -168.0, -188.0,
                                86.0, 72.0, 0.001) > 0.5 ||
                open_authored_play_clear(x, z))
                continue;
            scale = 0.72 + (double)((seed >> 8u) & 255u) / 255.0 * 0.92;
            {
                uint32_t family = seed % 16u;
                type = family < 7u ? ODWD_PROP_TREE :
                       family < 11u ? ODWD_PROP_SHRUB :
                       family < 13u ? ODWD_PROP_FLOWER :
                       family == 13u ? ODWD_PROP_ROCK :
                       family == 14u ? ODWD_PROP_SCULPTURE : ODWD_PROP_TREE;
            }
            prop_assign(&e->props[e->prop_count++], seed, type,
                        x, open_ground_height(x, z), z,
                        type == ODWD_PROP_TREE ? 1.05 * scale :
                        type == ODWD_PROP_ROCK ? 1.35 * scale :
                        type == ODWD_PROP_SCULPTURE ? 1.55 * scale : 0.0,
                        type == ODWD_PROP_FLOWER ? scale * 0.75 : scale,
                        (double)(seed & 65535u) / 65535.0 * TWO_PI);
            /* Sparse undergrowth clusters break the one-prop-per-cell rhythm
             * without making the world impassable. They are non-collidable. */
            if ((seed & 3u) == 0u && e->prop_count < ODWD_MAX_WORLD_PROPS) {
                uint32_t extra_type = (seed & 8u) ? ODWD_PROP_FLOWER :
                                                   ODWD_PROP_SHRUB;
                double ex = x + hash_signed(seed, gx, UINT32_C(0x39a1)) * 5.5;
                double ez = z + hash_signed(seed, gz, UINT32_C(0xc721)) * 5.5;
                prop_assign(&e->props[e->prop_count++], seed ^ UINT32_C(0xa5a5a5a5),
                            extra_type, ex, open_ground_height(ex, ez), ez,
                            0.0, scale * 0.48,
                            (double)((seed >> 6u) & 65535u) / 65535.0 * TWO_PI);
            }
        }
    }
}

static void props_refresh(odwd_engine_internal *e, int force) {
    if (e->config.world_mode == ODWD_MODE_OPEN_FIELD)
        props_refresh_open(e, force);
    else if (e->config.world_mode == ODWD_MODE_SURVIVAL)
        props_refresh_survival(e);
    else if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL)
        props_refresh_music_survival(e);
    else
        props_refresh_endless(e);
}

static void pickup_place_open(odwd_engine_internal *e,
                              pickup_internal *pickup,
                              const vehicle_internal *player) {
    uint32_t seed = hash32(e->config.seed ^ pickup->id * UINT32_C(0x9e3779b9) ^
                           pickup->generation * UINT32_C(0x85ebca6b));
    double angle = ((double)(seed & UINT32_C(0xffff)) / 65535.0) * TWO_PI;
    double distance = 72.0 + (double)((seed >> 16u) & 255u) / 255.0 * 138.0;
    pickup->x = player->x + dsin(angle) * distance;
    pickup->z = player->z + dcos(angle) * distance;
    pickup->y = open_ground_height(pickup->x, pickup->z) + 1.0;
    pickup->progress = player->traveled_distance + distance;
    pickup->active = 1u;
}

static void pickups_refresh(odwd_engine_internal *e) {
    uint32_t index;
    vehicle_internal *player = &e->vehicles[0];
    if (e->config.world_mode == ODWD_MODE_SURVIVAL ||
        e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) {
        for (index = 0u; index < ODWD_MAX_TURBO_PICKUPS; ++index)
            e->pickups[index].active = 0u;
        return;
    }
    for (index = 0u; index < ODWD_MAX_TURBO_PICKUPS; ++index) {
        pickup_internal *pickup = &e->pickups[index];
        if (e->config.world_mode == ODWD_MODE_OPEN_FIELD) {
            if (!pickup->active) pickup_place_open(e, pickup, player);
        } else {
            projection point;
            uint32_t seed;
            double lane;
            while (pickup->progress < e->road[0].s - 48.0)
                pickup->progress += (double)ODWD_MAX_TURBO_PICKUPS * 610.0;
            pickup->active = pickup->progress > e->road[0].s + 24.0 &&
                             pickup->progress < e->road[e->road_count - 2u].s - 24.0;
            if (!pickup->active) continue;
            point = road_sample_progress(e, pickup->progress, 0u);
            seed = hash32(e->config.seed ^ index ^
                          (uint32_t)(pickup->progress / 10.0));
            lane = ((seed & 1u) ? 1.0 : -1.0) *
                   (1.2 + (double)((seed >> 4u) & 255u) / 255.0 * 2.2);
            pickup->x = point.cx + point.rx * lane;
            pickup->y = point.cy + 0.92;
            pickup->z = point.cz + point.rz * lane;
        }
    }
}

static void pickups_initialize(odwd_engine_internal *e) {
    uint32_t index;
    for (index = 0u; index < ODWD_MAX_TURBO_PICKUPS; ++index) {
        pickup_internal *pickup = &e->pickups[index];
        memset(pickup, 0, sizeof(*pickup));
        pickup->id = index;
        pickup->amount = 0.26;
        pickup->progress = 286.0 + (double)index * 610.0;
    }
    pickups_refresh(e);
}

static void pickups_collect(odwd_engine_internal *e) {
    vehicle_internal *player = &e->vehicles[0];
    uint32_t index;
    if (e->config.world_mode == ODWD_MODE_SURVIVAL ||
        e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) return;
    for (index = 0u; index < ODWD_MAX_TURBO_PICKUPS; ++index) {
        pickup_internal *pickup = &e->pickups[index];
        double dx;
        double dz;
        if (!pickup->active) continue;
        dx = player->x - pickup->x;
        dz = player->z - pickup->z;
        if (dx * dx + dz * dz > 8.2 || dabs(player->y - pickup->y) > 2.4)
            continue;
        player->turbo = clampd(player->turbo + pickup->amount, 0.0, 1.0);
        pickup->active = 0u;
        ++pickup->generation;
        if (e->config.world_mode == ODWD_MODE_ENDLESS)
            pickup->progress += (double)ODWD_MAX_TURBO_PICKUPS * 610.0;
        e->event_flags |= ODWD_EVENT_TURBO_PICKUP;
    }
    pickups_refresh(e);
}

static void spring(double *x, double *v, double target,
                   double frequency, double dt) {
    double a = (target - *x) * frequency * frequency -
               2.0 * frequency * (*v);
    *v += a * dt;
    *x += *v * dt;
}

static void spring_angle(double *x, double *v, double target,
                         double frequency, double dt) {
    double error = wrap_angle(target - *x);
    spring(x, v, *x + error, frequency, dt);
    *x = wrap_angle(*x);
}

static void vehicle_place_at(odwd_engine_internal *e, vehicle_internal *v,
                             double progress, double lane, double speed) {
    projection p = road_sample_progress(e, progress, v->route_choice);
    v->x = p.cx + p.rx * lane;
    v->y = p.cy + 0.58;
    v->z = p.cz + p.rz * lane;
    v->yaw = datan2(p.tx, p.tz);
    v->vx = p.tx * speed;
    v->vy = p.ty * speed;
    v->vz = p.tz * speed;
    v->yaw_rate = 0.0;
    v->steer = 0.0;
    v->longitudinal = speed;
    v->lateral_speed = 0.0;
    v->slip_angle = 0.0;
    v->drift = 0.0;
    v->progress = p.s;
    v->road_lateral = lane;
    v->road_segment = p.segment;
    v->offroad_time = 0.0;
    v->stuck_time = 0.0;
    v->airborne = 0u;
    v->air_time = 0.0;
    v->body_pitch = 0.0;
    v->body_roll = 0.0;
    v->pitch_rate = 0.0;
    v->roll_rate = 0.0;
    v->jump_cooldown = 0.0;
    v->previous_x = v->x;
    v->previous_z = v->z;
}

static void vehicle_respawn(odwd_engine_internal *e, vehicle_internal *v,
                            int player_event) {
    double lane = v->is_player ? 0.0 : v->ai_lane;
    /* Falling is authoritative elimination in Survival; no recovery path may
     * accidentally revive a participant. */
    if (e->config.world_mode == ODWD_MODE_SURVIVAL) return;
    if (e->config.world_mode == ODWD_MODE_OPEN_FIELD) {
        v->x = v->checkpoint_x;
        v->z = v->checkpoint_z;
        v->y = open_ground_height(v->x, v->z) + 0.58;
        v->yaw = v->checkpoint_yaw;
        v->vx = 0.0;
        v->vy = 0.0;
        v->vz = 0.0;
        v->yaw_rate = 0.0;
        v->steer = 0.0;
        v->airborne = 0u;
        v->air_time = 0.0;
        v->body_pitch = 0.0;
        v->body_roll = 0.0;
        v->pitch_rate = 0.0;
        v->roll_rate = 0.0;
        v->jump_cooldown = 0.0;
        v->offroad_time = 0.0;
        v->stuck_time = 0.0;
    } else {
        vehicle_place_at(e, v, v->checkpoint + 5.0, lane, 8.0);
    }
    ++v->respawns;
    if (player_event) e->event_flags |= ODWD_EVENT_PLAYER_RESPAWN;
}

static void vehicles_initialize(odwd_engine_internal *e) {
    static const double starts[ODWD_MAX_VEHICLES] = {
        108.0, 121.0, 129.0, 137.0, 145.0, 153.0, 161.0, 169.0
    };
    static const double lanes[ODWD_MAX_VEHICLES] = {
        0.0, -2.0, 2.0, 2.2, -2.2, -0.8, 0.8, 0.0
    };
    uint32_t i;
    for (i = 0; i < e->vehicle_count && i < ODWD_MAX_VEHICLES; ++i) {
        vehicle_internal *v = &e->vehicles[i];
        memset(v, 0, sizeof(*v));
        v->is_player = i == 0u;
        v->route_choice = (hash32(e->config.seed + i * 7919u) >> 3) & 1u;
        v->ai_lane = lanes[i];
        v->ai_skill = 0.88 + (double)(hash32(e->config.seed ^ i) & 255u) / 2550.0;
        v->checkpoint = 84.0;
        v->turbo = i == 0u ? 0.34 : 0.18;
        if (e->config.world_mode == ODWD_MODE_SURVIVAL) {
            v->x = survival_bot_anchor_x(i);
            v->z = i == 0u ? -10.8 : survival_bot_anchor_z(i);
            v->y = SURVIVAL_ARENA_Y + 0.58;
            v->yaw = 0.0;
            v->previous_x = v->x;
            v->previous_z = v->z;
            v->progress = 0.0;
            v->checkpoint = 0.0;
            v->checkpoint_x = v->x;
            v->checkpoint_z = v->z;
            v->checkpoint_yaw = v->yaw;
        } else if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) {
            v->x = 0.0;
            v->z = 0.0;
            v->y = i == 0u ? MUSIC_ARENA_Y + 0.58 : MUSIC_ARENA_Y - 40.0;
            v->yaw = 0.0;
            v->previous_x = v->x;
            v->previous_z = v->z;
            v->progress = 0.0;
            v->checkpoint = 0.0;
            v->checkpoint_x = v->x;
            v->checkpoint_z = v->z;
            v->checkpoint_yaw = v->yaw;
            v->ai_archetype = i == 0u ? MUSIC_ENEMY_NONE :
                              1u + ((i - 1u) % 3u);
            v->ai_timer = 0.55 + (double)i * 0.52;
        } else if (e->config.world_mode == ODWD_MODE_OPEN_FIELD) {
            double angle = (double)i * (TWO_PI / (double)e->vehicle_count);
            double radius = i == 0u ? 0.0 : 14.0 + (double)i * 2.2;
            v->x = dsin(angle) * radius;
            v->z = dcos(angle) * radius;
            v->y = open_ground_height(v->x, v->z) + 0.58;
            v->yaw = wrap_angle(angle + HALF_PI);
            v->previous_x = v->x;
            v->previous_z = v->z;
            v->progress = 0.0;
            v->checkpoint = 0.0;
            v->checkpoint_x = v->x;
            v->checkpoint_z = v->z;
            v->checkpoint_yaw = v->yaw;
        } else {
            vehicle_place_at(e, v, starts[i], lanes[i], i == 0u ? 0.0 : 7.0);
        }
    }
}

static void ball_reset(odwd_engine_internal *e) {
    ball_internal *ball = &e->ball;
    ball->x = OPEN_FOOTBALL_X;
    ball->z = OPEN_FOOTBALL_Z;
    ball->y = open_ground_height(ball->x, ball->z) + OPEN_BALL_RADIUS;
    ball->vx = 0.0;
    ball->vy = 0.0;
    ball->vz = 0.0;
    ball->roll_angle = 0.0;
    ball->roll_axis_x = 1.0;
    ball->roll_axis_z = 0.0;
    ball->reset_timer = 0.0;
    ball->shot_cooldown = 0.0;
}

static void ball_initialize(odwd_engine_internal *e) {
    memset(&e->ball, 0, sizeof(e->ball));
    ball_reset(e);
}

static void camera_initialize(odwd_engine_internal *e) {
    vehicle_internal *v = &e->vehicles[0];
    camera_internal *c = &e->camera;
    memset(c, 0, sizeof(*c));
    c->view_yaw = v->yaw;
    c->pitch = 0.31;
    c->pitch_target = 0.31;
    c->distance = 7.4;
    c->fov = 0.96;
    c->focus_x = v->x;
    c->focus_y = v->y + 0.8;
    c->focus_z = v->z;
    c->mode = ODWD_CAMERA_CHASE;
}

static void camera_consume_look(camera_internal *c, const odwd_input *in) {
    double dx = clampd(in->look_dx, -2.0, 2.0);
    double dy = clampd(in->look_dy, -2.0, 2.0);
    c->mode = in->camera_mode < ODWD_CAMERA_MODE_COUNT ?
              in->camera_mode : ODWD_CAMERA_CHASE;
    if (dabs(dx) + dabs(dy) > 0.0001) {
        /* Direct manipulation must feel immediate. The spring takes over only
         * after release, so dragging never feels as if the camera is fighting
         * the finger. */
        c->view_yaw = wrap_angle(c->view_yaw + dx * 0.62);
        c->view_yaw_velocity *= 0.38;
        c->pitch_target = clampd(c->pitch_target - dy * 0.31, 0.08, 1.16);
        c->manual_idle = 0.0;
    } else if (in->buttons & ODWD_BUTTON_CAMERA_HOLD) {
        c->manual_idle = 0.0;
    } else {
        c->manual_idle += DT;
    }
}
static double vehicle_speed(const vehicle_internal *v) {
    return dsqrt(v->vx * v->vx + v->vz * v->vz);
}

static void vehicle_step_physics(odwd_engine_internal *e, vehicle_internal *v,
                                 double target_yaw, double throttle,
                                 double brake, double reverse, double handbrake,
                                 double turbo_request, double steering_input,
                                 uint32_t direct_steering) {
    const double mass = 1260.0;
    const double inertia = 2280.0;
    const double lf = 1.22, lr = 1.38;
    const double gravity = 9.81;
    /* BlockDash has a deliberately compact dodge arc. It must react at once
     * and return control quickly instead of floating for several seconds. */
    const double air_gravity = e->config.world_mode == ODWD_MODE_SURVIVAL ?
                               16.8 : 9.81;
    double fwd_x = dsin(v->yaw), fwd_z = dcos(v->yaw);
    double right_x = fwd_z, right_z = -fwd_x;
    double u = v->vx * fwd_x + v->vz * fwd_z;
    double lateral = v->vx * right_x + v->vz * right_z;
    double speed = vehicle_speed(v);
    double error = wrap_angle(target_yaw - v->yaw);
    double desired_steer, steer_rate, rear_grip, front_grip;
    double alpha_f, alpha_r, fyf, fyr, fxf, low_speed_blend;
    double total_fy, ax_body, ay_body, yaw_accel;
    double turbo_force = 0.0;
    double old_x = v->x;
    double old_z = v->z;
    uint32_t open_ramp_index = 0u;
    int open_on_ramp = 0;
    int survival_ramp_crossed = 0;
    double survival_ramp_yaw = 0.0;
    projection p;

    throttle = clampd(throttle, 0.0, 1.0);
    brake = clampd(brake, 0.0, 1.0);
    reverse = clampd(reverse, 0.0, 1.0);
    handbrake = clampd(handbrake, 0.0, 1.0);
    turbo_request = clampd(turbo_request, 0.0, 1.0);
    if (turbo_request > 0.0 && v->turbo > 0.0 && brake < 0.15 &&
        !v->airborne) {
        turbo_force = 0.84 + clampd(v->turbo, 0.0, 1.0) * 0.16;
        throttle = dmax(throttle, 0.88);
        v->turbo = dmax(0.0, v->turbo - DT * 0.125);
        v->turbo_active = 1u;
    } else {
        v->turbo_active = 0u;
    }
    /* Explicit pedals feed the steering rack directly.  v0.4 converted the
     * thumb command into a target heading and then filtered it a second time
     * here; that double integration was the main source of the "dragging a
     * rock" latency.  AI/legacy direction input still uses target_yaw, while
     * mobile/Flutter steering has one bounded, fast rack response. */
    if (direct_steering) {
        double rack_limit = 0.62 *
            (1.0 - smooth01((speed - 27.0) / 58.0) * 0.34);
        steering_input = clampd(steering_input, -1.0, 1.0);
        if (dabs(steering_input) < 0.025) steering_input = 0.0;
        desired_steer = steering_input * rack_limit;
        steer_rate = clampd((desired_steer - v->steer) *
                            (steering_input == 0.0 ? 34.0 : 29.0),
                            -8.4, 8.4);
    } else {
        desired_steer = clampd(error * (speed < 10.0 ? 1.36 : 1.10),
                               -0.59, 0.59);
        desired_steer *= 1.0 - smooth01((speed - 38.0) / 42.0) * 0.16;
        steer_rate = clampd((desired_steer - v->steer) * 18.0,
                            -5.3, 5.3);
    }
    v->steer += steer_rate * DT;

    /* Rear grip is deliberately stable unless DRIFT is held. Acceleration by
     * itself never asks for oversteer. */
    {
        int player_vehicle = v == &e->vehicles[0];
        double mode_grip = !player_vehicle ? 1.0 :
                           e->config.world_mode == ODWD_MODE_SURVIVAL ? 1.18 :
                           e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL ? 1.12 :
                           e->config.world_mode == ODWD_MODE_OPEN_FIELD ? 1.06 : 1.04;
        rear_grip = (player_vehicle ? 1.48 : 1.38) * mode_grip *
                    (1.0 - 0.54 * handbrake);
        front_grip = (player_vehicle ? 1.42 : 1.30) * mode_grip;
    }

    alpha_f = datan2(lateral + lf * v->yaw_rate, dabs(u) + 1.6) - v->steer;
    alpha_r = datan2(lateral - lr * v->yaw_rate, dabs(u) + 1.6);
    fyf = clampd(-90000.0 * alpha_f, -front_grip * mass * gravity * 0.52,
                 front_grip * mass * gravity * 0.52);
    fyr = clampd(-104000.0 * (1.0 - 0.46 * handbrake) * alpha_r,
                 -rear_grip * mass * gravity * 0.48,
                 rear_grip * mass * gravity * 0.48);
    /* A car sliding sideways at speed still has loaded tyres. Blending from
     * longitudinal speed alone disabled all lateral recovery when u ~= 0 and
     * let football/bumper impacts coast hundreds of metres. */
    low_speed_blend = smooth01(speed / 3.5);
    fyf *= low_speed_blend;
    fyr *= low_speed_blend;

    fxf = throttle * (u < 15.0 ? 13200.0 : 9650.0) *
          (1.0 + turbo_force * 1.08) *
          clampd(1.0 - speed / (70.0 + turbo_force * 31.0), 0.0, 1.0);
    /* A reverse pedal first brakes forward motion, then supplies a controlled
     * reverse drive force. Throttle symmetrically brakes if already reversing. */
    if (reverse > 0.0) {
        if (u > 0.65) fxf -= reverse * 17100.0;
        else fxf -= reverse * 7900.0 * clampd(1.0 - dabs(u) / 18.0, 0.0, 1.0);
    }
    if (throttle > 0.0 && u < -0.65) fxf += throttle * 9200.0;
    fxf -= 0.43 * u * dabs(u);
    fxf -= 180.0 * u / (dabs(u) + 1.0);
    if (brake > 0.0 && dabs(u) > 0.1)
        fxf -= sign_nonzero(u) * brake * 17600.0;

    if (v->airborne) {
        /* No tyre force or steering authority while all four wheels are in
         * the air. Only conservative aerodynamic drag remains. */
        fyf = 0.0;
        fyr = 0.0;
        fxf = -0.18 * u * dabs(u) - 70.0 * u / (dabs(u) + 1.0);
    }

    total_fy = fyf + fyr;
    if (!v->airborne && handbrake < 0.60) {
        /* Stability assist is a physical lateral tyre force, not pose
         * correction. It damps unwanted sideslip while leaving held DRIFT
         * free to rotate the rear. */
        int player_vehicle = v == &e->vehicles[0];
        double stability = -lateral * mass *
            ((player_vehicle ? 1.82 : 1.45) +
             clampd(speed * (player_vehicle ? 0.024 : 0.020), 0.0,
                    player_vehicle ? 1.32 : 1.10)) *
            (1.0 - handbrake * 1.45);
        double stability_limit = mass * gravity *
                                 (player_vehicle ? 0.54 : 0.42);
        total_fy += clampd(stability, -stability_limit, stability_limit);
    }
    ax_body = fxf / mass;
    yaw_accel = (lf * fyf - lr * fyr) / inertia -
                v->yaw_rate * (v->airborne ? 0.08 :
                               (handbrake > 0.05 ? 0.34 :
                                1.02 + clampd(speed * 0.006, 0.0, 0.34)));

    /* Drift is an intentional controllable state, not merely "rear tyres are
     * gone".  A held handbrake adds a bounded yaw moment in the commanded
     * steering direction and retains enough rear authority for countersteer.
     * This gives entry -> hold -> recovery phases instead of a binary spin. */
    if (!v->airborne && handbrake > 0.02 && speed > 6.0) {
        double drift_authority = smooth01((speed - 6.0) / 15.0) * handbrake;
        double command = direct_steering ? clampd(steering_input, -1.0, 1.0) :
                         clampd(v->steer / 0.55, -1.0, 1.0);
        double counter = clampd((-command * lateral) / (dabs(lateral) + 2.2),
                                0.0, 1.0);
        yaw_accel += command * drift_authority *
                     (1.10 + clampd(speed * 0.046, 0.0, 1.65));
        yaw_accel -= v->yaw_rate * counter * 0.46;
        total_fy += -lateral * mass * counter * 0.34;
    }
    ay_body = total_fy / mass;

    v->vx += (fwd_x * ax_body + right_x * ay_body) * DT;
    v->vz += (fwd_z * ax_body + right_z * ay_body) * DT;
    v->yaw_rate += yaw_accel * DT;
    v->yaw_rate = clampd(v->yaw_rate,
                         handbrake > 0.02 ? -2.65 : -2.32,
                         handbrake > 0.02 ?  2.65 :  2.32);
    v->yaw = wrap_angle(v->yaw + v->yaw_rate * DT);
    v->x += v->vx * DT;
    v->z += v->vz * DT;
    /* Music Survival is a closed arena. Holes are valid fall volumes, but
     * crossing the outer perimeter is a wall contact, never missing floor.
     * Enemies keep their exit lanes; this confinement is player-only. */
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL &&
        v == &e->vehicles[0] &&
        !music_map_contains(&e->music_survival, v->x, v->z)) {
        double nx = old_x / dmax(e->music_survival.half_w *
                                 e->music_survival.half_w, 1.0);
        double nz = old_z / dmax(e->music_survival.half_d *
                                 e->music_survival.half_d, 1.0);
        double nl = dsqrt(nx * nx + nz * nz);
        double outward;
        if (nl < 1.0e-6) { nx = 1.0; nz = 0.0; }
        else { nx /= nl; nz /= nl; }
        v->x = old_x;
        v->z = old_z;
        outward = v->vx * nx + v->vz * nz;
        if (outward > 0.0) {
            v->vx -= nx * outward * 1.62;
            v->vz -= nz * outward * 1.62;
            v->last_collision_impulse = dmax(v->last_collision_impulse, outward);
            e->event_flags |= ODWD_EVENT_COLLISION;
        }
    }
    v->traveled_distance += dsqrt((v->x - old_x) * (v->x - old_x) +
                                  (v->z - old_z) * (v->z - old_z));
    v->previous_x = old_x;
    v->previous_z = old_z;

    memset(&p, 0, sizeof(p));
    if (e->config.world_mode == ODWD_MODE_OPEN_FIELD) {
        p.cy = open_ground_height(v->x, v->z);
        open_on_ramp = open_ramp_surface(v->x, v->z, &open_ramp_index,
                                         &p.cy, NULL);
        p.half_width = 1000000.0;
        p.s = v->traveled_distance;
        v->progress = v->traveled_distance;
        v->road_lateral = 0.0;
        v->road_segment = 0u;
    } else if (e->config.world_mode == ODWD_MODE_SURVIVAL) {
        int support = 0;
        p.cy = survival_ground_height(e, v, &support);
        p.half_width = SURVIVAL_ARENA_HALF_W;
        p.s = (double)e->survival.sector_index +
              e->survival.sector_elapsed /
              dmax(e->survival.sector_duration, 0.001);
        v->progress = p.s;
        v->road_lateral = v->x;
        v->road_segment = 0u;
        if (!support && !v->airborne) {
            v->airborne = 1u;
            v->air_time = 0.0;
            v->vy = dmin(v->vy, 0.0);
        }
    } else if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) {
        int support = music_vehicle_supported_at(e, v->x, v->z);
        p.cy = music_ground_height_at(e, v->x, v->z);
        p.half_width = e->music_survival.half_w;
        p.s = e->music_survival.score;
        v->progress = p.s;
        v->road_lateral = v->x;
        v->road_segment = 0u;
        if (!support && !v->airborne) {
            v->airborne = 1u;
            v->air_time = 0.0;
            v->vy = dmin(v->vy, 0.0);
        }
    } else {
        p = road_project(e, v->x, v->y, v->z, v->road_segment,
                         v->route_choice);
        v->road_segment = p.segment;
        v->route_choice = p.alternate;
        v->progress = p.s;
        v->road_lateral = p.lateral;
    }
    {
        double desired_y = p.cy + 0.58;
        uint32_t launch_ramp = 0u;
        uint32_t trampoline = 0u;
        int launch_crossed = e->config.world_mode == ODWD_MODE_OPEN_FIELD &&
            open_ramp_launch_crossed(old_x, old_z, v->x, v->z,
                                     &launch_ramp);
        int trampoline_contact = e->config.world_mode == ODWD_MODE_OPEN_FIELD &&
            open_trampoline_at(v->x, v->z, &trampoline);
        if (e->config.world_mode == ODWD_MODE_SURVIVAL)
            survival_ramp_crossed = survival_ramp_launch_crossed(
                &e->survival, old_x, old_z, v->x, v->z, &survival_ramp_yaw);
        if (survival_ramp_crossed &&
            v->vx * dsin(survival_ramp_yaw) +
            v->vz * dcos(survival_ramp_yaw) > 5.0) {
            v->airborne = 1u;
            v->air_time = 0.0;
            v->vy = dmax(v->vy, e->survival.envelope.jump_impulse * 0.96 +
                                clampd(speed * 0.065, 0.0, 2.5));
            v->pitch_rate = -0.40 - clampd(speed * 0.006, 0.0, 0.22);
            v->roll_rate = clampd(v->yaw_rate * 0.18, -0.34, 0.34);
            v->jump_cooldown = 0.50;
            e->event_flags |= ODWD_EVENT_JUMP;
        } else if (!v->airborne && v->jump_cooldown <= 0.0 &&
            trampoline_contact && speed > 2.0) {
            v->airborne = 1u;
            v->air_time = 0.0;
            v->vy = 24.0 + clampd(speed * 0.19, 0.0, 8.5);
            v->pitch_rate = -0.62 - clampd(speed * 0.005, 0.0, 0.24);
            v->roll_rate = clampd(v->yaw_rate * 0.24 +
                                  v->lateral_speed * 0.018, -0.55, 0.55);
            v->jump_cooldown = 1.05;
            e->event_flags |= ODWD_EVENT_JUMP | ODWD_EVENT_TRAMPOLINE;
        } else if (!v->airborne && launch_crossed &&
                   v->vx * dsin(open_ramps[launch_ramp].yaw) +
                   v->vz * dcos(open_ramps[launch_ramp].yaw) > 5.0) {
            const open_ramp_definition *ramp = &open_ramps[launch_ramp];
            v->airborne = 1u;
            v->air_time = 0.0;
            v->vy = dmax(v->vy,
                         ramp->launch_speed + speed *
                         (launch_ramp == 3u ? 0.21 : 0.34) +
                         (launch_ramp == 3u ? 2.8 : 5.8));
            v->pitch_rate = -0.34 - ramp->launch_speed * 0.017 -
                            clampd(speed * 0.004, 0.0, 0.20);
            v->roll_rate = clampd(v->yaw_rate * 0.20 +
                                  v->lateral_speed * 0.014, -0.48, 0.48);
            v->jump_cooldown = 0.78;
            e->event_flags |= ODWD_EVENT_JUMP;
        }
        if (e->config.world_mode == ODWD_MODE_ENDLESS &&
            (p.flags & ODWD_ROAD_GAP)) {
            if (!v->airborne) {
                v->airborne = 1u;
                v->air_time = 0.0;
                v->vy = dmax(v->vy, dabs(u) * dmax(p.ty, 0.0) +
                                    clampd(dabs(u) * 0.055, 1.2, 4.5));
                e->event_flags |= ODWD_EVENT_JUMP;
            }
            v->vy -= air_gravity * DT;
            v->y += v->vy * DT;
            v->air_time += DT;
        } else if (v->airborne) {
            int can_land = 1;
            double landing_speed;
            v->vy -= air_gravity * DT;
            v->y += v->vy * DT;
            v->air_time += DT;
            if (e->config.world_mode == ODWD_MODE_SURVIVAL)
                can_land = survival_vehicle_supported(e, v);
            else if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL)
                can_land = music_vehicle_supported_at(e, v->x, v->z);
            if (v->y <= desired_y && can_land) {
                landing_speed = dmax(0.0, -v->vy);
                v->y = desired_y;
                v->vy = 0.0;
                v->airborne = 0u;
                v->pitch_rate *= 0.24;
                v->roll_rate *= 0.22;
                v->last_collision_impulse = dmax(v->last_collision_impulse,
                                                  landing_speed * 0.36);
                e->event_flags |= ODWD_EVENT_LAND;
            }
        } else {
            double vertical_accel = (desired_y - v->y) * 52.0 - v->vy * 11.5;
            v->vy += vertical_accel * DT;
            v->y += v->vy * DT;
            if (v->y < desired_y - 0.06) {
                v->y = desired_y - 0.06;
                if (v->vy < 0.0) v->vy = 0.0;
            }
        }

        if (v->airborne) {
            double horizontal_speed = dsqrt(v->vx * v->vx + v->vz * v->vz);
            double flight_pitch = -datan2(v->vy, horizontal_speed + 1.0);
            /* Angular momentum survives the launch. A light aerodynamic
             * aligning moment progressively points the nose along the flight
             * path, so the car crests and falls nose-first instead of hovering
             * in one frozen pose. */
            v->pitch_rate += (flight_pitch - v->body_pitch) * 0.92 * DT;
            v->roll_rate += v->yaw_rate * 0.075 * DT;
            v->pitch_rate *= 0.9990;
            v->roll_rate *= 0.9982;
            v->body_pitch = clampd(v->body_pitch + v->pitch_rate * DT,
                                   -1.12, 1.12);
            v->body_roll = clampd(v->body_roll + v->roll_rate * DT,
                                  -1.08, 1.08);
        } else {
            double slope_pitch = e->config.world_mode == ODWD_MODE_ENDLESS ?
                                 -datan2(p.ty, 1.0) :
                                 (open_on_ramp ?
                                  -datan2(open_ramps[open_ramp_index].height /
                                           open_ramps[open_ramp_index].length,
                                           1.0) : 0.0);
            double terrain_roll = 0.0;
            double road_roll;
            if (e->config.world_mode == ODWD_MODE_OPEN_FIELD && !open_on_ramp) {
                const double sample_f = 1.55;
                const double sample_r = 1.12;
                double hf = open_ground_height(v->x + fwd_x * sample_f,
                                               v->z + fwd_z * sample_f);
                double hb = open_ground_height(v->x - fwd_x * sample_f,
                                               v->z - fwd_z * sample_f);
                double hr = open_ground_height(v->x + right_x * sample_r,
                                               v->z + right_z * sample_r);
                double hl = open_ground_height(v->x - right_x * sample_r,
                                               v->z - right_z * sample_r);
                slope_pitch = -datan2(hf - hb, sample_f * 2.0);
                terrain_roll = datan2(hr - hl, sample_r * 2.0);
                slope_pitch = clampd(slope_pitch, -0.70, 0.70);
                terrain_roll = clampd(terrain_roll, -0.62, 0.62);
            }
            road_roll = clampd(terrain_roll - lateral * 0.009 -
                               v->yaw_rate * 0.052, -0.66, 0.66);
            /* Critically damped upright assist prevents an arcade jump or
             * bumper impact from leaving the car on its roof. */
            spring(&v->body_pitch, &v->pitch_rate, slope_pitch, 8.5, DT);
            spring(&v->body_roll, &v->roll_rate, road_roll, 10.5, DT);
        }
    }

    if (v->jump_cooldown > 0.0) v->jump_cooldown -= DT;

    if (e->config.world_mode == ODWD_MODE_ENDLESS &&
        dabs(p.lateral) > p.half_width) {
        double slowdown = clampd((dabs(p.lateral) - p.half_width) * 0.012, 0.0, 0.025);
        v->vx *= 1.0 - slowdown;
        v->vz *= 1.0 - slowdown;
        v->offroad_time += DT;
    } else {
        v->offroad_time = dmax(0.0, v->offroad_time - DT * 2.5);
    }

    if (e->config.world_mode == ODWD_MODE_ENDLESS &&
        !(p.flags & ODWD_ROAD_GAP) &&
        !road_main_branch_exit_open(e, &p) &&
        dabs(p.lateral) > p.half_width - ROAD_EDGE_CLEARANCE) {
        double side = sign_nonzero(p.lateral);
        double excess = dabs(p.lateral) -
                        (p.half_width - ROAD_EDGE_CLEARANCE);
        double outward = (v->vx * p.rx + v->vz * p.rz) * side;
        v->x -= p.rx * side * excess * 0.82;
        v->z -= p.rz * side * excess * 0.82;
        if (outward > 0.0) {
            double impulse = outward * 1.36;
            v->vx -= p.rx * side * impulse;
            v->vz -= p.rz * side * impulse;
            v->last_collision_impulse = dmax(v->last_collision_impulse, impulse);
            if (v->collision_cooldown <= 0.0) {
                ++v->collisions;
                v->collision_cooldown = 0.28;
            }
            e->event_flags |= ODWD_EVENT_COLLISION;
        }
    }

    {
        uint32_t prop_index;
        for (prop_index = 0u; prop_index < e->prop_count; ++prop_index) {
            const prop_internal *prop = &e->props[prop_index];
            double dx;
            double dz;
            double minimum;
            double d2;
            double distance;
            double nx;
            double nz;
            double relative;
            double penetration;
            if (!prop->collidable || v->y > prop->y + 2.8 * prop->scale)
                continue;
            dx = v->x - prop->x;
            dz = v->z - prop->z;
            if (prop->type == ODWD_PROP_BARRIER) {
                double cosine = dcos(prop->rotation);
                double sine = dsin(prop->rotation);
                double local_x = dx * cosine - dz * sine;
                double local_z = dx * sine + dz * cosine;
                double closest_x = clampd(local_x,
                                          -OPEN_BARRIER_HALF_LENGTH,
                                           OPEN_BARRIER_HALF_LENGTH);
                double closest_z = clampd(local_z,
                                          -OPEN_BARRIER_HALF_DEPTH,
                                           OPEN_BARRIER_HALF_DEPTH);
                double offset_x = local_x - closest_x;
                double offset_z = local_z - closest_z;
                double offset_sq = offset_x * offset_x +
                                   offset_z * offset_z;
                double local_nx;
                double local_nz;
                if (offset_sq >= VEHICLE_PROP_RADIUS *
                                 VEHICLE_PROP_RADIUS) continue;
                if (offset_sq > 1.0e-8) {
                    distance = dsqrt(offset_sq);
                    local_nx = offset_x / distance;
                    local_nz = offset_z / distance;
                    penetration = VEHICLE_PROP_RADIUS - distance;
                } else {
                    double face_x = OPEN_BARRIER_HALF_LENGTH -
                                    dabs(local_x);
                    double face_z = OPEN_BARRIER_HALF_DEPTH -
                                    dabs(local_z);
                    if (face_x < face_z) {
                        local_nx = sign_nonzero(local_x);
                        local_nz = 0.0;
                        penetration = VEHICLE_PROP_RADIUS + face_x;
                    } else {
                        local_nx = 0.0;
                        local_nz = sign_nonzero(local_z);
                        penetration = VEHICLE_PROP_RADIUS + face_z;
                    }
                }
                nx = local_nx * cosine + local_nz * sine;
                nz = -local_nx * sine + local_nz * cosine;
            } else if (prop->type == ODWD_PROP_GOAL) {
                double cosine = dcos(prop->rotation);
                double sine = dsin(prop->rotation);
                double local_x = dx * cosine - dz * sine;
                double local_z = dx * sine + dz * cosine;
                double post_x = local_x < 0.0 ? -OPEN_GOAL_MOUTH_HALF :
                                                OPEN_GOAL_MOUTH_HALF;
                double post_dx = local_x - post_x;
                double post_dz = local_z;
                double local_nx;
                double local_nz;
                minimum = OPEN_GOAL_POST_RADIUS + VEHICLE_PROP_RADIUS;
                d2 = post_dx * post_dx + post_dz * post_dz;
                if (d2 >= minimum * minimum) continue;
                distance = dsqrt(d2);
                if (distance < 0.001) {
                    local_nx = sign_nonzero(local_x);
                    local_nz = 0.0;
                    distance = 0.001;
                } else {
                    local_nx = post_dx / distance;
                    local_nz = post_dz / distance;
                }
                penetration = minimum - distance;
                nx = local_nx * cosine + local_nz * sine;
                nz = -local_nx * sine + local_nz * cosine;
            } else {
                minimum = prop->radius + VEHICLE_PROP_RADIUS;
                d2 = dx * dx + dz * dz;
                if (d2 >= minimum * minimum) continue;
                distance = dsqrt(d2);
                if (distance < 0.001) {
                    nx = 1.0;
                    nz = 0.0;
                    distance = 0.001;
                } else {
                    nx = dx / distance;
                    nz = dz / distance;
                }
                penetration = minimum - distance;
            }
            v->x += nx * penetration;
            v->z += nz * penetration;
            relative = v->vx * nx + v->vz * nz;
            if (relative < 0.0) {
                v->vx -= nx * relative * 1.62;
                v->vz -= nz * relative * 1.62;
                v->last_collision_impulse = dmax(v->last_collision_impulse,
                                                  -relative);
            }
            if (v->collision_cooldown <= 0.0) {
                ++v->collisions;
                v->collision_cooldown = 0.30;
            }
            e->event_flags |= ODWD_EVENT_COLLISION;
        }
    }

    if (e->config.world_mode == ODWD_MODE_SURVIVAL &&
        !e->survival.eliminated[(uint32_t)(v - e->vehicles)])
        survival_collide_obstacles(e, v);

    speed = vehicle_speed(v);
    fwd_x = dsin(v->yaw);
    fwd_z = dcos(v->yaw);
    right_x = fwd_z;
    right_z = -fwd_x;
    u = v->vx * fwd_x + v->vz * fwd_z;
    lateral = v->vx * right_x + v->vz * right_z;
    v->longitudinal = u;
    v->lateral_speed = lateral;
    v->slip_angle = datan2(lateral, dabs(u) + 0.35);
    v->drift = smooth01((dabs(v->slip_angle) - 0.07) / 0.38) *
               smooth01((speed - 7.0) / 17.0);
    if (speed < 1.0 && (throttle > 0.55 || reverse > 0.55)) v->stuck_time += DT;
    else v->stuck_time = dmax(0.0, v->stuck_time - DT * 0.5);
    if (v->collision_cooldown > 0.0) v->collision_cooldown -= DT;
    v->last_collision_impulse *= 0.93;
}

static void ai_step(odwd_engine_internal *e, vehicle_internal *v,
                    const vehicle_internal *player);

static double music_autodrive_point_score(const odwd_engine_internal *e,
                                          const vehicle_internal *v,
                                          double x, double z,
                                          double prediction_s) {
    const music_survival_state *m = &e->music_survival;
    double score = 0.0;
    uint32_t i;
    if (!music_map_contains(m, x, z) ||
        !music_map_contains(m, x + 3.0, z) ||
        !music_map_contains(m, x - 3.0, z) ||
        !music_map_contains(m, x, z + 3.0) ||
        !music_map_contains(m, x, z - 3.0)) return -1.0e12;
    if (music_hole_contains(m, x, z)) return -1.0e12;

    score -= (dabs(x) / dmax(m->half_w, 1.0) +
              dabs(z) / dmax(m->half_d, 1.0)) * 2.5;

    if (m->hazard_phase != MUSIC_EVENT_COOLDOWN) {
        for (i = 0u; i < m->hazard_count; ++i) {
            const music_hazard_zone *h = &m->hazards[i];
            double hx = x - h->x;
            double hz = z - h->z;
            double hd = dsqrt(hx * hx + hz * hz);
            double clearance = h->radius + 4.0;
            if (m->hazard_type == MUSIC_HAZARD_HOLES ||
                m->hazard_type == MUSIC_HAZARD_LAVA_RAIN ||
                m->hazard_type == MUSIC_HAZARD_METEORS ||
                m->hazard_type == MUSIC_HAZARD_LIGHTNING) {
                if (hd < clearance) score -= 900.0 + (clearance - hd) * 60.0;
                else score += dmin(hd - clearance, 18.0) * 0.28;
            }
        }
        if (m->hazard_type == MUSIC_HAZARD_SHOCKWAVE &&
            m->hazard_count > 0u && m->hazard_phase == MUSIC_EVENT_ACTIVE) {
            const music_hazard_zone *h = &m->hazards[0];
            double hx = x - h->x;
            double hz = z - h->z;
            double hd = dsqrt(hx * hx + hz * hz);
            double future_elapsed = m->phase_elapsed + prediction_s;
            double wave = clampd(future_elapsed /
                                 dmax(m->phase_duration, 0.001), 0.0, 1.0) *
                          h->strength;
            double separation = dabs(hd - wave);
            if (separation < 7.0) score -= (7.0 - separation) * 80.0;
        }
        if (m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD) {
            double ground = music_ground_height_at(e, x, z) - MUSIC_ARENA_Y;
            int inside_winner = 0;
            score += ground * 22.0;
            for (i = 0u; i < m->safe_count; ++i) {
                const music_safe_zone *safe = &m->safe[i];
                double dx = x - safe->x, dz = z - safe->z;
                double d = dsqrt(dx * dx + dz * dz);
                if (safe->level >= m->hazard_level) {
                    score += dmax(0.0, 55.0 - d) * 8.0;
                    if (d <= safe->radius * 0.86) {
                        score += 900.0;
                        inside_winner = 1;
                    }
                } else if (d <= safe->radius * 0.92) {
                    score -= 420.0;
                }
            }
            if (m->hazard_phase == MUSIC_EVENT_ACTIVE && !inside_winner &&
                ground < 1.0 + (double)m->hazard_level * 2.0)
                score -= 540.0;
        }
        if (m->hazard_type == MUSIC_HAZARD_METEORS &&
            music_inside_safe(m, x, z, 1)) score += 480.0;
    }

    /* Predict moving enemies along the candidate corridor. The old endpoint
     * check could choose a destination that was safe only after crossing an
     * enemy's future line. */
    for (i = 1u; i < e->vehicle_count; ++i) {
        const vehicle_internal *enemy = &e->vehicles[i];
        double ex, ez, ed;
        double px, pz;
        if (enemy->y < MUSIC_ARENA_Y - 5.0) continue;
        px = enemy->x + enemy->vx * prediction_s;
        pz = enemy->z + enemy->vz * prediction_s;
        ex = x - px;
        ez = z - pz;
        ed = dsqrt(ex * ex + ez * ez);
        if (ed < 8.5) score -= (8.5 - ed) * 85.0;
        else score += dmin(ed, 28.0) * 0.14;
    }
    (void)v;
    return score;
}

static double music_autodrive_score(const odwd_engine_internal *e,
                                    const vehicle_internal *v,
                                    double x, double z) {
    double dx = x - v->x;
    double dz = z - v->z;
    double distance = dsqrt(dx * dx + dz * dz);
    double score = 0.0;
    double worst = 1.0e30;
    uint32_t samples, sample;
    if (distance > 1.0) {
        double heading = datan2(dx, dz);
        score -= dabs(wrap_angle(heading - v->yaw)) * 2.6;
    }
    score += dmin(distance, 34.0) * 0.22;

    /* Score the full swept corridor, not just the endpoint. Samples become
     * denser as the requested path grows, bounded so AUTO remains cheap. */
    samples = (uint32_t)clampd(distance / 4.5 + 2.0, 3.0, 12.0);
    for (sample = 1u; sample <= samples; ++sample) {
        double t = (double)sample / (double)samples;
        double sx = v->x + dx * t;
        double sz = v->z + dz * t;
        double travel_s = clampd((distance * t) / 19.0, 0.0, 2.0);
        double point = music_autodrive_point_score(e, v, sx, sz, travel_s);
        if (point < -1.0e11) return -1.0e12;
        if (point < worst) worst = point;
        score += point / (double)samples;
    }
    /* A single dangerous part of the path must dominate a merely attractive
     * endpoint. */
    score += worst * 0.42;
    return score;
}

static int survival_uses_platform_navigation(const survival_state *s);
static int survival_bot_upcoming_gate(const odwd_engine_internal *e,
                                      const vehicle_internal *v,
                                      uint32_t index,
                                      double *target_x,
                                      double *forward_distance);
static int survival_bot_platform_target(const odwd_engine_internal *e,
                                        const vehicle_internal *v,
                                        uint32_t index,
                                        double *target_x,
                                        double *target_z);

static void survival_player_autodrive(odwd_engine_internal *e,
                                      vehicle_internal *v) {
    survival_state *s = &e->survival;
    const double anchor_z = -10.8;
    double tx = v->x;
    double tz = anchor_z;
    double gate_forward = 1.0e9;
    double nearest_forward = 1.0e9;
    double target_yaw, desired_speed, throttle, brake, reverse = 0.0;
    uint32_t i;
    int have_platform = 0;
    if (s->eliminated[0]) return;

    if (survival_uses_platform_navigation(s))
        have_platform = survival_bot_platform_target(e, v, 0u, &tx, &tz);

    if (!have_platform) {
        double gate_x;
        if (survival_bot_upcoming_gate(e, v, 0u, &gate_x, &gate_forward)) {
            tx = gate_x;
        } else if (s->solution_count > 0u) {
            double best = 1.0e30;
            uint32_t best_i = 0u;
            for (i = 0u; i < s->solution_count; ++i) {
                double d = dabs(s->solution_x[i] - v->x);
                if (d < best) { best = d; best_i = i; }
            }
            tx = s->solution_x[best_i];
        }
        if (s->family == SURVIVAL_LOW_WALL)
            tx = v->x;

        /* BlockDash is not a race toward +Z: the hazards travel toward the
         * player.  AUTO only needs forward motion as steering room to change
         * lane, then it must settle back into a safe longitudinal band.  The
         * previous controller continuously placed its target ahead and could
         * simply drive out of the north edge after a correct dodge. */
        {
            double lateral = dabs(tx - v->x);
            double lookahead = clampd(1.8 + lateral * 0.15, 1.8, 3.6);
            double z_ahead = v->z - anchor_z;
            target_yaw = lateral > 0.20 ? datan2(tx - v->x, lookahead) : 0.0;

            desired_speed = clampd(2.2 + lateral * 1.15, 0.0, 9.6);
            if (s->sector_index == 0u && s->sector_elapsed < 0.80)
                desired_speed = 0.0;
            if (lateral < 0.55) desired_speed = 0.0;
            if (gate_forward < 7.0 && lateral > 0.55)
                desired_speed = dmax(desired_speed, 5.2);
            if (nearest_forward < 7.0 && lateral > 0.55)
                desired_speed = dmax(desired_speed, 4.8);

            /* Keep the car around its starting row.  Reverse is a pedal here,
             * not a U-turn: heading stays forward while the car backs gently
             * into the longitudinal safe band after a lateral manoeuvre. */
            if (z_ahead > 3.2 && lateral < 1.20) {
                desired_speed = 0.0;
            } else if (z_ahead > 5.0) {
                desired_speed = dmin(desired_speed, lateral > 2.0 ? 3.2 : 0.0);
            }
            if (v->z < anchor_z - 1.4 && lateral < 1.20)
                desired_speed = dmax(desired_speed, 2.8);
        }
    } else {
        double dx = tx - v->x, dz = tz - v->z;
        double distance = dsqrt(dx * dx + dz * dz);
        double heading;
        target_yaw = distance > 0.25 ? datan2(dx, dz) : v->yaw;
        heading = dabs(wrap_angle(target_yaw - v->yaw));
        desired_speed = clampd(distance * 2.25, 0.0, 9.4);
        if (heading > 0.55)
            desired_speed *= clampd(1.0 - (heading - 0.55) / 1.20, 0.08, 1.0);
    }

    for (i = 0u; i < s->obstacle_count; ++i) {
        double dz = s->obstacles[i].z - v->z;
        if (s->obstacles[i].active && dz > 0.0 && dz < nearest_forward)
            nearest_forward = dz;
    }

    if (have_platform && (nearest_forward < 8.0 || gate_forward < 8.0))
        desired_speed *= 0.76;
    throttle = reverse > 0.02 ? 0.0 :
               clampd((desired_speed - v->longitudinal) * 0.18, 0.0, 1.0);
    brake = reverse > 0.02 ? 0.0 :
            clampd((v->longitudinal - desired_speed) * 0.18, 0.0, 0.94);
    if (s->requires_jump && nearest_forward < 5.9 && nearest_forward > 2.15)
        survival_try_jump(e, v, 1);
    vehicle_step_physics(e, v, target_yaw, throttle, brake, reverse,
                         0.0, 0.0, 0.0, 0u);
}

static void autodrive_step(odwd_engine_internal *e, vehicle_internal *v) {
    double target_yaw;
    double desired_speed;
    double throttle;
    double brake;
    double handbrake = 0.0;
    double turbo = 0.0;
    if (e->config.world_mode == ODWD_MODE_ENDLESS) {
        ai_step(e, v, v);
        return;
    }
    if (e->config.world_mode == ODWD_MODE_SURVIVAL) {
        survival_player_autodrive(e, v);
        return;
    }
    if (e->config.world_mode == ODWD_MODE_OPEN_FIELD) {
        static const double route[][2] = {
            {0.0, 92.0}, {92.0, 52.0},
            {OPEN_FOOTBALL_X - 18.0, OPEN_FOOTBALL_Z},
            {62.0, 222.0}, {OPEN_PEAK_X, OPEN_PEAK_Z - 34.0},
            {-76.0, 228.0}, {OPEN_BUMPER_X + 30.0, OPEN_BUMPER_Z},
            {-176.0, -174.0}, {-38.0, -128.0}, {36.0, -42.0}
        };
        double tx, tz, dx, dz, distance;
        uint32_t i;
        if (v->ai_timer > 0.0) v->ai_timer -= DT;
        tx = route[v->ai_state % (sizeof(route) / sizeof(route[0]))][0];
        tz = route[v->ai_state % (sizeof(route) / sizeof(route[0]))][1];
        dx = tx - v->x; dz = tz - v->z;
        distance = dsqrt(dx * dx + dz * dz);
        if (v->ai_timer <= 0.0 || distance < 15.0) {
            v->ai_state = (v->ai_state + 1u) %
                          (uint32_t)(sizeof(route) / sizeof(route[0]));
            v->ai_timer = 16.0;
            tx = route[v->ai_state][0];
            tz = route[v->ai_state][1];
        }
        /* Local prop avoidance bends the scenic route around houses, rocks,
         * goals and barriers without teleporting or modifying world geometry. */
        for (i = 0u; i < e->prop_count; ++i) {
            const prop_internal *prop = &e->props[i];
            double px, pz, d2, range;
            if (!prop->collidable) continue;
            px = v->x - prop->x; pz = v->z - prop->z;
            d2 = px * px + pz * pz;
            range = prop->radius + 9.0;
            if (d2 > 0.05 && d2 < range * range) {
                double d = dsqrt(d2);
                double push = (range - d) * 1.8;
                tx += px / d * push;
                tz += pz / d * push;
            }
        }
        dx = tx - v->x; dz = tz - v->z;
        distance = dsqrt(dx * dx + dz * dz);
        target_yaw = distance > 0.3 ? datan2(dx, dz) : v->yaw;
        desired_speed = 24.0 + dmin(distance * 0.10, 9.0);
        if (e->activity_zone == ODWD_ACTIVITY_BUMPER_ARENA) desired_speed = 18.0;
        if (e->activity_zone == ODWD_ACTIVITY_FOOTBALL) {
            double bx = e->ball.x - v->x;
            double bz = e->ball.z - v->z;
            target_yaw = datan2(bx, bz);
            desired_speed = 22.0;
        }
        throttle = clampd((desired_speed - v->longitudinal) * 0.14, 0.0, 1.0);
        brake = clampd((v->longitudinal - desired_speed) * 0.11, 0.0, 0.80);
        handbrake = smooth01((dabs(wrap_angle(target_yaw - v->yaw)) - 0.52) /
                             0.60) * smooth01((vehicle_speed(v) - 15.0) / 13.0) * 0.58;
        turbo = dabs(wrap_angle(target_yaw - v->yaw)) < 0.12 &&
                distance > 34.0 && v->turbo > 0.18 ? 1.0 : 0.0;
        vehicle_step_physics(e, v, target_yaw, throttle, brake, 0.0,
                             handbrake, turbo, 0.0, 0u);
        return;
    }
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) {
        music_survival_state *m = &e->music_survival;
        double dx, dz, distance;
        double flood_safe_radius = 0.0;
        int flood_target_forced = 0;
        int flood_hold = 0;
        uint32_t k;
        if (m->finished) {
            vehicle_step_physics(e, v, v->yaw, 0.0, 0.75, 0.0, 0.0, 0.0,
                                 0.0, 0u);
            return;
        }
        /* Flood has exactly one refuge high enough for the selected level.
         * Once it is telegraphed, make that physical fact authoritative for
         * AUTO instead of letting generic wandering candidates temporarily
         * steal the target. */
        if (m->hazard_type == MUSIC_HAZARD_LAVA_FLOOD &&
            m->hazard_phase != MUSIC_EVENT_COOLDOWN) {
            for (k = 0u; k < m->safe_count; ++k) {
                if (m->safe[k].level >= m->hazard_level) {
                    v->ai_target_x = m->safe[k].x;
                    v->ai_target_z = m->safe[k].z;
                    flood_safe_radius = m->safe[k].radius;
                    flood_target_forced = 1;
                    break;
                }
            }
        }
        v->ai_timer -= DT;
        dx = v->ai_target_x - v->x;
        dz = v->ai_target_z - v->z;
        distance = dsqrt(dx * dx + dz * dz);
        if (!flood_target_forced &&
            (v->ai_timer <= 0.0 || distance < 5.0 ||
             music_autodrive_score(e, v, v->ai_target_x, v->ai_target_z) < -300.0)) {
            double best_score = -1.0e20;
            double best_x = 0.0, best_z = 0.0;
            for (k = 0u; k < 32u; ++k) {
                double angle = ((double)k / 32.0) * TWO_PI;
                double radius = (k & 1u) ? 31.0 : 19.0;
                double cx = v->x + dsin(angle) * radius;
                double cz = v->z + dcos(angle) * radius;
                double candidate = music_autodrive_score(e, v, cx, cz);
                if (candidate > best_score) {
                    best_score = candidate; best_x = cx; best_z = cz;
                }
            }
            /* Explicit safe structures compete with generic candidates. */
            for (k = 0u; k < m->safe_count; ++k) {
                double candidate = music_autodrive_score(e, v,
                    m->safe[k].x, m->safe[k].z);
                if (candidate > best_score) {
                    best_score = candidate;
                    best_x = m->safe[k].x;
                    best_z = m->safe[k].z;
                }
            }
            v->ai_target_x = best_x;
            v->ai_target_z = best_z;
            v->ai_timer = 0.24;
        }
        dx = v->ai_target_x - v->x;
        dz = v->ai_target_z - v->z;
        distance = dsqrt(dx * dx + dz * dz);
        target_yaw = distance > 0.4 ? datan2(dx, dz) : v->yaw;
        desired_speed = 20.0 + e->music_energy * 8.0 + e->music_pulse * 3.0;
        if (m->hazard_phase == MUSIC_EVENT_ACTIVE) desired_speed += 3.5;
        /* Arrival is part of navigation. AUTO now sheds speed near its target
         * instead of crossing a refuge at ~22 m/s and immediately leaving the
         * only safe ground. */
        desired_speed = dmin(desired_speed, clampd(distance * 1.15, 0.0, desired_speed));
        if (flood_target_forced) {
            /* Brake for the refuge before reaching its centre. Using only
             * distance*1.15 let a fast car arrive correctly and then cross
             * the entire safe hill before the longitudinal controller could
             * shed its momentum. */
            desired_speed = dmin(desired_speed,
                clampd((distance - flood_safe_radius * 0.30) * 0.72,
                       0.0, 15.0));
            {
                double flood_h = MUSIC_ARENA_Y + 0.8 +
                                 (double)m->hazard_level * 2.05;
                double ground_h = music_ground_height_at(e, v->x, v->z);
                /* "Inside the hill" is not equivalent to "above the lava".
                 * Higher flood levels only leave the central crown safe, so
                 * AUTO keeps converging until the authoritative terrain
                 * height itself clears the future flood surface. */
                if (ground_h + 0.50 >= flood_h + 0.22) {
                    flood_hold = 1;
                    desired_speed = 0.0;
                    target_yaw = v->yaw;
                }
            }
        }
        throttle = clampd((desired_speed - v->longitudinal) * 0.15, 0.0, 1.0);
        brake = clampd((v->longitudinal - desired_speed) * 0.15, 0.0, 0.92);
        if (flood_target_forced)
            brake = dmax(brake,
                clampd((vehicle_speed(v) - desired_speed) * 0.12, 0.0, 1.0));
        if (flood_hold) {
            throttle = 0.0;
            brake = 1.0;
        }
        handbrake = smooth01((dabs(wrap_angle(target_yaw - v->yaw)) - 0.58) /
                             0.62) * smooth01((vehicle_speed(v) - 16.0) / 12.0) * 0.44;
        turbo = !flood_target_forced &&
                dabs(wrap_angle(target_yaw - v->yaw)) < 0.11 &&
                distance > 27.0 && v->turbo > 0.12 ? 1.0 : 0.0;
        vehicle_step_physics(e, v, target_yaw, throttle, brake, 0.0,
                             handbrake, turbo, 0.0, 0u);
    }
}

static void player_step(odwd_engine_internal *e, const odwd_input *in) {
    vehicle_internal *v = &e->vehicles[0];
    double jx = clampd(in->joystick_x, -1.0, 1.0);
    double jy = clampd(in->joystick_y, -1.0, 1.0);
    double magnitude = dsqrt(jx * jx + jy * jy);
    double target_yaw, local_angle, throttle, brake, reverse, handbrake, turbo;
    e->autodrive_active = (in->buttons & ODWD_BUTTON_AUTODRIVE) ? 1u : 0u;
    if (in->buttons & ODWD_BUTTON_HEADLIGHTS)
        v->headlights = v->headlights ? 0u : 1u;
    if (e->config.world_mode == ODWD_MODE_SURVIVAL && e->survival.eliminated[0]) {
        /* Once eliminated, do not fake a hovering car. Let the body visibly
         * tumble away while the camera remains anchored to the arena. */
        v->vy -= 22.0 * DT;
        v->y += v->vy * DT;
        v->pitch_rate -= 0.52 * DT;
        v->roll_rate += sign_nonzero(v->yaw_rate + 0.01) * 0.38 * DT;
        v->body_pitch = clampd(v->body_pitch + v->pitch_rate * DT, -1.3, 1.3);
        v->body_roll = clampd(v->body_roll + v->roll_rate * DT, -1.3, 1.3);
        return;
    }
    if (e->autodrive_active) {
        autodrive_step(e, v);
        return;
    }
    reverse = dmax(clampd(in->reverse, 0.0, 1.0),
                   (in->buttons & ODWD_BUTTON_REVERSE) ? 1.0 : 0.0);
    {
        uint32_t jump_down = (in->buttons & ODWD_BUTTON_JUMP) != 0u;
        /* Jump is edge-triggered in the authority layer. Holding a touch
         * cannot queue a delayed hop or immediately bunny-hop on landing. */
        survival_try_jump(e, v, jump_down && !e->player_jump_down);
        e->player_jump_down = jump_down;
    }
    if (in->buttons & ODWD_BUTTON_EXPLICIT_PEDALS) {
        double steering = clampd(jx, -1.0, 1.0);
        double reverse_direction = v->longitudinal < -0.45 ? -1.0 : 1.0;
        if (dabs(steering) < 0.025) steering = 0.0;
        /* Pedal mode is a steering rack, not a camera-relative direction
         * cursor. Looking around can never rotate the vehicle. */
        throttle = clampd(in->throttle, 0.0, 1.0);
        brake = clampd(in->brake, 0.0, 1.0);
        if (reverse > 0.02) throttle *= 1.0 - reverse;
        handbrake = (in->buttons & ODWD_BUTTON_HANDBRAKE) ? 1.0 : 0.0;
        turbo = (in->buttons & ODWD_BUTTON_TURBO) ? 1.0 : 0.0;
        vehicle_step_physics(e, v, v->yaw, throttle, brake, reverse,
                             handbrake, turbo, steering * reverse_direction,
                             1u);
        return;
    }
    if (magnitude < 0.12) {
        jx = jy = 0.0;
        magnitude = 0.0;
    } else {
        magnitude = clampd((magnitude - 0.12) / 0.88, 0.0, 1.0);
        magnitude *= dsqrt(dsqrt(magnitude));
    }
    local_angle = magnitude > 0.0 ? datan2(jx, jy) : 0.0;
    target_yaw = wrap_angle(e->camera.view_yaw + local_angle);
    throttle = reverse > 0.02 ? 0.0 : magnitude;
    brake = magnitude > 0.0 ?
        smooth01((dabs(wrap_angle(target_yaw - v->yaw)) - 1.12) / 0.78) * 0.34 : 0.0;
    handbrake = (in->buttons & ODWD_BUTTON_HANDBRAKE) ? 1.0 : 0.0;
    turbo = (in->buttons & ODWD_BUTTON_TURBO) ? 1.0 : 0.0;
    if (magnitude == 0.0) target_yaw = v->yaw;
    vehicle_step_physics(e, v, target_yaw, throttle, brake, reverse,
                         handbrake, turbo, 0.0, 0u);
}
static void open_activity_prepare(odwd_engine_internal *e) {
    vehicle_internal *player;
    uint32_t next;
    uint32_t index;
    if (e->config.world_mode != ODWD_MODE_OPEN_FIELD) {
        e->activity_zone = ODWD_ACTIVITY_EXPLORE;
        return;
    }
    player = &e->vehicles[0];
    /* Activity hysteresis prevents a single football/bumper impact from
     * disabling the rules and sending every bot back to exploration. The
     * larger exit boundary still lets the player leave deliberately. */
    if (e->activity_zone == ODWD_ACTIVITY_FOOTBALL &&
        dabs(player->x - OPEN_FOOTBALL_X) < 72.0 &&
        dabs(player->z - OPEN_FOOTBALL_Z) < 50.0)
        return;
    if (e->activity_zone == ODWD_ACTIVITY_BUMPER_ARENA) {
        double dx = player->x - OPEN_BUMPER_X;
        double dz = player->z - OPEN_BUMPER_Z;
        if (dx * dx + dz * dz < 74.0 * 74.0) return;
    }
    next = open_activity_zone_for(player->x, player->z);
    if (next == e->activity_zone) return;
    e->activity_zone = next;
    e->event_flags |= ODWD_EVENT_ACTIVITY_ZONE;
    if (next == ODWD_ACTIVITY_FOOTBALL) {
        /* Rivals may have been orbiting the exploration spawn hundreds of
         * metres away. Bring only out-of-field cars to deterministic team
         * slots once on entry; cars already playing keep continuous poses. */
        for (index = 1u; index < e->vehicle_count; ++index) {
            vehicle_internal *v = &e->vehicles[index];
            uint32_t team = index & 1u;
            uint32_t team_rank = (index - 1u) / 2u;
            double side = team == 0u ? -1.0 : 1.0;
            double z_lane = team_rank == 0u ? 0.0 :
                            (double)team_rank * 8.0 *
                            (team == 0u ? -1.0 : 1.0);
            if (dabs(v->x - OPEN_FOOTBALL_X) <
                    OPEN_GOAL_PLANE_HALF - 2.0 &&
                dabs(v->z - OPEN_FOOTBALL_Z) <
                    OPEN_FOOTBALL_TOUCHLINE_HALF - 2.0)
                continue;
            v->x = OPEN_FOOTBALL_X + side *
                   (18.0 + (double)team_rank * 3.5);
            v->z = OPEN_FOOTBALL_Z + z_lane;
            v->y = open_ground_height(v->x, v->z) + 0.58;
            v->yaw = team == 0u ? HALF_PI : -HALF_PI;
            v->vx = v->vz = v->vy = 0.0;
            v->airborne = 0u;
            v->body_pitch = v->body_roll = 0.0;
            v->pitch_rate = v->roll_rate = 0.0;
            v->jump_cooldown = v->air_time = 0.0;
        }
        return;
    }
    if (next != ODWD_ACTIVITY_BUMPER_ARENA) return;

    /* Entering the bumper bowl calls the rivals into deliberate starting
     * lanes. They arrive on the far ring, already moving into the activity,
     * instead of continually teleporting around the player. */
    for (index = 1u; index < e->vehicle_count; ++index) {
        vehicle_internal *v = &e->vehicles[index];
        double current_dx = v->x - OPEN_BUMPER_X;
        double current_dz = v->z - OPEN_BUMPER_Z;
        double angle = (double)(index - 1u) /
                       (double)dmax((double)e->vehicle_count - 1.0, 1.0) *
                       TWO_PI + 0.55;
        if (current_dx * current_dx + current_dz * current_dz <
            78.0 * 78.0) continue;
        v->x = OPEN_BUMPER_X + dsin(angle) * 34.0;
        v->z = OPEN_BUMPER_Z + dcos(angle) * 34.0;
        v->y = open_ground_height(v->x, v->z) + 0.58;
        v->yaw = wrap_angle(angle + PI);
        v->vx = -dsin(angle) * 10.0;
        v->vz = -dcos(angle) * 10.0;
        v->vy = 0.0;
        v->airborne = 0u;
        v->body_pitch = 0.0;
        v->body_roll = 0.0;
        v->pitch_rate = 0.0;
        v->roll_rate = 0.0;
        v->jump_cooldown = 0.0;
        v->air_time = 0.0;
    }
}

static void music_enemy_step(odwd_engine_internal *e, vehicle_internal *v,
                             const vehicle_internal *player) {
    music_survival_state *m = &e->music_survival;
    uint32_t index = (uint32_t)(v - e->vehicles);
    double tx, tz, target_yaw, speed, desired_speed, throttle, brake;
    if (v->y < MUSIC_ARENA_Y - 10.0) {
        v->ai_timer -= DT;
        return;
    }
    v->ai_timer -= DT;
    tx = v->ai_target_x;
    tz = v->ai_target_z;
    if (v->ai_archetype == MUSIC_ENEMY_CHASER ||
        v->ai_archetype == MUSIC_ENEMY_PUSHER) {
        if (v->ai_timer > 0.0) {
            double avoid_x = 0.0, avoid_z = 0.0;
            uint32_t i;
            tx = player->x;
            tz = player->z;
            /* Shelters and safe hills are treated as navigation features, not
             * invisible walls. Chasers bias around them before aiming at the
             * player, so their line remains legible rather than getting stuck. */
            for (i = 0u; i < m->safe_count; ++i) {
                const music_safe_zone *safe = &m->safe[i];
                double dx = v->x - safe->x;
                double dz = v->z - safe->z;
                double d2 = dx * dx + dz * dz;
                double range = safe->radius + 5.0;
                if (d2 < range * range && d2 > 0.04) {
                    double inv = 1.0 / dsqrt(d2);
                    avoid_x += dx * inv * (range - dsqrt(d2)) * 0.9;
                    avoid_z += dz * inv * (range - dsqrt(d2)) * 0.9;
                }
            }
            tx += avoid_x;
            tz += avoid_z;
        } else {
            /* Timed pursuers stop tracking, commit to their current heading,
             * then leave the arena exactly as requested. */
            if (v->ai_state < UINT32_C(0x80000000)) {
                double fx = dsin(v->yaw), fz = dcos(v->yaw);
                v->ai_target_x = v->x + fx * (m->half_w + m->half_d + 30.0);
                v->ai_target_z = v->z + fz * (m->half_w + m->half_d + 30.0);
                v->ai_state |= UINT32_C(0x80000000);
            }
            tx = v->ai_target_x;
            tz = v->ai_target_z;
        }
    }
    target_yaw = datan2(tx - v->x, tz - v->z);
    speed = vehicle_speed(v);
    if (v->ai_archetype == MUSIC_ENEMY_STRIKER)
        desired_speed = 35.0 + e->music_energy * 11.0 + e->music_bass * 6.0;
    else if (v->ai_archetype == MUSIC_ENEMY_CHASER)
        desired_speed = 19.0 + e->music_mid * 5.0 + e->music_energy * 3.0;
    else
        desired_speed = 17.0 + e->music_pulse * 4.0;
    throttle = clampd((desired_speed - v->longitudinal) * 0.18, 0.0, 1.0);
    brake = clampd((v->longitudinal - desired_speed) * 0.13, 0.0, 0.75);
    vehicle_step_physics(e, v, target_yaw, throttle, brake, 0.0, 0.0, 0.0,
                         0.0, 0u);
    if (!music_map_contains(m, v->x, v->z) ||
        dabs(v->x) > m->half_w + 7.0 || dabs(v->z) > m->half_d + 7.0) {
        v->x = 0.0;
        v->z = 0.0;
        v->y = MUSIC_ARENA_Y - 40.0;
        v->vx = v->vy = v->vz = 0.0;
        v->ai_state &= UINT32_C(0x7fffffff);
        v->ai_timer = 1.1 + (double)(hash32(e->config.seed ^ index * 1237u ^
                                m->event_index * 3301u) & 255u) / 255.0 * 3.4;
    } else if (speed < 0.8 && v->ai_timer < -3.0) {
        v->y = MUSIC_ARENA_Y - 40.0;
        v->ai_timer = 1.0;
    }
}

static int survival_uses_platform_navigation(const survival_state *s) {
    return s->family == SURVIVAL_FRAGMENTED ||
           s->family == SURVIVAL_BRIDGE ||
           s->family == SURVIVAL_ISLANDS ||
           s->family == SURVIVAL_RAMP_GAP ||
           s->family == SURVIVAL_MOVING_FLOOR;
}

/* Platform sectors are a different navigation problem from moving-wall
 * sectors.  Driving toward the generic wall-gap waypoint would intentionally
 * send a bot across a floor discontinuity.  Keep an already-supported rival
 * on its current island; during the warning phase select a reachable island
 * using distance plus a deterministic per-rival preference.  The final target
 * is inset by the real vehicle envelope, so the AI steers to usable floor,
 * not merely to the platform's mathematical centre. */
/* Infer the actual opening of the next moving wall from geometry.  This keeps
 * DOUBLE_WALL / PRECISION / MOVING_GAP honest: later gates may have a
 * different centre than solution_x[0], and the AI must react to that gate
 * rather than driving into it. */
static int survival_bot_upcoming_gate(const odwd_engine_internal *e,
                                      const vehicle_internal *v,
                                      uint32_t index,
                                      double *target_x,
                                      double *forward_distance) {
    const survival_state *s = &e->survival;
    double group_z = 1.0e300;
    double left_limit = -SURVIVAL_ARENA_HALF_W + s->envelope.half_width + 0.24;
    double right_limit = SURVIVAL_ARENA_HALF_W - s->envelope.half_width - 0.24;
    double begin[SURVIVAL_MAX_OBSTACLES];
    double end[SURVIVAL_MAX_OBSTACLES];
    uint32_t count = 0u, i, j;
    double best_cost = 1.0e300;
    int found = 0;

    for (i = 0u; i < s->obstacle_count; ++i) {
        const survival_obstacle *o = &s->obstacles[i];
        double dz;
        if (!o->active || o->type != ODWD_PROP_SURVIVAL_WALL || o->vz >= -0.05)
            continue;
        dz = o->z - v->z;
        if (dz < 0.7) continue;
        if (o->z < group_z) group_z = o->z;
    }
    if (group_z == 1.0e300) return 0;

    for (i = 0u; i < s->obstacle_count && count < SURVIVAL_MAX_OBSTACLES; ++i) {
        const survival_obstacle *o = &s->obstacles[i];
        double clearance;
        if (!o->active || o->type != ODWD_PROP_SURVIVAL_WALL ||
            dabs(o->z - group_z) > 0.28) continue;
        clearance = s->envelope.half_width + 0.20;
        begin[count] = clampd(o->x - o->half_x - clearance,
                              left_limit, right_limit);
        end[count] = clampd(o->x + o->half_x + clearance,
                            left_limit, right_limit);
        ++count;
    }
    if (count == 0u) return 0;
    for (i = 1u; i < count; ++i) {
        double b = begin[i], en = end[i];
        j = i;
        while (j > 0u && b < begin[j - 1u]) {
            begin[j] = begin[j - 1u]; end[j] = end[j - 1u]; --j;
        }
        begin[j] = b; end[j] = en;
    }
    {
        double cursor = left_limit;
        uint32_t gap_index = 0u;
        for (i = 0u; i <= count; ++i) {
            double gap_end = i < count ? begin[i] : right_limit;
            if (gap_end - cursor >= 0.32) {
                double center = (cursor + gap_end) * 0.5;
                double half = (gap_end - cursor) * 0.5;
                double lane = hash_signed(e->config.seed ^ s->sector_index,
                                          (int64_t)index,
                                          UINT32_C(0xc2b2ae35) + gap_index);
                double candidate = center + lane * dmin(0.72, half * 0.34);
                double cost = dabs(candidate - v->x);
                /* When several legal openings exist, rivals naturally split
                 * toward the nearest one instead of crossing through each
                 * other to obey one shared lane. */
                if (cost < best_cost) {
                    best_cost = cost;
                    *target_x = clampd(candidate, cursor + 0.10, gap_end - 0.10);
                    found = 1;
                }
                ++gap_index;
            }
            if (i < count && end[i] > cursor) cursor = end[i];
        }
    }
    if (found && forward_distance) *forward_distance = group_z - v->z;
    return found;
}

static int survival_bot_platform_target(const odwd_engine_internal *e,
                                        const vehicle_internal *v,
                                        uint32_t index,
                                        double *target_x,
                                        double *target_z) {
    const survival_state *s = &e->survival;
    uint32_t i;
    uint32_t best = UINT32_MAX;
    double best_cost = 1.0e300;
    double inset_x = s->envelope.half_width + 0.62;
    double inset_z = s->envelope.half_length + 0.68;
    if (!survival_uses_platform_navigation(s) || s->platform_count == 0u)
        return 0;

    /* Choose by distance to the usable *interior rectangle*, not distance to
     * its centre.  A bot that is already safely parked should not execute a
     * U-turn merely to seek a prettier centre point. */
    for (i = 0u; i < s->platform_count; ++i) {
        const survival_platform *p = &s->platforms[i];
        double usable_x, usable_z, px, pz, dx, dz, cost;
        uint32_t preferred, ring;
        if (!p->active) continue;
        usable_x = p->half_x - inset_x;
        usable_z = p->half_z - inset_z;
        if (usable_x < 0.15 || usable_z < 0.15) continue;
        px = clampd(v->x, p->x - usable_x, p->x + usable_x);
        pz = clampd(v->z, p->z - usable_z, p->z + usable_z);
        dx = v->x - px;
        dz = v->z - pz;
        cost = dx * dx + dz * dz;
        /* Only use preference as a tie-breaker during the warning floor. Once
         * real gaps exist, geometric proximity has absolute priority. */
        if (s->sector_elapsed < s->warning_time) {
            preferred = hash32(e->config.seed ^
                               s->sector_index * UINT32_C(0x7f4a7c15) ^
                               index * UINT32_C(0x9e3779b9)) %
                        s->platform_count;
            ring = i > preferred ? i - preferred : preferred - i;
            if (ring > s->platform_count / 2u) ring = s->platform_count - ring;
            cost += (double)ring * 0.035;
        }
        if (cost < best_cost) {
            best_cost = cost;
            best = i;
        }
    }

    if (best != UINT32_MAX) {
        const survival_platform *p = &s->platforms[best];
        double usable_x = dmax(0.0, p->half_x - inset_x);
        double usable_z = dmax(0.0, p->half_z - inset_z);
        double lane_x = hash_signed(e->config.seed ^ s->sector_index,
                                    (int64_t)index, UINT32_C(0xa24baed5));
        double lane_z = hash_signed(e->config.seed ^ index,
                                    (int64_t)s->sector_index,
                                    UINT32_C(0x9fb21c65));
        double px = clampd(v->x, p->x - usable_x, p->x + usable_x);
        double pz = clampd(v->z, p->z - usable_z, p->z + usable_z);
        /* Small personal settling offsets prevent overlap without turning a
         * safe parked car into a cross-platform traveller. */
        px += lane_x * dmin(0.42, usable_x * 0.12);
        pz += lane_z * dmin(0.32, usable_z * 0.10);
        *target_x = clampd(px, p->x - usable_x, p->x + usable_x);
        *target_z = clampd(pz, p->z - usable_z, p->z + usable_z);
        return 1;
    }
    return 0;
}

static void ai_step(odwd_engine_internal *e, vehicle_internal *v,
                    const vehicle_internal *player) {
    double speed = vehicle_speed(v);
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) {
        music_enemy_step(e, v, player);
        return;
    }
    if (e->config.world_mode == ODWD_MODE_SURVIVAL) {
        survival_state *s = &e->survival;
        uint32_t index = (uint32_t)(v - e->vehicles);
        uint32_t choice;
        double tx, tz, target_yaw, desired_speed, throttle, brake;
        double nearest_forward = 1.0e9;
        double gate_forward = 1.0e9;
        uint32_t i;
        if (s->eliminated[index]) {
            v->vy -= 9.81 * DT;
            v->y += v->vy * DT;
            return;
        }
        choice = s->solution_count > 0u ?
                 hash32(e->config.seed ^ index * 971u ^ s->sector_index * 6151u) %
                 s->solution_count : 0u;
        tx = s->solution_x[choice];
        tz = survival_bot_anchor_z(index);
        if (s->family == SURVIVAL_LOW_WALL) {
            /* A full-width low wall is a jump test, not a lane-merging test.
             * Keep every rival in its own row/column so seven cars do not
             * converge at x=0 before the obstacle arrives. */
            tx = survival_bot_anchor_x(index);
            tz = survival_bot_anchor_z(index);
        } else if (!survival_bot_platform_target(e, v, index, &tx, &tz)) {
            double gate_x;
            /* Use the actual next gate if one exists. DOUBLE_WALL and
             * PRECISION intentionally move the opening between waves. */
            if (survival_bot_upcoming_gate(e, v, index, &gate_x, &gate_forward)) {
                tx = gate_x;
            } else {
                /* Bots must not form one suicidal train through the exact
                 * pixel when a sector has a declared static solution. */
                double lane = hash_signed(e->config.seed ^ s->sector_index,
                                          (int64_t)index,
                                          UINT32_C(0x61c88647));
                double spread = s->solution_count > 1u ? 1.25 : 0.88;
                tx += lane * spread + (v->ai_skill - 0.93) * 2.2;
            }
            /* Cars need some longitudinal room to change lanes, but no rival
             * should race indefinitely toward the north edge. */
            {
                double lateral_need = dabs(tx - v->x);
                double anchor_z = survival_bot_anchor_z(index);
                tz = anchor_z + clampd(lateral_need * 0.34, 0.0, 4.4);
                if (v->z > anchor_z + 5.2) tz = anchor_z;
                if (v->z < anchor_z - 5.2) tz = anchor_z + 2.2;
            }
        }
        for (i = 0u; i < s->obstacle_count; ++i) {
            double dz = s->obstacles[i].z - v->z;
            if (s->obstacles[i].active && dz > 0.0 && dz < nearest_forward)
                nearest_forward = dz;
        }
        {
            double dx = tx - v->x, dz = tz - v->z;
            double distance = dsqrt(dx * dx + dz * dz);
            target_yaw = distance < 0.28 ? v->yaw : datan2(dx, dz);
            desired_speed = 12.6 + v->ai_skill * 5.4 + s->difficulty * 2.0 +
                            (double)(index & 1u) * 0.65;
            if (survival_uses_platform_navigation(s)) {
                /* Zero distance means zero requested travel. The previous
                 * minimum of 3.2 m/s made a bot accelerate even after reaching
                 * safety and was a second cause of platform suicides. */
                desired_speed = clampd(distance * 2.35, 0.0,
                                       8.4 + v->ai_skill * 1.4);
            } else if (s->family == SURVIVAL_LOW_WALL) {
                desired_speed = clampd(distance * 2.0, 0.0, 3.2);
            } else if (distance < 4.0) {
                desired_speed = dmin(desired_speed, distance * 3.0 + 1.2);
            }
            /* Never apply race throttle while the target is substantially
             * behind the nose. Turn/brake first; then accelerate into line. */
            {
                double heading = dabs(wrap_angle(target_yaw - v->yaw));
                if (heading > 0.72)
                    desired_speed *= clampd(1.0 - (heading - 0.72) / 1.55,
                                            0.12, 1.0);
            }
        }
        if (nearest_forward < 8.0 || gate_forward < 8.0)
            desired_speed *= 0.78;
        throttle = clampd((desired_speed - v->longitudinal) * 0.16, 0.0, 0.94);
        brake = clampd((v->longitudinal - desired_speed) * 0.13, 0.0, 0.82);
        if (s->requires_jump) {
            /* Time the compact dodge so obstacle contact occurs near the
             * vertical apex. Triggering at ~8 m made the bot land before the
             * low wall arrived, then get swept off the arena. */
            double reaction = 5.15 + v->ai_skill * 0.68;
            /* Competence changes timing, not whether the bot randomly decides
             * to commit suicide. Every rival reacts to a mandatory low wall. */
            if (nearest_forward < reaction && nearest_forward > 2.2)
                survival_try_jump(e, v, 1);
        }
        vehicle_step_physics(e, v, target_yaw, throttle, brake, 0.0, 0.0, 0.0,
                             0.0, 0u);
        return;
    }
    if (e->config.world_mode == ODWD_MODE_OPEN_FIELD) {
        uint32_t ai_index = (uint32_t)(v - e->vehicles);
        double phase = (double)e->tick * DT * (0.11 + v->ai_skill * 0.025) +
                       v->ai_lane * 0.73;
        double orbit = 34.0 + dabs(v->ai_lane) * 4.0;
        double tx;
        double tz;
        double target_yaw;
        double desired_speed;
        double throttle;
        double brake;
        double handbrake;
        double boundary_brake = 0.0;
        int suppress_handbrake = 0;
        if (e->activity_zone == ODWD_ACTIVITY_FOOTBALL) {
            uint32_t team = ai_index & 1u;
            uint32_t team_rank = (ai_index - 1u) / 2u;
            double attack = team == 0u ? 1.0 : -1.0;
            double pitch_half_x = OPEN_GOAL_PLANE_HALF - 4.0;
            double pitch_half_z = OPEN_FOOTBALL_TOUCHLINE_HALF - 4.0;
            if (team_rank == 0u) {
                /* Defenders hold a readable line between their own goal and
                 * the ball. The remaining cars approach from behind the
                 * ball so their contact pushes it toward the opposing goal.
                 * Rank zero exists once for each alternating team. */
                double own_goal_x = OPEN_FOOTBALL_X -
                                    attack * OPEN_GOAL_PLANE_HALF;
                tx = own_goal_x + attack * 15.0 +
                     (e->ball.x - OPEN_FOOTBALL_X) * 0.18;
                tz = OPEN_FOOTBALL_Z +
                     (e->ball.z - OPEN_FOOTBALL_Z) * 0.58 +
                     v->ai_lane * 0.75;
                desired_speed = 23.0 + v->ai_skill * 7.0;
            } else {
                double goal_x = OPEN_FOOTBALL_X +
                                attack * OPEN_GOAL_PLANE_HALF;
                double shot_x = goal_x - e->ball.x;
                double shot_z = OPEN_FOOTBALL_Z - e->ball.z;
                double shot_length = dsqrt(shot_x * shot_x +
                                            shot_z * shot_z);
                double approach = 3.3 + v->ai_skill * 1.2;
                if (shot_length < 0.001) shot_length = 1.0;
                /* Approach behind the ball on the ball->goal vector. Merely
                 * following its z coordinate produced many touches but sent
                 * almost every shot outside the narrow cinematic goal. */
                tx = e->ball.x - shot_x / shot_length * approach;
                tz = e->ball.z - shot_z / shot_length * approach +
                     v->ai_lane * 0.22;
                desired_speed = 27.0 + v->ai_skill * 8.0;
            }
            tx = clampd(tx, OPEN_FOOTBALL_X - pitch_half_x,
                        OPEN_FOOTBALL_X + pitch_half_x);
            tz = clampd(tz, OPEN_FOOTBALL_Z - pitch_half_z,
                        OPEN_FOOTBALL_Z + pitch_half_z);
            {
                double predicted_x = v->x + v->vx * 0.82;
                double predicted_z = v->z + v->vz * 0.82;
                double outside_x = dabs(predicted_x - OPEN_FOOTBALL_X) -
                                   pitch_half_x;
                double outside_z = dabs(predicted_z - OPEN_FOOTBALL_Z) -
                                   pitch_half_z;
                double outward_x = sign_nonzero(v->x - OPEN_FOOTBALL_X) *
                                   v->vx;
                double outward_z = sign_nonzero(v->z - OPEN_FOOTBALL_Z) *
                                   v->vz;
                int current_outside =
                    dabs(v->x - OPEN_FOOTBALL_X) > pitch_half_x ||
                    dabs(v->z - OPEN_FOOTBALL_Z) > pitch_half_z;
                double distance;
                if (current_outside) {
                    /* Aim for the nearest point well inside the field rather
                     * than chasing the ball farther out of bounds. */
                    tx = clampd(v->x, OPEN_FOOTBALL_X - pitch_half_x + 8.0,
                                OPEN_FOOTBALL_X + pitch_half_x - 8.0);
                    tz = clampd(v->z, OPEN_FOOTBALL_Z - pitch_half_z + 7.0,
                                OPEN_FOOTBALL_Z + pitch_half_z - 7.0);
                    desired_speed = dmin(desired_speed, 10.5);
                    suppress_handbrake = 1;
                }
                distance = dsqrt((tx - v->x) * (tx - v->x) +
                                  (tz - v->z) * (tz - v->z));
                /* Constant 35 m/s targets made cars overshoot the entire
                 * pitch. Approach speed now falls with target distance. */
                desired_speed = dmin(desired_speed, 5.0 + distance * 0.72);
                /* Brake only motion that is carrying the car outward. A car
                 * already outside must retain throttle while returning. */
                boundary_brake = smooth01((dmax(outside_x, outside_z) +
                                             1.5) / 7.0) *
                                 smooth01((dmax(outward_x, outward_z) -
                                             0.25) / 6.0) * 0.88;
                if (boundary_brake > 0.02) suppress_handbrake = 1;
            }
        } else if (e->activity_zone == ODWD_ACTIVITY_BUMPER_ARENA) {
            double lane_angle = phase * 1.72 + v->ai_lane;
            tx = OPEN_BUMPER_X + dsin(lane_angle) * 27.0;
            tz = OPEN_BUMPER_Z + dcos(lane_angle) * 27.0;
            if (((e->tick / 240u) +
                 (uint64_t)(dabs(v->ai_lane) * 70.0)) & UINT64_C(1)) {
                tx = player->x;
                tz = player->z;
            }
            desired_speed = 27.0 + v->ai_skill * 8.0;
        } else {
            tx = player->x + dsin(phase) * orbit;
            tz = player->z + dcos(phase) * orbit;
            desired_speed = 22.0 + v->ai_skill * 9.0;
        }
        target_yaw = datan2(tx - v->x, tz - v->z);
        throttle = clampd((desired_speed - v->longitudinal) * 0.13,
                          0.0, 0.96);
        brake = clampd((v->longitudinal - desired_speed) * 0.10,
                       0.0, 0.72);
        if (boundary_brake > 0.0) {
            throttle *= 1.0 - boundary_brake * 0.82;
            brake = dmax(brake, boundary_brake);
        }
        handbrake = smooth01((dabs(wrap_angle(target_yaw - v->yaw)) -
                              0.34) / 0.72) *
                    smooth01((speed - 11.0) / 10.0) *
                    (e->activity_zone == ODWD_ACTIVITY_BUMPER_ARENA ?
                     0.28 : (e->activity_zone == ODWD_ACTIVITY_FOOTBALL ?
                             0.34 : 0.54));
        if (suppress_handbrake) handbrake = 0.0;
        vehicle_step_physics(e, v, target_yaw, throttle, brake, 0.0,
                             handbrake, 0.0, 0.0, 0u);
        return;
    }
    double lookahead = 15.0 + speed * 0.58;
    projection target = road_sample_progress(e, v->progress + lookahead,
                                             v->route_choice);
    double lane = v->ai_lane;
    double tx = target.cx + target.rx * lane;
    double tz = target.cz + target.rz * lane;
    double target_yaw = datan2(tx - v->x, tz - v->z);
    const road_internal *rn = &e->road[target.segment];
    double curve = dabs(rn->curvature);
    double desired_speed = 37.0 - clampd(curve * 1450.0, 0.0, 17.0);
    double gap = player->progress - v->progress;
    double throttle, brake, handbrake;
    desired_speed *= v->ai_skill;
    /* Rubber band changes acceleration/speed intent only.  It never writes pose. */
    if (gap > 0.0) desired_speed += clampd(gap * 0.050, 0.0, 31.0);
    else desired_speed -= clampd((-gap) * 0.050, 0.0, 38.0);
    desired_speed = clampd(desired_speed, 0.0, 68.0);
    throttle = clampd((desired_speed - v->longitudinal) * 0.16, 0.0, 1.0);
    brake = clampd((v->longitudinal - desired_speed) * 0.12, 0.0, 0.85);
    handbrake = smooth01((curve - 0.0065) / 0.0060) *
                smooth01((speed - 15.0) / 12.0) * 0.55;
    vehicle_step_physics(e, v, target_yaw, throttle, brake, 0.0, handbrake, 0.0,
                         0.0, 0u);
}

static void collide_vehicles(odwd_engine_internal *e) {
    uint32_t i, j;
    const double min_distance = 3.05;
    for (i = 0; i < e->vehicle_count; ++i) {
        for (j = i + 1u; j < e->vehicle_count; ++j) {
            vehicle_internal *a = &e->vehicles[i];
            vehicle_internal *b = &e->vehicles[j];
            double dx = b->x - a->x, dz = b->z - a->z;
            double d2 = dx * dx + dz * dz;
            double distance, nx, nz, overlap, relative, impulse;
            int bumper_collision = 0;
            if (e->config.world_mode == ODWD_MODE_SURVIVAL &&
                (e->survival.eliminated[i] || e->survival.eliminated[j])) continue;
            if (dabs(a->y - b->y) > 1.8) continue;
            if (e->config.world_mode == ODWD_MODE_SURVIVAL) {
                survival_obstacle other;
                double onx, onz;
                memset(&other, 0, sizeof(other));
                if (d2 > 6.2 * 6.2) continue;
                other.x = b->x; other.y = b->y;
                other.z = b->z; other.yaw = b->yaw;
                other.half_x = VEHICLE_ENVELOPE_HALF_WIDTH;
                other.half_y = 0.80;
                other.half_z = VEHICLE_ENVELOPE_HALF_LENGTH;
                if (!survival_obb_contact(a, &other, &onx, &onz, &overlap))
                    continue;
                nx = -onx;
                nz = -onz;
                distance = dsqrt(d2);
            } else {
                if (d2 >= min_distance * min_distance) continue;
                distance = dsqrt(d2);
                if (distance < 0.001) {
                    nx = ((i + j) & 1u) ? 1.0 : -1.0;
                    nz = 0.0;
                    distance = 0.001;
                } else {
                    nx = dx / distance;
                    nz = dz / distance;
                }
                overlap = min_distance - distance;
            }
            if (dabs(nx) + dabs(nz) < 0.001) { nx = 1.0; nz = 0.0; }
            a->x -= nx * overlap * 0.51;
            a->z -= nz * overlap * 0.51;
            b->x += nx * overlap * 0.49;
            b->z += nz * overlap * 0.49;
            relative = (b->vx - a->vx) * nx + (b->vz - a->vz) * nz;
            impulse = relative < 0.0 ? -(1.20 * relative) * 0.5 : 0.0;
            if (e->config.world_mode == ODWD_MODE_OPEN_FIELD &&
                e->activity_zone == ODWD_ACTIVITY_BUMPER_ARENA) {
                double mx = (a->x + b->x) * 0.5 - OPEN_BUMPER_X;
                double mz = (a->z + b->z) * 0.5 - OPEN_BUMPER_Z;
                if (mx * mx + mz * mz < 61.0 * 61.0) {
                    bumper_collision = 1;
                    impulse = clampd(impulse * 3.05 + 0.8, 0.0, 27.0);
                }
            } else if (e->config.world_mode == ODWD_MODE_SURVIVAL) {
                impulse = clampd(impulse * 1.35, 0.0, 16.0);
            } else if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL &&
                       i == 0u) {
                if (b->ai_archetype == MUSIC_ENEMY_PUSHER)
                    impulse = clampd(impulse * 2.15 + 1.6, 0.0, 23.0);
                else {
                    double danger = b->ai_archetype == MUSIC_ENEMY_STRIKER ?
                                    1.35 : 0.92;
                    impulse = clampd(impulse * 1.42 + 0.35, 0.0, 19.0);
                    if (relative < -2.0)
                        music_damage_player(e, clampd((-relative) * 0.0048 * danger,
                                                      0.018, 0.18));
                }
            }
            a->vx -= nx * impulse;
            a->vz -= nz * impulse;
            b->vx += nx * impulse;
            b->vz += nz * impulse;
            a->last_collision_impulse = dmax(a->last_collision_impulse, impulse);
            b->last_collision_impulse = dmax(b->last_collision_impulse, impulse);
            if (bumper_collision && impulse > 4.6) {
                double lift = clampd(impulse * 0.50, 3.0, 10.5);
                a->vy = dmax(a->vy, lift * 0.76);
                b->vy = dmax(b->vy, lift);
                a->airborne = 1u;
                b->airborne = 1u;
                a->air_time = 0.0;
                b->air_time = 0.0;
                a->roll_rate -= nx * 0.50;
                b->roll_rate += nx * 0.50;
                a->jump_cooldown = 0.48;
                b->jump_cooldown = 0.48;
                e->event_flags |= ODWD_EVENT_JUMP;
            }
            if (a->collision_cooldown <= 0.0) { ++a->collisions; a->collision_cooldown = 0.25; }
            if (b->collision_cooldown <= 0.0) { ++b->collisions; b->collision_cooldown = 0.25; }
            e->event_flags |= ODWD_EVENT_COLLISION;
        }
    }
}
static void open_ball_step(odwd_engine_internal *e) {
    ball_internal *ball;
    double ground;
    double previous_x;
    double previous_y;
    double previous_z;
    double sensor_x;
    double sensor_y;
    double sensor_z;
    int post_contact = 0;
    uint32_t index;
    if (e->config.world_mode != ODWD_MODE_OPEN_FIELD) return;
    ball = &e->ball;
    if (ball->reset_timer > 0.0) {
        ball->reset_timer -= DT;
        if (ball->reset_timer <= 0.0) ball_reset(e);
        return;
    }
    if (ball->shot_cooldown > 0.0)
        ball->shot_cooldown = dmax(0.0, ball->shot_cooldown - DT);

    previous_x = ball->x;
    previous_y = ball->y;
    previous_z = ball->z;
    ball->vy -= 9.81 * DT;
    ball->x += ball->vx * DT;
    ball->y += ball->vy * DT;
    ball->z += ball->vz * DT;
    {
        double planar_speed = dsqrt(ball->vx * ball->vx + ball->vz * ball->vz);
        if (planar_speed > 0.04) {
            /* Rolling axis is perpendicular to planar travel. The visual spin
             * is part of authoritative ball state so render FPS cannot change
             * it and the football no longer reads like a sliding puck. */
            ball->roll_axis_x = ball->vz / planar_speed;
            ball->roll_axis_z = -ball->vx / planar_speed;
            ball->roll_angle = wrap_angle(ball->roll_angle +
                                          planar_speed / OPEN_BALL_RADIUS * DT);
        }
    }
    ground = open_ground_height(ball->x, ball->z) + OPEN_BALL_RADIUS;
    if (ball->y <= ground) {
        ball->y = ground;
        if (ball->vy < -0.48) ball->vy = -ball->vy * 0.66;
        else ball->vy = 0.0;
        /* Lower rolling resistance makes the football read as a real round
         * ball rather than a heavy puck, while still settling deterministically. */
        ball->vx *= 0.9960;
        ball->vz *= 0.9960;
        if (dabs(ball->vx) + dabs(ball->vz) < 0.014)
            ball->vx = ball->vz = 0.0;
    } else {
        ball->vx *= 0.9992;
        ball->vz *= 0.9992;
    }

    for (index = 0u; index < e->vehicle_count; ++index) {
        vehicle_internal *vehicle = &e->vehicles[index];
        double dx = ball->x - vehicle->x;
        double dz = ball->z - vehicle->z;
        double d2 = dx * dx + dz * dz;
        const double minimum = 2.28;
        double distance;
        double nx;
        double nz;
        double closing;
        double impulse;
        double impulse_nx;
        double impulse_nz;
        if (d2 >= minimum * minimum ||
            dabs(ball->y - vehicle->y) > 2.25) continue;
        distance = dsqrt(d2);
        if (distance < 0.001) {
            nx = dsin(vehicle->yaw);
            nz = dcos(vehicle->yaw);
            distance = 0.001;
        } else {
            nx = dx / distance;
            nz = dz / distance;
        }
        ball->x += nx * (minimum - distance);
        ball->z += nz * (minimum - distance);
        closing = (vehicle->vx - ball->vx) * nx +
                  (vehicle->vz - ball->vz) * nz;
        impulse = clampd(dmax(0.0, closing) * 1.48 +
                         (minimum - distance) * 8.2, 0.0, 36.0);
        impulse_nx = nx;
        impulse_nz = nz;
        ball->vx += impulse_nx * impulse;
        ball->vz += impulse_nz * impulse;
        if (index > 0u && e->activity_zone == ODWD_ACTIVITY_FOOTBALL &&
            impulse > 2.6 && ball->shot_cooldown <= 0.0) {
            uint32_t team = index & 1u;
            double attack = team == 0u ? 1.0 : -1.0;
            if (attack * nx > 0.04) {
                double goal_x = OPEN_FOOTBALL_X +
                                attack * OPEN_GOAL_PLANE_HALF;
                double aim_x = goal_x - ball->x;
                double aim_z = OPEN_FOOTBALL_Z - ball->z;
                double aim_length = dsqrt(aim_x * aim_x + aim_z * aim_z);
                double shot_speed = clampd(21.0 + impulse * 0.48,
                                           22.0, 36.0);
                if (aim_length < 0.001) aim_length = 1.0;
                ball->vx = aim_x / aim_length * shot_speed;
                ball->vz = aim_z / aim_length * shot_speed;
                ball->shot_cooldown = 1.75;
            }
        }
        ball->vy = dmax(ball->vy, clampd(impulse * 0.12, 0.35, 4.2));
        vehicle->vx -= impulse_nx * impulse * 0.085;
        vehicle->vz -= impulse_nz * impulse * 0.085;
        vehicle->last_collision_impulse = dmax(vehicle->last_collision_impulse,
                                               impulse * 0.28);
        e->event_flags |= ODWD_EVENT_BALL_HIT;
    }

    {
        double horizontal_speed = dsqrt(ball->vx * ball->vx +
                                        ball->vz * ball->vz);
        if (horizontal_speed > 42.0) {
            ball->vx *= 42.0 / horizontal_speed;
            ball->vz *= 42.0 / horizontal_speed;
        }
    }

    sensor_x = ball->x;
    sensor_y = ball->y;
    sensor_z = ball->z;

    /* Goal posts are physical before the swept goal sensor is evaluated. */
    {
        uint32_t goal_side;
        for (goal_side = 0u; goal_side < 2u; ++goal_side) {
            double plane_x = OPEN_FOOTBALL_X +
                             (goal_side == 0u ? -OPEN_GOAL_PLANE_HALF :
                                                OPEN_GOAL_PLANE_HALF);
            uint32_t post_side;
            for (post_side = 0u; post_side < 2u; ++post_side) {
                double post_z = OPEN_FOOTBALL_Z +
                                (post_side == 0u ? -OPEN_GOAL_MOUTH_HALF :
                                                   OPEN_GOAL_MOUTH_HALF);
                double dx = ball->x - plane_x;
                double dz = ball->z - post_z;
                double minimum = OPEN_BALL_RADIUS + OPEN_GOAL_POST_RADIUS;
                double d2 = dx * dx + dz * dz;
                if (d2 < minimum * minimum &&
                    ball->y < open_ground_height(plane_x, post_z) +
                              OPEN_GOAL_HEIGHT) {
                    double distance = dsqrt(d2);
                    double nx = distance > 0.001 ? dx / distance :
                                (goal_side == 0u ? 1.0 : -1.0);
                    double nz = distance > 0.001 ? dz / distance : 0.0;
                    double normal_speed = ball->vx * nx + ball->vz * nz;
                    post_contact = 1;
                    if (distance < 0.001) distance = 0.001;
                    ball->x += nx * (minimum - distance);
                    ball->z += nz * (minimum - distance);
                    if (normal_speed < 0.0) {
                        ball->vx -= nx * normal_speed * 1.66;
                        ball->vz -= nz * normal_speed * 1.66;
                    }
                }
            }
        }
    }

    {
        double left_plane = OPEN_FOOTBALL_X - OPEN_GOAL_PLANE_HALF;
        double right_plane = OPEN_FOOTBALL_X + OPEN_GOAL_PLANE_HALF;
        double plane_x = 0.0;
        double t = 0.0;
        uint32_t scoring_side = 0u;
        if (previous_x > left_plane && sensor_x <= left_plane) {
            plane_x = left_plane;
            t = (previous_x - left_plane) / (previous_x - sensor_x);
            scoring_side = 1u;
        } else if (previous_x < right_plane && sensor_x >= right_plane) {
            plane_x = right_plane;
            t = (right_plane - previous_x) / (sensor_x - previous_x);
            scoring_side = 2u;
        }
        if (scoring_side != 0u && !post_contact) {
            double cross_z = previous_z + (sensor_z - previous_z) * t;
            double cross_y = previous_y + (sensor_y - previous_y) * t;
            double goal_ground = open_ground_height(plane_x,
                                                     OPEN_FOOTBALL_Z);
            if (dabs(cross_z - OPEN_FOOTBALL_Z) <=
                    OPEN_GOAL_MOUTH_HALF - OPEN_BALL_RADIUS -
                    OPEN_GOAL_POST_RADIUS &&
                cross_y <= goal_ground + OPEN_GOAL_HEIGHT -
                           OPEN_BALL_RADIUS) {
                if (scoring_side == 1u) ++e->football_score_right;
                else ++e->football_score_left;
                ball->reset_timer = 1.15;
                e->event_flags |= ODWD_EVENT_GOAL;
            } else ball->reset_timer = 0.65;
        }
    }
    if (ball->reset_timer <= 0.0 &&
        dabs(previous_z - OPEN_FOOTBALL_Z) <
            OPEN_FOOTBALL_TOUCHLINE_HALF &&
        dabs(sensor_z - OPEN_FOOTBALL_Z) >=
            OPEN_FOOTBALL_TOUCHLINE_HALF)
        ball->reset_timer = 0.65;
    if (ball->reset_timer <= 0.0 &&
        (dabs(ball->x - OPEN_FOOTBALL_X) >
             OPEN_GOAL_PLANE_HALF + 10.0 ||
         dabs(ball->z - OPEN_FOOTBALL_Z) > 42.0))
        ball->reset_timer = 0.65;
    if ((ball->x - OPEN_FOOTBALL_X) * (ball->x - OPEN_FOOTBALL_X) +
        (ball->z - OPEN_FOOTBALL_Z) * (ball->z - OPEN_FOOTBALL_Z) >
        145.0 * 145.0)
        ball->reset_timer = 0.65;
}

static void refresh_vehicle_projections(odwd_engine_internal *e) {
    uint32_t index;
    if (e->config.world_mode == ODWD_MODE_OPEN_FIELD ||
        e->config.world_mode == ODWD_MODE_SURVIVAL ||
        e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) return;
    for (index = 0u; index < e->vehicle_count; ++index) {
        vehicle_internal *v = &e->vehicles[index];
        projection p = road_project(e, v->x, v->y, v->z,
                                    v->road_segment, v->route_choice);
        v->road_segment = p.segment;
        v->route_choice = p.alternate;
        v->progress = p.s;
        v->road_lateral = p.lateral;
    }
}

static void update_checkpoints_and_recovery(odwd_engine_internal *e,
                                            const odwd_input *in) {
    uint32_t i;
    if (e->config.world_mode == ODWD_MODE_SURVIVAL) return;
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) {
        if (in->buttons & ODWD_BUTTON_RESPAWN)
            music_recover_vehicle(e, &e->vehicles[0], 1);
        return;
    }
    for (i = 0; i < e->vehicle_count; ++i) {
        vehicle_internal *v = &e->vehicles[i];
        const vehicle_internal *player = &e->vehicles[0];
        int64_t checkpoint_index = floor_i64_nonnegative(v->progress /
                                   e->config.checkpoint_spacing_m);
        double reached = (double)checkpoint_index * e->config.checkpoint_spacing_m;
        if (reached > v->checkpoint + 1.0) {
            v->checkpoint = reached;
            if (e->config.world_mode == ODWD_MODE_OPEN_FIELD) {
                v->checkpoint_x = v->x;
                v->checkpoint_z = v->z;
                v->checkpoint_yaw = v->yaw;
            }
            if (v->is_player) {
                e->last_checkpoint = reached;
                e->event_flags |= ODWD_EVENT_CHECKPOINT;
            }
        }
        if ((v->is_player && (in->buttons & ODWD_BUTTON_RESPAWN)) ||
            v->offroad_time > (v->is_player ? 3.2 : 5.5) ||
            v->stuck_time > (v->is_player ? 5.0 : 7.5)) {
            vehicle_respawn(e, v, v->is_player != 0u);
        } else if (e->config.world_mode == ODWD_MODE_ENDLESS &&
                   !v->is_player && player->progress - v->progress > 620.0) {
            /* A racer outside the streamed rear window is recycled behind the
             * player, where the camera cannot observe the transition. */
            double target = dmax(player->progress - 150.0 - (double)i * 9.0,
                                 player->checkpoint + 8.0);
            v->checkpoint = (double)floor_i64_nonnegative(
                target / e->config.checkpoint_spacing_m) *
                e->config.checkpoint_spacing_m;
            vehicle_place_at(e, v, target, v->ai_lane, 16.0);
            ++v->respawns;
        } else if (e->config.world_mode == ODWD_MODE_ENDLESS &&
                   !v->is_player && v->progress - player->progress > 940.0) {
            /* Likewise keep a runaway leader inside the bounded forward
             * simulation window without moving a visible nearby rival. */
            double target = player->progress + 280.0 + (double)i * 11.0;
            v->checkpoint = (double)floor_i64_nonnegative(
                target / e->config.checkpoint_spacing_m) *
                e->config.checkpoint_spacing_m;
            vehicle_place_at(e, v, target, v->ai_lane, 13.0);
            ++v->respawns;
        }
    }
}

static void update_drift_mastery(odwd_engine_internal *e) {
    vehicle_internal *player = &e->vehicles[0];
    double speed = vehicle_speed(player);
    double active = player->drift;
    if (e->drift_chain < 1.0) e->drift_chain = 1.0;
    if (!player->airborne && active > 0.075 && speed > 7.0) {
        double turbo_gain;
        /* Reward sustained angle + speed, but keep score deterministic and
         * frame-rate independent by integrating only on the 120 Hz spine. */
        e->drift_score += active * speed *
            (0.40 + e->drift_chain * 0.095);
        e->drift_chain = clampd(e->drift_chain + active * 0.0038, 1.0, 5.0);

        /* Mastery has a gameplay payoff: a clean controlled slide regenerates
         * boost.  The gain is continuous (no hidden combo threshold), scales
         * with real slip + speed, and is bounded so holding DRIFT in a slow
         * circle can never become an infinite turbo exploit. */
        turbo_gain = active *
            (0.025 + clampd(speed, 0.0, 48.0) * 0.0014) *
            (0.75 + e->drift_chain * 0.12);
        player->turbo = clampd(player->turbo + turbo_gain * DT, 0.0, 1.0);
        e->drift_idle = 0.0;
    } else {
        e->drift_idle += DT;
        if (e->drift_idle > 0.58)
            e->drift_chain = clampd(e->drift_chain - DT * 0.54, 1.0, 5.0);
    }
    /* A heavy hit costs combo mastery without deleting accumulated score. */
    if ((e->event_flags & ODWD_EVENT_COLLISION) &&
        player->last_collision_impulse > 4.0) {
        e->drift_chain = dmax(1.0, e->drift_chain * 0.72);
        e->drift_idle = dmax(e->drift_idle, 0.58);
    }
}

static void update_places(odwd_engine_internal *e) {
    uint32_t i, j;
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) {
        for (i = 0u; i < e->vehicle_count; ++i)
            e->vehicles[i].place = i == 0u ? 1u : 0u;
        return;
    }
    if (e->config.world_mode == ODWD_MODE_SURVIVAL) {
        for (i = 0u; i < e->vehicle_count; ++i) {
            if (e->survival.eliminated[i])
                e->vehicles[i].place = e->survival.elimination_place[i];
            else if (e->survival.finished)
                e->vehicles[i].place = 1u;
            else
                e->vehicles[i].place = 0u;
        }
        return;
    }
    for (i = 0; i < e->vehicle_count; ++i) {
        uint32_t place = 1u;
        for (j = 0; j < e->vehicle_count; ++j)
            if (e->vehicles[j].progress > e->vehicles[i].progress + 0.01) ++place;
        e->vehicles[i].place = place;
    }
}

static void camera_update(odwd_engine_internal *e) {
    camera_internal *c = &e->camera;
    vehicle_internal *v = &e->vehicles[0];
    double speed = vehicle_speed(v);
    double fwd_x = dsin(v->yaw), fwd_z = dcos(v->yaw);
    double look_ahead = v->airborne ? 0.17 : 0.105;
    double desired_focus_x = v->x + v->vx * look_ahead + fwd_x * 0.72;
    double desired_focus_y = v->y + 0.84 + e->music_beat * 0.20 +
                             e->music_bass * 0.24 + e->music_pulse * 0.14;
    double desired_focus_z = v->z + v->vz * look_ahead + fwd_z * 0.72;
    double desired_yaw = wrap_angle(v->yaw +
        clampd(v->slip_angle * 0.18, -0.13, 0.13));
    double desired_distance = 7.1 + speed * 0.055;
    double desired_roll = clampd(-v->lateral_speed * 0.008 - v->yaw_rate * 0.038 +
                                 dsin((double)e->tick * DT * 4.7) *
                                 e->music_high * 0.018, -0.12, 0.12);
    double desired_fov;
    double base_pitch = 0.30;
    double yaw_frequency = 7.6;
    double horizontal, sx, cx;
    double focus_error_x, focus_error_y, focus_error_z, focus_error;
    double max_focus_lag;

    if (!v->airborne && speed < 0.55)
        c->showcase_idle += DT;
    else
        c->showcase_idle = dmax(0.0, c->showcase_idle - DT * 4.5);

    if (e->config.world_mode == ODWD_MODE_SURVIVAL &&
        e->survival.eliminated[0]) {
        /* Do not chase the falling body vertically: the arena remains a
         * stable visual reference, making elimination read as an actual fall. */
        desired_focus_y = SURVIVAL_ARENA_Y + 1.0;
        desired_distance = 10.8;
        base_pitch = 0.34;
    }

    switch (c->mode) {
        case ODWD_CAMERA_CINEMATIC:
            desired_yaw = wrap_angle(v->yaw -
                clampd(v->lateral_speed * 0.012 + v->yaw_rate * 0.10,
                       -0.30, 0.30));
            desired_distance = 9.5 + speed * 0.068 + v->drift * 1.05;
            base_pitch = 0.27;
            desired_focus_y += 0.22;
            yaw_frequency = 6.2;
            break;
        case ODWD_CAMERA_LOW_ACTION:
            desired_yaw = v->yaw;
            desired_distance = 5.65 + speed * 0.033;
            base_pitch = 0.145;
            desired_focus_y -= 0.16;
            yaw_frequency = 8.6;
            break;
        case ODWD_CAMERA_ORBIT:
            desired_yaw = wrap_angle(v->yaw + 0.72 +
                         dsin((double)e->tick * DT * 0.22) * 0.22);
            desired_distance = 10.8 + speed * 0.047;
            base_pitch = 0.37;
            desired_focus_y += 0.30;
            yaw_frequency = 3.6;
            break;
        case ODWD_CAMERA_LEFT_RIG:
            desired_yaw = wrap_angle(v->yaw - 0.50);
            desired_distance = 8.2 + speed * 0.046;
            base_pitch = 0.25;
            desired_focus_x += fwd_z * 0.30;
            desired_focus_z -= fwd_x * 0.30;
            yaw_frequency = 7.0;
            break;
        case ODWD_CAMERA_RIGHT_RIG:
            desired_yaw = wrap_angle(v->yaw + 0.50);
            desired_distance = 8.2 + speed * 0.046;
            base_pitch = 0.25;
            desired_focus_x -= fwd_z * 0.30;
            desired_focus_z += fwd_x * 0.30;
            yaw_frequency = 7.0;
            break;
        case ODWD_CAMERA_ROOF:
            /* Camera 7 in the 1-based UI used to be a 56-degree pseudo-top
             * shot (pitch ~= 0.98), which explains the reported "looks at the
             * ground" failure. Keep the ABI name for compatibility, but make
             * the rig a close professional roof-follow: low elevation, strong
             * forward framing and fast yaw tracking. */
            desired_yaw = wrap_angle(v->yaw +
                         clampd(v->slip_angle * 0.08, -0.055, 0.055));
            desired_distance = 4.75 + speed * 0.020;
            base_pitch = 0.34;
            desired_focus_x += fwd_x * 2.45;
            desired_focus_z += fwd_z * 2.45;
            desired_focus_y += 0.18;
            desired_roll *= 0.42;
            yaw_frequency = 9.6;
            break;
        case ODWD_CAMERA_DIRECTOR: {
            uint32_t shot = (uint32_t)((e->tick / (ODWD_TICK_HZ * 4u)) % 3u);
            double phase = (double)e->tick * DT;
            if (shot == 0u) {
                desired_yaw = wrap_angle(v->yaw + 0.34 + dsin(phase * 0.31) * 0.10);
                desired_distance = 8.3 + speed * 0.052;
                base_pitch = 0.36;
            } else if (shot == 1u) {
                desired_yaw = wrap_angle(v->yaw - 0.92 + dsin(phase * 0.27) * 0.12);
                desired_distance = 10.2 + speed * 0.044;
                base_pitch = 0.31;
            } else {
                desired_yaw = wrap_angle(v->yaw + 0.16);
                desired_distance = 11.4 + speed * 0.058;
                base_pitch = 0.48;
                desired_focus_x += fwd_x * 1.4;
                desired_focus_z += fwd_z * 1.4;
            }
            desired_focus_y += 0.55;
            yaw_frequency = 5.4;
            break;
        }
        case ODWD_CAMERA_CHASE:
        default:
            break;
    }

    /* After a short genuine rest, the core becomes a showcase camera.  It is
     * cancelled immediately by movement or direct look input, and therefore
     * never steals control during driving. */
    if (c->showcase_idle > 2.35 && c->manual_idle > 0.70 && !v->airborne) {
        double t = c->showcase_idle - 2.35;
        uint32_t shot = (uint32_t)(t / 4.6) % 4u;
        double phase = t * (0.18 + (double)shot * 0.018);
        if (shot == 0u) {
            desired_yaw = wrap_angle(v->yaw + 0.75 + phase);
            desired_distance = 8.6;
            base_pitch = 0.29;
        } else if (shot == 1u) {
            desired_yaw = wrap_angle(v->yaw + PI - 0.56 + phase * 0.34);
            desired_distance = 10.4;
            base_pitch = 0.33;
        } else if (shot == 2u) {
            desired_yaw = wrap_angle(v->yaw - 1.08 - phase * 0.42);
            desired_distance = 7.4;
            base_pitch = 0.20;
        } else {
            desired_yaw = wrap_angle(v->yaw + 0.18 + dsin(phase) * 0.24);
            desired_distance = 11.8;
            base_pitch = 0.50;
        }
        desired_focus_x = v->x + fwd_x * 0.38;
        desired_focus_y = v->y + 1.02;
        desired_focus_z = v->z + fwd_z * 0.38;
        desired_roll *= 0.22;
        yaw_frequency = 3.2;
    }

    /* Airborne framing deliberately pulls back as height/vertical velocity
     * grows, preserving both the car and its landing zone. Landing then returns
     * through the same damped rig instead of snapping. */
    if (v->airborne && c->mode != ODWD_CAMERA_ROOF) {
        desired_distance += clampd(2.1 + v->air_time * 1.65 +
                                   dabs(v->vy) * 0.20, 2.1, 6.4);
        base_pitch += 0.08;
        desired_focus_y += clampd(v->vy * 0.045, -0.35, 0.55);
        yaw_frequency = dmax(yaw_frequency, 5.2);
    }
    desired_distance += e->music_energy * 0.22 - e->music_pulse * 0.92 -
                        e->music_bass * 0.18;
    if (v->turbo_active) desired_distance -= 0.50;
    desired_distance = clampd(desired_distance, 4.6, v->airborne ? 19.0 : 14.8);
    desired_fov = clampd(0.91 + speed * 0.0048 + v->drift * 0.025 +
                         e->music_pulse * 0.085 + e->music_high * 0.050 +
                         e->music_flux * 0.035 +
                         (v->turbo_active ? 0.105 : 0.0),
                         0.86, 1.38);
    if (e->config.world_mode == ODWD_MODE_OPEN_FIELD)
        desired_distance += c->mode == ODWD_CAMERA_LOW_ACTION ? 0.15 : 0.70;
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL &&
        e->music_survival.hazard_type == MUSIC_HAZARD_QUAKE &&
        e->music_survival.hazard_phase == MUSIC_EVENT_ACTIVE) {
        double quake = (0.020 + (double)e->music_survival.hazard_level * 0.010) *
                       (0.65 + e->music_bass * 0.75);
        desired_roll += dsin(e->music_survival.phase_elapsed * 13.0) * quake;
        desired_focus_y += dabs(dsin(e->music_survival.phase_elapsed * 16.0)) *
                           quake * 2.0;
    }

    if (c->manual_idle > 0.34)
        c->pitch_target += (base_pitch - c->pitch_target) *
                           clampd(DT * 5.8, 0.0, 1.0);
    /* Normal re-acquire begins quickly. Even during manual hold an emergency
     * framing assist activates only if the car would end up far behind the
     * camera, preventing a complete loss of the vehicle. */
    if (c->manual_idle > 0.22) {
        spring_angle(&c->view_yaw, &c->view_yaw_velocity, desired_yaw,
                     yaw_frequency, DT);
    }
    spring(&c->pitch, &c->pitch_velocity, c->pitch_target, 10.6, DT);
    spring(&c->distance, &c->distance_velocity, desired_distance, 7.1, DT);
    spring(&c->roll, &c->roll_velocity, desired_roll, 10.0, DT);
    spring(&c->fov, &c->fov_velocity, desired_fov, 6.7, DT);
    spring(&c->focus_x, &c->focus_vx, desired_focus_x, 12.6, DT);
    spring(&c->focus_y, &c->focus_vy, desired_focus_y, 11.8, DT);
    spring(&c->focus_z, &c->focus_vz, desired_focus_z, 12.6, DT);

    /* Bounded spring-arm lag: keep cinematic damping but never allow focus to
     * trail arbitrarily far behind a fast car. */
    focus_error_x = c->focus_x - desired_focus_x;
    focus_error_y = c->focus_y - desired_focus_y;
    focus_error_z = c->focus_z - desired_focus_z;
    focus_error = dsqrt(focus_error_x * focus_error_x +
                        focus_error_y * focus_error_y +
                        focus_error_z * focus_error_z);
    max_focus_lag = clampd(2.7 + speed * 0.032, 2.7, 5.0);
    if (focus_error > max_focus_lag && focus_error > 0.001) {
        double k = max_focus_lag / focus_error;
        c->focus_x = desired_focus_x + focus_error_x * k;
        c->focus_y = desired_focus_y + focus_error_y * k;
        c->focus_z = desired_focus_z + focus_error_z * k;
        c->focus_vx *= 0.72;
        c->focus_vy *= 0.72;
        c->focus_vz *= 0.72;
    }

    c->collision_impulse = dmax(c->collision_impulse * 0.90,
                                v->last_collision_impulse * 0.060);
    horizontal = c->distance * dcos(c->pitch);
    sx = dsin(c->view_yaw);
    cx = dcos(c->view_yaw);
    c->target_x = c->focus_x;
    c->target_y = c->focus_y;
    c->target_z = c->focus_z;
    c->x = c->focus_x - sx * horizontal + c->collision_impulse *
           dsin((double)e->tick * DT * 17.3);
    c->z = c->focus_z - cx * horizontal + c->collision_impulse *
           dcos((double)e->tick * DT * 13.1);
    c->y = c->focus_y + c->distance * dsin(c->pitch) +
           c->collision_impulse * 0.35 *
           dsin((double)e->tick * DT * 21.1);
}
static uint64_t fnv_u64(uint64_t h, uint64_t v) {
    unsigned i;
    for (i = 0; i < 8u; ++i) {
        h ^= (v >> (i * 8u)) & UINT64_C(0xff);
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static uint64_t double_bits(double d) {
    uint64_t u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

static uint64_t state_hash_internal(const odwd_engine_internal *e) {
    uint64_t h = UINT64_C(1469598103934665603);
    uint32_t i;
    h = fnv_u64(h, e->tick);
    h = fnv_u64(h, (uint64_t)e->road_global_first);
    h = fnv_u64(h, (uint64_t)e->vehicle_count);
    h = fnv_u64(h, (uint64_t)e->config.world_mode);
    h = fnv_u64(h, (uint64_t)e->config.seed);
    h = fnv_u64(h, (uint64_t)e->config.rival_count);
    h = fnv_u64(h, double_bits(e->config.section_length_m));
    h = fnv_u64(h, double_bits(e->config.checkpoint_spacing_m));
    h = fnv_u64(h, (uint64_t)e->section_index);
    h = fnv_u64(h, (uint64_t)e->player_jump_down);
    h = fnv_u64(h, (uint64_t)e->event_flags);
    for (i = 0; i < e->vehicle_count; ++i) {
        const vehicle_internal *v = &e->vehicles[i];
        h = fnv_u64(h, double_bits(v->x));
        h = fnv_u64(h, double_bits(v->y));
        h = fnv_u64(h, double_bits(v->z));
        h = fnv_u64(h, double_bits(v->vx));
        h = fnv_u64(h, double_bits(v->vy));
        h = fnv_u64(h, double_bits(v->vz));
        h = fnv_u64(h, double_bits(v->yaw));
        h = fnv_u64(h, double_bits(v->yaw_rate));
        h = fnv_u64(h, double_bits(v->steer));
        h = fnv_u64(h, double_bits(v->longitudinal));
        h = fnv_u64(h, double_bits(v->lateral_speed));
        h = fnv_u64(h, double_bits(v->slip_angle));
        h = fnv_u64(h, double_bits(v->drift));
        h = fnv_u64(h, double_bits(v->progress));
        h = fnv_u64(h, double_bits(v->road_lateral));
        h = fnv_u64(h, double_bits(v->checkpoint));
        h = fnv_u64(h, double_bits(v->checkpoint_x));
        h = fnv_u64(h, double_bits(v->checkpoint_z));
        h = fnv_u64(h, double_bits(v->checkpoint_yaw));
        h = fnv_u64(h, double_bits(v->offroad_time));
        h = fnv_u64(h, double_bits(v->stuck_time));
        h = fnv_u64(h, double_bits(v->collision_cooldown));
        h = fnv_u64(h, double_bits(v->last_collision_impulse));
        h = fnv_u64(h, double_bits(v->turbo));
        h = fnv_u64(h, double_bits(v->air_time));
        h = fnv_u64(h, double_bits(v->traveled_distance));
        h = fnv_u64(h, double_bits(v->previous_x));
        h = fnv_u64(h, double_bits(v->previous_z));
        h = fnv_u64(h, double_bits(v->body_pitch));
        h = fnv_u64(h, double_bits(v->body_roll));
        h = fnv_u64(h, double_bits(v->pitch_rate));
        h = fnv_u64(h, double_bits(v->roll_rate));
        h = fnv_u64(h, double_bits(v->jump_cooldown));
        h = fnv_u64(h, double_bits(v->ai_lane));
        h = fnv_u64(h, double_bits(v->ai_skill));
        h = fnv_u64(h, double_bits(v->ai_timer));
        h = fnv_u64(h, double_bits(v->ai_target_x));
        h = fnv_u64(h, double_bits(v->ai_target_z));
        h = fnv_u64(h, (uint64_t)v->ai_archetype);
        h = fnv_u64(h, (uint64_t)v->ai_state);
        h = fnv_u64(h, (uint64_t)v->respawns);
        h = fnv_u64(h, (uint64_t)v->collisions);
        h = fnv_u64(h, (uint64_t)v->place);
        h = fnv_u64(h, (uint64_t)v->is_player);
        h = fnv_u64(h, (uint64_t)v->road_segment);
        h = fnv_u64(h, (uint64_t)v->route_choice);
        h = fnv_u64(h, (uint64_t)v->headlights);
        h = fnv_u64(h, (uint64_t)v->turbo_active);
        h = fnv_u64(h, (uint64_t)v->airborne);
    }
    h = fnv_u64(h, double_bits(e->camera.view_yaw));
    h = fnv_u64(h, double_bits(e->camera.x));
    h = fnv_u64(h, double_bits(e->camera.y));
    h = fnv_u64(h, double_bits(e->camera.z));
    h = fnv_u64(h, double_bits(e->camera.target_x));
    h = fnv_u64(h, double_bits(e->camera.target_y));
    h = fnv_u64(h, double_bits(e->camera.target_z));
    h = fnv_u64(h, double_bits(e->camera.focus_x));
    h = fnv_u64(h, double_bits(e->camera.focus_y));
    h = fnv_u64(h, double_bits(e->camera.focus_z));
    h = fnv_u64(h, double_bits(e->camera.focus_vx));
    h = fnv_u64(h, double_bits(e->camera.focus_vy));
    h = fnv_u64(h, double_bits(e->camera.focus_vz));
    h = fnv_u64(h, double_bits(e->camera.view_yaw_velocity));
    h = fnv_u64(h, double_bits(e->camera.pitch));
    h = fnv_u64(h, double_bits(e->camera.pitch_target));
    h = fnv_u64(h, double_bits(e->camera.pitch_velocity));
    h = fnv_u64(h, double_bits(e->camera.roll));
    h = fnv_u64(h, double_bits(e->camera.roll_velocity));
    h = fnv_u64(h, double_bits(e->camera.distance));
    h = fnv_u64(h, double_bits(e->camera.distance_velocity));
    h = fnv_u64(h, double_bits(e->camera.fov));
    h = fnv_u64(h, double_bits(e->camera.fov_velocity));
    h = fnv_u64(h, double_bits(e->camera.manual_idle));
    h = fnv_u64(h, double_bits(e->camera.showcase_idle));
    h = fnv_u64(h, double_bits(e->camera.collision_impulse));
    h = fnv_u64(h, (uint64_t)e->camera.mode);
    h = fnv_u64(h, (uint64_t)e->activity_zone);
    h = fnv_u64(h, (uint64_t)e->football_score_left);
    h = fnv_u64(h, (uint64_t)e->football_score_right);
    h = fnv_u64(h, double_bits(e->ball.x));
    h = fnv_u64(h, double_bits(e->ball.y));
    h = fnv_u64(h, double_bits(e->ball.z));
    h = fnv_u64(h, double_bits(e->ball.vx));
    h = fnv_u64(h, double_bits(e->ball.vy));
    h = fnv_u64(h, double_bits(e->ball.vz));
    h = fnv_u64(h, double_bits(e->ball.roll_angle));
    h = fnv_u64(h, double_bits(e->ball.roll_axis_x));
    h = fnv_u64(h, double_bits(e->ball.roll_axis_z));
    h = fnv_u64(h, double_bits(e->ball.reset_timer));
    h = fnv_u64(h, double_bits(e->ball.shot_cooldown));
    h = fnv_u64(h, double_bits(e->music_energy));
    h = fnv_u64(h, double_bits(e->music_bass));
    h = fnv_u64(h, double_bits(e->music_lowpass));
    h = fnv_u64(h, double_bits(e->music_mid_lowpass));
    h = fnv_u64(h, double_bits(e->music_mid));
    h = fnv_u64(h, double_bits(e->music_high));
    h = fnv_u64(h, double_bits(e->music_flux));
    h = fnv_u64(h, double_bits(e->music_pulse));
    h = fnv_u64(h, double_bits(e->music_prev_abs));
    h = fnv_u64(h, double_bits(e->music_onset_age));
    h = fnv_u64(h, double_bits(e->music_beat_interval));
    h = fnv_u64(h, double_bits(e->music_beat_phase));
    h = fnv_u64(h, double_bits(e->music_envelope));
    h = fnv_u64(h, double_bits(e->music_beat));
    h = fnv_u64(h, double_bits(e->music_playback_time));
    h = fnv_u64(h, double_bits(e->drift_score));
    h = fnv_u64(h, double_bits(e->drift_chain));
    h = fnv_u64(h, double_bits(e->drift_idle));
    h = fnv_u64(h, double_bits(e->last_checkpoint));
    h = fnv_u64(h, (uint64_t)e->music_beat_pending);
    h = fnv_u64(h, double_bits(e->music_survival.half_w));
    h = fnv_u64(h, double_bits(e->music_survival.half_d));
    h = fnv_u64(h, double_bits(e->music_survival.health));
    h = fnv_u64(h, double_bits(e->music_survival.score));
    h = fnv_u64(h, double_bits(e->music_survival.elapsed));
    h = fnv_u64(h, double_bits(e->music_survival.phase_elapsed));
    h = fnv_u64(h, double_bits(e->music_survival.phase_duration));
    h = fnv_u64(h, (uint64_t)e->music_survival.event_index);
    h = fnv_u64(h, (uint64_t)e->music_survival.map_variant);
    h = fnv_u64(h, (uint64_t)e->music_survival.hazard_type);
    h = fnv_u64(h, (uint64_t)e->music_survival.hazard_phase);
    h = fnv_u64(h, (uint64_t)e->music_survival.hazard_level);
    h = fnv_u64(h, (uint64_t)e->music_survival.hazard_count);
    h = fnv_u64(h, (uint64_t)e->music_survival.safe_count);
    h = fnv_u64(h, (uint64_t)e->music_survival.enemy_pressure);
    h = fnv_u64(h, (uint64_t)e->music_survival.finished);
    for (i = 0u; i < e->music_survival.hazard_count; ++i) {
        const music_hazard_zone *mh = &e->music_survival.hazards[i];
        h = fnv_u64(h, double_bits(mh->x));
        h = fnv_u64(h, double_bits(mh->z));
        h = fnv_u64(h, double_bits(mh->radius));
        h = fnv_u64(h, double_bits(mh->strength));
        h = fnv_u64(h, (uint64_t)mh->variant);
    }
    for (i = 0u; i < e->music_survival.safe_count; ++i) {
        const music_safe_zone *ms = &e->music_survival.safe[i];
        h = fnv_u64(h, double_bits(ms->x));
        h = fnv_u64(h, double_bits(ms->z));
        h = fnv_u64(h, double_bits(ms->radius));
        h = fnv_u64(h, double_bits(ms->height));
        h = fnv_u64(h, (uint64_t)ms->level);
        h = fnv_u64(h, (uint64_t)ms->shelter);
    }
    h = fnv_u64(h, double_bits(e->survival.envelope.half_length));
    h = fnv_u64(h, double_bits(e->survival.envelope.half_width));
    h = fnv_u64(h, double_bits(e->survival.envelope.half_height));
    h = fnv_u64(h, double_bits(e->survival.envelope.max_useful_speed));
    h = fnv_u64(h, double_bits(e->survival.envelope.acceleration));
    h = fnv_u64(h, double_bits(e->survival.envelope.braking));
    h = fnv_u64(h, double_bits(e->survival.envelope.lateral_capacity));
    h = fnv_u64(h, double_bits(e->survival.envelope.turn_radius));
    h = fnv_u64(h, double_bits(e->survival.envelope.jump_impulse));
    h = fnv_u64(h, double_bits(e->survival.envelope.jump_distance));
    h = fnv_u64(h, (uint64_t)e->survival.sector_index);
    h = fnv_u64(h, (uint64_t)e->survival.family);
    h = fnv_u64(h, (uint64_t)e->survival.history_count);
    h = fnv_u64(h, (uint64_t)e->survival.alive_count);
    h = fnv_u64(h, (uint64_t)e->survival.player_final_place);
    h = fnv_u64(h, (uint64_t)e->survival.finished);
    h = fnv_u64(h, (uint64_t)e->survival.solution_count);
    h = fnv_u64(h, double_bits(e->survival.sector_elapsed));
    h = fnv_u64(h, double_bits(e->survival.sector_duration));
    h = fnv_u64(h, double_bits(e->survival.warning_time));
    h = fnv_u64(h, double_bits(e->survival.difficulty));
    h = fnv_u64(h, double_bits(e->survival.obstacle_speed));
    h = fnv_u64(h, (uint64_t)e->survival.requires_jump);
    for (i = 0u; i < SURVIVAL_HISTORY; ++i)
        h = fnv_u64(h, (uint64_t)e->survival.history[i]);
    for (i = 0u; i < ODWD_MAX_VEHICLES; ++i) {
        h = fnv_u64(h, e->survival.elimination_tick[i]);
        h = fnv_u64(h, (uint64_t)e->survival.eliminated[i]);
        h = fnv_u64(h, (uint64_t)e->survival.elimination_place[i]);
    }
    for (i = 0u; i < e->survival.solution_count; ++i)
        h = fnv_u64(h, double_bits(e->survival.solution_x[i]));
    for (i = 0u; i < e->survival.obstacle_count; ++i) {
        const survival_obstacle *o = &e->survival.obstacles[i];
        h = fnv_u64(h, (uint64_t)o->type);
        h = fnv_u64(h, (uint64_t)o->variant);
        h = fnv_u64(h, (uint64_t)o->active);
        h = fnv_u64(h, double_bits(o->x));
        h = fnv_u64(h, double_bits(o->y));
        h = fnv_u64(h, double_bits(o->z));
        h = fnv_u64(h, double_bits(o->half_x));
        h = fnv_u64(h, double_bits(o->half_y));
        h = fnv_u64(h, double_bits(o->half_z));
        h = fnv_u64(h, double_bits(o->yaw));
        h = fnv_u64(h, double_bits(o->vx));
        h = fnv_u64(h, double_bits(o->vz));
    }
    for (i = 0u; i < e->survival.platform_count; ++i) {
        const survival_platform *p = &e->survival.platforms[i];
        h = fnv_u64(h, (uint64_t)p->variant);
        h = fnv_u64(h, (uint64_t)p->active);
        h = fnv_u64(h, double_bits(p->x));
        h = fnv_u64(h, double_bits(p->y));
        h = fnv_u64(h, double_bits(p->z));
        h = fnv_u64(h, double_bits(p->half_x));
        h = fnv_u64(h, double_bits(p->half_z));
        h = fnv_u64(h, double_bits(p->vx));
        h = fnv_u64(h, double_bits(p->vz));
    }
    h = fnv_u64(h, (uint64_t)(uint32_t)e->prop_cell_x);
    h = fnv_u64(h, (uint64_t)(uint32_t)e->prop_cell_z);
    h = fnv_u64(h, (uint64_t)e->prop_count);
    for (i = 0u; i < ODWD_MAX_TURBO_PICKUPS; ++i) {
        h = fnv_u64(h, double_bits(e->pickups[i].progress));
        h = fnv_u64(h, double_bits(e->pickups[i].x));
        h = fnv_u64(h, double_bits(e->pickups[i].y));
        h = fnv_u64(h, double_bits(e->pickups[i].z));
        h = fnv_u64(h, double_bits(e->pickups[i].amount));
        h = fnv_u64(h, (uint64_t)e->pickups[i].id);
        h = fnv_u64(h, (uint64_t)e->pickups[i].generation);
        h = fnv_u64(h, (uint64_t)e->pickups[i].active);
    }
    return h;
}

static const odwd_engine_internal *checked_const(const void *storage) {
    const odwd_engine_internal *e = (const odwd_engine_internal *)storage;
    if (!storage || e->magic != ODWD_MAGIC) return NULL;
    return e;
}

static odwd_engine_internal *checked(void *storage) {
    return (odwd_engine_internal *)(uintptr_t)checked_const(storage);
}

void odwd_config_defaults(odwd_config *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = (uint32_t)sizeof(*config);
    config->abi_version = ODWD_ABI_VERSION;
    config->seed = UINT32_C(0x0d9a0321);
    config->rival_count = 5u;
    config->section_length_m = 1800.0;
    config->checkpoint_spacing_m = 360.0;
    config->world_mode = ODWD_MODE_ENDLESS;
}

void odwd_input_neutral(odwd_input *input) {
    if (!input) return;
    memset(input, 0, sizeof(*input));
    input->struct_size = (uint32_t)sizeof(*input);
}

size_t odwd_engine_required_bytes(void) { return sizeof(odwd_engine_internal); }

uint32_t odwd_engine_required_alignment(void) {
    return (uint32_t)_Alignof(odwd_engine_internal);
}

int odwd_engine_init(void *storage, size_t storage_bytes,
                     const odwd_config *config) {
    odwd_engine_internal *e;
    odwd_config defaults;
    if (!storage) return ODWD_E_ARGUMENT;
    if (storage_bytes < sizeof(odwd_engine_internal)) return ODWD_E_STORAGE;
    if (((uintptr_t)storage % _Alignof(odwd_engine_internal)) != 0u)
        return ODWD_E_STORAGE;
    odwd_config_defaults(&defaults);
    if (config) {
        if (config->struct_size != sizeof(*config)) return ODWD_E_ARGUMENT;
        if (config->abi_version != ODWD_ABI_VERSION) return ODWD_E_ABI;
        defaults = *config;
    }
    if (defaults.rival_count >= ODWD_MAX_VEHICLES) return ODWD_E_ARGUMENT;
    if (defaults.world_mode != ODWD_MODE_ENDLESS &&
        defaults.world_mode != ODWD_MODE_OPEN_FIELD &&
        defaults.world_mode != ODWD_MODE_SURVIVAL &&
        defaults.world_mode != ODWD_MODE_MUSIC_SURVIVAL) return ODWD_E_ARGUMENT;
    if (!(defaults.section_length_m >= 300.0 && defaults.section_length_m <= 100000.0))
        return ODWD_E_ARGUMENT;
    if (!(defaults.checkpoint_spacing_m >= 120.0 && defaults.checkpoint_spacing_m <= 700.0))
        return ODWD_E_ARGUMENT;
    memset(storage, 0, sizeof(odwd_engine_internal));
    e = (odwd_engine_internal *)storage;
    e->config = defaults;
    e->vehicle_count = defaults.rival_count + 1u;
    e->drift_chain = 1.0;
    road_initialize(e);
    vehicles_initialize(e);
    if (defaults.world_mode == ODWD_MODE_SURVIVAL)
        survival_initialize(e);
    if (defaults.world_mode == ODWD_MODE_MUSIC_SURVIVAL)
        music_survival_initialize(e);
    e->prop_cell_x = INT32_MIN;
    e->prop_cell_z = INT32_MIN;
    props_refresh(e, 1);
    pickups_initialize(e);
    ball_initialize(e);
    e->activity_zone = defaults.world_mode == ODWD_MODE_OPEN_FIELD ?
                       open_activity_zone_for(e->vehicles[0].x,
                                              e->vehicles[0].z) :
                       ODWD_ACTIVITY_EXPLORE;
    update_places(e);
    camera_initialize(e);
    e->last_checkpoint = e->vehicles[0].checkpoint;
    e->magic = ODWD_MAGIC;
    camera_update(e);
    return ODWD_OK;
}

static void sanitize_input(const odwd_input *source, odwd_input *dest) {
    odwd_input_neutral(dest);
    if (!source || source->struct_size != sizeof(*source)) return;
    dest->buttons = source->buttons & (ODWD_BUTTON_RESPAWN |
                                      ODWD_BUTTON_HANDBRAKE |
                                      ODWD_BUTTON_CAMERA_HOLD |
                                      ODWD_BUTTON_TURBO |
                                      ODWD_BUTTON_HEADLIGHTS |
                                      ODWD_BUTTON_EXPLICIT_PEDALS |
                                      ODWD_BUTTON_REVERSE |
                                      ODWD_BUTTON_JUMP |
                                      ODWD_BUTTON_AUTODRIVE);
    dest->joystick_x = finite_control(source->joystick_x) ? source->joystick_x : 0.0;
    dest->joystick_y = finite_control(source->joystick_y) ? source->joystick_y : 0.0;
    dest->look_dx = finite_control(source->look_dx) ? source->look_dx : 0.0;
    dest->look_dy = finite_control(source->look_dy) ? source->look_dy : 0.0;
    dest->throttle = finite_control(source->throttle) ? source->throttle : 0.0;
    dest->brake = finite_control(source->brake) ? source->brake : 0.0;
    dest->reverse = finite_control(source->reverse) ? source->reverse : 0.0;
    dest->music_energy = finite_control(source->music_energy) ?
                         source->music_energy : 0.0;
    dest->music_beat = finite_control(source->music_beat) ?
                       source->music_beat : 0.0;
    dest->camera_mode = source->camera_mode < ODWD_CAMERA_MODE_COUNT ?
                        source->camera_mode : ODWD_CAMERA_CHASE;
}

int odwd_engine_step(void *storage, const odwd_input *input,
                     odwd_frame *frame_out) {
    odwd_engine_internal *e = checked(storage);
    odwd_input in;
    uint32_t i;
    if (!e) return ODWD_E_STATE;
    sanitize_input(input, &in);
    e->event_flags = ODWD_EVENT_NONE;
    e->music_energy = dmax(e->music_energy,
                           clampd(in.music_energy, 0.0, 1.0));
    e->music_beat = dmax(e->music_beat,
                         clampd(in.music_beat, 0.0, 1.0));
    if (e->music_beat_pending) {
        e->event_flags |= ODWD_EVENT_MUSIC_BEAT;
        e->music_beat_pending = 0u;
    }
    camera_consume_look(&e->camera, &in);
    player_step(e, &in);
    open_activity_prepare(e);
    music_survival_tick(e);
    for (i = 1u; i < e->vehicle_count; ++i)
        ai_step(e, &e->vehicles[i], &e->vehicles[0]);
    collide_vehicles(e);
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL &&
        !music_map_contains(&e->music_survival,
                            e->vehicles[0].x, e->vehicles[0].z)) {
        vehicle_internal *player = &e->vehicles[0];
        /* The arena perimeter is authoritative even after vehicle-vs-vehicle
         * resolution.  A rival impact used to happen after the per-vehicle
         * boundary check and could therefore push the player through a wall
         * that was otherwise closed. Restore the last supported centre and
         * shed the collision velocity instead of turning that contact into a
         * fake missing-floor death. */
        if (music_map_contains(&e->music_survival,
                               player->previous_x, player->previous_z)) {
            player->x = player->previous_x;
            player->z = player->previous_z;
        } else {
            player->x = 0.0;
            player->z = 0.0;
        }
        player->vx *= 0.28;
        player->vz *= 0.28;
        player->yaw_rate *= 0.35;
        e->event_flags |= ODWD_EVENT_COLLISION;
    }
    survival_tick(e);
    open_ball_step(e);
    if (e->event_flags & ODWD_EVENT_COLLISION)
        refresh_vehicle_projections(e);
    update_drift_mastery(e);
    pickups_collect(e);
    update_checkpoints_and_recovery(e, &in);
    update_places(e);
    if (e->config.world_mode == ODWD_MODE_ENDLESS) {
        while (e->vehicles[0].road_segment >= ROAD_SHIFT_AT)
            road_stream_shift(e);
        if (e->event_flags & ODWD_EVENT_STREAM_SHIFT) {
            props_refresh(e, 1);
            pickups_refresh(e);
        }
    } else {
        props_refresh(e, 0);
    }
    {
        uint32_t new_section = e->config.world_mode == ODWD_MODE_SURVIVAL ?
            e->survival.sector_index :
            (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL ?
             e->music_survival.event_index :
             (uint32_t)floor_i64_nonnegative(
                e->vehicles[0].progress / e->config.section_length_m));
        if (new_section != e->section_index) e->event_flags |= ODWD_EVENT_SECTION;
        e->section_index = new_section;
    }
    ++e->tick;
    camera_update(e);
    e->music_beat *= 0.915;
    e->music_energy *= 0.997;
    e->music_mid *= 0.996;
    e->music_high *= 0.994;
    e->music_flux *= 0.90;
    e->music_pulse *= 0.905;
    if (e->music_beat_interval > 0.12) {
        e->music_beat_phase += DT / e->music_beat_interval;
        while (e->music_beat_phase >= 1.0) e->music_beat_phase -= 1.0;
    }
    if (frame_out) return odwd_engine_read_frame(e, frame_out);
    return ODWD_OK;
}

int odwd_engine_read_frame(const void *storage, odwd_frame *frame_out) {
    const odwd_engine_internal *e = checked_const(storage);
    double section_base;
    double cycle_time;
    double phase;
    double night;
    if (!e) return ODWD_E_STATE;
    if (!frame_out) return ODWD_E_ARGUMENT;
    memset(frame_out, 0, sizeof(*frame_out));
    frame_out->struct_size = (uint32_t)sizeof(*frame_out);
    frame_out->abi_version = ODWD_ABI_VERSION;
    frame_out->tick = e->tick;
    frame_out->event_flags = e->event_flags;
    frame_out->vehicle_count = e->vehicle_count;
    frame_out->player_place = e->vehicles[0].place;
    frame_out->section_index = e->section_index;
    frame_out->world_mode = e->config.world_mode;
    frame_out->sector_style = e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL ?
                              e->music_survival.hazard_type :
                              e->section_index % 6u;
    frame_out->headlights_on = e->vehicles[0].headlights;
    frame_out->activity_zone = e->activity_zone;
    frame_out->football_score_left = e->football_score_left;
    frame_out->football_score_right = e->football_score_right;
    frame_out->simulation_time_s = (double)e->tick / (double)ODWD_TICK_HZ;
    section_base = (double)e->section_index * e->config.section_length_m;
    frame_out->section_progress_01 = e->config.world_mode == ODWD_MODE_SURVIVAL ?
        clampd(e->survival.sector_elapsed /
               dmax(e->survival.sector_duration, 0.001), 0.0, 1.0) :
        (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL ?
         clampd(e->music_survival.phase_elapsed /
                dmax(e->music_survival.phase_duration, 0.001), 0.0, 1.0) :
         clampd((e->vehicles[0].progress - section_base) /
                e->config.section_length_m, 0.0, 1.0));
    frame_out->endless_progress_m = e->vehicles[0].progress;
    frame_out->last_checkpoint_m = e->last_checkpoint;
    frame_out->drift_score = e->drift_score;
    frame_out->drift_chain = e->drift_chain;
    frame_out->deterministic_state_hash = state_hash_internal(e);
    cycle_time = frame_out->simulation_time_s / DAY_CYCLE_SECONDS;
    phase = cycle_time - (double)(uint64_t)cycle_time;
    if (phase < 0.50) night = 0.0;
    else if (phase < 0.62) night = smooth01((phase - 0.50) / 0.12);
    else if (phase < 0.88) night = 1.0;
    else night = 1.0 - smooth01((phase - 0.88) / 0.12);
    frame_out->day_phase_01 = phase;
    frame_out->night_amount_01 = night;
    frame_out->music_energy_01 = clampd(e->music_energy, 0.0, 1.0);
    frame_out->music_beat_01 = clampd(e->music_beat, 0.0, 1.0);
    frame_out->music_bass_01 = clampd(e->music_bass, 0.0, 1.0);
    frame_out->music_mid_01 = clampd(e->music_mid, 0.0, 1.0);
    frame_out->music_high_01 = clampd(e->music_high, 0.0, 1.0);
    frame_out->music_flux_01 = clampd(e->music_flux, 0.0, 1.0);
    frame_out->music_pulse_01 = clampd(e->music_pulse, 0.0, 1.0);
    frame_out->music_beat_phase_01 = clampd(e->music_beat_phase, 0.0, 1.0);
    frame_out->survival_alive_count = e->config.world_mode == ODWD_MODE_SURVIVAL ?
                                      e->survival.alive_count : 0u;
    frame_out->survival_player_eliminated =
        e->config.world_mode == ODWD_MODE_SURVIVAL ? e->survival.eliminated[0] : 0u;
    frame_out->survival_final_place = e->config.world_mode == ODWD_MODE_SURVIVAL ?
                                      e->survival.player_final_place : 0u;
    frame_out->survival_sector_family = e->config.world_mode == ODWD_MODE_SURVIVAL ?
                                        e->survival.family : 0u;
    frame_out->survival_sector_time_01 = e->config.world_mode == ODWD_MODE_SURVIVAL ?
        clampd(e->survival.sector_elapsed / dmax(e->survival.sector_duration, 0.001),
               0.0, 1.0) : 0.0;
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL) {
        frame_out->music_survival_health_01 = clampd(e->music_survival.health, 0.0, 1.0);
        frame_out->music_survival_score = e->music_survival.score;
        frame_out->music_hazard_type = e->music_survival.hazard_type;
        frame_out->music_hazard_phase = e->music_survival.hazard_phase;
        frame_out->music_hazard_level = e->music_survival.hazard_level;
        frame_out->music_enemy_pressure = e->music_survival.enemy_pressure;
        frame_out->music_survival_finished = e->music_survival.finished;
        frame_out->music_hazard_time_01 = clampd(e->music_survival.phase_elapsed /
            dmax(e->music_survival.phase_duration, 0.001), 0.0, 1.0);
    }
    /* The button is a level input and therefore safe to expose directly. */
    frame_out->autodrive_active = e->autodrive_active;
    return ODWD_OK;
}

int odwd_engine_read_vehicle(const void *storage, uint32_t vehicle_index,
                             odwd_vehicle_snapshot *vehicle_out) {
    const odwd_engine_internal *e = checked_const(storage);
    const vehicle_internal *v;
    if (!e) return ODWD_E_STATE;
    if (!vehicle_out) return ODWD_E_ARGUMENT;
    if (vehicle_index >= e->vehicle_count) return ODWD_E_INDEX;
    v = &e->vehicles[vehicle_index];
    memset(vehicle_out, 0, sizeof(*vehicle_out));
    vehicle_out->struct_size = (uint32_t)sizeof(*vehicle_out);
    vehicle_out->vehicle_id = vehicle_index;
    vehicle_out->is_player = v->is_player;
    vehicle_out->place = v->place;
    vehicle_out->position_x = v->x;
    vehicle_out->position_y = v->y;
    vehicle_out->position_z = v->z;
    vehicle_out->velocity_x = v->vx;
    vehicle_out->velocity_y = v->vy;
    vehicle_out->velocity_z = v->vz;
    vehicle_out->heading_rad = v->yaw;
    vehicle_out->steering_rad = v->steer;
    vehicle_out->speed_mps = vehicle_speed(v);
    vehicle_out->longitudinal_speed_mps = v->longitudinal;
    vehicle_out->lateral_speed_mps = v->lateral_speed;
    vehicle_out->slip_angle_rad = v->slip_angle;
    vehicle_out->drift_intensity = v->drift;
    vehicle_out->road_progress_m = v->progress;
    vehicle_out->road_lateral_m = v->road_lateral;
    vehicle_out->checkpoint_progress_m = v->checkpoint;
    vehicle_out->respawn_count = v->respawns;
    vehicle_out->collision_count = v->collisions;
    vehicle_out->headlights_on = v->headlights;
    vehicle_out->airborne = v->airborne;
    vehicle_out->turbo_active = v->turbo_active;
    vehicle_out->turbo_01 = clampd(v->turbo, 0.0, 1.0);
    vehicle_out->air_time_s = v->air_time;
    vehicle_out->traveled_distance_m = v->traveled_distance;
    vehicle_out->body_pitch_rad = v->body_pitch;
    vehicle_out->body_roll_rad = v->body_roll;
    return ODWD_OK;
}

int odwd_engine_read_camera(const void *storage,
                            odwd_camera_snapshot *camera_out) {
    const odwd_engine_internal *e = checked_const(storage);
    const camera_internal *c;
    if (!e) return ODWD_E_STATE;
    if (!camera_out) return ODWD_E_ARGUMENT;
    c = &e->camera;
    memset(camera_out, 0, sizeof(*camera_out));
    camera_out->struct_size = (uint32_t)sizeof(*camera_out);
    camera_out->camera_mode = c->mode;
    camera_out->position_x = c->x;
    camera_out->position_y = c->y;
    camera_out->position_z = c->z;
    camera_out->target_x = c->target_x;
    camera_out->target_y = c->target_y;
    camera_out->target_z = c->target_z;
    camera_out->forward_yaw_rad = c->view_yaw;
    camera_out->pitch_rad = c->pitch;
    camera_out->roll_rad = c->roll;
    camera_out->distance_m = c->distance;
    camera_out->vertical_fov_rad = c->fov;
    camera_out->speed_response = vehicle_speed(&e->vehicles[0]);
    camera_out->collision_impulse = c->collision_impulse;
    return ODWD_OK;
}

uint32_t odwd_engine_road_node_count(const void *storage) {
    const odwd_engine_internal *e = checked_const(storage);
    return e ? e->road_count : 0u;
}

int odwd_engine_read_road_node(const void *storage, uint32_t node_index,
                               odwd_road_node *node_out) {
    const odwd_engine_internal *e = checked_const(storage);
    const road_internal *n, *previous, *next;
    double ax, ay, az, bx, by, bz, len;
    double pax, pay, paz, pbx, pby, pbz;
    if (!e) return ODWD_E_STATE;
    if (!node_out) return ODWD_E_ARGUMENT;
    if (node_index >= e->road_count) return ODWD_E_INDEX;
    n = &e->road[node_index];
    previous = &e->road[node_index > 0u ? node_index - 1u : node_index];
    next = &e->road[node_index + (node_index + 1u < e->road_count ? 1u : 0u)];
    memset(node_out, 0, sizeof(*node_out));
    node_out->struct_size = (uint32_t)sizeof(*node_out);
    node_out->flags = n->flags;
    node_out->global_node_index = n->global_index;
    node_out->progress_m = n->s;
    node_out->center_x = n->x;
    node_out->center_y = n->y;
    node_out->center_z = n->z;
    ax = next->x - previous->x;
    ay = next->y - previous->y;
    az = next->z - previous->z;
    len = dsqrt(ax * ax + ay * ay + az * az);
    if (len < 1.0e-9) { ax = 0.0; ay = 0.0; az = 1.0; len = 1.0; }
    node_out->tangent_x = ax / len;
    node_out->tangent_y = ay / len;
    node_out->tangent_z = az / len;
    node_out->half_width_m = n->half_width;
    road_point_components(n, 1u, &bx, &by, &bz);
    node_out->alternate_x = bx;
    node_out->alternate_y = by;
    node_out->alternate_z = bz;
    node_out->alternate_half_width_m = road_alt_half_width(n);
    node_out->curvature_per_m = n->curvature;
    road_point_components(previous, 1u, &pax, &pay, &paz);
    road_point_components(next, 1u, &pbx, &pby, &pbz);
    ax = pbx - pax;
    ay = pby - pay;
    az = pbz - paz;
    len = dsqrt(ax * ax + ay * ay + az * az);
    if (len < 1.0e-9) { ax = node_out->tangent_x;
                        ay = node_out->tangent_y;
                        az = node_out->tangent_z; len = 1.0; }
    node_out->alternate_tangent_x = ax / len;
    node_out->alternate_tangent_y = ay / len;
    node_out->alternate_tangent_z = az / len;
    node_out->surface_grip_01 = (n->flags & ODWD_ROAD_SPEED_SECTION) ?
                                0.96 : 1.0;
    return ODWD_OK;
}

uint32_t odwd_engine_world_prop_count(const void *storage) {
    const odwd_engine_internal *e = checked_const(storage);
    return e ? e->prop_count +
               (e->config.world_mode == ODWD_MODE_OPEN_FIELD ? 1u : 0u) : 0u;
}

int odwd_engine_read_world_prop(const void *storage, uint32_t prop_index,
                                odwd_world_prop_snapshot *prop_out) {
    const odwd_engine_internal *e = checked_const(storage);
    const prop_internal *prop;
    uint32_t count;
    if (!e) return ODWD_E_STATE;
    if (!prop_out) return ODWD_E_ARGUMENT;
    count = odwd_engine_world_prop_count(storage);
    if (prop_index >= count) return ODWD_E_INDEX;
    if (e->config.world_mode == ODWD_MODE_OPEN_FIELD &&
        prop_index == e->prop_count) {
        memset(prop_out, 0, sizeof(*prop_out));
        prop_out->struct_size = (uint32_t)sizeof(*prop_out);
        prop_out->prop_id = UINT32_C(0x7f000001);
        prop_out->type = ODWD_PROP_BALL;
        prop_out->position_x = e->ball.x;
        prop_out->position_y = e->ball.y;
        prop_out->position_z = e->ball.z;
        prop_out->radius_m = OPEN_BALL_RADIUS;
        prop_out->scale = 1.0;
        prop_out->rotation_rad = e->ball.roll_angle;
        prop_out->extent_x_m = e->ball.roll_axis_x;
        prop_out->extent_z_m = e->ball.roll_axis_z;
        return ODWD_OK;
    }
    prop = &e->props[prop_index];
    memset(prop_out, 0, sizeof(*prop_out));
    prop_out->struct_size = (uint32_t)sizeof(*prop_out);
    prop_out->prop_id = prop->id;
    prop_out->type = prop->type;
    prop_out->collidable = prop->collidable;
    prop_out->position_x = prop->x;
    prop_out->position_y = prop->y;
    prop_out->position_z = prop->z;
    prop_out->radius_m = prop->radius;
    prop_out->scale = prop->scale;
    prop_out->rotation_rad = prop->rotation;
    prop_out->variant = prop->variant;
    prop_out->extent_x_m = prop->extent_x;
    prop_out->extent_y_m = prop->extent_y;
    prop_out->extent_z_m = prop->extent_z;
    if (prop->type == ODWD_PROP_RAMP ||
        prop->type == ODWD_PROP_RAMP_LARGE) {
        uint32_t ramp_index = prop->id - UINT32_C(0x71000000);
        if (ramp_index < (uint32_t)(sizeof(open_ramps) /
                                    sizeof(open_ramps[0]))) {
            double start_x = open_ramps[ramp_index].x -
                             dsin(open_ramps[ramp_index].yaw) *
                             open_ramps[ramp_index].length * 0.5;
            double start_z = open_ramps[ramp_index].z -
                             dcos(open_ramps[ramp_index].yaw) *
                             open_ramps[ramp_index].length * 0.5;
            prop_out->variant = ramp_index;
            prop_out->extent_x_m = open_ramps[ramp_index].width;
            prop_out->extent_y_m = open_ramps[ramp_index].height;
            prop_out->scale = open_ramps[ramp_index].length;
            prop_out->position_y = open_ground_height(start_x, start_z) + 0.10;
        }
    } else if (prop->type == ODWD_PROP_BIRD_FLYING) {
        double phase = (double)e->tick * DT * 0.24 + prop->rotation;
        prop_out->position_x += dsin(phase) * 13.0 * prop->scale;
        prop_out->position_z += dcos(phase) * 9.0 * prop->scale;
        prop_out->position_y += dsin(phase * 1.7) * 2.2;
    } else if (prop->type == ODWD_PROP_BIRD_GROUND) {
        double dx = prop->x - e->vehicles[0].x;
        double dz = prop->z - e->vehicles[0].z;
        double distance = dsqrt(dx * dx + dz * dz);
        if (distance < 22.0) {
            double amount = smooth01((22.0 - distance) / 14.0);
            double inv = distance > 0.001 ? 1.0 / distance : 1.0;
            prop_out->type = ODWD_PROP_BIRD_FLYING;
            prop_out->position_x += dx * inv * amount * 8.0;
            prop_out->position_z += dz * inv * amount * 8.0;
            prop_out->position_y += amount * 7.0 +
                                    dsin((double)e->tick * DT * 8.0 +
                                         prop->rotation) * 0.45;
        }
    }
    return ODWD_OK;
}

uint32_t odwd_engine_pickup_count(const void *storage) {
    return checked_const(storage) ? ODWD_MAX_TURBO_PICKUPS : 0u;
}

int odwd_engine_read_pickup(const void *storage, uint32_t pickup_index,
                            odwd_pickup_snapshot *pickup_out) {
    const odwd_engine_internal *e = checked_const(storage);
    const pickup_internal *pickup;
    if (!e) return ODWD_E_STATE;
    if (!pickup_out) return ODWD_E_ARGUMENT;
    if (pickup_index >= ODWD_MAX_TURBO_PICKUPS) return ODWD_E_INDEX;
    pickup = &e->pickups[pickup_index];
    memset(pickup_out, 0, sizeof(*pickup_out));
    pickup_out->struct_size = (uint32_t)sizeof(*pickup_out);
    pickup_out->pickup_id = pickup->id;
    pickup_out->type = ODWD_PICKUP_TURBO;
    pickup_out->active = pickup->active;
    pickup_out->position_x = pickup->x;
    pickup_out->position_y = pickup->y;
    pickup_out->position_z = pickup->z;
    pickup_out->progress_m = pickup->progress;
    pickup_out->amount_01 = pickup->amount;
    return ODWD_OK;
}

double odwd_engine_ground_height(const void *storage, double x, double z) {
    const odwd_engine_internal *e = checked_const(storage);
    double height;
    if (!e || !finite_control(x) || !finite_control(z)) return 0.0;
    if (e->config.world_mode == ODWD_MODE_SURVIVAL) return SURVIVAL_ARENA_Y;
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL)
        return music_ground_height_at(e, x, z);
    if (e->config.world_mode != ODWD_MODE_OPEN_FIELD) return 0.0;
    height = open_ground_height(x, z);
    if (open_ramp_surface(x, z, NULL, &height, NULL)) return height;
    return height;
}

double odwd_engine_base_ground_height(const void *storage, double x, double z) {
    const odwd_engine_internal *e = checked_const(storage);
    if (!e || !finite_control(x) || !finite_control(z)) return 0.0;
    if (e->config.world_mode == ODWD_MODE_MUSIC_SURVIVAL)
        return music_ground_height_at(e, x, z);
    if (e->config.world_mode != ODWD_MODE_OPEN_FIELD) return 0.0;
    return open_ground_height(x, z);
}

int odwd_engine_submit_music_pcm(void *storage, const float *interleaved,
                                 uint32_t frame_count, uint32_t channels,
                                 uint32_t sample_rate,
                                 double playback_time_s) {
    odwd_engine_internal *e = checked(storage);
    uint32_t frame;
    double energy_sum = 0.0;
    double bass_sum = 0.0;
    double mid_sum = 0.0;
    double high_sum = 0.0;
    double derivative_sum = 0.0;
    double alpha;
    double alpha_mid;
    double energy;
    double bass;
    double mid;
    double high;
    double flux;
    double onset;
    double previous_time;
    if (!e) return ODWD_E_STATE;
    if (!interleaved || frame_count == 0u || frame_count > 8192u ||
        channels == 0u || channels > 2u ||
        sample_rate < 8000u || sample_rate > 192000u ||
        !finite_control(playback_time_s)) return ODWD_E_ARGUMENT;
    if (playback_time_s + 0.02 < e->music_playback_time ||
        playback_time_s > e->music_playback_time + 0.75) {
        e->music_lowpass = 0.0;
        e->music_mid_lowpass = 0.0;
        e->music_envelope = 0.0;
        e->music_prev_abs = 0.0;
        e->music_onset_age = 0.0;
        e->music_beat_phase = 0.0;
    }
    previous_time = e->music_playback_time;
    alpha = clampd(1130.9733552923256 / (double)sample_rate, 0.004, 0.20);
    alpha_mid = clampd(12566.37061435917 / (double)sample_rate, 0.02, 0.42);
    for (frame = 0u; frame < frame_count; ++frame) {
        uint32_t channel;
        double mono = 0.0;
        for (channel = 0u; channel < channels; ++channel) {
            double sample = (double)interleaved[frame * channels + channel];
            if (!finite_control(sample)) sample = 0.0;
            mono += clampd(sample, -1.0, 1.0);
        }
        mono /= (double)channels;
        e->music_lowpass += (mono - e->music_lowpass) * alpha;
        e->music_mid_lowpass += (mono - e->music_mid_lowpass) * alpha_mid;
        {
            double mid_component = e->music_mid_lowpass - e->music_lowpass;
            double high_component = mono - e->music_mid_lowpass;
            double derivative = mono - e->music_prev_abs;
            mid_sum += mid_component * mid_component;
            high_sum += high_component * high_component;
            derivative_sum += derivative * derivative;
            e->music_prev_abs = mono;
        }
        energy_sum += mono * mono;
        bass_sum += e->music_lowpass * e->music_lowpass;
    }
    energy = clampd(dsqrt(energy_sum / (double)frame_count) * 3.65,
                    0.0, 1.0);
    bass = clampd(dsqrt(bass_sum / (double)frame_count) * 5.8,
                  0.0, 1.0);
    mid = clampd(dsqrt(mid_sum / (double)frame_count) * 4.4, 0.0, 1.0);
    high = clampd(dsqrt(high_sum / (double)frame_count) * 3.4, 0.0, 1.0);
    flux = clampd(dsqrt(derivative_sum / (double)frame_count) * 1.9 +
                  dmax(0.0, mid - e->music_mid) * 1.15 +
                  dmax(0.0, high - e->music_high) * 1.35, 0.0, 1.0);
    onset = clampd((energy - e->music_envelope * 1.08) * 4.8 +
                   dmax(0.0, bass - e->music_bass) * 2.3 +
                   dmax(0.0, mid - e->music_mid) * 1.4 +
                   flux * 0.55, 0.0, 1.0);
    e->music_envelope = dmax(energy, e->music_envelope * 0.88);
    e->music_energy = clampd(energy * 0.58 + bass * 0.20 +
                             mid * 0.14 + high * 0.08, 0.0, 1.0);
    e->music_bass = bass;
    e->music_mid = mid;
    e->music_high = high;
    e->music_flux = dmax(e->music_flux, flux);
    e->music_pulse = dmax(e->music_pulse,
                          clampd(onset * 0.78 + bass * 0.14 + flux * 0.30,
                                 0.0, 1.0));
    if (playback_time_s >= previous_time)
        e->music_onset_age += playback_time_s - previous_time;
    if (onset > 0.16) {
        if (onset > e->music_beat + 0.08) e->music_beat_pending = 1u;
        e->music_beat = dmax(e->music_beat, dmax(onset, e->music_pulse));
        if (onset > 0.27 && e->music_onset_age >= 0.18) {
            if (e->music_onset_age <= 1.25) {
                if (e->music_beat_interval < 0.12)
                    e->music_beat_interval = e->music_onset_age;
                else
                    e->music_beat_interval = e->music_beat_interval * 0.78 +
                                             e->music_onset_age * 0.22;
            }
            e->music_onset_age = 0.0;
            e->music_beat_phase = 0.0;
        }
    }
    e->music_playback_time = playback_time_s;
    return ODWD_OK;
}

uint64_t odwd_engine_state_hash(const void *storage) {
    const odwd_engine_internal *e = checked_const(storage);
    return e ? state_hash_internal(e) : UINT64_C(0);
}
