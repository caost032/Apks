#ifndef ODPAR_GAME_H
#define ODPAR_GAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODG_API_VERSION UINT32_C(37)
#define ODG_MAX_RENDER_WIDTH UINT32_C(1280)
#define ODG_MAX_RENDER_HEIGHT UINT32_C(1280)
#define ODG_MAX_RENDER_PIXELS (UINT32_C(1280) * UINT32_C(720))
#define ODG_TICK_RATE UINT32_C(120)

/* Stable host-discovery contract. The game API and the FFI schema evolve
 * independently: hosts query both before reading any POD snapshot. */
#define ODG_FFI_ABI_VERSION UINT32_C(9)
#define ODG_FFI_ENDIAN_MARKER UINT32_C(0x01020304)
#define ODG_PIXEL_FORMAT_RGBA8 UINT32_C(1)
#define ODG_FFI_FEATURE_FRAMEBUFFER_PTR  (UINT64_C(1) << 0)
#define ODG_FFI_FEATURE_FRAMEBUFFER_COPY (UINT64_C(1) << 1)
#define ODG_FFI_FEATURE_STATS_PTR        (UINT64_C(1) << 2)
#define ODG_FFI_FEATURE_STATS_COPY       (UINT64_C(1) << 3)
#define ODG_FFI_FEATURE_PORTRAIT_RENDER  (UINT64_C(1) << 4)
#define ODG_FFI_FEATURE_FIXED_120HZ      (UINT64_C(1) << 5)
#define ODG_FFI_FEATURE_CAMERA_INPUT     (UINT64_C(1) << 6)
#define ODG_FFI_FEATURE_GENERIC_ITEMS    (UINT64_C(1) << 7)
#define ODG_FFI_FEATURE_INVENTORY_QUERY  (UINT64_C(1) << 8)
#define ODG_FFI_FEATURE_COMMAND_QUEUE    (UINT64_C(1) << 9)
#define ODG_FFI_FEATURE_INTERACTION_HINT (UINT64_C(1) << 10)
#define ODG_FFI_FEATURE_MUSIC_ANALYZER   (UINT64_C(1) << 11)
#define ODG_FFI_FEATURE_RESOURCES        (UINT64_C(1) << 12)
#define ODG_FFI_FEATURE_CRAFTING         (UINT64_C(1) << 13)
#define ODG_FFI_FEATURE_ARTIFACTS        (UINT64_C(1) << 14)
#define ODG_FFI_FEATURE_SAVE_LOAD        (UINT64_C(1) << 15)
#define ODG_FFI_FEATURE_MAP_QUERY        (UINT64_C(1) << 16)
#define ODG_FFI_FEATURE_TEXTURE_UPLOAD   (UINT64_C(1) << 17)
#define ODG_FFI_FEATURE_AVATAR_SKINS     (UINT64_C(1) << 18)
#define ODG_FFI_FEATURE_CHUNK_WORLDGEN    (UINT64_C(1) << 19)
#define ODG_FFI_FEATURE_CAMERA_PROFILE     (UINT64_C(1) << 20)
#define ODG_FFI_FEATURE_REMOTE_VIEW        (UINT64_C(1) << 21)
#define ODG_FFI_FEATURE_CHUNK_RUNTIME       (UINT64_C(1) << 22)
#define ODG_FFI_FEATURE_TURRET_MODES        (UINT64_C(1) << 23)
#define ODG_FFI_FEATURE_ARTIFACT_PAGING      (UINT64_C(1) << 24)
#define ODG_FFI_FEATURE_ECOLOGY             (UINT64_C(1) << 25)
#define ODG_FFI_FEATURE_NUTRITION           (UINT64_C(1) << 26)
#define ODG_FFI_FEATURE_FAUNA               (UINT64_C(1) << 27)
#define ODG_FFI_FEATURE_TERRAIN_SURFACE     (UINT64_C(1) << 28)
#define ODG_FFI_FEATURE_WEATHER             (UINT64_C(1) << 29)
#define ODG_FFI_FEATURE_DEATH_RECOVERY      (UINT64_C(1) << 30)
#define ODG_FFI_FEATURE_TERRITORY_POLICY    (UINT64_C(1) << 31)
#define ODG_FFI_FEATURE_TRAIL_BREAK         (UINT64_C(1) << 32)
#define ODG_FFI_FEATURE_HYDRATION           (UINT64_C(1) << 33)
#define ODG_FFI_FEATURE_FLUID_REGISTRY      (UINT64_C(1) << 34)
#define ODG_FFI_FEATURE_SAVE_SCHEMA_QUERY    (UINT64_C(1) << 35)
#define ODG_FFI_FEATURE_FAUNA_NESTING_REGISTRY (UINT64_C(1) << 36)
#define ODG_FFI_FEATURE_GEOLOGY              (UINT64_C(1) << 37)
#define ODG_FFI_FEATURE_DAY_NIGHT            (UINT64_C(1) << 38)
#define ODG_FFI_FEATURE_RESPIRATION          (UINT64_C(1) << 39)
#define ODG_FFI_FEATURE_CONSTRUCTION_BLOCKS   (UINT64_C(1) << 40)
#define ODG_FFI_FEATURE_CONSTRUCTION_SHAPES   (UINT64_C(1) << 41)
#define ODG_FFI_FEATURE_CONSTRUCTION_DURABILITY (UINT64_C(1) << 42)

#define ODG_AVATAR_FACE_FRONT UINT32_C(0)
#define ODG_AVATAR_FACE_RIGHT UINT32_C(1)
#define ODG_AVATAR_FACE_BACK UINT32_C(2)
#define ODG_AVATAR_FACE_LEFT UINT32_C(3)
#define ODG_AVATAR_FACE_TOP UINT32_C(4)
#define ODG_AVATAR_FACE_BOTTOM UINT32_C(5)
#define ODG_AVATAR_FACE_COUNT UINT32_C(6)
#define ODG_AVATAR_TEXTURE_SIZE UINT32_C(256)

#define ODG_VISUAL_THEME_NEON_TIDES     UINT32_C(0)
#define ODG_VISUAL_THEME_EMERALD_CROWN  UINT32_C(1)
#define ODG_VISUAL_THEME_SOLAR_EMBER    UINT32_C(2)
#define ODG_VISUAL_THEME_OBSIDIAN_PULSE UINT32_C(3)
#define ODG_VISUAL_THEME_COUNT          UINT32_C(4)

#define ODG_PRESENTATION_GAMEPLAY UINT32_C(0)
#define ODG_PRESENTATION_SHOWCASE UINT32_C(1)

#define ODG_CAMERA_MODE_FIRST_PERSON UINT32_C(0)
#define ODG_CAMERA_MODE_CLOSE UINT32_C(1)
#define ODG_CAMERA_MODE_MEDIUM UINT32_C(2)
#define ODG_CAMERA_MODE_FAR UINT32_C(3)
#define ODG_CAMERA_MODE_COUNT UINT32_C(4)
#define ODG_MUSIC_REACTIVITY_MAX_Q16 UINT32_C(98303) /* 150% */
#define ODG_REMOTE_VIEW_WIDTH UINT32_C(320)
#define ODG_REMOTE_VIEW_HEIGHT UINT32_C(180)
#define ODG_AVATAR_PREVIEW_SIZE UINT32_C(256)

/* FIRE remains reserved for ABI compatibility. Territory gameplay does not damage actors. */
#define ODG_BUTTON_FIRE    (UINT32_C(1) << 0)
#define ODG_BUTTON_DASH    (UINT32_C(1) << 1)
#define ODG_BUTTON_RESTART (UINT32_C(1) << 2)
#define ODG_BUTTON_INTERACT (UINT32_C(1) << 3) /* universal press/hold/release interaction */
#define ODG_BUTTON_ACTION ODG_BUTTON_INTERACT /* source compatibility alias */
#define ODG_BUTTON_DROP    (UINT32_C(1) << 4) /* drop currently carried world item */
#define ODG_BUTTON_JUMP    (UINT32_C(1) << 5) /* vertical traversal over ledges/steps */

#define ODG_STATUS_OK 0
#define ODG_STATUS_INVALID_ARGUMENT 1
#define ODG_STATUS_INVALID_STATE 2
#define ODG_STATUS_UNSUPPORTED 3
#define ODG_STATUS_BUFFER_TOO_SMALL 4
#define ODG_STATUS_VERSION_MISMATCH 5

#define ODG_DEATH_NONE 0u
#define ODG_DEATH_TRAIL_CUT 1u
#define ODG_DEATH_SELF_CROSS 2u /* legacy; self trails are non-lethal in API v8 */
#define ODG_DEATH_BOUNDARY 3u
#define ODG_DEATH_TERRITORY_LOST 4u
#define ODG_DEATH_TURRET_TRAIL_CUT 5u
#define ODG_DEATH_STARVATION 6u
#define ODG_DEATH_FAUNA 7u
#define ODG_DEATH_COMBAT 8u
#define ODG_DEATH_DEHYDRATION 9u
#define ODG_DEATH_DROWNING 10u
#define ODG_DEATH_MONSTER 11u


/* -------------------------------------------------------------------------
 * API 20 generic item / inventory / interaction / ecosystem contract. Gameplay rules
 * remain authoritative in C; Flutter reads snapshots and submits commands.
 * ------------------------------------------------------------------------- */
#define ODG_INVENTORY_BASE_SLOTS UINT32_C(4)
#define ODG_INVENTORY_BACKPACK_SLOTS UINT32_C(8)
#define ODG_INVENTORY_MAX_SLOTS UINT32_C(12)
#define ODG_MANUAL_DROP_REPICKUP_TICKS UINT32_C(90)
#define ODG_INTERACT_TAP_MAX_TICKS UINT32_C(34)
#define ODG_INTERACT_HOLD_TICKS UINT32_C(66)

#define ODG_ITEM_CATEGORY_NONE UINT32_C(0)
#define ODG_ITEM_CATEGORY_RESOURCE UINT32_C(1)
#define ODG_ITEM_CATEGORY_TOOL UINT32_C(2)
#define ODG_ITEM_CATEGORY_TECH UINT32_C(3)
#define ODG_ITEM_CATEGORY_DEPLOYABLE UINT32_C(4)
#define ODG_ITEM_CATEGORY_EQUIPMENT UINT32_C(5)
#define ODG_ITEM_CATEGORY_FOOD UINT32_C(6)
#define ODG_ITEM_CATEGORY_SEED UINT32_C(7)

#define ODG_MATERIAL_NONE UINT32_C(0)
#define ODG_MATERIAL_WOOD UINT32_C(1)
#define ODG_MATERIAL_STONE UINT32_C(2)
#define ODG_MATERIAL_IRON UINT32_C(3)

#define ODG_ITEM_NONE UINT32_C(0)
#define ODG_ITEM_WOOD UINT32_C(1)
#define ODG_ITEM_STONE UINT32_C(2)
#define ODG_ITEM_IRON UINT32_C(3)
#define ODG_ITEM_AMMO UINT32_C(4)
#define ODG_ITEM_REPROGRAM_CHIP UINT32_C(5)
#define ODG_ITEM_ASCENSION_CHIP UINT32_C(6)
#define ODG_ITEM_AXE UINT32_C(7)
#define ODG_ITEM_PICKAXE UINT32_C(8)
#define ODG_ITEM_TURRET UINT32_C(9)
#define ODG_ITEM_WORKBENCH UINT32_C(10)
#define ODG_ITEM_SMITHY UINT32_C(11)
#define ODG_ITEM_CHEST UINT32_C(12)
#define ODG_ITEM_BACKPACK UINT32_C(13)
#define ODG_ITEM_APPLE UINT32_C(14)
#define ODG_ITEM_APPLE_SEED UINT32_C(15)
#define ODG_ITEM_BIRD_TRAP UINT32_C(16)
#define ODG_ITEM_LEATHER UINT32_C(17)
#define ODG_ITEM_RAW_MEAT UINT32_C(18)
#define ODG_ITEM_HUNTING_KNIFE UINT32_C(19)
#define ODG_ITEM_SWORD UINT32_C(20)
#define ODG_ITEM_WATER_FLASK UINT32_C(21)
#define ODG_ITEM_RAIN_BARREL UINT32_C(22)
#define ODG_ITEM_COAL UINT32_C(23)
#define ODG_ITEM_TORCH UINT32_C(24)
#define ODG_ITEM_NIGHT_SHARD UINT32_C(25)
#define ODG_ITEM_RAW_FISH UINT32_C(26)
#define ODG_ITEM_BUILDING_BLOCK UINT32_C(27)
#define ODG_ITEM_RAFT UINT32_C(28)
#define ODG_ITEM_TYPE_COUNT UINT32_C(29)

#define ODG_ITEM_FLAG_TOOL (UINT32_C(1) << 0)
#define ODG_ITEM_FLAG_RESOURCE (UINT32_C(1) << 1)
#define ODG_ITEM_FLAG_CHIP (UINT32_C(1) << 2)
#define ODG_ITEM_FLAG_ARTIFACT (UINT32_C(1) << 3)
#define ODG_ITEM_FLAG_DURABILITY (UINT32_C(1) << 4)
#define ODG_ITEM_FLAG_PROTECTED (UINT32_C(1) << 5)
#define ODG_ITEM_FLAG_FOOD (UINT32_C(1) << 6)
#define ODG_ITEM_FLAG_SEED (UINT32_C(1) << 7)

/* Capability bits are host-visible hints; C remains the only gameplay authority. */
#define ODG_ITEM_CAP_HARVEST UINT32_C(1)
#define ODG_ITEM_CAP_MINE UINT32_C(2)
#define ODG_ITEM_CAP_REPROGRAM UINT32_C(4)
#define ODG_ITEM_CAP_UPGRADE UINT32_C(8)
#define ODG_ITEM_CAP_PLACE UINT32_C(16)
#define ODG_ITEM_CAP_STORE UINT32_C(32)
#define ODG_ITEM_CAP_EXPAND_INVENTORY UINT32_C(64)
#define ODG_ITEM_CAP_CONSUME UINT32_C(128)
#define ODG_ITEM_CAP_PLANT UINT32_C(256)
#define ODG_ITEM_CAP_HUNT UINT32_C(512)
#define ODG_ITEM_CAP_ATTACK UINT32_C(1024)
#define ODG_ITEM_CAP_COLLECT_WATER UINT32_C(2048)
#define ODG_ITEM_CAP_IRRIGATE UINT32_C(4096)
#define ODG_ITEM_CAP_DRINK UINT32_C(8192)
#define ODG_ITEM_CAP_REFILL_TURRET UINT32_C(16384)
#define ODG_ITEM_CAP_CONSTRUCT UINT32_C(32768)

#define ODG_INTERACTION_NONE UINT32_C(0)
#define ODG_INTERACTION_PICKUP UINT32_C(1)
#define ODG_INTERACTION_PLACE UINT32_C(2)
#define ODG_INTERACTION_REPROGRAM UINT32_C(3)
#define ODG_INTERACTION_UPGRADE UINT32_C(4)
#define ODG_INTERACTION_REFILL UINT32_C(5)
#define ODG_INTERACTION_PICKUP_ARTIFACT UINT32_C(6)
#define ODG_INTERACTION_HARVEST UINT32_C(7)
#define ODG_INTERACTION_OPEN_ARTIFACT UINT32_C(8)
#define ODG_INTERACTION_PLACE_ARTIFACT UINT32_C(9)
#define ODG_INTERACTION_GATHER_FRUIT UINT32_C(10)
#define ODG_INTERACTION_HUNT_FAUNA UINT32_C(11)
#define ODG_INTERACTION_ATTACK_ACTOR UINT32_C(12)
#define ODG_INTERACTION_COLLECT_WATER UINT32_C(13)
#define ODG_INTERACTION_IRRIGATE UINT32_C(14)
#define ODG_INTERACTION_DRINK_WATER UINT32_C(15)
#define ODG_INTERACTION_USE_VEHICLE UINT32_C(16)
#define ODG_INTERACTION_PLACE_CONSTRUCTION UINT32_C(17)
#define ODG_INTERACTION_DISMANTLE_CONSTRUCTION UINT32_C(18)
#define ODG_INTERACTION_REPAIR_CONSTRUCTION UINT32_C(19)
#define ODG_INTERACTION_ATTACK_CONSTRUCTION UINT32_C(20)

#define ODG_INTERACTION_TARGET_NONE UINT32_C(0)
#define ODG_INTERACTION_TARGET_PICKUP UINT32_C(1)
#define ODG_INTERACTION_TARGET_TURRET UINT32_C(2)
#define ODG_INTERACTION_TARGET_RESOURCE UINT32_C(3)
#define ODG_INTERACTION_TARGET_ARTIFACT UINT32_C(4)
#define ODG_INTERACTION_TARGET_FAUNA UINT32_C(5)
#define ODG_INTERACTION_TARGET_ACTOR UINT32_C(6)
#define ODG_INTERACTION_TARGET_SURFACE UINT32_C(7)
#define ODG_INTERACTION_TARGET_CONSTRUCTION UINT32_C(8)

#define ODG_COMMAND_NONE UINT32_C(0)
#define ODG_COMMAND_SELECT_SLOT UINT32_C(1)
#define ODG_COMMAND_DROP_SELECTED UINT32_C(2)
#define ODG_COMMAND_PLACE_SELECTED UINT32_C(3)
#define ODG_COMMAND_USE_SELECTED UINT32_C(4)
#define ODG_COMMAND_EQUIP_BACKPACK UINT32_C(5)
#define ODG_COMMAND_REQUEST_RESPAWN UINT32_C(6)
#define ODG_COMMAND_MOVE_SLOT UINT32_C(7)
#define ODG_COMMAND_CRAFT UINT32_C(8)
#define ODG_COMMAND_REPAIR_SELECTED UINT32_C(9)
#define ODG_COMMAND_CLOSE_ARTIFACT UINT32_C(10)
#define ODG_COMMAND_STORAGE_DEPOSIT UINT32_C(11)
#define ODG_COMMAND_STORAGE_WITHDRAW UINT32_C(12)
#define ODG_COMMAND_SET_TURRET_MODE UINT32_C(13)
#define ODG_COMMAND_CONSUME_SELECTED UINT32_C(14)
#define ODG_COMMAND_PLANT_SELECTED UINT32_C(15)
#define ODG_COMMAND_DRINK_SELECTED UINT32_C(16)
#define ODG_COMMAND_SET_CONSTRUCTION_SHAPE UINT32_C(17)

#define ODG_TURRET_MODE_DEFENSE UINT32_C(0)
#define ODG_TURRET_MODE_HARVEST UINT32_C(1)
#define ODG_TURRET_ARTIFACT_STATE_MAX_AMMO_MASK UINT32_C(0x0000ffff)
#define ODG_TURRET_ARTIFACT_STATE_MODE_SHIFT UINT32_C(16)



#define ODG_RESOURCE_TREE UINT32_C(1)
#define ODG_RESOURCE_STONE UINT32_C(2)
#define ODG_RESOURCE_IRON UINT32_C(3)
#define ODG_RESOURCE_FLORA UINT32_C(4)
#define ODG_RESOURCE_COAL UINT32_C(5)
#define ODG_RESOURCE_STATE_AVAILABLE UINT32_C(1)
#define ODG_RESOURCE_STATE_DEPLETED UINT32_C(2)

#define ODG_STATION_NONE UINT32_C(0)
#define ODG_STATION_WORKBENCH ODG_ITEM_WORKBENCH
#define ODG_STATION_SMITHY ODG_ITEM_SMITHY

#define ODG_RECIPE_MAX_INGREDIENTS UINT32_C(4)
#define ODG_RECIPE_AXE_WOOD UINT32_C(1)
#define ODG_RECIPE_AXE_STONE UINT32_C(2)
#define ODG_RECIPE_PICKAXE_WOOD UINT32_C(3)
#define ODG_RECIPE_PICKAXE_STONE UINT32_C(4)
#define ODG_RECIPE_BACKPACK UINT32_C(5)
#define ODG_RECIPE_CHEST UINT32_C(6)
#define ODG_RECIPE_SMITHY UINT32_C(7)
#define ODG_RECIPE_AXE_IRON UINT32_C(8)
#define ODG_RECIPE_PICKAXE_IRON UINT32_C(9)
#define ODG_RECIPE_TURRET_WOOD UINT32_C(10)
#define ODG_RECIPE_TURRET_STONE UINT32_C(11)
#define ODG_RECIPE_TURRET_IRON UINT32_C(12)
#define ODG_RECIPE_REPROGRAM_WOOD UINT32_C(13)
#define ODG_RECIPE_REPROGRAM_STONE UINT32_C(14)
#define ODG_RECIPE_REPROGRAM_IRON UINT32_C(15)
#define ODG_RECIPE_AMMO_X12 UINT32_C(16)
#define ODG_RECIPE_ASCEND_STONE UINT32_C(17)
#define ODG_RECIPE_ASCEND_IRON UINT32_C(18)
#define ODG_RECIPE_BIRD_TRAP UINT32_C(19)
#define ODG_RECIPE_HUNTING_KNIFE UINT32_C(20)
#define ODG_RECIPE_SWORD_WOOD UINT32_C(21)
#define ODG_RECIPE_SWORD_STONE UINT32_C(22)
#define ODG_RECIPE_SWORD_IRON UINT32_C(23)
#define ODG_RECIPE_WATER_FLASK UINT32_C(24)
#define ODG_RECIPE_RAIN_BARREL UINT32_C(25)
#define ODG_RECIPE_TORCH_X4 UINT32_C(26)
#define ODG_RECIPE_BUILD_BLOCK_WOOD UINT32_C(27)
#define ODG_RECIPE_BUILD_BLOCK_STONE UINT32_C(28)
#define ODG_RECIPE_BUILD_BLOCK_IRON UINT32_C(29)
#define ODG_RECIPE_RAFT UINT32_C(30)
#define ODG_RECIPE_COUNT UINT32_C(30)

#define ODG_ARTIFACT_CAP_OPEN_UI (UINT32_C(1) << 0)
#define ODG_ARTIFACT_CAP_MOVE (UINT32_C(1) << 1)
#define ODG_ARTIFACT_CAP_STORE (UINT32_C(1) << 2)
#define ODG_ARTIFACT_CAP_REMOTE_VIEW (UINT32_C(1) << 3)
#define ODG_ARTIFACT_CAP_FIRE (UINT32_C(1) << 4)
#define ODG_ARTIFACT_CAP_HARVEST (UINT32_C(1) << 5)
#define ODG_ARTIFACT_CAP_UPGRADE (UINT32_C(1) << 6)
#define ODG_ARTIFACT_CAP_TAME (UINT32_C(1) << 7)
#define ODG_ARTIFACT_CAP_COLLECT_RAIN (UINT32_C(1) << 8)
#define ODG_ARTIFACT_CAP_LIGHT (UINT32_C(1) << 9)
#define ODG_ARTIFACT_CAP_CONSTRUCTION (UINT32_C(1) << 10)
#define ODG_ARTIFACT_CAP_VEHICLE (UINT32_C(1) << 11)
#define ODG_ARTIFACT_STATE_PROTECTED (UINT32_C(1) << 0)
#define ODG_ARTIFACT_STATE_DEATH_CACHE (UINT32_C(1) << 1)

#define ODG_ARTIFACT_MAX_ENTRIES UINT32_C(64)
#define ODG_RESOURCE_MAX_ENTRIES UINT32_C(128)
#define ODG_CHEST_SLOTS UINT32_C(24)


#define ODG_CHUNK_SIZE_CELLS INT32_C(32)
#define ODG_BIOME_PLAIN UINT32_C(1)
#define ODG_BIOME_FOREST UINT32_C(2)
#define ODG_BIOME_ROCKY UINT32_C(3)
#define ODG_BIOME_HIGHLANDS UINT32_C(4)
#define ODG_BIOME_WETLAND UINT32_C(5)
#define ODG_CHUNK_STATE_UNSEEN UINT32_C(0)
#define ODG_CHUNK_STATE_ACTIVE UINT32_C(1)
#define ODG_CHUNK_STATE_DIRTY UINT32_C(2)
#define ODG_CHUNK_STATE_SLEEPING UINT32_C(3)

typedef struct {
    uint32_t struct_size;
    int64_t chunk_x;
    int64_t chunk_z;
    uint64_t stable_id;
    uint32_t biome;
    uint32_t tree_count;
    uint32_t stone_count;
    uint32_t iron_count;
    uint32_t has_procedural_turret;
    uint32_t turret_material_tier;
    int32_t corner_height_milli[4]; /* NW, NE, SW, SE from global seam-safe height */
    int32_t center_height_milli;
    uint32_t reserved_u32[3];
} odg_chunk_descriptor;

#define ODG_SAVE_SCHEMA_VERSION UINT32_C(25)
#define ODG_MAP_MAX_RESOLUTION UINT32_C(128)
#define ODG_MAP_MAX_MARKERS UINT32_C(96)
#define ODG_MAP_FLAG_PLAYABLE (UINT32_C(1) << 0)
#define ODG_MAP_FLAG_TRAIL (UINT32_C(1) << 1)
#define ODG_MAP_FLAG_RESOURCE (UINT32_C(1) << 2)
#define ODG_MAP_FLAG_ARTIFACT (UINT32_C(1) << 3)
#define ODG_MAP_FLAG_WATER (UINT32_C(1) << 4)
#define ODG_MAP_FLAG_STEEP (UINT32_C(1) << 5)
#define ODG_MAP_FLAG_FAUNA (UINT32_C(1) << 6)
#define ODG_MAP_FLAG_CONSTRUCTION (UINT32_C(1) << 7)
#define ODG_MAP_MARKER_ACTOR UINT32_C(1)
#define ODG_MAP_MARKER_TURRET UINT32_C(2)
#define ODG_MAP_MARKER_ARTIFACT UINT32_C(3)
#define ODG_MAP_MARKER_CONSTRUCTION UINT32_C(4)

/* Ecosystem/content IDs are append-only. Never recycle a removed ID; reserve it. */
#define ODG_FLORA_SPECIES_APPLE_TREE UINT32_C(1)
#define ODG_FLORA_STAGE_SEEDLING UINT32_C(1)
#define ODG_FLORA_STAGE_SAPLING UINT32_C(2)
#define ODG_FLORA_STAGE_YOUNG UINT32_C(3)
#define ODG_FLORA_STAGE_MATURE UINT32_C(4)
#define ODG_FLORA_STAGE_OLD UINT32_C(5)
#define ODG_FLORA_MAX_STAGES UINT32_C(5)
#define ODG_FLORA_GROWTH_TREE UINT32_C(1)
#define ODG_FLORA_GROWTH_SHRUB UINT32_C(2)
#define ODG_FLORA_GROWTH_HERB UINT32_C(3)
#define ODG_FLORA_GROWTH_CROP UINT32_C(4)
#define ODG_FLORA_HARVEST_DESTROYS_PLANT (UINT32_C(1) << 0)
#define ODG_FLORA_HARVEST_TURRET_ELIGIBLE (UINT32_C(1) << 1)
#define ODG_FLORA_HARVEST_BLOCKING (UINT32_C(1) << 2)
#define ODG_FLORA_HARVEST_ALLOW_HAND (UINT32_C(1) << 3)

#define ODG_FAUNA_FAMILY_BIRD UINT32_C(1)
#define ODG_FAUNA_FAMILY_MAMMAL UINT32_C(2)
#define ODG_FAUNA_FAMILY_AQUATIC UINT32_C(3)
#define ODG_FAUNA_FAMILY_MONSTER UINT32_C(4)
#define ODG_FAUNA_FAMILY_REPTILE UINT32_C(5)
#define ODG_FAUNA_SPECIES_ORCHARD_BIRD UINT32_C(1)
#define ODG_FAUNA_SPECIES_FOREST_DEER UINT32_C(2)
#define ODG_FAUNA_SPECIES_MEADOW_RABBIT UINT32_C(3)
#define ODG_FAUNA_SPECIES_FIELD_FOWL UINT32_C(4)
#define ODG_FAUNA_SPECIES_RIVER_FISH UINT32_C(5)
#define ODG_FAUNA_SPECIES_NIGHT_STALKER UINT32_C(6)
#define ODG_FAUNA_SPECIES_MARSH_CROCODILE UINT32_C(7)
#define ODG_FAUNA_STAGE_YOUNG UINT32_C(1)
#define ODG_FAUNA_STAGE_JUVENILE UINT32_C(2)
#define ODG_FAUNA_STAGE_ADULT UINT32_C(3)
#define ODG_FAUNA_STAGE_OLD UINT32_C(4)
#define ODG_FAUNA_SEX_NONE UINT32_C(0)
#define ODG_FAUNA_SEX_FEMALE UINT32_C(1)
#define ODG_FAUNA_SEX_MALE UINT32_C(2)
#define ODG_FAUNA_REPRO_EGG UINT32_C(1)
#define ODG_FAUNA_REPRO_LIVE UINT32_C(2)
#define ODG_FAUNA_REPRO_SPAWN UINT32_C(3)
#define ODG_FAUNA_REPRO_NONE UINT32_C(4)
#define ODG_FAUNA_BEHAVIOR_CAN_FLY (UINT32_C(1) << 0)
#define ODG_FAUNA_BEHAVIOR_FORAGE (UINT32_C(1) << 1)
/* Source-compatible legacy name: bit 1 means habitat/visible-food foraging, not land-only locomotion. */
#define ODG_FAUNA_BEHAVIOR_GROUND_FORAGE ODG_FAUNA_BEHAVIOR_FORAGE
#define ODG_FAUNA_BEHAVIOR_HERD (UINT32_C(1) << 2)
#define ODG_FAUNA_BEHAVIOR_FLEE_ACTORS (UINT32_C(1) << 3)
#define ODG_FAUNA_BEHAVIOR_NESTING (UINT32_C(1) << 4)
#define ODG_FAUNA_BEHAVIOR_TAMEABLE (UINT32_C(1) << 5)
#define ODG_FAUNA_BEHAVIOR_HEAD_PERCH (UINT32_C(1) << 6)
#define ODG_FAUNA_BEHAVIOR_AQUATIC (UINT32_C(1) << 7)
#define ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS (UINT32_C(1) << 8)
#define ODG_FAUNA_BEHAVIOR_NOCTURNAL (UINT32_C(1) << 9)
#define ODG_FAUNA_BEHAVIOR_OUTSIDE_TERRITORY_HUNTER (UINT32_C(1) << 10)
#define ODG_FAUNA_BEHAVIOR_DEFENSIVE_ATTACK (UINT32_C(1) << 11)
#define ODG_FAUNA_BEHAVIOR_AMPHIBIOUS (UINT32_C(1) << 12)
#define ODG_FAUNA_BEHAVIOR_PREDATOR (UINT32_C(1) << 13)
#define ODG_NEST_SUBSTRATE_TREE (UINT32_C(1) << 0)
#define ODG_NEST_SUBSTRATE_GROUND (UINT32_C(1) << 1)
#define ODG_NEST_SUBSTRATE_CLIFF (UINT32_C(1) << 2)
#define ODG_NEST_SUBSTRATE_STRUCTURE (UINT32_C(1) << 3)
#define ODG_FAUNA_MAX_ENTRIES UINT32_C(48)
#define ODG_FAUNA_MAX_NESTS UINT32_C(12)
#define ODG_LOOT_MAX_ENTRIES UINT32_C(4)
#define ODG_LOOT_DEER UINT32_C(1)
#define ODG_LOOT_RABBIT UINT32_C(2)
#define ODG_LOOT_FIELD_FOWL UINT32_C(3)
#define ODG_LOOT_NIGHT_STALKER UINT32_C(4)
#define ODG_LOOT_RIVER_FISH UINT32_C(5)
#define ODG_LOOT_MARSH_CROCODILE UINT32_C(6)

#define ODG_SURFACE_FLAG_WATER (UINT32_C(1) << 0)
#define ODG_SURFACE_FLAG_STEEP (UINT32_C(1) << 1)
#define ODG_SURFACE_FLAG_WET (UINT32_C(1) << 2)
#define ODG_SURFACE_FLAG_MOUNTAIN (UINT32_C(1) << 3)

/* Subsurface geology is queried by depth below the authoritative surface. Ores live in
 * strata/cave volume; surface resource nodes are only geological exposures. */
#define ODG_GEOLOGY_MATERIAL_AIR UINT32_C(0)
#define ODG_GEOLOGY_MATERIAL_TOPSOIL UINT32_C(1)
#define ODG_GEOLOGY_MATERIAL_SUBSOIL UINT32_C(2)
#define ODG_GEOLOGY_MATERIAL_STONE UINT32_C(3)
#define ODG_GEOLOGY_MATERIAL_COAL_ORE UINT32_C(4)
#define ODG_GEOLOGY_MATERIAL_IRON_ORE UINT32_C(5)
#define ODG_GEOLOGY_FLAG_CAVE (UINT32_C(1) << 0)
#define ODG_GEOLOGY_FLAG_ORE (UINT32_C(1) << 1)
#define ODG_DAY_LENGTH_TICKS (UINT32_C(24) * UINT32_C(60) * ODG_TICK_RATE)
#define ODG_WORLDGEN_VERSION_LEGACY UINT32_C(1)
#define ODG_WORLDGEN_VERSION_BATHYMETRY UINT32_C(2)
#define ODG_WORLDGEN_VERSION_SAFE_TURRETS UINT32_C(3)
#define ODG_WORLDGEN_VERSION_CANONICAL_RESOURCES UINT32_C(4)
#define ODG_WORLDGEN_VERSION_RESOURCE_ID_NAMESPACES UINT32_C(5)
#define ODG_WORLDGEN_VERSION_CURRENT ODG_WORLDGEN_VERSION_RESOURCE_ID_NAMESPACES

#define ODG_MESSAGE_NONE UINT32_C(0)
#define ODG_MESSAGE_WRONG_TIER UINT32_C(1)
#define ODG_MESSAGE_WRONG_UPGRADE UINT32_C(2)
#define ODG_MESSAGE_TOOL_REQUIRED UINT32_C(3)
#define ODG_MESSAGE_PICKAXE_TIER_REQUIRED UINT32_C(4)
#define ODG_MESSAGE_INVENTORY_FULL UINT32_C(5)
#define ODG_MESSAGE_OWNER_ONLY UINT32_C(6)
#define ODG_MESSAGE_EMPTY_CHEST_TO_MOVE UINT32_C(7)
#define ODG_MESSAGE_INVALID_PLACEMENT UINT32_C(8)
#define ODG_MESSAGE_STATION_REQUIRED UINT32_C(9)
#define ODG_MESSAGE_MISSING_RESOURCES UINT32_C(10)
#define ODG_MESSAGE_TERRITORY_REQUIRED UINT32_C(11)
#define ODG_MESSAGE_TOO_DRY UINT32_C(12)
#define ODG_MESSAGE_TOO_STEEP UINT32_C(13)
#define ODG_MESSAGE_NOT_HUNGRY UINT32_C(14)
#define ODG_MESSAGE_NOT_THIRSTY UINT32_C(15)

typedef struct {
    uint32_t struct_size;
    uint32_t type_id;
    uint32_t category;
    uint32_t display_code;
    uint32_t max_stack;
    uint32_t default_material_tier;
    uint32_t flags;
    uint32_t base_durability;
    uint32_t capability_bits;
    uint32_t reserved_u32[3];
} odg_item_definition;

#define ODG_FOOD_FLAG_PLANT UINT32_C(1)
#define ODG_FOOD_FLAG_ANIMAL UINT32_C(2)
#define ODG_FOOD_FLAG_RAW UINT32_C(4)
typedef struct {
    uint32_t struct_size;
    uint32_t item_type;
    uint32_t satiety_restore;
    uint32_t hydration_restore;
    uint32_t heal_amount;
    uint32_t flags;
    uint32_t ground_lifetime_ticks;
    uint32_t reserved_u32;
} odg_food_definition;

/* Fluids and their containers are independent registries. Items never encode a
 * particular liquid in their type ID; payload_id carries fluid identity + amount. */
#define ODG_FLUID_NONE UINT32_C(0)
#define ODG_FLUID_WATER UINT32_C(1)
#define ODG_FLUID_FLAG_POTABLE (UINT32_C(1) << 0)
#define ODG_FLUID_FLAG_IRRIGATION (UINT32_C(1) << 1)
#define ODG_FLUID_FLAG_RAIN_SOURCE (UINT32_C(1) << 2)
#define ODG_FLUID_FLAG_NATURAL_SOURCE (UINT32_C(1) << 3)
#define ODG_FLUID_CONTAINER_FLAG_PORTABLE (UINT32_C(1) << 0)
#define ODG_FLUID_CONTAINER_FLAG_SEALED (UINT32_C(1) << 1)
typedef struct {
    uint32_t struct_size;
    uint32_t fluid_id;
    uint32_t display_code;
    uint32_t hydration_restore_per_unit;
    uint32_t flags;
    uint32_t reserved_u32[3];
} odg_fluid_definition;

typedef struct {
    uint32_t struct_size;
    uint32_t item_type;
    uint32_t capacity_units;
    uint32_t accepted_fluid_flags;
    uint32_t flags;
    uint32_t reserved_u32[3];
} odg_fluid_container_definition;

typedef struct {
    uint32_t type_id;
    uint32_t quantity;
    uint32_t material_tier;
    uint32_t durability;
    uint32_t max_durability;
    uint32_t flags;
    uint64_t instance_id;
    uint64_t payload_id;
} odg_item_stack;

typedef struct {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t actor_id;
    uint32_t slot_count;
    uint32_t base_slot_count;
    uint32_t selected_slot;
    uint32_t equipped_backpack_type;
    uint32_t reserved_u32;
    odg_item_stack slots[ODG_INVENTORY_MAX_SLOTS];
} odg_inventory_snapshot;

typedef struct {
    uint32_t struct_size;
    uint32_t action;
    uint32_t target_kind;
    uint32_t target_id;
    uint32_t valid;
    uint32_t requires_hold;
    uint32_t progress_ticks;
    uint32_t threshold_ticks;
    uint32_t message_code;
    uint32_t reserved_u32[3];
} odg_interaction_hint;

typedef struct {
    uint32_t struct_size;
    uint32_t type;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint64_t payload;
} odg_command;

typedef struct {
    uint32_t struct_size;
    uint32_t sample_rate;
    uint64_t playback_time_us;
    uint32_t energy_q16;
    uint32_t bass_q16;
    uint32_t low_mid_q16;
    uint32_t mid_q16;
    uint32_t high_q16;
    uint32_t onset_q16;
    uint32_t beat_q16;
    uint32_t beat_strength_q16;
    uint32_t activity_q16;
    uint32_t reserved_u32[3];
} odg_music_reactive_frame;

typedef struct {
    uint32_t item_type;
    uint32_t material_tier;
    uint32_t quantity;
    uint32_t reserved_u32;
} odg_recipe_ingredient;

typedef struct {
    uint32_t struct_size;
    uint32_t recipe_id;
    uint32_t station_item_type;
    uint32_t display_code;
    uint32_t output_item_type;
    uint32_t output_quantity;
    uint32_t output_material_tier;
    uint32_t ingredient_count;
    odg_recipe_ingredient ingredients[ODG_RECIPE_MAX_INGREDIENTS];
    uint32_t reserved_u32[4];
} odg_recipe_definition;

typedef struct {
    uint64_t stable_id;
    uint32_t resource_id;
    uint32_t kind;
    uint32_t state;
    int32_t x_milli;
    int32_t z_milli;
    uint32_t progress_ticks;
    uint32_t required_ticks;
    uint32_t yield_preview_min;
    uint32_t yield_preview_max;
    uint32_t species_id;
    uint32_t flora_stage;
    uint32_t fruit_count;
    uint32_t soil_moisture_permille;
} odg_resource_entry;

typedef struct {
    uint32_t struct_size;
    uint32_t count;
    odg_resource_entry entries[ODG_RESOURCE_MAX_ENTRIES];
} odg_resource_snapshot;

typedef struct {
    uint32_t struct_size;
    uint32_t species_id;
    uint32_t display_code;
    uint32_t fruit_item_type;
    uint32_t seed_item_type;
    uint32_t growth_form;
    uint32_t harvest_item_type;
    uint32_t harvest_tool_item_type;
    uint32_t harvest_flags;
    uint32_t harvest_base_ticks;
    uint32_t seedling_ticks;
    uint32_t sapling_ticks;
    uint32_t young_ticks;
    uint32_t old_ticks;
    uint32_t fruit_cycle_ticks;
    uint32_t fruit_capacity_min;
    uint32_t fruit_capacity_max;
    uint32_t max_dynamic_per_chunk;
    uint32_t min_spacing_milli;
    uint32_t planting_clearance_milli;
    uint32_t min_growth_moisture_permille;
    uint32_t preferred_growth_moisture_permille;
    uint32_t seed_germination_permille;
    uint32_t fruit_seed_recovery_permille;
    uint32_t fallen_fruit_seed_permille;
    uint32_t natural_drop_max_per_cycle;
    uint32_t collision_radius_milli;
    uint32_t variant_count;
    uint32_t stage_yield_permille[ODG_FLORA_MAX_STAGES];
    uint32_t stage_harvest_time_permille[ODG_FLORA_MAX_STAGES];
    uint32_t stage_collision_permille[ODG_FLORA_MAX_STAGES];
    uint32_t reserved_u32[2];
} odg_flora_species_definition;

typedef struct {
    uint32_t struct_size;
    uint32_t species_id;
    uint32_t family;
    uint32_t display_code;
    uint32_t variant_count;
    uint32_t max_health;
    uint32_t satiety_decay_ticks;
    uint32_t forage_below_permille;
    uint32_t wild_population_target;
    uint32_t wild_population_hard_cap;
    uint32_t young_ticks;
    uint32_t juvenile_ticks;
    uint32_t old_ticks;
    uint32_t lifespan_ticks;
    uint32_t breeding_cooldown_ticks;
    uint32_t breeding_min_satiety_permille;
    uint32_t breeding_min_hydration_permille;
    uint32_t reproduction_mode;
    uint32_t offspring_min;
    uint32_t offspring_max;
    uint32_t gestation_or_incubation_ticks;
    uint32_t loot_table_id;
    uint32_t behavior_flags;
    uint32_t ground_speed_milli_per_s;
    uint32_t flee_speed_milli_per_s;
    uint32_t flight_speed_milli_per_s;
    uint32_t body_radius_milli;
    uint32_t hydration_decay_ticks;
    uint32_t drink_below_permille;
} odg_fauna_species_definition;

typedef struct {
    uint32_t struct_size;
    uint32_t fauna_species_id;
    uint32_t item_type;
    uint32_t satiety_restore;
    uint32_t propagation_survival_permille;
    uint32_t hydration_restore;
    uint32_t flags;
    uint32_t reserved_u32;
} odg_fauna_diet_definition;

#define ODG_BIOME_MASK(biome_id) (UINT32_C(1) << (biome_id))
typedef struct {
    uint32_t struct_size;
    uint32_t fauna_species_id;
    uint32_t biome_mask;
    int32_t min_altitude_milli;
    int32_t max_altitude_milli;
    uint32_t min_moisture_permille;
    uint32_t max_moisture_permille;
    uint32_t ambient_forage_restore;
    uint32_t ambient_forage_interval_ticks;
    uint32_t migration_interval_ticks;
    uint32_t spawn_weight;
    uint32_t ambient_water_restore;
    uint32_t ambient_water_interval_ticks;
} odg_fauna_habitat_definition;

/* Optional nesting component keyed by fauna species. Egg reproduction does not imply
 * a tree host: substrate is explicit data so ground/cliff/structure nesters compose
 * the same reproduction system without species-specific branches. */
typedef struct {
    uint32_t struct_size;
    uint32_t fauna_species_id;
    uint32_t substrate_mask;
    uint32_t search_range_milli;
    uint32_t min_host_flora_stage;
    uint32_t max_ground_slope_permille;
    uint32_t nest_spacing_milli;
    int32_t height_offset_milli;
    uint32_t reserved_u32[2];
} odg_fauna_nesting_definition;

typedef struct {
    uint32_t item_type;
    uint32_t quantity_min;
    uint32_t quantity_max;
    uint32_t chance_permille;
} odg_loot_entry;

typedef struct {
    uint32_t struct_size;
    uint32_t loot_table_id;
    uint32_t entry_count;
    uint32_t reserved_u32;
    odg_loot_entry entries[ODG_LOOT_MAX_ENTRIES];
} odg_loot_table_definition;

typedef struct {
    uint64_t stable_id;
    uint32_t fauna_id;
    uint32_t species_id;
    uint32_t family;
    uint32_t variant;
    uint32_t state;
    uint32_t tame;
    uint32_t owner_actor_id;
    uint32_t health;
    uint32_t max_health;
    uint32_t satiety_permille;
    uint32_t hydration_permille;
    uint32_t life_stage;
    uint32_t sex;
    uint32_t nest_id;
    uint64_t age_ticks;
    int32_t x_milli;
    int32_t y_milli;
    int32_t z_milli;
} odg_fauna_entry;

typedef struct {
    uint32_t struct_size;
    uint32_t count;
    uint32_t bird_count;
    uint32_t mammal_count;
    odg_fauna_entry entries[ODG_FAUNA_MAX_ENTRIES];
} odg_fauna_snapshot;

#define ODG_FAUNA_NEST_STATE_INCUBATING UINT32_C(1)
typedef struct {
    uint64_t stable_id;
    uint32_t nest_id;
    uint32_t species_id;
    uint32_t state;
    uint32_t substrate;
    uint32_t egg_count;
    uint32_t hatch_ticks;
    uint32_t parent_a;
    uint32_t parent_b;
    uint64_t host_resource_stable_id;
    int32_t x_milli;
    int32_t y_milli;
    int32_t z_milli;
} odg_fauna_nest_entry;

typedef struct {
    uint32_t struct_size;
    uint32_t count;
    odg_fauna_nest_entry entries[ODG_FAUNA_MAX_NESTS];
} odg_fauna_nest_snapshot;

typedef struct {
    uint32_t struct_size;
    int32_t height_milli;
    int32_t normal_x_q15;
    int32_t normal_y_q15;
    int32_t normal_z_q15;
    uint32_t biome;
    uint32_t moisture_permille;
    uint32_t water_depth_milli;
    uint32_t rain_permille;
    uint32_t flags;
    uint32_t reserved_u32[2];
} odg_surface_sample;

typedef struct {
    uint64_t instance_id;
    uint32_t artifact_id;
    uint32_t item_type;
    uint32_t owner_actor_id;
    uint32_t material_tier;
    uint32_t capability_bits;
    int32_t x_milli;
    int32_t z_milli;
    uint32_t storage_used;
    uint32_t state;
} odg_artifact_entry;

typedef struct {
    uint32_t struct_size;
    uint32_t count;
    uint32_t opened_artifact_id;
    uint32_t total_count;
    odg_artifact_entry entries[ODG_ARTIFACT_MAX_ENTRIES];
} odg_artifact_snapshot;

typedef struct {
    uint32_t struct_size;
    uint32_t artifact_id;
    uint32_t slot_count;
    uint32_t used_slots;
    odg_item_stack slots[ODG_CHEST_SLOTS];
} odg_storage_snapshot;

/* Lightweight structural blocks are not artifact entities. A block has no chest-sized
 * payload or capability state; it lives in a sparse construction store keyed by stable
 * global position. Complex stations/vehicles remain artifacts. */
#define ODG_CONSTRUCTION_MAX_ENTRIES UINT32_C(64)
#define ODG_CONSTRUCTION_SHAPE_WALL UINT32_C(1)
#define ODG_CONSTRUCTION_SHAPE_FLOOR UINT32_C(2)
#define ODG_CONSTRUCTION_SHAPE_DOORWAY UINT32_C(3)
#define ODG_CONSTRUCTION_SHAPE_ROOF UINT32_C(4)
typedef struct {
    uint64_t instance_id;
    uint32_t construction_id;
    uint32_t owner_actor_id;
    uint32_t controller_actor_id; /* UINT32_MAX when neutral */
    uint32_t material_tier;
    uint32_t shape;
    int32_t x_milli;
    int32_t z_milli;
    uint32_t state;
    uint32_t health;
    uint32_t max_health;
} odg_construction_entry;

typedef struct {
    uint32_t struct_size;
    uint32_t count;
    uint32_t total_count;
    uint32_t selected_shape; /* current local-player construction mode */
    odg_construction_entry entries[ODG_CONSTRUCTION_MAX_ENTRIES];
} odg_construction_snapshot;

typedef struct {
    uint32_t struct_size;
    uint32_t item_type;
    uint32_t material_tier;
    uint32_t durability_before;
    uint32_t durability_after;
    uint32_t cost_item_type;
    uint32_t cost_quantity;
    uint32_t station_item_type;
} odg_repair_quote;

typedef struct {
    uint32_t struct_size;
    int32_t min_x_milli;
    int32_t min_z_milli;
    int32_t max_x_milli;
    int32_t max_z_milli;
    uint32_t width;
    uint32_t height;
    uint32_t reserved_u32;
} odg_map_query_desc;

typedef struct {
    uint32_t owner_actor_plus_one;
    uint32_t flags;
    int32_t height_milli;
    uint32_t reserved_u32;
} odg_map_sample;

typedef struct {
    uint32_t kind;
    uint32_t id;
    uint32_t owner_actor_id;
    uint32_t material_tier;
    int32_t x_milli;
    int32_t z_milli;
    uint32_t state;
    uint32_t reserved_u32;
} odg_map_marker;

/* Stable, POD-only snapshot for FFI/WASM/native hosts. */
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    uint64_t tick;
    uint64_t match_seed;
    uint32_t width;
    uint32_t height;
    uint32_t alive_count;
    uint32_t player_alive;
    uint32_t player_health;      /* authoritative current hit points */
    uint32_t player_max_health;  /* authoritative maximum hit points */
    uint32_t player_level;       /* coverage tier */
    uint32_t player_score;       /* territory cells */
    uint32_t player_kills;       /* territorial defeats */
    uint32_t player_deaths;
    uint32_t zone_radius_milli;  /* compatibility: world half-size */
    uint32_t simulation_hz;
    uint32_t render_triangles;
    uint32_t render_pixels_touched;
    uint64_t deterministic_state_hash;
    uint32_t territory_cells;
    uint32_t territory_total_cells; /* playable cells, not bounding square */
    uint32_t territory_permille;
    uint32_t player_trail_cells;
    uint32_t player_trail_active;
    uint32_t match_over;
    uint32_t winner_id;
    uint32_t player_death_reason;
    uint32_t turret_total;
    uint32_t player_owned_turrets;
    uint32_t player_carrying_turret;
    uint32_t carried_turret_ammo;
    uint32_t turret_action_available;
    uint32_t ammo_crates_total;
    uint32_t player_carrying_ammo_crate;
    uint32_t player_carried_ammo;
    uint32_t player_ammo_reserve;
    uint32_t chips_total;
    uint32_t player_carrying_chip;
    uint32_t player_chip_kind;
    uint32_t hack_action_available;
    uint32_t drop_action_available;
    uint32_t nearby_owned_turret_visible;
    uint32_t nearby_owned_turret_ammo;
    uint32_t nearby_owned_turret_max_ammo;
    uint32_t player_satiety_permille;
    uint32_t player_hydration_permille;
    uint32_t player_trail_broken;
    uint32_t weather_rain_permille;
    uint32_t fauna_count;
} odg_game_stats;

typedef struct {
    uint32_t actor_id;
    uint32_t score;
    uint32_t level;
    uint32_t alive;
    uint32_t is_player;
    uint32_t name_code;
} odg_leader_entry;

/* POD-only discovery contract for the versioned FFI ABI. It contains no pointers, size_t values,
 * native enums or floating-point fields. Hosts validate every snapshot layout they consume
 * before reading authoritative C state. */
typedef struct {
    uint32_t struct_size;
    uint32_t ffi_abi_version;
    uint32_t engine_api_version;
    uint32_t endian_marker;
    uint32_t game_stats_size;
    uint32_t leader_entry_size;
    uint32_t tick_rate;
    uint32_t max_render_width;
    uint32_t max_render_height;
    uint32_t max_render_pixels;
    uint32_t framebuffer_pixel_format;
    uint32_t framebuffer_bytes_per_pixel;
    uint64_t feature_bits;
    uint32_t inventory_snapshot_size;
    uint32_t interaction_hint_size;
    uint32_t resource_snapshot_size;
    uint32_t artifact_snapshot_size;
    uint32_t food_definition_size;
    uint32_t flora_species_definition_size;
    uint32_t fauna_species_definition_size;
    uint32_t fauna_snapshot_size;
    uint32_t fauna_nest_snapshot_size;
    uint32_t surface_sample_size;
    uint32_t fluid_definition_size;
    uint32_t fluid_container_definition_size;
    uint32_t fauna_nesting_definition_size;
    uint32_t construction_snapshot_size; /* ABI v8: validates durability snapshot layout */
} odg_ffi_abi_info;

uint32_t odg_api_version(void);
int32_t odg_ffi_abi_query(uint32_t requested_ffi_abi,
                          odg_ffi_abi_info *out_info,
                          uint64_t capacity,
                          uint64_t *out_required);
int32_t odg_init(uint64_t seed, uint32_t width, uint32_t height);
int32_t odg_resize(uint32_t width, uint32_t height);
void odg_set_visual_theme(uint32_t theme);
uint32_t odg_visual_theme(void);
void odg_set_presentation_mode(uint32_t mode);
uint32_t odg_presentation_mode(void);
void odg_set_camera_mode(uint32_t mode);
uint32_t odg_camera_mode(void);
void odg_set_music_reactivity_q16(uint32_t amount_q16);
uint32_t odg_music_reactivity_q16(void);
int32_t odg_reset(uint64_t seed);
void odg_set_input(int32_t move_x_q15, int32_t move_y_q15,
                   int32_t aim_x_q15, int32_t aim_y_q15,
                   uint32_t buttons);
/* Exact native/app world-heading path for replays, AI drivers and specialized hosts.
 * Normal gameplay should use odg_set_input(): its move vector is camera-local and the
 * look vector is independent. */
void odg_set_world_input(int32_t world_x_q15, int32_t world_z_q15,
                         int32_t strength_q15,
                         int32_t aim_x_q15, int32_t aim_y_q15,
                         uint32_t buttons);
void odg_tick_us(uint32_t elapsed_us);
void odg_step_ticks(uint32_t ticks);

uintptr_t odg_render_frame(void);
uintptr_t odg_framebuffer_ptr(void);
uint32_t odg_framebuffer_bytes(void);
uint32_t odg_framebuffer_stride_bytes(void);
int32_t odg_copy_framebuffer(uint8_t *out_rgba,
                             uint64_t capacity,
                             uint64_t *out_required);
uint32_t odg_render_width(void);
uint32_t odg_render_height(void);

const odg_game_stats *odg_stats(void);
uintptr_t odg_stats_ptr(void);
int32_t odg_copy_stats(odg_game_stats *out_stats,
                       uint64_t capacity,
                       uint64_t *out_required);
uint32_t odg_player_health(void);
uint32_t odg_player_max_health(void);
uint32_t odg_player_score(void);
uint32_t odg_player_level(void);
uint32_t odg_player_kills(void);
uint32_t odg_player_deaths(void);
uint32_t odg_alive_count(void);
uint32_t odg_zone_radius_milli(void);
uint64_t odg_state_hash(void);

uint32_t odg_territory_total_cells(void);
uint32_t odg_player_territory_cells(void);
uint32_t odg_player_territory_permille(void);
uint32_t odg_player_trail_cells(void);
uint32_t odg_player_trail_active(void);
uint32_t odg_match_over(void);
uint32_t odg_winner_id(void);
uint32_t odg_player_death_reason(void);
uint32_t odg_turret_count(void);
uint32_t odg_player_owned_turrets(void);
uint32_t odg_player_carrying_turret(void);
uint32_t odg_player_carried_turret_ammo(void);
uint32_t odg_player_turret_action_available(void);
uint32_t odg_ammo_crate_count(void);
uint32_t odg_player_carrying_ammo_crate(void);
uint32_t odg_player_carried_ammo(void);
uint32_t odg_player_ammo_reserve(void);
uint32_t odg_chip_count(void);
uint32_t odg_player_carrying_chip(void);
uint32_t odg_player_chip_kind(void);
uint32_t odg_player_hack_action_available(void);
uint32_t odg_player_drop_action_available(void);
uint32_t odg_player_nearby_owned_turret_visible(void);
uint32_t odg_player_nearby_owned_turret_ammo(void);
uint32_t odg_player_nearby_owned_turret_max_ammo(void);
int32_t odg_player_facing_x_q15(void);
int32_t odg_player_facing_z_q15(void);
int32_t odg_camera_dir_x_q15(void);
int32_t odg_camera_dir_z_q15(void);
int32_t odg_control_basis_x_q15(void);
int32_t odg_control_basis_z_q15(void);
int32_t odg_control_heading_x_q15(void);
int32_t odg_control_heading_z_q15(void);
int32_t odg_control_local_x_q15(void);
int32_t odg_control_local_z_q15(void);
int32_t odg_control_strength_q15(void);

uint32_t odg_leader_count(void);
int32_t odg_leader_get(uint32_t rank, odg_leader_entry *out_entry);
uint32_t odg_leader_score(uint32_t rank);
uint32_t odg_leader_name_code(uint32_t rank);
uint32_t odg_leader_is_player(uint32_t rank);

/* Capability queries retained across API revisions; runtime authority is ODG_API_VERSION. */
uint32_t odg_item_definition_count(void);
int32_t odg_item_definition_get(uint32_t type_id, odg_item_definition *out_definition,
                                uint64_t capacity, uint64_t *out_required);
int32_t odg_copy_inventory(uint32_t actor_id, odg_inventory_snapshot *out_inventory,
                           uint64_t capacity, uint64_t *out_required);
int32_t odg_copy_interaction_hint(odg_interaction_hint *out_hint,
                                  uint64_t capacity, uint64_t *out_required);
int32_t odg_command_submit(const odg_command *command, uint64_t capacity);
uint32_t odg_recipe_count(void);
int32_t odg_recipe_get(uint32_t recipe_id, odg_recipe_definition *out_recipe,
                       uint64_t capacity, uint64_t *out_required);
uint32_t odg_recipe_max_craftable(uint32_t actor_id, uint32_t recipe_id);
int32_t odg_craft(uint32_t actor_id, uint32_t recipe_id, uint32_t quantity);
int32_t odg_copy_resources(odg_resource_snapshot *out_resources,
                           uint64_t capacity, uint64_t *out_required);
int32_t odg_copy_artifacts(odg_artifact_snapshot *out_artifacts,
                           uint64_t capacity, uint64_t *out_required);
/* Paged global artifact query. total_count is the active artifact count across generic
 * artifacts + turrets. offset is over that stable deterministic sequence; each page
 * contains at most ODG_ARTIFACT_MAX_ENTRIES. */
int32_t odg_copy_artifacts_page(uint32_t offset,
                                odg_artifact_snapshot *out_artifacts,
                                uint64_t capacity, uint64_t *out_required);
uint32_t odg_construction_count(void);
int32_t odg_copy_construction_page(uint32_t offset,
                                   odg_construction_snapshot *out_construction,
                                   uint64_t capacity, uint64_t *out_required);
uint32_t odg_opened_artifact_id(void);
int32_t odg_copy_artifact_storage(uint32_t actor_id, uint32_t artifact_id,
                                  odg_storage_snapshot *out_storage,
                                  uint64_t capacity, uint64_t *out_required);
int32_t odg_artifact_storage_deposit(uint32_t actor_id, uint32_t artifact_id,
                                     uint32_t inventory_slot, uint32_t quantity);
int32_t odg_artifact_storage_withdraw(uint32_t actor_id, uint32_t artifact_id,
                                      uint32_t storage_slot, uint32_t quantity);
int32_t odg_repair_quote_selected(uint32_t actor_id, odg_repair_quote *out_quote,
                                  uint64_t capacity, uint64_t *out_required);
int32_t odg_repair_selected(uint32_t actor_id);
uint32_t odg_save_schema_version(void);
uint32_t odg_save_schema_supported(uint32_t schema_version);
uint64_t odg_save_blob_size(void);
int32_t odg_save_write(uint8_t *out_blob, uint64_t capacity, uint64_t *out_required);
int32_t odg_save_load(const uint8_t *blob, uint64_t size);
int32_t odg_map_query(const odg_map_query_desc *query,
                      odg_map_sample *out_samples, uint64_t sample_capacity,
                      uint64_t *out_required_samples,
                      odg_map_marker *out_markers, uint32_t marker_capacity,
                      uint32_t *out_marker_count);

/* Pure order-independent worldgen descriptor. It never mutates the active simulation. */
int32_t odg_chunk_descriptor_get(int64_t chunk_x,int64_t chunk_z,
                                 odg_chunk_descriptor *out_descriptor,
                                 uint64_t capacity,uint64_t *out_required);
int32_t odg_world_height_milli64(int64_t world_cell_x,int64_t world_cell_z,int32_t *out_height_milli);
int32_t odg_world_surface_sample64(int64_t world_cell_x,int64_t world_cell_z,
                                   odg_surface_sample *out_sample,uint64_t capacity,uint64_t *out_required);
uint32_t odg_world_geology_material64(int64_t world_cell_x,int64_t world_cell_z,uint32_t depth_milli);
uint32_t odg_world_geology_ore_resource64(int64_t world_cell_x,int64_t world_cell_z,uint32_t depth_milli);
uint32_t odg_world_cave_openness_permille64(int64_t world_cell_x,int64_t world_cell_z,uint32_t depth_milli);
uint32_t odg_world_cave_entrance64(int64_t world_cell_x,int64_t world_cell_z);
uint32_t odg_worldgen_version(void);
uint32_t odg_day_index(void);
uint32_t odg_day_phase_permille(void);
uint32_t odg_daylight_permille(void);
uint32_t odg_is_night(void);
uint32_t odg_food_definition_count(void);
int32_t odg_food_definition_get(uint32_t index,odg_food_definition *out_definition,
                                uint64_t capacity,uint64_t *out_required);
uint32_t odg_fluid_definition_count(void);
int32_t odg_fluid_definition_get(uint32_t index,odg_fluid_definition *out_definition,
                                 uint64_t capacity,uint64_t *out_required);
uint32_t odg_fluid_container_definition_count(void);
int32_t odg_fluid_container_definition_get(uint32_t index,odg_fluid_container_definition *out_definition,
                                           uint64_t capacity,uint64_t *out_required);
uint32_t odg_flora_species_count(void);
int32_t odg_flora_species_get(uint32_t index,odg_flora_species_definition *out_definition,
                              uint64_t capacity,uint64_t *out_required);
uint32_t odg_fauna_species_count(void);
int32_t odg_fauna_species_get(uint32_t index,odg_fauna_species_definition *out_definition,
                              uint64_t capacity,uint64_t *out_required);
uint32_t odg_fauna_diet_count(void);
int32_t odg_fauna_diet_get(uint32_t index,odg_fauna_diet_definition *out_definition,
                           uint64_t capacity,uint64_t *out_required);
uint32_t odg_fauna_habitat_count(void);
int32_t odg_fauna_habitat_get(uint32_t index,odg_fauna_habitat_definition *out_definition,
                              uint64_t capacity,uint64_t *out_required);
uint32_t odg_fauna_nesting_count(void);
int32_t odg_fauna_nesting_get(uint32_t index,odg_fauna_nesting_definition *out_definition,
                              uint64_t capacity,uint64_t *out_required);
uint32_t odg_loot_table_count(void);
int32_t odg_loot_table_get(uint32_t index,odg_loot_table_definition *out_definition,
                           uint64_t capacity,uint64_t *out_required);
int32_t odg_copy_fauna(odg_fauna_snapshot *out_snapshot,uint64_t capacity,uint64_t *out_required);
int32_t odg_copy_fauna_nests(odg_fauna_nest_snapshot *out_snapshot,uint64_t capacity,uint64_t *out_required);
uint32_t odg_player_satiety_permille(void);
uint32_t odg_player_hydration_permille(void);
uint32_t odg_player_oxygen_permille(void);
uint32_t odg_player_trail_broken(void);
uint32_t odg_weather_rain_permille(void);

int32_t odg_render_artifact_view(uint32_t artifact_id, uint8_t *out_rgba,
                                 uint64_t capacity, uint64_t *out_required);
int32_t odg_render_avatar_preview(uint32_t yaw_q16, uint8_t *out_rgba,
                                  uint64_t capacity, uint64_t *out_required);
/* Paused editor preview. yaw/pitch/mode are presentation-only and never modify the
 * authoritative ControlCamera or player/world state. Output is 320x180 RGBA8. */
int32_t odg_render_camera_preview(uint32_t camera_mode, uint32_t yaw_q16, int32_t pitch_q15,
                                  uint8_t *out_rgba, uint64_t capacity, uint64_t *out_required);
int32_t odg_avatar_texture_upload(uint32_t face, const uint8_t *rgba,
                                  uint32_t width, uint32_t height, uint32_t stride);
int32_t odg_avatar_texture_clear(uint32_t face);
uint32_t odg_avatar_texture_present(uint32_t face);
void odg_music_reset(void);
int32_t odg_music_submit_pcm_f32(const float *interleaved, uint32_t frame_count,
                                 uint32_t channels, uint32_t sample_rate,
                                 uint64_t playback_time_us);
int32_t odg_copy_music_frame(odg_music_reactive_frame *out_frame,
                             uint64_t capacity, uint64_t *out_required);

#ifdef __cplusplus
}
#endif

#endif
