#ifndef ODPAR_GAME_INTERNAL_H
#define ODPAR_GAME_INTERNAL_H

#include "odpar_game.h"
#include "odm_rng.h"

#include <stddef.h>
#include <stdint.h>

#define ODG_FX_SHIFT 10
#define ODG_FX_ONE ((int32_t)1 << ODG_FX_SHIFT)
#define ODG_Q15_ONE 32767
#define ODG_GRID_SHIFT 7u
#define ODG_GRID_SIZE (1u << ODG_GRID_SHIFT)
#define ODG_CELL_COUNT (ODG_GRID_SIZE * ODG_GRID_SIZE)
#define ODG_CELL_FX ODG_FX_ONE
#define ODG_WORLD_HALF_CELLS ((int32_t)(ODG_GRID_SIZE / 2u))
#define ODG_WORLD_HALF_FX (ODG_WORLD_HALF_CELLS * ODG_FX_ONE)
#define ODG_MAX_ACTORS 10u
#define ODG_BOT_COUNT (ODG_MAX_ACTORS - 1u)
#define ODG_MAX_OBSTACLES 24u
#define ODG_MAX_PARTICLES 220u
#define ODG_MAX_LEADERS 8u
#define ODG_MAX_TRAIL_POINTS ODG_CELL_COUNT
#define ODG_MAX_TRAIL_PATH_POINTS 512u
#define ODG_CHUNK_CELL_COUNT ((uint32_t)ODG_CHUNK_SIZE_CELLS * (uint32_t)ODG_CHUNK_SIZE_CELLS)
#define ODG_CHUNK_PACKED_OWNER_BYTES (ODG_CHUNK_CELL_COUNT / 2u)
/* Bounded resident/persistence cache, not a world-sized array. Procedural unseen chunks
 * remain virtual and cost zero memory; only visited/modified chunks get records. */
#define ODG_CHUNK_CACHE_INITIAL_CAPACITY 64u
#define ODG_FLOATING_ORIGIN_TRIGGER_FX (24 * ODG_FX_ONE)

#define ODG_COMMAND_QUEUE_CAPACITY 64u
#define ODG_HARVEST_GRACE_TICKS 18u
#define ODG_RESOURCE_DEPLETED_VISUAL_TICKS (UINT32_C(20) * ODG_TICK_RATE)
#define ODG_RESOURCE_INTERACT_RANGE_FX (2 * ODG_FX_ONE)
#define ODG_ARTIFACT_INTERACT_RANGE_FX (3 * ODG_FX_ONE)
#define ODG_MELEE_RANGE_FX (8 * ODG_FX_ONE / 5)
#define ODG_BOT_TRAIL_SOFT_LIMIT 36u
#define ODG_PLAYER_ID 0u
#define ODG_PLAYER_SPEED_FX 60
#define ODG_BOT_SPEED_FX 47
#define ODG_PLAYER_INPUT_DEADZONE 3000
#define ODG_PLAYER_TURN_MAX_SIN_Q15 4300
#define ODG_BOT_TURN_MAX_SIN_Q15 2100
#define ODG_CAMERA_TURN_MAX_SIN_Q15 2500
#define ODG_PLAYER_TURN_ACCEL_Q15 9200
#define ODG_BOT_TURN_ACCEL_Q15 5000
#define ODG_CAMERA_TURN_ACCEL_Q15 5600
#define ODG_PLAYER_MOVE_TURN_MAX_SIN_Q15 9000
#define ODG_BOT_MOVE_TURN_MAX_SIN_Q15 3600
#define ODG_BOT_STEER_COMMIT_TICKS 24u
#define ODG_SLIDE_LOCK_TICKS 18u
#define ODG_CONTACT_STEER_MIN_DOT_Q15 5600
#define ODG_BOT_PROGRESS_WINDOW_TICKS 72u
#define ODG_BOT_PROGRESS_MIN_FX 180
#define ODG_CAMERA_DISTANCE_FX 3050
#define ODG_CAMERA_CLOSE_DISTANCE_FX 1680
#define ODG_CAMERA_FAR_DISTANCE_FX 4600
#define ODG_CAMERA_MIN_DISTANCE_FX 1280
#define ODG_CAMERA_COLLISION_RADIUS_FX 150
#define ODG_CAMERA_PLAYER_HEIGHT_FX 1750
#define ODG_CAMERA_GROUND_CLEARANCE_FX 1010
#define ODG_CAMERA_LOOK_DEADZONE 900
#define ODG_CAMERA_LOOK_MAX_SIN_Q15 2050
#define ODG_CAMERA_MANUAL_HOLD_TICKS 96u
#define ODG_CAMERA_PITCH_DEFAULT_Q15 6000
#define ODG_CAMERA_PITCH_MIN_Q15 3600
#define ODG_CAMERA_PITCH_MAX_Q15 12500
#define ODG_CAMERA_PITCH_STEP_Q15 260
/* ~2.4 degrees. Small intentional stick changes must not be quantized away. */
#define ODG_INPUT_REBASE_DOT_Q15 32738
#define ODG_DASH_SPEED_NUM 15
#define ODG_DASH_SPEED_DEN 10
#define ODG_DASH_COOLDOWN_TICKS (2u * ODG_TICK_RATE)
#define ODG_DASH_DURATION_TICKS 18u
#define ODG_MAX_STEP_US 50000u
#define ODG_TICK_US_NUM 1000000u
#define ODG_RENDER_PIXELS_MAX ODG_MAX_RENDER_PIXELS
#define ODG_CAPTURE_WIN_PERMILLE 550u
#define ODG_ACTOR_PLAYER 1u
#define ODG_ACTOR_BOT 2u
/* Internal fauna locomotion/decision state is persisted in SAVE; keep one authority so
 * simulation and semantic validation cannot silently disagree on the valid state set. */
#define ODG_FAUNA_STATE_GROUND UINT32_C(1)
#define ODG_FAUNA_STATE_FLIGHT UINT32_C(2)
#define ODG_FAUNA_STATE_FLEE UINT32_C(3)
#define ODG_FAUNA_STATE_FORAGE UINT32_C(4)
#define ODG_FAUNA_STATE_NEST UINT32_C(5)
#define ODG_FAUNA_STATE_AGGRO UINT32_C(6)
#define ODG_FAUNA_STATE_HUNT_PREY UINT32_C(7)
#define ODG_BOT_INSIDE 0u
#define ODG_BOT_OUTBOUND 1u
#define ODG_BOT_SIDELEG 2u
#define ODG_BOT_RETURN 3u
#define ODG_OWNER_NONE 0u
#define ODG_OWNER_FROM_ID(id) ((uint8_t)((id) + 1u))
#define ODG_ID_FROM_OWNER(owner) ((uint32_t)((owner) - 1u))

/* Authoritative mathematical floor division for signed world coordinates. Keep every
 * chunk/map/geology/render caller on this primitive so negative coordinates cannot
 * disagree at floating-origin or chunk boundaries. */
static inline int64_t odg_floor_div_i64_internal(int64_t n,int64_t d) {
    int64_t q=n/d,r=n%d;
    if(r!=0 && ((r<0)!=(d<0))) --q;
    return q;
}
#define ODG_TURRET_NEUTRAL 0u
#define ODG_TURRET_NONE UINT32_MAX
#define ODG_INSTANCE_ID_PROCEDURAL_BIT UINT64_C(0x8000000000000000)
#define ODG_RESOURCE_STABLE_PROCEDURAL_BIT UINT64_C(0x8000000000000000)
#define ODG_INSTANCE_ID_SEQUENTIAL_MAX UINT64_C(0x7fffffffffffffff)
#define ODG_TURRET_TRAIL_MIN_CELLS 4u
#define ODG_TURRET_CAPTURE_RADIUS 2
#define ODG_TURRET_COLLISION_RADIUS_FX (3 * ODG_FX_ONE / 5)
#define ODG_AMMO_DELIVERY_RANGE_FX (3 * ODG_FX_ONE)
#define ODG_TURRET_TARGET_NONE 0u
#define ODG_TURRET_TARGET_TRAIL 1u
#define ODG_TURRET_TARGET_TERRITORY 2u
#define ODG_TURRET_TARGET_RESOURCE 3u
#define ODG_TURRET_MIN_TARGET_FX (2 * ODG_FX_ONE)
#define ODG_TURRET_RETARGET_GRACE_TICKS 42u
#define ODG_TURRET_AMMO_LABEL_RANGE_FX (10 * ODG_FX_ONE)
#define ODG_PICKUP_RANGE_FX (13 * ODG_FX_ONE / 10)
#define ODG_CHIP_HACK_RANGE_FX (23 * ODG_FX_ONE / 10)
#define ODG_ACTOR_SATIETY_MAX UINT32_C(1000)
#define ODG_ACTOR_SATIETY_DECAY_TICKS (UINT32_C(324)) /* ~45 min full -> empty */
#define ODG_ACTOR_HYDRATION_MAX UINT32_C(1000)
#define ODG_ACTOR_HYDRATION_DECAY_TICKS UINT32_C(432) /* ~60 min full -> empty */
#define ODG_STARVATION_DAMAGE_TICKS (UINT32_C(10) * ODG_TICK_RATE)
#define ODG_DEHYDRATION_DAMAGE_TICKS (UINT32_C(12) * ODG_TICK_RATE)
#define ODG_TERRITORY_RECOVERY_DELAY_TICKS (UINT32_C(12) * ODG_TICK_RATE)
#define ODG_MAX_STEP_HEIGHT_FX (ODG_FX_ONE * 11 / 20)
#define ODG_JUMP_INITIAL_VY_FX INT32_C(68)
#define ODG_GRAVITY_FX INT32_C(4)
#define ODG_JUMP_GRAVITY_FX INT32_C(3)
#define ODG_WATER_MOVE_FACTOR_Q15 INT32_C(22500)
#define ODG_RAFT_SPEED_FX INT32_C(68)
/* Water locomotion is a medium, not a terrain slow-zone. Actors wade in shallow water,
 * then become buoyant once the body would otherwise be fully submerged. The draft keeps
 * the cube visibly in the water while its breathing point remains above the surface. */
#define ODG_SWIM_MIN_DEPTH_MILLI UINT32_C(700)
#define ODG_SWIM_DRAFT_MILLI UINT32_C(390)
#define ODG_SWIM_BUOYANCY_STEP_FX INT32_C(14)
#define ODG_SWIM_SHORE_STEP_MILLI INT32_C(520)
#define ODG_BOT_OXYGEN_ESCAPE_PERMILLE UINT32_C(720)
#define ODG_DEATH_CACHE_LIFETIME_TICKS (UINT32_C(45) * UINT32_C(60) * ODG_TICK_RATE)
#define ODG_MELEE_COOLDOWN_TICKS (ODG_TICK_RATE * UINT32_C(7) / UINT32_C(20))
#define ODG_DAMAGE_FLASH_TICKS UINT32_C(18)
#define ODG_FAUNA_STARVATION_DAMAGE_TICKS (UINT32_C(30) * ODG_TICK_RATE)
#define ODG_FAUNA_DEHYDRATION_DAMAGE_TICKS (UINT32_C(36) * ODG_TICK_RATE)
#define ODG_FAUNA_DECISION_MAX_TICKS (UINT32_C(6) * ODG_TICK_RATE)

typedef struct {
    odg_item_stack slots[ODG_INVENTORY_MAX_SLOTS];
    uint32_t slot_count;
    uint32_t selected_slot;
    uint32_t equipped_backpack_type;
    uint32_t reserved_u32;
} odg_inventory;

typedef struct {
    uint32_t active;
    uint32_t id;
    int32_t x, z; /* floating-origin cache; valid only when local_resident != 0 */
    int64_t global_fx_x, global_fx_z; /* authoritative persistent world position */
    uint32_t local_resident;
    uint32_t pickup_cd;
    uint32_t age_ticks;
    uint32_t lifetime_ticks;
    odg_item_stack stack;
} odg_world_pickup;

typedef struct {
    uint32_t used;
    uint32_t state;
    int64_t chunk_x;
    int64_t chunk_z;
    uint64_t stable_id;
    uint64_t last_touch_tick;
    uint64_t depleted_resource_mask;
    /* Procedural turret override. 0 = pure worldgen/default, 1 = deployed modified
     * state, 2 = permanently removed from this chunk (picked up/moved). */
    uint32_t procedural_turret_state;
    uint32_t procedural_turret_owner;
    uint32_t procedural_turret_material_tier;
    uint32_t procedural_turret_mode;
    uint32_t procedural_turret_ammo;
    uint32_t procedural_turret_shots_fired;
    uint32_t procedural_turret_cells_conquered;
    uint32_t procedural_turret_reserved;
    uint64_t procedural_turret_instance_id;
    uint32_t territory_cells[ODG_MAX_ACTORS];
    uint8_t territory_packed[ODG_CHUNK_PACKED_OWNER_BYTES];
    uint8_t trail_packed[ODG_CHUNK_PACKED_OWNER_BYTES];
} odg_chunk_runtime;

/* Derived, rebuildable sleeping-chunk summary. It is deliberately NOT serialized:
 * save files contain the logical world, and these counters are reconstructed after
 * load. This keeps allocator/query acceleration out of the deterministic save/hash. */
typedef struct {
    uint32_t actor_count;
    uint32_t artifact_count;
    uint32_t turret_count;
    uint32_t resource_count;
    uint32_t construction_count;
} odg_chunk_summary_runtime;

typedef struct {
    odg_command entries[ODG_COMMAND_QUEUE_CAPACITY];
    uint32_t read_index;
    uint32_t write_index;
    uint32_t count;
} odg_command_queue;

typedef struct {
    uint32_t active;
    uint32_t id;
    uint64_t stable_id;
    uint32_t kind;
    uint32_t state;
    int32_t x, z; /* floating-origin cache */
    int64_t global_fx_x, global_fx_z;
    uint32_t local_resident;
    uint32_t harvest_actor;
    uint32_t harvest_progress;
    uint32_t harvest_required;
    uint32_t harvest_grace;
    uint32_t yield_min;
    uint32_t yield_max;
    uint32_t bootstrap_actor_id; /* planner affinity only; never ownership/exclusivity */
    int64_t chunk_x;
    int64_t chunk_z;
    uint32_t chunk_ordinal;
    uint32_t procedural;
    uint32_t turret_hits;
    /* Flora component. species_id==0 for ore/mineral resources. */
    uint32_t species_id;
    uint32_t flora_stage;
    uint32_t variant;
    uint32_t fruit_count;
    uint32_t fruit_capacity;
    uint32_t soil_moisture_permille;
    uint32_t fruit_cycle_ticks;
    uint32_t windfall_count;
    uint64_t age_ticks;
} odg_resource_node;

typedef struct {
    odg_item_stack slots[ODG_CHEST_SLOTS];
} odg_storage;

typedef struct {
    uint32_t active;
    uint32_t id;
    uint64_t instance_id;
    uint32_t item_type;
    uint32_t owner_actor_id;
    uint32_t material_tier;
    uint32_t capability_bits;
    uint32_t state;
    int32_t x, z; /* floating-origin cache */
    int64_t global_fx_x, global_fx_z;
    uint32_t local_resident;
    uint64_t aux_tick; /* expiry/cycle tick; meaning defined by artifact state/capabilities */
    uint32_t aux_u32;  /* e.g. collected rain units */
    uint32_t fluid_type_id; /* ODG_FLUID_* for fluid-storage artifacts; persisted in SAVE18 */
    odg_storage storage;
} odg_artifact;

typedef struct {
    uint32_t active;
    uint32_t id;
    uint64_t instance_id;
    uint32_t owner_actor_id;
    uint32_t material_tier;
    uint32_t shape;
    uint32_t state;
    int32_t x,z; /* floating-origin cache */
    int64_t global_fx_x,global_fx_z;
    uint32_t local_resident;
    uint32_t health;
    uint32_t max_health;
    uint32_t reserved_u32; /* frozen compatibility slot; must stay zero */
} odg_construction_block;

typedef struct {
    uint32_t active;
    uint32_t type;
    uint32_t id;
    uint32_t name_code;
    int32_t x, z; /* floating-origin simulation/render cache */
    int64_t global_fx_x, global_fx_z; /* authoritative persistent actor position */
    uint32_t local_resident;
    int32_t vx, vz;
    int32_t face_x_q15, face_z_q15;
    int32_t radius;
    uint32_t hp, max_hp;
    uint32_t satiety_permille;
    uint32_t satiety_decay_accum;
    uint32_t starvation_accum;
    uint32_t hydration_permille;
    uint32_t hydration_decay_accum;
    uint32_t dehydration_accum;
    uint32_t level;
    uint32_t score;
    uint32_t kills;
    uint32_t deaths;
    uint32_t dash_cd;
    uint32_t dash_ticks;
    uint32_t flash_ticks;
    uint32_t melee_cooldown_ticks;
    uint32_t death_reason;
    uint32_t trail_active;
    uint32_t trail_broken; /* enemy/turret cut: cannot redraw until own territory is reached */
    uint32_t trail_len;
    uint32_t territory_recovery_ticks;
    uint32_t last_cell;
    uint32_t home_cell;
    int64_t home_global_cell_x;
    int64_t home_global_cell_z;
    int64_t last_global_cell_x;
    int64_t last_global_cell_z;
    int64_t trail_head_global_cell_x;
    int64_t trail_head_global_cell_z;
    uint32_t think_cd;
    uint32_t bot_mode;
    uint32_t bot_leg_target;
    int32_t ai_x_q15, ai_z_q15;
    int32_t bot_out_x_q15, bot_out_z_q15;
    int32_t turn_sign;
    int32_t turn_rate_q15;
    int32_t speed_fx;
    int32_t steer_q15;
    int32_t control_raw_x_q15;
    int32_t control_raw_z_q15;
    uint32_t ai_commit_ticks;
    uint32_t ai_plan_cell;
    int64_t ai_plan_global_cell_x;
    int64_t ai_plan_global_cell_z;
    uint32_t bot_economy_item_type;
    uint32_t bot_economy_target_id;
    uint32_t slide_lock_ticks;
    uint32_t slide_axis;
    int32_t slide_dir_x_q15, slide_dir_z_q15;
    int32_t progress_x, progress_z;
    uint32_t progress_ticks;
    uint32_t stuck_windows;
    /* Retired free-capture-ammo accumulator. Kept as a save-layout tombstone; never reuse. */
    uint32_t capture_ammo_credit;
    uint32_t respawn_ticks;
    int32_t vertical_offset_fx;
    int32_t vertical_velocity_fx;
    uint32_t grounded;
    uint32_t trail_head_cell;
    uint32_t trail_render_anchor_cell;
    uint32_t trail_path_len;
    int32_t trail_path_x[ODG_MAX_TRAIL_PATH_POINTS];
    int32_t trail_path_z[ODG_MAX_TRAIL_PATH_POINTS];
    odg_inventory inventory;
    odm_rng rng;
} odg_actor;

typedef struct {
    uint32_t active;
    uint32_t id;
    uint64_t stable_id;
    uint32_t species_id;
    uint32_t family;
    uint32_t variant;
    uint32_t state;
    uint32_t tame;
    uint32_t owner_actor_id;
    uint32_t legacy_persistent_u32; /* frozen SAVE-layout tombstone; current value is always zero */
    uint32_t hp,max_hp;
    uint32_t satiety_permille;
    uint32_t satiety_decay_accum;
    uint32_t starvation_accum;
    uint32_t hydration_permille;
    uint32_t hydration_decay_accum;
    uint32_t dehydration_accum;
    uint32_t life_stage;
    uint32_t sex;
    uint32_t decision_ticks;
    uint32_t breeding_cooldown;
    uint32_t pregnancy_ticks;
    uint32_t legacy_target_pickup_id; /* frozen legacy sentinel; current value is always UINT32_MAX */
    uint32_t nest_id;
    uint64_t age_ticks;
    int32_t x,z;
    int64_t global_fx_x,global_fx_z;
    uint32_t local_resident;
    int32_t y_offset_fx;
    int32_t vx,vz;
    int32_t face_x_q15,face_z_q15;
    odm_rng rng;
} odg_fauna_entity;

typedef struct {
    uint32_t active;
    uint32_t id;
    uint64_t stable_id;
    uint32_t species_id;
    uint32_t substrate;
    uint32_t egg_count;
    uint32_t hatch_ticks;
    uint32_t parent_a;
    uint32_t parent_b;
    uint64_t host_resource_stable_id;
    int32_t x,z;
    int64_t global_fx_x,global_fx_z;
    uint32_t local_resident;
} odg_fauna_nest;

typedef struct { int32_t x, z, hx, hz, height_fx; uint32_t palette; } odg_obstacle;
typedef struct { uint32_t active; int32_t x,z,vx,vz,y_fx,vy_fx; uint32_t life,color; } odg_particle;
typedef struct {
    int32_t move_x_q15, move_z_q15;
    int32_t move_strength_q15;
    int32_t aim_x_q15, aim_z_q15;
    uint32_t buttons;
    uint32_t world_heading_mode;
} odg_input;
typedef struct {
    uint32_t active;
    uint32_t id;
    uint64_t instance_id;
    uint32_t procedural;
    int64_t source_chunk_x;
    int64_t source_chunk_z;
    uint32_t material_tier;
    uint8_t owner;
    uint8_t pad0, pad1, pad2;
    int32_t x, z; /* floating-origin cache */
    int64_t global_fx_x, global_fx_z;
    uint32_t local_resident;
    uint32_t ammo;
    uint32_t max_ammo;
    uint32_t fire_cd;
    uint32_t fire_period;
    int32_t range_fx;
    uint32_t carried_by;
    uint32_t shots_fired;
    uint32_t cells_conquered;
    uint32_t last_target_cell; /* active-window presentation cache only */
    int64_t target_global_cell_x;
    int64_t target_global_cell_z;
    uint32_t beam_ticks;
    uint32_t target_kind;
    uint32_t aim_ticks;
    uint32_t aim_required;
    uint32_t target_actor_id;
    uint32_t retarget_cd;
    uint32_t mode;
    uint64_t target_resource_stable_id;
    int32_t head_x_q15, head_z_q15;
    int32_t head_turn_rate_q15;
    int32_t head_turn_sign;
} odg_turret;

typedef struct {
    uint32_t state;
    uint32_t owner;
    uint32_t material_tier;
    uint32_t mode;
    uint32_t ammo;
    uint32_t shots_fired;
    uint32_t cells_conquered;
    uint64_t instance_id;
} odg_chunk_turret_state;

#define ODG_SPATIAL_KIND_TURRET 1u
#define ODG_SPATIAL_KIND_PICKUP 2u
#define ODG_SPATIAL_KIND_RESOURCE 3u
#define ODG_SPATIAL_KIND_ARTIFACT 4u
#define ODG_SPATIAL_KIND_CONSTRUCTION 5u
typedef struct {
    int64_t chunk_x, chunk_z;
    uint32_t kind;
    uint32_t id;
} odg_spatial_ref;


typedef struct {
    uint32_t oxygen_permille;
    uint32_t oxygen_loss_accum;
    uint32_t drowning_accum;
} odg_respiration_state;

typedef struct {
    uint32_t struct_size;
    uint32_t worldgen_version;
    odg_respiration_state actors[ODG_MAX_ACTORS];
    odg_respiration_state fauna[ODG_FAUNA_MAX_ENTRIES];
    uint32_t fauna_attack_cooldown[ODG_FAUNA_MAX_ENTRIES];
    uint32_t fauna_target_actor[ODG_FAUNA_MAX_ENTRIES];
    uint32_t fauna_aggro_ticks[ODG_FAUNA_MAX_ENTRIES];
} odg_persistent_runtime_state;

typedef struct {
    uint32_t initialized;
    uint32_t width, height;
    uint64_t seed;
    uint64_t tick;
    uint64_t tick_accum_scaled;
    odm_rng rng;
    odg_input input;
    uint32_t prev_buttons;
    odg_actor actors[ODG_MAX_ACTORS];
    odg_fauna_entity fauna[ODG_FAUNA_MAX_ENTRIES];
    odg_fauna_nest fauna_nests[ODG_FAUNA_MAX_NESTS];
    uint32_t fauna_count;
    uint32_t fauna_nest_count;
    odm_rng ecology_rng;
    uint32_t weather_rain_permille;
    /* Frozen save-layout slot. Weather epoch is exactly derivable from tick and therefore
     * must not be duplicated as mutable authoritative state. New worlds keep this zero. */
    uint32_t save_reserved_weather_u32;
    odg_obstacle obstacles[ODG_MAX_OBSTACLES];
    odg_particle particles[ODG_MAX_PARTICLES];
    uint32_t obstacle_count;
    uint32_t turret_count;
    uint32_t pickup_count;
    uint32_t resource_count;
    uint32_t artifact_count;
    uint32_t opened_artifact_id;
    uint64_t next_instance_id;
    odg_command_queue commands;
    odg_interaction_hint interaction_hint;
    uint32_t interact_ticks;
    uint32_t interact_hold_fired;
    uint32_t interact_pressed_prev;
    /* Logical global position of local cell (0,0). Kept chunk-aligned. Local C gameplay
     * stays precise around the player while these 64-bit coordinates can travel for years. */
    int64_t world_origin_cell_x;
    int64_t world_origin_cell_z;
    uint32_t chunk_cache_used;
    uint32_t chunk_recenter_count;
    /* Frozen SAVE compatibility bytes from the retired finite-arena mask. Always one
     * in Open Domain and never gameplay authority; retained only to preserve raw layout. */
    uint8_t playable[ODG_CELL_COUNT];
    uint8_t bot_nav_edges[ODG_CELL_COUNT]; /* 1=L 2=R 4=-Z 8=+Z, derived at round build */
    uint8_t territory[ODG_CELL_COUNT];
    uint8_t trail_owner[ODG_CELL_COUNT];
    /* Frozen SAVE14-19 compatibility slots from the retired local flood-fill capture.
     * They stay zero and physically present until a future compact-save schema explicitly
     * migrates the raw legacy suffix instead of silently changing its byte layout. */
    uint8_t save_reserved_flood_seen[ODG_CELL_COUNT];
    uint16_t save_reserved_flood_queue[ODG_CELL_COUNT];
    uint32_t playable_count; /* compatibility sentinel == ODG_CELL_COUNT */
    uint32_t territory_count[ODG_MAX_ACTORS];
    /* CONTROL CAMERA: authoritative because camera-relative input resolves against it. */
    int32_t camera_dir_x_q15;
    int32_t camera_dir_z_q15;
    int32_t control_basis_x_q15;
    int32_t control_basis_z_q15;
    int32_t control_heading_x_q15;
    int32_t control_heading_z_q15;
    int32_t control_strength_q15;
    uint32_t control_active;
    uint32_t match_over;
    uint32_t winner_id;
    odg_game_stats stats;
    /* Dedicated presentation RNG. Particle shape must never consume the authoritative
     * gameplay RNG or participate in SAVE/hash identity. It intentionally lives after
     * `stats`, the boundary that excludes presentation state from persistence. */
    odm_rng visual_rng;
    /* RENDER CAMERA: presentation-only. None of these values may influence gameplay
     * hash, collision, AI, territory, crafting, harvesting or RNG. */
    int32_t camera_anchor_x;
    int32_t camera_anchor_z;
    int32_t camera_turn_rate_q15;
    uint32_t camera_manual_ticks;
    int32_t camera_height_fx;
    int32_t camera_distance_fx;
    int32_t camera_target_distance_fx;
    int32_t camera_pitch_q15;
    uint32_t camera_mode;
    uint32_t music_reactivity_q16;
    uint32_t remote_view_active;
    int32_t remote_view_x;
    int32_t remote_view_z;
    int32_t remote_view_dir_x_q15;
    int32_t remote_view_dir_z_q15;
    /* Presentation-only camera origin for remote artifact views. It is deliberately
     * 64-bit and independent from the gameplay floating origin. */
    int64_t remote_view_global_fx_x;
    int64_t remote_view_global_fx_z;
    uint32_t avatar_preview_active;
    uint32_t avatar_preview_yaw_q16;
    uint32_t camera_preview_active;
    uint32_t camera_preview_yaw_q16;
    int32_t camera_preview_pitch_q15;
    uint32_t camera_preview_mode;
    uint32_t visual_theme; /* presentation-only: excluded from state hash */
    uint32_t presentation_mode; /* menu/showcase camera only; excluded from state hash */
    odg_leader_entry leaders[ODG_MAX_LEADERS];
    uint32_t leader_count;
    uint32_t render_triangles;
    uint32_t render_pixels_touched;
} odg_world;

extern odg_world g_odg;
extern odg_persistent_runtime_state g_odg_persistent_runtime;
extern odg_chunk_runtime *g_odg_chunk_cache;
extern uint32_t g_odg_chunk_cache_capacity;
extern odg_chunk_summary_runtime *g_odg_chunk_summaries;
extern uint32_t g_odg_chunk_summary_capacity;
extern odg_turret *g_odg_turrets;
extern uint32_t g_odg_turret_capacity;
extern odg_world_pickup *g_odg_pickups;
extern uint32_t g_odg_pickup_capacity;
extern odg_resource_node *g_odg_resources;
extern uint32_t g_odg_resource_capacity;
extern odg_artifact *g_odg_artifacts;
extern uint32_t g_odg_artifact_capacity;
extern odg_construction_block *g_odg_construction_blocks;
extern uint32_t g_odg_construction_capacity;
extern uint32_t g_odg_construction_count;
extern odg_spatial_ref *g_odg_spatial_refs;
extern uint32_t g_odg_spatial_ref_count;
int odg_entities_reserve_turrets(uint32_t needed);
int odg_entities_reserve_pickups(uint32_t needed);
int odg_entities_reserve_resources(uint32_t needed);
int odg_entities_reserve_artifacts(uint32_t needed);
int odg_entities_reserve_construction(uint32_t needed);
void odg_entities_reset_runtime(void);
void odg_entities_release_runtime(void);
void odg_entities_sync_globals_from_local(void);
void odg_entities_refresh_local_cache(void);
void odg_entities_spatial_mark_dirty(void);
void odg_entities_spatial_rebuild(void);
const odg_spatial_ref *odg_entities_spatial_refs(uint32_t *out_count);
uint32_t odg_entities_spatial_lower_bound(int64_t chunk_x,int64_t chunk_z);
typedef int (*odg_spatial_visit_fn)(uint32_t id,void *context);
int odg_entities_spatial_visit_near_global(uint32_t kind,int64_t global_fx_x,int64_t global_fx_z,
                                           int32_t radius_fx,odg_spatial_visit_fn visit,void *context);
extern uint8_t g_odg_framebuffer[ODG_RENDER_PIXELS_MAX * 4u];
extern uint16_t g_odg_depth[ODG_RENDER_PIXELS_MAX];
void *odg_memset(void *dst, int value, size_t n);
void *odg_memcpy(void *dst, const void *src, size_t n);
void *odg_mem_realloc(void *ptr, size_t size);
void odg_mem_free(void *ptr);
void odg_mem_heap_reset(void);
static inline int32_t odg_abs_i32(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t odg_clamp_i32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline uint32_t odg_min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline float odg_fx_to_float(int32_t v) { return (float)v / (float)ODG_FX_ONE; }
static inline int64_t odg_dist2(int32_t ax, int32_t az, int32_t bx, int32_t bz) { int64_t dx=(int64_t)ax-bx,dz=(int64_t)az-bz; return dx*dx+dz*dz; }
uint32_t odg_rand_bounded(odm_rng *rng, uint32_t bound);
int32_t odg_rand_range_fx(odm_rng *rng, int32_t lo, int32_t hi);
void odg_normalize_q15(int32_t x, int32_t z, int32_t *out_x, int32_t *out_z);
uint32_t odg_isqrt_u64(uint64_t v);
uint32_t odg_cell_from_world(int32_t x, int32_t z);
int32_t odg_terrain_height_fx(int32_t x, int32_t z);
int odg_environment_surface_local(int32_t x,int32_t z,odg_surface_sample *out_sample);
int odg_actor_bodies_clear_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_actor_id);
int odg_fauna_bodies_clear_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_fauna_id);
int odg_dynamic_position_clear_internal(int32_t x,int32_t z,int32_t radius,
                                        uint32_t ignore_actor_id,uint32_t ignore_fauna_id);
uint8_t odg_bot_navigation_edge_mask_internal(uint32_t cell);
void odg_bot_navigation_rebuild_internal(void);
int odg_environment_normal_local_q15(int32_t x,int32_t z,int32_t *out_x,int32_t *out_y,int32_t *out_z);
void odg_environment_tick(void);
void odg_survival_reset_new_world(void);
void odg_survival_reset_legacy_world(void);
void odg_survival_tick(void);
void odg_survival_reset_actor(uint32_t actor_id);
void odg_survival_reset_fauna(uint32_t fauna_id);
void odg_fauna_deactivate_internal(uint32_t fauna_id);
int odg_fauna_runtime_combat_state_validate_internal(uint32_t fauna_id,uint32_t attack_cooldown,
                                                     uint32_t target_actor,uint32_t aggro_ticks);
int odg_survival_state_validate(const odg_persistent_runtime_state *state);
int odg_survival_loaded_fauna_state_validate_internal(uint32_t fauna_id);
int odg_save_identity_validate_internal(void);
int odg_actor_is_swimming_internal(const odg_actor *actor);
int32_t odg_actor_swim_target_offset_fx_internal(const odg_actor *actor);
int odg_geology_surface_exposure_internal(int64_t world_cell_x,int64_t world_cell_z,uint32_t resource_kind);
int64_t odg_global_center_cell_x_internal(void);
int64_t odg_global_center_cell_z_internal(void);
void odg_local_fx_to_global_fx_internal(int32_t x,int32_t z,int64_t *out_x_fx,int64_t *out_z_fx);
int odg_global_fx_to_local_internal(int64_t x_fx,int64_t z_fx,int32_t *out_x,int32_t *out_z);
int odg_global_cell_to_local_internal(int64_t gx,int64_t gz,uint32_t *out_cell);
int odg_global_cell_center_to_local_fx_internal(int64_t gx,int64_t gz,int32_t *out_x,int32_t *out_z);
void odg_chunks_release_runtime(void);
int odg_chunks_reserve_runtime(uint32_t needed);
void odg_chunks_reset_runtime(void);
void odg_chunks_capture_active_window(void);
void odg_chunks_load_active_window(void);
void odg_chunks_maybe_recenter(void);
void odg_chunks_refresh_summaries(void);
const odg_chunk_summary_runtime *odg_chunk_summary_at(int64_t chunk_x,int64_t chunk_z);
uint8_t odg_chunk_owner_at_global_cell(int64_t gx,int64_t gz);
uint8_t odg_chunk_runtime_owner_at_ordinal_internal(const odg_chunk_runtime *record,uint32_t ordinal);
uint8_t odg_chunk_runtime_trail_at_ordinal_internal(const odg_chunk_runtime *record,uint32_t ordinal);
int odg_chunk_runtime_state_validate_internal(const odg_chunk_runtime *record,uint32_t expected_index);
int odg_chunks_derived_cache_validate_internal(void);
uint8_t odg_chunk_trail_at_global_cell(int64_t gx,int64_t gz);
void odg_chunk_set_owner_at_global_cell(int64_t gx,int64_t gz,uint8_t owner);
void odg_chunk_set_trail_at_global_cell(int64_t gx,int64_t gz,uint8_t owner);
void odg_chunk_clear_trail_owner(uint8_t owner);
void odg_local_fx_to_global_cell_internal(int32_t x,int32_t z,int64_t *out_gx,int64_t *out_gz);
void odg_global_fx_to_global_cell_internal(int64_t x_fx,int64_t z_fx,int64_t *out_gx,int64_t *out_gz);
int odg_chunk_prepare_resource_depletion_internal(int64_t chunk_x,int64_t chunk_z,uint32_t ordinal);
int odg_chunk_mark_resource_depleted(int64_t chunk_x,int64_t chunk_z,uint32_t ordinal);
int odg_chunk_resource_depleted(int64_t chunk_x,int64_t chunk_z,uint32_t ordinal);
uint32_t odg_chunk_coal_candidate_count_internal(int64_t chunk_x,int64_t chunk_z);
int odg_chunk_procedural_turret_cell(int64_t chunk_x,int64_t chunk_z,int64_t *out_gx,int64_t *out_gz);
int odg_chunk_procedural_turret_reserves_local_circle_internal(int32_t x,int32_t z,int32_t radius);
int odg_chunk_procedural_turret_state(int64_t chunk_x,int64_t chunk_z,odg_chunk_turret_state *out_state);
int odg_chunk_prepare_procedural_turret_state_internal(int64_t chunk_x,int64_t chunk_z);
int odg_chunk_store_procedural_turret_state(int64_t chunk_x,int64_t chunk_z,const odg_chunk_turret_state *state);
int odg_chunk_mark_procedural_turret_removed(int64_t chunk_x,int64_t chunk_z);
void odg_turrets_stream_refresh(void);
int odg_turret_prepare_procedural_persist(const odg_turret *turret);
int odg_turret_persist_procedural(const odg_turret *turret);
int32_t odg_cell_center_x(uint32_t cell);
int32_t odg_cell_center_z(uint32_t cell);
int odg_world_build(uint64_t seed);
void odg_sim_step(void);
void odg_render_internal(void);
void odg_rebuild_stats(void);
void odg_reset_presentation_rng_internal(void);
void odg_emit_particles(int32_t x, int32_t z, uint32_t color, uint32_t count);
void odg_update_turret_ownership_internal(void);
int odg_turret_drop_candidate_internal(const odg_actor *p, int32_t *out_x, int32_t *out_z);
int odg_world_circle_aabb_overlap_internal(int32_t x,int32_t z,int32_t radius,const odg_obstacle *obstacle);
int odg_world_cell_safe_ground_internal(int64_t gx,int64_t gz);
int odg_world_position_safe_ground_internal(int32_t x,int32_t z);
int odg_position_clear_internal(int32_t x,int32_t z,int32_t radius);
int odg_position_clear_ignoring_resource_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_resource_id);
int odg_position_clear_ignoring_artifact_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_artifact_id);
void odg_actor_apply_damage_internal(uint32_t victim_id,uint32_t killer_id,uint32_t damage,uint32_t reason);

const odg_item_definition *odg_item_definition_internal(uint32_t type_id);
int odg_item_material_variant_valid_internal(uint32_t type_id,uint32_t material_tier);
void odg_inventory_init(odg_inventory *inventory);
uint32_t odg_inventory_capacity(const odg_inventory *inventory);
odg_item_stack *odg_inventory_selected(odg_inventory *inventory);
const odg_item_stack *odg_inventory_selected_const(const odg_inventory *inventory);
int odg_item_stack_empty_internal(const odg_item_stack *stack);
int odg_item_stack_protected_internal(const odg_item_stack *stack);
int odg_item_stack_normalize_internal(odg_item_stack *stack);
int odg_item_stack_metadata_compatible_internal(const odg_item_stack *a,const odg_item_stack *b);
uint32_t odg_item_attack_damage_internal(const odg_item_stack *stack);
int odg_item_attack_profile_valid_internal(uint32_t item_type);
uint32_t odg_item_max_durability_internal(uint32_t type_id,uint32_t material_tier);
void odg_item_wear_internal(odg_item_stack *stack,uint32_t amount);
int odg_slots_add(odg_item_stack *slots,uint32_t capacity,const odg_item_stack *stack);
int odg_slots_remove(odg_item_stack *slots,uint32_t capacity,uint32_t slot,uint32_t quantity,odg_item_stack *removed);
int odg_inventory_add(odg_inventory *inventory,const odg_item_stack *stack);
int odg_inventory_remove_from_slot(odg_inventory *inventory,uint32_t slot,uint32_t quantity,odg_item_stack *removed);
int odg_inventory_find_type(const odg_inventory *inventory,uint32_t type_id,uint32_t material_tier,uint32_t *out_slot);
int odg_inventory_find_capability_internal(const odg_inventory *inventory,uint32_t capability_bits,uint32_t material_tier,uint32_t *out_slot);
uint32_t odg_item_inventory_expander_slots_internal(uint32_t item_type);
int odg_item_inventory_expander_recovery_internal(uint32_t item_type);
int odg_inventory_expanders_validate_internal(void);
int odg_inventory_equip_expander_type_internal(odg_inventory *inventory,uint32_t item_type);
int odg_inventory_equip_first_expander_internal(odg_inventory *inventory);
uint32_t odg_inventory_total(const odg_inventory *inventory,uint32_t type_id,uint32_t material_tier);
int odg_inventory_consume(odg_inventory *inventory,uint32_t type_id,uint32_t material_tier,uint32_t quantity);
uint64_t odg_next_instance_id(void);
int odg_world_pickups_prepare_internal(uint32_t additional);
void odg_world_pickup_deactivate_internal(odg_world_pickup *pickup);
int odg_spawn_world_pickup(const odg_item_stack *stack,int32_t x,int32_t z,uint32_t pickup_cd);
int odg_drop_inventory_slot(uint32_t actor_id,uint32_t slot,uint32_t quantity,uint32_t cooldown);
int odg_turret_profiles_validate_internal(void);
int odg_turret_persisted_profile_validate_internal(uint32_t material_tier,uint32_t mode,uint32_t ammo);
int odg_turret_state_validate_internal(const odg_turret *turret,uint32_t expected_id);
void odg_apply_turret_tier(odg_turret *turret,uint32_t tier,int preserve_ammo);
int odg_turret_place_selected(uint32_t actor_id);
void odg_rebuild_interaction_hint(void);
int odg_command_validate_internal(const odg_command *command);
int odg_command_queue_state_validate_internal(const odg_command_queue *queue);
int odg_actor_request_ready_respawn_internal(uint32_t actor_id);
void odg_process_commands(void);
void odg_handle_interaction(void);
void odg_update_world_pickups(void);
void odg_resources_build(void);
void odg_resources_stream_refresh(void);
void odg_resources_migrate_canonical_worldgen_internal(void);
int odg_resources_migrate_identity_worldgen_internal(void);
void odg_resources_tick(void);
int odg_resource_position_blocked(int32_t x,int32_t z,int32_t radius);
int odg_resource_position_blocked_ignoring_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_resource_id);
int32_t odg_resource_collision_radius_fx_internal(const odg_resource_node *resource);
uint32_t odg_resource_physical_height_milli_internal(const odg_resource_node *resource);
int odg_resource_turret_hit(uint64_t stable_id,uint32_t turret_tier);
int odg_resource_build_hint(const odg_actor *actor,odg_interaction_hint *hint);
int odg_resource_hold_tick(uint32_t actor_id,uint32_t resource_id);
const odg_flora_species_definition *odg_resource_flora_definition_internal(const odg_resource_node *resource);
int odg_resource_is_flora_internal(const odg_resource_node *resource);
int odg_resource_profiles_validate_internal(void);
int odg_resource_state_validate_internal(const odg_resource_node *resource,uint32_t expected_id);
uint32_t odg_resource_harvest_item_type_internal(const odg_resource_node *resource);
uint32_t odg_resource_harvest_material_internal(const odg_resource_node *resource);
uint32_t odg_resource_harvest_tool_capability_internal(const odg_resource_node *resource);
uint32_t odg_resource_harvest_tool_type_internal(const odg_resource_node *resource);
uint32_t odg_resource_harvest_min_tool_tier_internal(const odg_resource_node *resource);
int odg_resource_harvest_allows_hand_internal(const odg_resource_node *resource);
void odg_artifacts_build_initial(void);
void odg_artifacts_tick(void);
int odg_artifact_item_deployable_internal(uint32_t item_type);
int odg_artifact_profiles_validate_internal(void);
int odg_artifact_state_validate_internal(const odg_artifact *artifact,uint32_t expected_id);
int odg_artifact_cross_reference_validate_internal(void);
int odg_artifact_position_blocked(int32_t x,int32_t z,int32_t radius,uint32_t ignore_id);
uint32_t odg_artifact_light_permille_internal(int32_t x,int32_t z);
int odg_artifact_placement_candidate_for_item_internal(const odg_actor *actor,uint32_t item_type,int32_t *out_x,int32_t *out_z);
uint32_t odg_artifact_actor_vehicle_internal(uint32_t actor_id);
int odg_artifact_actor_can_access_internal(uint32_t actor_id,const odg_artifact *artifact);
int odg_artifact_vehicle_toggle_internal(uint32_t actor_id,uint32_t artifact_id);
int odg_artifact_vehicle_can_move_actor_internal(const odg_actor *actor,int32_t dx,int32_t dz);
int odg_artifact_vehicle_move_actor_internal(odg_actor *actor,int32_t dx,int32_t dz);
int odg_artifact_surface_allows_item_internal(uint32_t item_type,int32_t x,int32_t z);
uint32_t odg_artifact_physical_height_milli_internal(const odg_artifact *artifact);
int32_t odg_artifact_collision_radius_fx_internal(const odg_artifact *artifact);
int odg_artifact_build_hint(const odg_actor *actor,const odg_item_stack *selected,odg_interaction_hint *hint);
int odg_artifact_execute_tap(uint32_t actor_id,const odg_interaction_hint *hint);
int odg_artifact_execute_hold(uint32_t actor_id,const odg_interaction_hint *hint);
int odg_artifact_place_selected(uint32_t actor_id);
int odg_construction_profiles_validate_internal(void);
int odg_construction_shape_valid_internal(uint32_t shape);
uint32_t odg_construction_selected_shape_internal(uint32_t actor_id);
int odg_construction_set_shape_internal(uint32_t actor_id,uint32_t shape);
int odg_construction_loaded_state_validate_internal(uint32_t source_schema);
int odg_construction_position_blocked_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_id);
uint32_t odg_construction_shape_height_milli_internal(uint32_t shape);
uint32_t odg_construction_physical_height_milli_internal(const odg_construction_block *block);
int32_t odg_construction_collision_radius_fx_internal(const odg_construction_block *block);
int32_t odg_construction_airspace_radius_fx_internal(const odg_construction_block *block);
uint32_t odg_airspace_required_offset_milli_internal(int32_t x,int32_t z,uint32_t radius_milli,uint32_t clearance_milli);
int odg_construction_placement_candidate_internal(const odg_actor *actor,int32_t *out_x,int32_t *out_z,int64_t *out_gx,int64_t *out_gz);
int odg_construction_place_selected_at_global_cell_internal(uint32_t actor_id,int64_t gx,int64_t gz);
int odg_construction_place_selected_internal(uint32_t actor_id);
int odg_construction_build_hint_internal(const odg_actor *actor,const odg_item_stack *selected,odg_interaction_hint *hint);
int odg_construction_execute_tap_internal(uint32_t actor_id,const odg_interaction_hint *hint);
int odg_construction_execute_hold_internal(uint32_t actor_id,const odg_interaction_hint *hint);
int odg_construction_dismantle_internal(uint32_t actor_id,uint32_t construction_id);
int odg_construction_actor_may_dismantle_internal(uint32_t actor_id,uint32_t construction_id);
uint32_t odg_construction_max_health_internal(uint32_t material_tier,uint32_t shape);
int odg_construction_apply_damage_internal(uint32_t attacker_id,uint32_t construction_id,uint32_t damage);
int odg_construction_repair_internal(uint32_t actor_id,uint32_t construction_id);
void odg_construction_sync_globals_from_local_internal(void);
void odg_construction_refresh_local_cache_internal(void);
void odg_construction_reset_runtime_internal(void);
void odg_construction_release_runtime_internal(void);
int odg_construction_import_legacy_artifact_internal(const odg_artifact *artifact);
int odg_bot_economy_direction_internal(odg_actor *actor,int32_t *out_x,int32_t *out_z);
int odg_bot_route_requires_raft_internal(const odg_actor *actor,int32_t target_x,int32_t target_z);
int odg_bot_logistics_prepare_vehicle_internal(odg_actor *actor,int32_t target_x,int32_t target_z,int32_t *out_x,int32_t *out_z);
int odg_crafting_station_near_actor(uint32_t actor_id,uint32_t station_item_type,uint32_t *out_artifact_id);
void odg_music_decay_visual_tick(void);
uint32_t odg_music_beat_q16_internal(void);
uint32_t odg_avatar_texture_sample_lod_internal(uint32_t actor_id,uint32_t face,uint32_t u_q16,uint32_t v_q16,uint32_t lod);
int odg_glyph5x7_internal(char ch,uint8_t rows[7]);

const odg_food_definition *odg_food_definition_internal(uint32_t item_type);
const odg_fluid_definition *odg_fluid_definition_internal(uint32_t fluid_id);
const odg_fluid_container_definition *odg_fluid_container_definition_internal(uint32_t item_type);
uint32_t odg_fluid_payload_id_internal(uint64_t payload_id);
uint32_t odg_fluid_payload_units_internal(uint64_t payload_id);
uint64_t odg_fluid_payload_make_internal(uint32_t fluid_id,uint32_t units);
int odg_actor_consume_food_internal(uint32_t actor_id,uint32_t slot);
int odg_actor_drink_fluid_internal(uint32_t actor_id,uint32_t fluid_id,uint32_t available_units,uint32_t *out_used_units);
int odg_actor_drink_selected_internal(uint32_t actor_id);
int odg_actor_drink_environment_internal(uint32_t actor_id);
void odg_nutrition_tick(void);
const odg_flora_species_definition *odg_flora_species_internal(uint32_t species_id);
int32_t odg_flora_collision_radius_fx_internal(const odg_flora_species_definition *definition,uint32_t stage);
const odg_flora_species_definition *odg_flora_species_for_seed_internal(uint32_t seed_item_type,uint64_t payload_id);
const odg_flora_species_definition *odg_flora_species_for_fruit_internal(uint32_t fruit_item_type,uint64_t payload_id);
const odg_flora_species_definition *odg_flora_worldgen_species_internal(uint32_t growth_form,int32_t x,int32_t z,uint64_t entropy);
void odg_ecology_init_resource(odg_resource_node *resource,uint32_t species_id,uint32_t stage,uint32_t variant);
void odg_ecology_tick(void);
int odg_ecology_gather_fruit(uint32_t actor_id,uint32_t resource_id);
int odg_ecology_plant_selected(uint32_t actor_id);
int odg_ecology_irrigate_nearest(uint32_t actor_id,uint32_t requested_units,uint32_t *out_used);
int odg_resource_spawn_flora(uint32_t species_id,uint32_t stage,uint32_t variant,int32_t x,int32_t z,uint32_t bootstrap_actor_id);
const odg_fauna_species_definition *odg_fauna_species_internal(uint32_t species_id);
const odg_fauna_diet_definition *odg_fauna_diet_internal(uint32_t species_id,uint32_t item_type);
const odg_fauna_habitat_definition *odg_fauna_habitat_internal(uint32_t species_id);
const odg_fauna_nesting_definition *odg_fauna_nesting_internal(uint32_t species_id);
int odg_fauna_profiles_validate_internal(void);
int odg_fauna_tree_nest_position_internal(const odg_resource_node *host,int32_t *out_x,int32_t *out_z);
void odg_fauna_build_initial(void);
void odg_fauna_tick(void);
void odg_fauna_refresh_local_cache(void);
void odg_fauna_shift_local(int32_t shift_x,int32_t shift_z);
int odg_fauna_build_hint(const odg_actor *actor,const odg_item_stack *selected,odg_interaction_hint *hint);
int odg_fauna_hunt(uint32_t actor_id,uint32_t fauna_id);
int odg_territory_allows_environment_action(uint32_t actor_id,int32_t x,int32_t z);
int odg_territory_actor_controls_position(uint32_t actor_id,int32_t x,int32_t z);
int odg_content_registry_validate(void);
int odg_recipe_profiles_validate_internal(void);
uint32_t odg_recipe_find_output_internal(uint32_t item_type,uint32_t material_tier);
int odg_artifact_create_death_cache(uint32_t actor_id);
int odg_artifact_is_death_cache(const odg_artifact *artifact);
int odg_artifact_recover_death_cache(uint32_t actor_id,uint32_t artifact_id);

#endif
