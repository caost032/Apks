#include "game_internal.h"

#include <stdint.h>

#define FAUNA_SPAWN_ATTEMPTS UINT32_C(96)
#define FAUNA_OFFSPRING_SPAWN_ATTEMPTS UINT32_C(48)
#define FAUNA_MIGRATION_MIN_DISTANCE_FX (UINT32_C(20) * ODG_FX_ONE)
#define FAUNA_MIGRATION_MAX_DISTANCE_FX (UINT32_C(46) * ODG_FX_ONE)
#define FAUNA_NEST_VISIBLE_VALIDATE_RANGE_FX (UINT32_C(48) * ODG_FX_ONE)
#define FAUNA_FLIGHT_CRUISE_MILLI UINT32_C(3000)
#define FAUNA_FLIGHT_MAX_MILLI UINT32_C(6500)
#define FAUNA_FLIGHT_CLEARANCE_MILLI UINT32_C(260)
#define FAUNA_FLIGHT_ALTITUDE_STEP_FX INT32_C(25)

typedef struct {
    uint32_t species_id;
    uint32_t attack_damage;
    uint32_t attack_cooldown_ticks;
    uint32_t aggro_range_milli;
    uint32_t attack_range_milli;
    uint32_t chase_speed_milli_per_s;
    uint32_t retaliation_ticks;
} odg_fauna_aggression_profile;

/* Combat temperament is data. Passive animals have no row; defensive animals only use
 * the profile after provocation, while HOSTILE_ACTORS species may acquire targets. */
static const odg_fauna_aggression_profile g_aggression[] = {
    {ODG_FAUNA_SPECIES_FOREST_DEER,10u,ODG_TICK_RATE*6u/5u,6500u,1050u,1550u,8u*ODG_TICK_RATE},
    {ODG_FAUNA_SPECIES_NIGHT_STALKER,9u,ODG_TICK_RATE,14500u,1100u,1680u,0u},
    /* Crocodiles are dangerous around water but deliberately slower on land than a
     * fleeing player. Contact is punishing, not a guaranteed death spiral. */
    {ODG_FAUNA_SPECIES_MARSH_CROCODILE,16u,ODG_TICK_RATE*3u/2u,8500u,1250u,1420u,0u}
};

static const odg_fauna_aggression_profile *aggression_profile(uint32_t species_id){
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_aggression)/sizeof(g_aggression[0]));++i)
        if(g_aggression[i].species_id==species_id)return &g_aggression[i];
    return NULL;
}

int odg_fauna_runtime_combat_state_validate_internal(uint32_t fauna_id,uint32_t attack_cooldown,
                                                     uint32_t target_actor,uint32_t aggro_ticks){
    const odg_fauna_entity *entity;const odg_fauna_aggression_profile *profile;
    if(fauna_id>=ODG_FAUNA_MAX_ENTRIES||(target_actor!=UINT32_MAX&&target_actor>=ODG_MAX_ACTORS))return 0;
    entity=&g_odg.fauna[fauna_id];
    /* Current saves give inactive slots one representation. Legacy load migration erases
     * the historical combat bytes before this cross-entity semantic check runs. */
    if(!entity->active)return attack_cooldown==0u&&target_actor==UINT32_MAX&&aggro_ticks==0u;
    profile=aggression_profile(entity->species_id);
    if(profile==NULL)return attack_cooldown==0u&&target_actor==UINT32_MAX&&aggro_ticks==0u;
    if(attack_cooldown>profile->attack_cooldown_ticks||aggro_ticks>profile->retaliation_ticks)return 0;
    return 1;
}

typedef struct {
    uint32_t predator_species_id;
    uint32_t prey_species_id;
    uint32_t hunt_below_satiety_permille;
    uint32_t hunt_range_milli;
    uint32_t consume_range_milli;
    uint32_t satiety_restore;
    uint32_t min_prey_population;
} odg_predator_relation;

/* Predator/prey is an ecological relation, not a hard-coded crocodile branch. The prey
 * floor is deliberate: natural predation must not erase the local food web and force
 * migration to fight an extermination loop. Player hunting has different economics. */
static const odg_predator_relation g_predator_relations[] = {
    /* consume_range must reach physical contact without requiring the two body discs to
     * overlap. Crocodile (720 mm) + fish (220 mm) = 940 mm, so 1 m leaves a small,
     * deterministic contact tolerance while locomotion remains collision-safe. */
    {ODG_FAUNA_SPECIES_MARSH_CROCODILE,ODG_FAUNA_SPECIES_RIVER_FISH,560u,9000u,1000u,420u,3u}
};

static const odg_predator_relation *predator_relation(uint32_t species_id){
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_predator_relations)/sizeof(g_predator_relations[0]));++i)
        if(g_predator_relations[i].predator_species_id==species_id)return &g_predator_relations[i];
    return NULL;
}

static const odg_fauna_species_definition g_species[] = {
    {
        .struct_size=sizeof(odg_fauna_species_definition),
        .species_id=ODG_FAUNA_SPECIES_ORCHARD_BIRD,
        .family=ODG_FAUNA_FAMILY_BIRD,
        .display_code=3001u,
        .variant_count=8u,
        .max_health=28u,
        .satiety_decay_ticks=900u,
        .forage_below_permille=520u,
        .wild_population_target=6u,
        .wild_population_hard_cap=12u,
        .young_ticks=2u*60u*ODG_TICK_RATE,
        .juvenile_ticks=4u*60u*ODG_TICK_RATE,
        .old_ticks=60u*60u*ODG_TICK_RATE,
        .lifespan_ticks=90u*60u*ODG_TICK_RATE,
        .breeding_cooldown_ticks=8u*60u*ODG_TICK_RATE,
        .breeding_min_satiety_permille=650u,
        .breeding_min_hydration_permille=620u,
        .reproduction_mode=ODG_FAUNA_REPRO_EGG,
        .offspring_min=1u,
        .offspring_max=2u,
        .gestation_or_incubation_ticks=3u*60u*ODG_TICK_RATE,
        .loot_table_id=0u,
        .behavior_flags=ODG_FAUNA_BEHAVIOR_CAN_FLY|ODG_FAUNA_BEHAVIOR_FORAGE|
                        ODG_FAUNA_BEHAVIOR_FLEE_ACTORS|ODG_FAUNA_BEHAVIOR_NESTING|
                        ODG_FAUNA_BEHAVIOR_TAMEABLE|ODG_FAUNA_BEHAVIOR_HEAD_PERCH,
        .ground_speed_milli_per_s=650u,
        .flee_speed_milli_per_s=1000u,
        .flight_speed_milli_per_s=1800u,
        .body_radius_milli=180u,
        .hydration_decay_ticks=1300u,
        .drink_below_permille=500u
    },
    {
        .struct_size=sizeof(odg_fauna_species_definition),
        .species_id=ODG_FAUNA_SPECIES_FOREST_DEER,
        .family=ODG_FAUNA_FAMILY_MAMMAL,
        .display_code=3002u,
        .variant_count=5u,
        .max_health=180u,
        .satiety_decay_ticks=1400u,
        .forage_below_permille=480u,
        .wild_population_target=4u,
        .wild_population_hard_cap=8u,
        .young_ticks=5u*60u*ODG_TICK_RATE,
        .juvenile_ticks=10u*60u*ODG_TICK_RATE,
        .old_ticks=120u*60u*ODG_TICK_RATE,
        .lifespan_ticks=180u*60u*ODG_TICK_RATE,
        .breeding_cooldown_ticks=15u*60u*ODG_TICK_RATE,
        .breeding_min_satiety_permille=700u,
        .breeding_min_hydration_permille=650u,
        .reproduction_mode=ODG_FAUNA_REPRO_LIVE,
        .offspring_min=1u,
        .offspring_max=2u,
        .gestation_or_incubation_ticks=6u*60u*ODG_TICK_RATE,
        .loot_table_id=ODG_LOOT_DEER,
        .behavior_flags=ODG_FAUNA_BEHAVIOR_FORAGE|ODG_FAUNA_BEHAVIOR_HERD|
                        ODG_FAUNA_BEHAVIOR_FLEE_ACTORS|ODG_FAUNA_BEHAVIOR_DEFENSIVE_ATTACK,
        .ground_speed_milli_per_s=950u,
        .flee_speed_milli_per_s=1850u,
        .flight_speed_milli_per_s=0u,
        .body_radius_milli=650u,
        .hydration_decay_ticks=1500u,
        .drink_below_permille=480u
    },
    {
        .struct_size=sizeof(odg_fauna_species_definition),
        .species_id=ODG_FAUNA_SPECIES_MEADOW_RABBIT,
        .family=ODG_FAUNA_FAMILY_MAMMAL,
        .display_code=3003u,
        .variant_count=4u,
        .max_health=55u,
        .satiety_decay_ticks=1000u,
        .forage_below_permille=500u,
        .wild_population_target=6u,
        .wild_population_hard_cap=12u,
        .young_ticks=2u*60u*ODG_TICK_RATE,
        .juvenile_ticks=3u*60u*ODG_TICK_RATE,
        .old_ticks=40u*60u*ODG_TICK_RATE,
        .lifespan_ticks=60u*60u*ODG_TICK_RATE,
        .breeding_cooldown_ticks=7u*60u*ODG_TICK_RATE,
        .breeding_min_satiety_permille=620u,
        .breeding_min_hydration_permille=600u,
        .reproduction_mode=ODG_FAUNA_REPRO_LIVE,
        .offspring_min=1u,
        .offspring_max=3u,
        .gestation_or_incubation_ticks=2u*60u*ODG_TICK_RATE,
        .loot_table_id=ODG_LOOT_RABBIT,
        .behavior_flags=ODG_FAUNA_BEHAVIOR_FORAGE|ODG_FAUNA_BEHAVIOR_FLEE_ACTORS,
        .ground_speed_milli_per_s=1100u,
        .flee_speed_milli_per_s=2300u,
        .flight_speed_milli_per_s=0u,
        .body_radius_milli=280u,
        .hydration_decay_ticks=1200u,
        .drink_below_permille=500u
    },
    {
        .struct_size=sizeof(odg_fauna_species_definition),
        .species_id=ODG_FAUNA_SPECIES_FIELD_FOWL,
        .family=ODG_FAUNA_FAMILY_BIRD,
        .display_code=3004u,
        .variant_count=5u,
        .max_health=40u,
        .satiety_decay_ticks=950u,
        .forage_below_permille=520u,
        .wild_population_target=4u,
        .wild_population_hard_cap=8u,
        .young_ticks=2u*60u*ODG_TICK_RATE,
        .juvenile_ticks=4u*60u*ODG_TICK_RATE,
        .old_ticks=50u*60u*ODG_TICK_RATE,
        .lifespan_ticks=75u*60u*ODG_TICK_RATE,
        .breeding_cooldown_ticks=6u*60u*ODG_TICK_RATE,
        .breeding_min_satiety_permille=640u,
        .breeding_min_hydration_permille=620u,
        .reproduction_mode=ODG_FAUNA_REPRO_EGG,
        .offspring_min=1u,
        .offspring_max=3u,
        .gestation_or_incubation_ticks=150u*ODG_TICK_RATE,
        .loot_table_id=ODG_LOOT_FIELD_FOWL,
        .behavior_flags=ODG_FAUNA_BEHAVIOR_FORAGE|ODG_FAUNA_BEHAVIOR_FLEE_ACTORS|
                        ODG_FAUNA_BEHAVIOR_NESTING,
        .ground_speed_milli_per_s=760u,
        .flee_speed_milli_per_s=1320u,
        .flight_speed_milli_per_s=0u,
        .body_radius_milli=300u,
        .hydration_decay_ticks=1250u,
        .drink_below_permille=500u
    },
    {
        .struct_size=sizeof(odg_fauna_species_definition),
        .species_id=ODG_FAUNA_SPECIES_RIVER_FISH,
        .family=ODG_FAUNA_FAMILY_AQUATIC,
        .display_code=3005u,
        .variant_count=6u,
        .max_health=32u,
        .satiety_decay_ticks=1200u,
        .forage_below_permille=500u,
        .wild_population_target=5u,
        .wild_population_hard_cap=10u,
        .young_ticks=2u*60u*ODG_TICK_RATE,
        .juvenile_ticks=3u*60u*ODG_TICK_RATE,
        .old_ticks=45u*60u*ODG_TICK_RATE,
        .lifespan_ticks=70u*60u*ODG_TICK_RATE,
        .breeding_cooldown_ticks=6u*60u*ODG_TICK_RATE,
        .breeding_min_satiety_permille=600u,
        .breeding_min_hydration_permille=0u,
        .reproduction_mode=ODG_FAUNA_REPRO_SPAWN,
        .offspring_min=1u,
        .offspring_max=3u,
        .gestation_or_incubation_ticks=2u*60u*ODG_TICK_RATE,
        .loot_table_id=ODG_LOOT_RIVER_FISH,
        .behavior_flags=ODG_FAUNA_BEHAVIOR_AQUATIC|ODG_FAUNA_BEHAVIOR_FORAGE,
        .ground_speed_milli_per_s=1050u,
        .flee_speed_milli_per_s=1650u,
        .flight_speed_milli_per_s=0u,
        .body_radius_milli=220u,
        .hydration_decay_ticks=UINT32_MAX,
        .drink_below_permille=0u
    },
    {
        .struct_size=sizeof(odg_fauna_species_definition),
        .species_id=ODG_FAUNA_SPECIES_NIGHT_STALKER,
        .family=ODG_FAUNA_FAMILY_MONSTER,
        .display_code=3006u,
        .variant_count=4u,
        .max_health=72u,
        .satiety_decay_ticks=UINT32_MAX,
        .forage_below_permille=0u,
        .wild_population_target=0u,
        .wild_population_hard_cap=8u,
        .young_ticks=1u,
        .juvenile_ticks=1u,
        .old_ticks=30u*60u*ODG_TICK_RATE,
        .lifespan_ticks=45u*60u*ODG_TICK_RATE,
        .breeding_cooldown_ticks=0u,
        .breeding_min_satiety_permille=0u,
        .breeding_min_hydration_permille=0u,
        .reproduction_mode=ODG_FAUNA_REPRO_NONE,
        .offspring_min=0u,
        .offspring_max=0u,
        .gestation_or_incubation_ticks=0u,
        .loot_table_id=ODG_LOOT_NIGHT_STALKER,
        .behavior_flags=ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS|ODG_FAUNA_BEHAVIOR_NOCTURNAL|
                        ODG_FAUNA_BEHAVIOR_OUTSIDE_TERRITORY_HUNTER,
        .ground_speed_milli_per_s=1180u,
        .flee_speed_milli_per_s=1780u,
        .flight_speed_milli_per_s=0u,
        .body_radius_milli=420u,
        .hydration_decay_ticks=UINT32_MAX,
        .drink_below_permille=0u
    },
    {
        .struct_size=sizeof(odg_fauna_species_definition),
        .species_id=ODG_FAUNA_SPECIES_MARSH_CROCODILE,
        .family=ODG_FAUNA_FAMILY_REPTILE,
        .display_code=3007u,
        .variant_count=5u,
        .max_health=240u,
        .satiety_decay_ticks=1500u,
        .forage_below_permille=520u,
        .wild_population_target=2u,
        .wild_population_hard_cap=4u,
        .young_ticks=6u*60u*ODG_TICK_RATE,
        .juvenile_ticks=12u*60u*ODG_TICK_RATE,
        .old_ticks=150u*60u*ODG_TICK_RATE,
        .lifespan_ticks=240u*60u*ODG_TICK_RATE,
        .breeding_cooldown_ticks=18u*60u*ODG_TICK_RATE,
        .breeding_min_satiety_permille=720u,
        .breeding_min_hydration_permille=700u,
        .reproduction_mode=ODG_FAUNA_REPRO_EGG,
        .offspring_min=1u,
        .offspring_max=3u,
        .gestation_or_incubation_ticks=8u*60u*ODG_TICK_RATE,
        .loot_table_id=ODG_LOOT_MARSH_CROCODILE,
        .behavior_flags=ODG_FAUNA_BEHAVIOR_AMPHIBIOUS|ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS|
                        ODG_FAUNA_BEHAVIOR_NESTING|ODG_FAUNA_BEHAVIOR_PREDATOR,
        .ground_speed_milli_per_s=820u,
        .flee_speed_milli_per_s=1180u,
        .flight_speed_milli_per_s=0u,
        .body_radius_milli=720u,
        .hydration_decay_ticks=1900u,
        .drink_below_permille=650u
    }
};

static const odg_fauna_diet_definition g_diets[] = {
    {
        .struct_size=sizeof(odg_fauna_diet_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_ORCHARD_BIRD,
        .item_type=ODG_ITEM_APPLE,
        .satiety_restore=260u,
        .propagation_survival_permille=280u,
        .hydration_restore=90u,
        .flags=0u,
        .reserved_u32=0u
    },
    {
        .struct_size=sizeof(odg_fauna_diet_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_ORCHARD_BIRD,
        .item_type=ODG_ITEM_APPLE_SEED,
        .satiety_restore=120u,
        .propagation_survival_permille=120u,
        .hydration_restore=15u,
        .flags=0u,
        .reserved_u32=0u
    },
    {
        .struct_size=sizeof(odg_fauna_diet_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_FOREST_DEER,
        .item_type=ODG_ITEM_APPLE,
        .satiety_restore=220u,
        .propagation_survival_permille=0u,
        .hydration_restore=110u,
        .flags=0u,
        .reserved_u32=0u
    },
    {
        .struct_size=sizeof(odg_fauna_diet_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_MEADOW_RABBIT,
        .item_type=ODG_ITEM_APPLE,
        .satiety_restore=180u,
        .propagation_survival_permille=0u,
        .hydration_restore=100u,
        .flags=0u,
        .reserved_u32=0u
    },
    {
        .struct_size=sizeof(odg_fauna_diet_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_MEADOW_RABBIT,
        .item_type=ODG_ITEM_APPLE_SEED,
        .satiety_restore=80u,
        .propagation_survival_permille=0u,
        .hydration_restore=10u,
        .flags=0u,
        .reserved_u32=0u
    },
    {
        .struct_size=sizeof(odg_fauna_diet_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_FIELD_FOWL,
        .item_type=ODG_ITEM_APPLE,
        .satiety_restore=170u,
        .propagation_survival_permille=0u,
        .hydration_restore=85u,
        .flags=0u,
        .reserved_u32=0u
    },
    {
        .struct_size=sizeof(odg_fauna_diet_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_FIELD_FOWL,
        .item_type=ODG_ITEM_APPLE_SEED,
        .satiety_restore=140u,
        .propagation_survival_permille=0u,
        .hydration_restore=12u,
        .flags=0u,
        .reserved_u32=0u
    }
    ,{
        .struct_size=sizeof(odg_fauna_diet_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_MARSH_CROCODILE,
        .item_type=ODG_ITEM_RAW_FISH,
        .satiety_restore=360u,
        .propagation_survival_permille=0u,
        .hydration_restore=40u,
        .flags=0u,
        .reserved_u32=0u
    },
    {
        .struct_size=sizeof(odg_fauna_diet_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_MARSH_CROCODILE,
        .item_type=ODG_ITEM_RAW_MEAT,
        .satiety_restore=320u,
        .propagation_survival_permille=0u,
        .hydration_restore=35u,
        .flags=0u,
        .reserved_u32=0u
    }
};

static const odg_fauna_habitat_definition g_habitats[] = {
    {
        .struct_size=sizeof(odg_fauna_habitat_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_ORCHARD_BIRD,
        .biome_mask=ODG_BIOME_MASK(ODG_BIOME_PLAIN)|ODG_BIOME_MASK(ODG_BIOME_FOREST)|
                    ODG_BIOME_MASK(ODG_BIOME_WETLAND),
        .min_altitude_milli=-100,
        .max_altitude_milli=3000,
        .min_moisture_permille=250u,
        .max_moisture_permille=1000u,
        .ambient_forage_restore=55u,
        .ambient_forage_interval_ticks=45u*ODG_TICK_RATE,
        .migration_interval_ticks=6u*60u*ODG_TICK_RATE,
        .spawn_weight=45u,
        .ambient_water_restore=110u,
        .ambient_water_interval_ticks=90u*ODG_TICK_RATE
    },
    {
        .struct_size=sizeof(odg_fauna_habitat_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_FOREST_DEER,
        .biome_mask=ODG_BIOME_MASK(ODG_BIOME_PLAIN)|ODG_BIOME_MASK(ODG_BIOME_FOREST)|
                    ODG_BIOME_MASK(ODG_BIOME_HIGHLANDS),
        .min_altitude_milli=100,
        .max_altitude_milli=3900,
        .min_moisture_permille=250u,
        .max_moisture_permille=930u,
        .ambient_forage_restore=80u,
        .ambient_forage_interval_ticks=60u*ODG_TICK_RATE,
        .migration_interval_ticks=8u*60u*ODG_TICK_RATE,
        .spawn_weight=35u,
        .ambient_water_restore=160u,
        .ambient_water_interval_ticks=120u*ODG_TICK_RATE
    },
    {
        .struct_size=sizeof(odg_fauna_habitat_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_MEADOW_RABBIT,
        .biome_mask=ODG_BIOME_MASK(ODG_BIOME_PLAIN)|ODG_BIOME_MASK(ODG_BIOME_FOREST)|
                    ODG_BIOME_MASK(ODG_BIOME_WETLAND),
        .min_altitude_milli=-50,
        .max_altitude_milli=2600,
        .min_moisture_permille=200u,
        .max_moisture_permille=880u,
        .ambient_forage_restore=70u,
        .ambient_forage_interval_ticks=50u*ODG_TICK_RATE,
        .migration_interval_ticks=5u*60u*ODG_TICK_RATE,
        .spawn_weight=55u,
        .ambient_water_restore=130u,
        .ambient_water_interval_ticks=100u*ODG_TICK_RATE
    },
    {
        .struct_size=sizeof(odg_fauna_habitat_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_FIELD_FOWL,
        .biome_mask=ODG_BIOME_MASK(ODG_BIOME_PLAIN)|ODG_BIOME_MASK(ODG_BIOME_FOREST)|
                    ODG_BIOME_MASK(ODG_BIOME_WETLAND),
        .min_altitude_milli=-50,
        .max_altitude_milli=2400,
        .min_moisture_permille=260u,
        .max_moisture_permille=960u,
        .ambient_forage_restore=65u,
        .ambient_forage_interval_ticks=48u*ODG_TICK_RATE,
        .migration_interval_ticks=7u*60u*ODG_TICK_RATE,
        .spawn_weight=30u,
        .ambient_water_restore=120u,
        .ambient_water_interval_ticks=95u*ODG_TICK_RATE
    },
    {
        .struct_size=sizeof(odg_fauna_habitat_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_RIVER_FISH,
        .biome_mask=ODG_BIOME_MASK(ODG_BIOME_PLAIN)|ODG_BIOME_MASK(ODG_BIOME_FOREST)|
                    ODG_BIOME_MASK(ODG_BIOME_WETLAND)|ODG_BIOME_MASK(ODG_BIOME_ROCKY),
        .min_altitude_milli=-1600,
        .max_altitude_milli=300,
        .min_moisture_permille=0u,
        .max_moisture_permille=1000u,
        .ambient_forage_restore=85u,
        .ambient_forage_interval_ticks=45u*ODG_TICK_RATE,
        .migration_interval_ticks=5u*60u*ODG_TICK_RATE,
        .spawn_weight=45u,
        .ambient_water_restore=1u,
        .ambient_water_interval_ticks=ODG_TICK_RATE
    },
    {
        .struct_size=sizeof(odg_fauna_habitat_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_NIGHT_STALKER,
        .biome_mask=ODG_BIOME_MASK(ODG_BIOME_PLAIN)|ODG_BIOME_MASK(ODG_BIOME_FOREST)|
                    ODG_BIOME_MASK(ODG_BIOME_ROCKY)|ODG_BIOME_MASK(ODG_BIOME_HIGHLANDS),
        .min_altitude_milli=-100,
        .max_altitude_milli=5200,
        .min_moisture_permille=0u,
        .max_moisture_permille=1000u,
        .ambient_forage_restore=0u,
        .ambient_forage_interval_ticks=0u,
        .migration_interval_ticks=10u*60u*ODG_TICK_RATE,
        .spawn_weight=0u,
        .ambient_water_restore=1u,
        .ambient_water_interval_ticks=10u*60u*ODG_TICK_RATE
    }
    ,{
        .struct_size=sizeof(odg_fauna_habitat_definition),
        .fauna_species_id=ODG_FAUNA_SPECIES_MARSH_CROCODILE,
        .biome_mask=ODG_BIOME_MASK(ODG_BIOME_WETLAND)|ODG_BIOME_MASK(ODG_BIOME_PLAIN)|
                    ODG_BIOME_MASK(ODG_BIOME_FOREST),
        .min_altitude_milli=-800,
        .max_altitude_milli=900,
        .min_moisture_permille=420u,
        .max_moisture_permille=1000u,
        .ambient_forage_restore=0u,
        .ambient_forage_interval_ticks=0u,
        .migration_interval_ticks=12u*60u*ODG_TICK_RATE,
        .spawn_weight=18u,
        .ambient_water_restore=180u,
        .ambient_water_interval_ticks=70u*ODG_TICK_RATE
    }
};

static const odg_fauna_nesting_definition g_nesting[] = {
    {sizeof(odg_fauna_nesting_definition),ODG_FAUNA_SPECIES_ORCHARD_BIRD,ODG_NEST_SUBSTRATE_TREE,10000u,ODG_FLORA_STAGE_MATURE,1000u,1800u,2180,{0u,0u}},
    {sizeof(odg_fauna_nesting_definition),ODG_FAUNA_SPECIES_FIELD_FOWL,ODG_NEST_SUBSTRATE_GROUND,8000u,0u,320u,1700u,120,{0u,0u}},
    {sizeof(odg_fauna_nesting_definition),ODG_FAUNA_SPECIES_MARSH_CROCODILE,ODG_NEST_SUBSTRATE_GROUND,9000u,0u,260u,2800u,160,{0u,0u}}
};

static const odg_loot_table_definition g_loot[] = {
    {sizeof(odg_loot_table_definition),ODG_LOOT_DEER,2u,0u,{{ODG_ITEM_LEATHER,1u,2u,1000u},{ODG_ITEM_RAW_MEAT,1u,3u,1000u},{0u,0u,0u,0u},{0u,0u,0u,0u}}},
    {sizeof(odg_loot_table_definition),ODG_LOOT_RABBIT,1u,0u,{{ODG_ITEM_RAW_MEAT,1u,2u,1000u},{0u,0u,0u,0u},{0u,0u,0u,0u},{0u,0u,0u,0u}}},
    {sizeof(odg_loot_table_definition),ODG_LOOT_FIELD_FOWL,1u,0u,{{ODG_ITEM_RAW_MEAT,1u,2u,1000u},{0u,0u,0u,0u},{0u,0u,0u,0u},{0u,0u,0u,0u}}},
    {sizeof(odg_loot_table_definition),ODG_LOOT_NIGHT_STALKER,1u,0u,{{ODG_ITEM_NIGHT_SHARD,1u,2u,850u},{0u,0u,0u,0u},{0u,0u,0u,0u},{0u,0u,0u,0u}}},
    {sizeof(odg_loot_table_definition),ODG_LOOT_RIVER_FISH,1u,0u,{{ODG_ITEM_RAW_FISH,1u,2u,1000u},{0u,0u,0u,0u},{0u,0u,0u,0u},{0u,0u,0u,0u}}},
    {sizeof(odg_loot_table_definition),ODG_LOOT_MARSH_CROCODILE,2u,0u,{{ODG_ITEM_RAW_MEAT,2u,5u,1000u},{ODG_ITEM_LEATHER,1u,2u,900u},{0u,0u,0u,0u},{0u,0u,0u,0u}}}
};

static uint32_t table_count_species(void){return (uint32_t)(sizeof(g_species)/sizeof(g_species[0]));}
static uint32_t table_count_diets(void){return (uint32_t)(sizeof(g_diets)/sizeof(g_diets[0]));}
static uint32_t table_count_habitats(void){return (uint32_t)(sizeof(g_habitats)/sizeof(g_habitats[0]));}
static uint32_t table_count_nesting(void){return (uint32_t)(sizeof(g_nesting)/sizeof(g_nesting[0]));}
static uint32_t table_count_loot(void){return (uint32_t)(sizeof(g_loot)/sizeof(g_loot[0]));}

const odg_fauna_species_definition *odg_fauna_species_internal(uint32_t species_id){uint32_t i;for(i=0u;i<table_count_species();++i)if(g_species[i].species_id==species_id)return &g_species[i];return NULL;}
const odg_fauna_diet_definition *odg_fauna_diet_internal(uint32_t species_id,uint32_t item_type){uint32_t i;for(i=0u;i<table_count_diets();++i)if(g_diets[i].fauna_species_id==species_id&&g_diets[i].item_type==item_type)return &g_diets[i];return NULL;}
const odg_fauna_habitat_definition *odg_fauna_habitat_internal(uint32_t species_id){uint32_t i;for(i=0u;i<table_count_habitats();++i)if(g_habitats[i].fauna_species_id==species_id)return &g_habitats[i];return NULL;}
const odg_fauna_nesting_definition *odg_fauna_nesting_internal(uint32_t species_id){uint32_t i;for(i=0u;i<table_count_nesting();++i)if(g_nesting[i].fauna_species_id==species_id)return &g_nesting[i];return NULL;}
uint32_t odg_fauna_species_count(void){return table_count_species();}
int32_t odg_fauna_species_get(uint32_t index,odg_fauna_species_definition *out,uint64_t capacity,uint64_t *required){if(required!=NULL)*required=sizeof(*out);if(index>=table_count_species())return ODG_STATUS_INVALID_ARGUMENT;if(out==NULL||capacity<sizeof(*out))return ODG_STATUS_BUFFER_TOO_SMALL;*out=g_species[index];return ODG_STATUS_OK;}
uint32_t odg_fauna_diet_count(void){return table_count_diets();}
int32_t odg_fauna_diet_get(uint32_t index,odg_fauna_diet_definition *out,uint64_t capacity,uint64_t *required){if(required!=NULL)*required=sizeof(*out);if(index>=table_count_diets())return ODG_STATUS_INVALID_ARGUMENT;if(out==NULL||capacity<sizeof(*out))return ODG_STATUS_BUFFER_TOO_SMALL;*out=g_diets[index];return ODG_STATUS_OK;}
uint32_t odg_fauna_habitat_count(void){return table_count_habitats();}
int32_t odg_fauna_habitat_get(uint32_t index,odg_fauna_habitat_definition *out,uint64_t capacity,uint64_t *required){if(required!=NULL)*required=sizeof(*out);if(index>=table_count_habitats())return ODG_STATUS_INVALID_ARGUMENT;if(out==NULL||capacity<sizeof(*out))return ODG_STATUS_BUFFER_TOO_SMALL;*out=g_habitats[index];return ODG_STATUS_OK;}
uint32_t odg_fauna_nesting_count(void){return table_count_nesting();}
int32_t odg_fauna_nesting_get(uint32_t index,odg_fauna_nesting_definition *out,uint64_t capacity,uint64_t *required){if(required!=NULL)*required=sizeof(*out);if(index>=table_count_nesting())return ODG_STATUS_INVALID_ARGUMENT;if(out==NULL||capacity<sizeof(*out))return ODG_STATUS_BUFFER_TOO_SMALL;*out=g_nesting[index];return ODG_STATUS_OK;}
uint32_t odg_loot_table_count(void){return table_count_loot();}
int32_t odg_loot_table_get(uint32_t index,odg_loot_table_definition *out,uint64_t capacity,uint64_t *required){if(required!=NULL)*required=sizeof(*out);if(index>=table_count_loot())return ODG_STATUS_INVALID_ARGUMENT;if(out==NULL||capacity<sizeof(*out))return ODG_STATUS_BUFFER_TOO_SMALL;*out=g_loot[index];return ODG_STATUS_OK;}

static const odg_loot_table_definition *loot_internal(uint32_t id){uint32_t i;for(i=0u;i<table_count_loot();++i)if(g_loot[i].loot_table_id==id)return &g_loot[i];return NULL;}

int odg_fauna_profiles_validate_internal(void){
    uint32_t i,j;
    for(i=0u;i<table_count_species();++i){
        const odg_fauna_species_definition *species=&g_species[i];
        const odg_fauna_aggression_profile *combat=aggression_profile(species->species_id);
        const int needs_combat=(species->behavior_flags&(ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS|ODG_FAUNA_BEHAVIOR_DEFENSIVE_ATTACK))!=0u;
        if(species->loot_table_id!=0u&&loot_internal(species->loot_table_id)==NULL)return 0;
        if(needs_combat!=(combat!=NULL))return 0;
        if(combat!=NULL){
            if(combat->attack_damage==0u||combat->attack_cooldown_ticks==0u||
               combat->aggro_range_milli==0u||combat->attack_range_milli==0u||
               combat->attack_range_milli>combat->aggro_range_milli||
               combat->chase_speed_milli_per_s==0u)return 0;
            if((species->behavior_flags&ODG_FAUNA_BEHAVIOR_DEFENSIVE_ATTACK)!=0u&&combat->retaliation_ticks==0u)return 0;
            if((species->behavior_flags&ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS)!=0u&&combat->retaliation_ticks!=0u)return 0;
        }
    }
    for(i=0u;i<table_count_loot();++i){
        if(g_loot[i].loot_table_id==0u||g_loot[i].entry_count==0u||g_loot[i].entry_count>ODG_LOOT_MAX_ENTRIES)return 0;
        for(j=i+1u;j<table_count_loot();++j)if(g_loot[j].loot_table_id==g_loot[i].loot_table_id)return 0;
    }
    for(i=0u;i<(uint32_t)(sizeof(g_aggression)/sizeof(g_aggression[0]));++i){
        const odg_fauna_species_definition *species=odg_fauna_species_internal(g_aggression[i].species_id);
        if(species==NULL)return 0;
        for(j=i+1u;j<(uint32_t)(sizeof(g_aggression)/sizeof(g_aggression[0]));++j)
            if(g_aggression[j].species_id==g_aggression[i].species_id)return 0;
    }
    for(i=0u;i<table_count_species();++i){
        const odg_fauna_species_definition *species=&g_species[i];
        if(((species->behavior_flags&ODG_FAUNA_BEHAVIOR_PREDATOR)!=0u)!=(predator_relation(species->species_id)!=NULL))return 0;
    }
    for(i=0u;i<(uint32_t)(sizeof(g_predator_relations)/sizeof(g_predator_relations[0]));++i){
        const odg_predator_relation *relation=&g_predator_relations[i];
        const odg_fauna_species_definition *predator=odg_fauna_species_internal(relation->predator_species_id);
        const odg_fauna_species_definition *prey=odg_fauna_species_internal(relation->prey_species_id);
        if(predator==NULL||prey==NULL||predator==prey||relation->hunt_below_satiety_permille>1000u||
           relation->hunt_range_milli==0u||relation->consume_range_milli==0u||
           relation->consume_range_milli>relation->hunt_range_milli||relation->satiety_restore==0u||
           relation->min_prey_population>=prey->wild_population_hard_cap)return 0;
        for(j=i+1u;j<(uint32_t)(sizeof(g_predator_relations)/sizeof(g_predator_relations[0]));++j)
            if(g_predator_relations[j].predator_species_id==relation->predator_species_id&&
               g_predator_relations[j].prey_species_id==relation->prey_species_id)return 0;
    }
    return 1;
}
static uint32_t fauna_population(uint32_t species){uint32_t i,n=0u;for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i)if(g_odg.fauna[i].active&&g_odg.fauna[i].species_id==species)++n;return n;}
static uint32_t alloc_fauna(void){uint32_t i;for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i)if(!g_odg.fauna[i].active)return i;return UINT32_MAX;}
static uint32_t alloc_nest(void){uint32_t i;for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i)if(!g_odg.fauna_nests[i].active)return i;return UINT32_MAX;}

void odg_fauna_deactivate_internal(uint32_t fauna_id){
    odg_fauna_entity *entity;
    if(fauna_id>=ODG_FAUNA_MAX_ENTRIES)return;
    entity=&g_odg.fauna[fauna_id];if(!entity->active)return;
    if(g_odg.fauna_count>0u)--g_odg.fauna_count;
    odg_survival_reset_fauna(fauna_id);
    /* Dead/despawned slots are not entities. Erase stale AI/physics/ecology bytes so a
     * future allocator reuse cannot inherit history and current saves stop accumulating
     * irrelevant tombstone state. Historical SAVE23 tombstones remain load-compatible. */
    odg_memset(entity,0,sizeof(*entity));
}

static int fauna_water_nearby(int32_t x,int32_t z,uint32_t range_m){
    static const int32_t dirs[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    uint32_t r,d;
    for(r=2u;r<=range_m;r+=2u)for(d=0u;d<8u;++d){
        odg_surface_sample sample;
        int32_t tx=x+dirs[d][0]*(int32_t)r*ODG_FX_ONE;
        int32_t tz=z+dirs[d][1]*(int32_t)r*ODG_FX_ONE;
        if(odg_environment_surface_local(tx,tz,&sample)&&(sample.flags&ODG_SURFACE_FLAG_WATER)!=0u&&sample.water_depth_milli>=350u)return 1;
    }
    return 0;
}

static int habitat_matches(uint32_t species_id,int32_t x,int32_t z){
    const odg_fauna_species_definition *d=odg_fauna_species_internal(species_id);
    const odg_fauna_habitat_definition *h=odg_fauna_habitat_internal(species_id);
    odg_surface_sample s;
    if(d==NULL||h==NULL||!odg_environment_surface_local(x,z,&s))return 0;
    if((h->biome_mask&ODG_BIOME_MASK(s.biome))==0u)return 0;
    if(s.height_milli<h->min_altitude_milli||s.height_milli>h->max_altitude_milli)return 0;
    if(s.moisture_permille<h->min_moisture_permille||s.moisture_permille>h->max_moisture_permille)return 0;
    if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_AQUATIC)!=0u){
        uint32_t min_depth=d->body_radius_milli+120u;
        if((s.flags&ODG_SURFACE_FLAG_WATER)==0u||s.water_depth_milli<min_depth)return 0;
    }else if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_AMPHIBIOUS)!=0u){
        if((s.flags&ODG_SURFACE_FLAG_WATER)!=0u){
            if(s.water_depth_milli<350u)return 0;
        }else{
            if((s.flags&ODG_SURFACE_FLAG_STEEP)!=0u||!fauna_water_nearby(x,z,8u))return 0;
        }
    }else{
        if((s.flags&ODG_SURFACE_FLAG_WATER)!=0u)return 0;
        if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)==0u&&(s.flags&ODG_SURFACE_FLAG_STEEP)!=0u)return 0;
    }
    return 1;
}

static void fauna_sync_aquatic_height(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    odg_surface_sample surface;uint32_t below_surface,offset_milli;
    if(e==NULL||d==NULL||(d->behavior_flags&ODG_FAUNA_BEHAVIOR_AQUATIC)==0u)return;
    if(!odg_environment_surface_local(e->x,e->z,&surface)||(surface.flags&ODG_SURFACE_FLAG_WATER)==0u){
        e->y_offset_fx=0;return;
    }
    below_surface=d->body_radius_milli<220u?220u:d->body_radius_milli;
    offset_milli=surface.water_depth_milli>below_surface?surface.water_depth_milli-below_surface:0u;
    e->y_offset_fx=(int32_t)(((uint64_t)offset_milli*ODG_FX_ONE)/1000u);
}

static void fauna_sync_surface_swim_height(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    odg_surface_sample surface;uint32_t draft,offset_milli;
    if(e==NULL||d==NULL)return;
    if(!odg_environment_surface_local(e->x,e->z,&surface)||(surface.flags&ODG_SURFACE_FLAG_WATER)==0u){e->y_offset_fx=0;return;}
    draft=d->body_radius_milli;
    if(draft<180u)draft=180u;
    offset_milli=surface.water_depth_milli>draft?surface.water_depth_milli-draft:0u;
    e->y_offset_fx=(int32_t)(((uint64_t)offset_milli*ODG_FX_ONE)/1000u);
}

static int32_t fauna_body_radius_fx(const odg_fauna_species_definition *d){
    uint64_t radius;
    if(d==NULL)return ODG_FX_ONE/10;
    radius=((uint64_t)d->body_radius_milli*(uint64_t)ODG_FX_ONE)/UINT64_C(1000);
    if(radius<(uint64_t)(ODG_FX_ONE/10))radius=(uint64_t)(ODG_FX_ONE/10);
    return radius>(uint64_t)INT32_MAX?INT32_MAX:(int32_t)radius;
}

static int fauna_spawn_body_clear(const odg_fauna_species_definition *d,int32_t x,int32_t z){
    int32_t radius=fauna_body_radius_fx(d);
    if(!odg_position_clear_internal(x,z,radius))return 0;
    return odg_dynamic_position_clear_internal(x,z,radius,UINT32_MAX,UINT32_MAX);
}

static int spawn_fauna(uint32_t species_id,uint32_t stage,int32_t x,int32_t z,uint32_t sex){
    const odg_fauna_species_definition *d=odg_fauna_species_internal(species_id);uint32_t id;odg_fauna_entity *e;
    if(d==NULL||fauna_population(species_id)>=d->wild_population_hard_cap||!habitat_matches(species_id,x,z)||
       !fauna_spawn_body_clear(d,x,z))return 0;
    id=alloc_fauna();if(id==UINT32_MAX)return 0;
    e=&g_odg.fauna[id];odg_memset(e,0,sizeof(*e));odg_survival_reset_fauna(id);
    e->active=1u;e->id=id;e->stable_id=(g_odg.seed^((uint64_t)species_id<<48u)^((uint64_t)id<<16u)^g_odg.tick^UINT64_C(0x4641554e415f4944));e->species_id=species_id;e->family=d->family;
    e->variant=d->variant_count==0u?0u:odg_rand_bounded(&g_odg.ecology_rng,d->variant_count);e->state=ODG_FAUNA_STATE_GROUND;e->owner_actor_id=UINT32_MAX;e->hp=d->max_health;e->max_hp=d->max_health;e->satiety_permille=800u;
    /* Aquatic species spawn already immersed in their habitat. Keep hydration canonical
     * from the creation tick instead of relying on the next fauna tick to repair it; a
     * freshly initialized world must be immediately serializable. */
    e->hydration_permille=(d->behavior_flags&ODG_FAUNA_BEHAVIOR_AQUATIC)!=0u?1000u:850u;
    e->life_stage=stage;e->sex=sex;e->x=x;e->z=z;e->local_resident=1u;e->face_z_q15=ODG_Q15_ONE;e->nest_id=UINT32_MAX;e->legacy_target_pickup_id=UINT32_MAX;
    fauna_sync_aquatic_height(e,d);
    if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_AMPHIBIOUS)!=0u)fauna_sync_surface_swim_height(e,d);
    odg_local_fx_to_global_fx_internal(x,z,&e->global_fx_x,&e->global_fx_z);(void)odm_rng_seed_derived(&e->rng,g_odg.seed,UINT64_C(0x4641554e415f524e),id+species_id*97u);
    if(stage==ODG_FAUNA_STAGE_ADULT)e->age_ticks=(uint64_t)d->young_ticks+d->juvenile_ticks;else if(stage==ODG_FAUNA_STAGE_JUVENILE)e->age_ticks=d->young_ticks;
    ++g_odg.fauna_count;return 1;
}

static int spawn_fauna_offspring_near(uint32_t species_id,int32_t origin_x,int32_t origin_z,
                                      uint32_t sex,odm_rng *rng){
    const odg_fauna_species_definition *d=odg_fauna_species_internal(species_id);
    uint32_t attempt;int32_t body,min_radius,max_radius;
    if(d==NULL||rng==NULL)return 0;
    body=fauna_body_radius_fx(d);
    min_radius=body>INT32_MAX/2?INT32_MAX:body*2;
    if(min_radius<=INT32_MAX-ODG_FX_ONE/5)min_radius+=ODG_FX_ONE/5;
    max_radius=min_radius<=INT32_MAX-3*ODG_FX_ONE?min_radius+3*ODG_FX_ONE:INT32_MAX;
    for(attempt=0u;attempt<FAUNA_OFFSPRING_SPAWN_ATTEMPTS;++attempt){
        int32_t ox=odg_rand_range_fx(rng,-max_radius,max_radius);
        int32_t oz=odg_rand_range_fx(rng,-max_radius,max_radius);
        int64_t d2=(int64_t)ox*ox+(int64_t)oz*oz;
        if(d2<(int64_t)min_radius*min_radius||d2>(int64_t)max_radius*max_radius)continue;
        if(spawn_fauna(species_id,ODG_FAUNA_STAGE_YOUNG,origin_x+ox,origin_z+oz,sex))return 1;
    }
    return 0;
}

static int spawn_nearby_habitat(uint32_t species_id,uint32_t stage,uint32_t sex,int migration){
    uint32_t attempt;
    int32_t min_fx=migration?(int32_t)FAUNA_MIGRATION_MIN_DISTANCE_FX:5*ODG_FX_ONE;
    int32_t max_fx=migration?(int32_t)FAUNA_MIGRATION_MAX_DISTANCE_FX:45*ODG_FX_ONE;
    const odg_actor *player=&g_odg.actors[ODG_PLAYER_ID];
    for(attempt=0u;attempt<FAUNA_SPAWN_ATTEMPTS;++attempt){
        int32_t x=player->x+odg_rand_range_fx(&g_odg.ecology_rng,-max_fx,max_fx);
        int32_t z=player->z+odg_rand_range_fx(&g_odg.ecology_rng,-max_fx,max_fx);
        int64_t d2=odg_dist2(player->x,player->z,x,z);
        if(d2<(int64_t)min_fx*min_fx||d2>(int64_t)max_fx*max_fx)continue;
        if(spawn_fauna(species_id,stage,x,z,sex))return 1;
    }
    return 0;
}

void odg_fauna_build_initial(void){
    uint32_t si,k;g_odg.fauna_count=0u;g_odg.fauna_nest_count=0u;odg_memset(g_odg.fauna,0,sizeof(g_odg.fauna));odg_memset(g_odg.fauna_nests,0,sizeof(g_odg.fauna_nests));
    for(si=0u;si<table_count_species();++si){const odg_fauna_species_definition *d=&g_species[si];for(k=0u;k<d->wild_population_target;++k)(void)spawn_nearby_habitat(d->species_id,ODG_FAUNA_STAGE_ADULT,(k&1u)?ODG_FAUNA_SEX_MALE:ODG_FAUNA_SEX_FEMALE,0);}
}

static void fauna_update_stage(odg_fauna_entity *e,const odg_fauna_species_definition *d){uint64_t a1=d->young_ticks,a2=a1+d->juvenile_ticks,a3=a2+d->old_ticks;if(e->age_ticks<a1)e->life_stage=ODG_FAUNA_STAGE_YOUNG;else if(e->age_ticks<a2)e->life_stage=ODG_FAUNA_STAGE_JUVENILE;else if(e->age_ticks<a3)e->life_stage=ODG_FAUNA_STAGE_ADULT;else e->life_stage=ODG_FAUNA_STAGE_OLD;}

static int nest_active_for_entity(const odg_fauna_entity *e){return e->nest_id<ODG_FAUNA_MAX_NESTS&&g_odg.fauna_nests[e->nest_id].active!=0u;}

static void fauna_try_feed(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)(7*ODG_FX_ONE)*(7*ODG_FX_ONE);
    if(e->satiety_permille>=d->forage_below_permille)return;
    for(i=0u;i<g_odg.pickup_count;++i){odg_world_pickup *p=&g_odg_pickups[i];const odg_fauna_diet_definition *diet;int64_t d2;if(!p->active||p->local_resident==0u||p->stack.quantity==0u)continue;diet=odg_fauna_diet_internal(e->species_id,p->stack.type_id);if(diet==NULL)continue;d2=odg_dist2(e->x,e->z,p->x,p->z);if(d2<best_d2){best_d2=d2;best=i;}}
    if(best!=UINT32_MAX){odg_world_pickup *p=&g_odg_pickups[best];const odg_fauna_diet_definition *diet=odg_fauna_diet_internal(e->species_id,p->stack.type_id);int32_t dx=p->x-e->x,dz=p->z-e->z;int32_t nx,nz;odg_normalize_q15(dx,dz,&nx,&nz);e->face_x_q15=nx;e->face_z_q15=nz;e->state=ODG_FAUNA_STATE_FORAGE;
        if(best_d2<(int64_t)ODG_FX_ONE*ODG_FX_ONE){
            uint32_t sat=e->satiety_permille+diet->satiety_restore;
            uint32_t hyd=e->hydration_permille+diet->hydration_restore;
            e->satiety_permille=sat>1000u?1000u:sat;
            e->hydration_permille=hyd>1000u?1000u:hyd;
            e->starvation_accum=0u;
            if(diet->hydration_restore>0u)e->dehydration_accum=0u;
            if(--p->stack.quantity==0u)odg_world_pickup_deactivate_internal(p);
            e->decision_ticks=2u*ODG_TICK_RATE;
        }
    }
}

static void fauna_try_ambient_forage(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    const odg_fauna_habitat_definition *h=odg_fauna_habitat_internal(e->species_id);uint32_t phase,sat;
    if(d==NULL||(d->behavior_flags&ODG_FAUNA_BEHAVIOR_FORAGE)==0u)return;
    if(h==NULL||h->ambient_forage_interval_ticks==0u||h->ambient_forage_restore==0u||e->satiety_permille>=d->forage_below_permille)return;
    phase=(uint32_t)(e->stable_id%(uint64_t)h->ambient_forage_interval_ticks);
    if((uint32_t)(g_odg.tick%(uint64_t)h->ambient_forage_interval_ticks)!=phase)return;
    if(!habitat_matches(e->species_id,e->x,e->z))return;
    sat=e->satiety_permille+h->ambient_forage_restore;e->satiety_permille=sat>1000u?1000u:sat;e->starvation_accum=0u;e->state=ODG_FAUNA_STATE_FORAGE;
}


static int fauna_find_nearby_water(const odg_fauna_entity *e,int32_t *out_x,int32_t *out_z,int64_t *out_d2){
    static const int32_t dirs[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    uint32_t radius,di;
    int found=0;
    int64_t best=(int64_t)(9*ODG_FX_ONE)*(9*ODG_FX_ONE);
    int32_t bx=0,bz=0;
    if(e==NULL)return 0;
    for(radius=2u;radius<=8u;radius+=2u){
        for(di=0u;di<8u;++di){
            int32_t tx=e->x+dirs[di][0]*(int32_t)radius*ODG_FX_ONE;
            int32_t tz=e->z+dirs[di][1]*(int32_t)radius*ODG_FX_ONE;
            odg_surface_sample sample;
            int64_t d2;
            if(!odg_environment_surface_local(tx,tz,&sample)||(sample.flags&ODG_SURFACE_FLAG_WATER)==0u)continue;
            d2=odg_dist2(e->x,e->z,tx,tz);
            if(d2<best){best=d2;bx=tx;bz=tz;found=1;}
        }
    }
    if(!found)return 0;
    if(out_x!=NULL)*out_x=bx;
    if(out_z!=NULL)*out_z=bz;
    if(out_d2!=NULL)*out_d2=best;
    return 1;
}

static int fauna_try_water(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    const odg_fauna_habitat_definition *h;
    odg_surface_sample here;
    int32_t wx,wz,nx,nz;
    int64_t d2;
    uint32_t phase,restore;
    if(e==NULL||d==NULL)return 0;
    if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_AQUATIC)!=0u){e->hydration_permille=1000u;e->dehydration_accum=0u;return 0;}
    if(e->hydration_permille>=d->drink_below_permille)return 0;
    h=odg_fauna_habitat_internal(e->species_id);
    if(h==NULL||h->ambient_water_interval_ticks==0u||h->ambient_water_restore==0u)return 0;
    if(fauna_find_nearby_water(e,&wx,&wz,&d2)){
        odg_normalize_q15(wx-e->x,wz-e->z,&nx,&nz);
        e->face_x_q15=nx;e->face_z_q15=nz;e->state=ODG_FAUNA_STATE_FORAGE;e->decision_ticks=2u*ODG_TICK_RATE;
        phase=(uint32_t)(e->stable_id%(uint64_t)h->ambient_water_interval_ticks);
        if(d2<=(int64_t)(2*ODG_FX_ONE)*(2*ODG_FX_ONE)&&
           ((uint32_t)(g_odg.tick%(uint64_t)h->ambient_water_interval_ticks)==phase||e->hydration_permille<180u)){
            restore=h->ambient_water_restore;
            e->hydration_permille=e->hydration_permille+restore>1000u?1000u:e->hydration_permille+restore;
            e->dehydration_accum=0u;
        }
        return 1;
    }
    /* Rain/dew is deliberately weak backup hydration: it prevents mass extinction in a
     * wet biome but does not replace an actual water source. */
    if(!odg_environment_surface_local(e->x,e->z,&here)||here.rain_permille<550u||
       here.moisture_permille<h->min_moisture_permille)return 0;
    phase=(uint32_t)((e->stable_id>>8u)%(uint64_t)h->ambient_water_interval_ticks);
    if((uint32_t)(g_odg.tick%(uint64_t)h->ambient_water_interval_ticks)!=phase)return 1;
    restore=h->ambient_water_restore/3u;
    if(restore==0u)restore=1u;
    e->hydration_permille=e->hydration_permille+restore>1000u?1000u:e->hydration_permille+restore;
    e->dehydration_accum=0u;
    return 1;
}

static void fauna_move(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    const odg_fauna_aggression_profile *aggression=aggression_profile(e->species_id);
    int32_t speed_milli;
    int32_t speed_fx,nx,nz,radius_fx;
    odg_surface_sample now,next;
    int have_now,is_aquatic,is_amphibious,is_flying,emergency_swim,wants_landing;
    is_flying=(d->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)!=0u&&e->life_stage!=ODG_FAUNA_STAGE_YOUNG&&e->state==ODG_FAUNA_STATE_FLIGHT;
    if(is_flying&&d->flight_speed_milli_per_s!=0u)
        speed_milli=(int32_t)d->flight_speed_milli_per_s;
    else if((e->state==ODG_FAUNA_STATE_AGGRO||e->state==ODG_FAUNA_STATE_HUNT_PREY)&&aggression!=NULL)
        speed_milli=(int32_t)aggression->chase_speed_milli_per_s;
    else speed_milli=(e->state==ODG_FAUNA_STATE_FLEE)?(int32_t)d->flee_speed_milli_per_s:(int32_t)d->ground_speed_milli_per_s;
    have_now=odg_environment_surface_local(e->x,e->z,&now);
    is_aquatic=(d->behavior_flags&ODG_FAUNA_BEHAVIOR_AQUATIC)!=0u;
    is_amphibious=(d->behavior_flags&ODG_FAUNA_BEHAVIOR_AMPHIBIOUS)!=0u;
    wants_landing=(d->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)!=0u&&
                  e->life_stage!=ODG_FAUNA_STAGE_YOUNG&&e->state!=ODG_FAUNA_STATE_FLIGHT&&e->y_offset_fx>0;
    emergency_swim=!is_aquatic&&!is_amphibious&&!is_flying&&have_now&&(now.flags&ODG_SURFACE_FLAG_WATER)!=0u;
    if(emergency_swim)speed_milli=(speed_milli*3)/5;
    speed_fx=(speed_milli*ODG_FX_ONE)/(1000*(int32_t)ODG_TICK_RATE);
    nx=e->x+(int32_t)(((int64_t)e->face_x_q15*speed_fx)/ODG_Q15_ONE);
    nz=e->z+(int32_t)(((int64_t)e->face_z_q15*speed_fx)/ODG_Q15_ONE);
    radius_fx=(int32_t)(((uint64_t)d->body_radius_milli*ODG_FX_ONE)/1000u);
    if(radius_fx<ODG_FX_ONE/12)radius_fx=ODG_FX_ONE/12;
    /* Flight-to-ground is a real physical transition. A bird may cruise over a crown,
     * but it cannot zero its vertical offset while that ground footprint is occupied.
     * Descend only while advancing over clear dry terrain; otherwise resume flight. */
    if(wants_landing){
        if(odg_environment_surface_local(nx,nz,&next)&&
           (next.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u&&
           (!have_now||odg_abs_i32(next.height_milli-now.height_milli)<=700)&&
           odg_position_clear_internal(nx,nz,radius_fx)&&
           odg_dynamic_position_clear_internal(nx,nz,radius_fx,UINT32_MAX,e->id)){
            e->x=nx;e->z=nz;
            if(e->y_offset_fx>FAUNA_FLIGHT_ALTITUDE_STEP_FX)e->y_offset_fx-=FAUNA_FLIGHT_ALTITUDE_STEP_FX;
            else e->y_offset_fx=0;
        }else{
            e->state=ODG_FAUNA_STATE_FLIGHT;e->decision_ticks=ODG_TICK_RATE;
        }
    }else if(is_aquatic){
        if(odg_environment_surface_local(nx,nz,&next)&&(next.flags&ODG_SURFACE_FLAG_WATER)!=0u&&
           next.water_depth_milli>=d->body_radius_milli+120u&&odg_position_clear_internal(nx,nz,radius_fx)&&
           odg_dynamic_position_clear_internal(nx,nz,radius_fx,UINT32_MAX,e->id)){
            e->x=nx;e->z=nz;fauna_sync_aquatic_height(e,d);
        }else e->decision_ticks=0u;
    }else if(is_amphibious){
        if(odg_environment_surface_local(nx,nz,&next)&&
           (((next.flags&ODG_SURFACE_FLAG_WATER)!=0u&&next.water_depth_milli>=350u)||
            ((next.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u&&(!have_now||odg_abs_i32(next.height_milli-now.height_milli)<=700)))&&
           odg_position_clear_internal(nx,nz,radius_fx)&&
           odg_dynamic_position_clear_internal(nx,nz,radius_fx,UINT32_MAX,e->id)){
            e->x=nx;e->z=nz;
            if((next.flags&ODG_SURFACE_FLAG_WATER)!=0u)fauna_sync_surface_swim_height(e,d);else e->y_offset_fx=0;
        }else e->decision_ticks=0u;
    }else if(emergency_swim){
        if(odg_environment_surface_local(nx,nz,&next)&&
           ((next.flags&ODG_SURFACE_FLAG_STEEP)==0u)&&
           odg_position_clear_internal(nx,nz,radius_fx)&&
           odg_dynamic_position_clear_internal(nx,nz,radius_fx,UINT32_MAX,e->id)){
            e->x=nx;e->z=nz;
            if((next.flags&ODG_SURFACE_FLAG_WATER)!=0u)fauna_sync_surface_swim_height(e,d);else e->y_offset_fx=0;
        }else e->decision_ticks=0u;
    }else if(is_flying){
        uint32_t required=odg_airspace_required_offset_milli_internal(nx,nz,d->body_radius_milli,FAUNA_FLIGHT_CLEARANCE_MILLI);
        uint32_t target_milli=required>FAUNA_FLIGHT_CRUISE_MILLI?required:FAUNA_FLIGHT_CRUISE_MILLI;
        int32_t target_fx,current_milli;
        if(required==UINT32_MAX||target_milli>FAUNA_FLIGHT_MAX_MILLI){e->decision_ticks=0u;return;}
        target_fx=(int32_t)(((uint64_t)target_milli*ODG_FX_ONE)/1000u);
        current_milli=(int32_t)(((int64_t)e->y_offset_fx*1000)/(int64_t)ODG_FX_ONE);
        /* Never pass horizontally through a crown while climbing. Gain the required
         * clearance first; once clear, cruise over the obstacle and descend smoothly. */
        if(current_milli+(int32_t)FAUNA_FLIGHT_CLEARANCE_MILLI<(int32_t)required){
            int32_t gap=target_fx-e->y_offset_fx;
            if(gap>0)e->y_offset_fx+=gap>FAUNA_FLIGHT_ALTITUDE_STEP_FX?FAUNA_FLIGHT_ALTITUDE_STEP_FX:gap;
            return;
        }
        e->x=nx;e->z=nz;
        if(e->y_offset_fx<target_fx){int32_t gap=target_fx-e->y_offset_fx;e->y_offset_fx+=gap>FAUNA_FLIGHT_ALTITUDE_STEP_FX?FAUNA_FLIGHT_ALTITUDE_STEP_FX:gap;}
        else if(e->y_offset_fx>target_fx){int32_t gap=e->y_offset_fx-target_fx;e->y_offset_fx-=gap>FAUNA_FLIGHT_ALTITUDE_STEP_FX?FAUNA_FLIGHT_ALTITUDE_STEP_FX:gap;}
    }else if(have_now&&odg_environment_surface_local(nx,nz,&next)&&
            (next.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u&&
            odg_abs_i32(next.height_milli-now.height_milli)<=700&&
            odg_position_clear_internal(nx,nz,radius_fx)&&
            odg_dynamic_position_clear_internal(nx,nz,radius_fx,UINT32_MAX,e->id)){
        e->x=nx;e->z=nz;e->y_offset_fx=0;
    }else{
        e->decision_ticks=0u;
        /* Never snap an airborne animal down through an obstacle. The landing branch
         * above owns altitude removal; ground animals stay at zero as before. */
        if(e->y_offset_fx>0&&(d->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)!=0u)
            e->state=ODG_FAUNA_STATE_FLIGHT;
        else e->y_offset_fx=0;
    }
    odg_local_fx_to_global_fx_internal(e->x,e->z,&e->global_fx_x,&e->global_fx_z);
}

static int fauna_seek_shore(odg_fauna_entity *e){
    static const int32_t dirs[16][2]={{32767,0},{30274,12539},{23170,23170},{12539,30274},{0,32767},{-12539,30274},{-23170,23170},{-30274,12539},{-32767,0},{-30274,-12539},{-23170,-23170},{-12539,-30274},{0,-32767},{12539,-30274},{23170,-23170},{30274,-12539}};
    static const uint32_t radii[]={2u,3u,4u,6u,8u,12u};
    uint32_t r,d;int64_t best=INT64_MAX;int found=0;int32_t bx=0,bz=0;
    if(e==NULL)return 0;
    for(r=0u;r<(uint32_t)(sizeof(radii)/sizeof(radii[0]));++r){
        int32_t dist=(int32_t)radii[r]*ODG_FX_ONE;
        for(d=0u;d<16u;++d){
            int32_t tx=e->x+(int32_t)(((int64_t)dirs[d][0]*dist)/ODG_Q15_ONE);
            int32_t tz=e->z+(int32_t)(((int64_t)dirs[d][1]*dist)/ODG_Q15_ONE);
            odg_surface_sample sample;int64_t d2;
            if(!odg_environment_surface_local(tx,tz,&sample)||(sample.flags&ODG_SURFACE_FLAG_STEEP)!=0u)continue;
            if((sample.flags&ODG_SURFACE_FLAG_WATER)!=0u&&sample.water_depth_milli>=350u)continue;
            d2=odg_dist2(e->x,e->z,tx,tz);if(d2<best){best=d2;bx=tx;bz=tz;found=1;}
        }
        if(found)break;
    }
    if(!found)return 0;
    odg_normalize_q15(bx-e->x,bz-e->z,&e->face_x_q15,&e->face_z_q15);
    e->state=ODG_FAUNA_STATE_FLEE;e->decision_ticks=ODG_TICK_RATE/2u;
    return 1;
}

static int fauna_seek_herd(odg_fauna_entity *e){
    uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)(10*ODG_FX_ONE)*(10*ODG_FX_ONE);
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){odg_fauna_entity *o=&g_odg.fauna[i];int64_t d2;if(o==e||!o->active||!o->local_resident||o->species_id!=e->species_id)continue;d2=odg_dist2(e->x,e->z,o->x,o->z);if(d2>=(int64_t)(3*ODG_FX_ONE)*(3*ODG_FX_ONE)&&d2<best_d2){best_d2=d2;best=i;}}
    if(best!=UINT32_MAX){odg_normalize_q15(g_odg.fauna[best].x-e->x,g_odg.fauna[best].z-e->z,&e->face_x_q15,&e->face_z_q15);return 1;}return 0;
}


static void destroy_nest(uint32_t nest_id);

static int fauna_try_predation(odg_fauna_entity *predator,const odg_fauna_species_definition *definition){
    const odg_predator_relation *relation;uint32_t i,best=UINT32_MAX;int32_t range_fx,consume_fx;int64_t best_d2;
    if(predator==NULL||definition==NULL)return 0;
    relation=predator_relation(predator->species_id);
    if(relation==NULL||predator->satiety_permille>=relation->hunt_below_satiety_permille)return 0;
    if(fauna_population(relation->prey_species_id)<=relation->min_prey_population)return 0;
    range_fx=(int32_t)(((uint64_t)relation->hunt_range_milli*ODG_FX_ONE)/1000u);
    consume_fx=(int32_t)(((uint64_t)relation->consume_range_milli*ODG_FX_ONE)/1000u);
    best_d2=(int64_t)range_fx*range_fx;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        odg_fauna_entity *prey=&g_odg.fauna[i];int64_t d2;
        if(!prey->active||!prey->local_resident||prey==predator||prey->species_id!=relation->prey_species_id)continue;
        d2=odg_dist2(predator->x,predator->z,prey->x,prey->z);
        if(d2<best_d2){best_d2=d2;best=i;}
    }
    if(best==UINT32_MAX)return 0;
    if(best_d2<=(int64_t)consume_fx*consume_fx){
        odg_fauna_entity *prey=&g_odg.fauna[best];uint32_t sat=predator->satiety_permille+relation->satiety_restore;
        if(nest_active_for_entity(prey))destroy_nest(prey->nest_id);
        odg_fauna_deactivate_internal(best);
        predator->satiety_permille=sat>1000u?1000u:sat;predator->starvation_accum=0u;
        predator->state=ODG_FAUNA_STATE_FORAGE;predator->decision_ticks=2u*ODG_TICK_RATE;
        return 2;
    }
    {
        odg_fauna_entity *prey=&g_odg.fauna[best];int32_t nx,nz;
        odg_normalize_q15(prey->x-predator->x,prey->z-predator->z,&predator->face_x_q15,&predator->face_z_q15);
        predator->state=ODG_FAUNA_STATE_HUNT_PREY;predator->decision_ticks=1u;
        odg_normalize_q15(prey->x-predator->x,prey->z-predator->z,&nx,&nz);
        prey->face_x_q15=nx;prey->face_z_q15=nz;prey->state=ODG_FAUNA_STATE_FLEE;prey->decision_ticks=ODG_TICK_RATE/2u;
    }
    return 1;
}

static void fauna_choose_direction(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    uint32_t i;int64_t threat_d2=(int64_t)(6*ODG_FX_ONE)*(6*ODG_FX_ONE);uint32_t threat=UINT32_MAX;
    if((d->behavior_flags&(ODG_FAUNA_BEHAVIOR_AQUATIC|ODG_FAUNA_BEHAVIOR_AMPHIBIOUS))==0u){
        odg_surface_sample current;
        if(odg_environment_surface_local(e->x,e->z,&current)&&(current.flags&ODG_SURFACE_FLAG_WATER)!=0u&&fauna_seek_shore(e)){
            /* A flying species crossing water searches for a dry landing area while
             * remaining airborne; it does not become an emergency swimmer merely
             * because the terrain below is water. */
            if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)!=0u&&e->life_stage!=ODG_FAUNA_STAGE_YOUNG)
                e->state=ODG_FAUNA_STATE_FLIGHT;
            return;
        }
    }
    if(nest_active_for_entity(e)){odg_fauna_nest *n=&g_odg.fauna_nests[e->nest_id];int64_t d2=odg_dist2(e->x,e->z,n->x,n->z);if(d2>(int64_t)(3*ODG_FX_ONE)*(3*ODG_FX_ONE)){odg_normalize_q15(n->x-e->x,n->z-e->z,&e->face_x_q15,&e->face_z_q15);e->state=(d->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)?ODG_FAUNA_STATE_FLIGHT:ODG_FAUNA_STATE_GROUND;e->decision_ticks=2u*ODG_TICK_RATE;return;}e->state=ODG_FAUNA_STATE_NEST;e->decision_ticks=2u*ODG_TICK_RATE;return;}
    if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_FLEE_ACTORS)!=0u)for(i=0u;i<ODG_MAX_ACTORS;++i){odg_actor *a=&g_odg.actors[i];int64_t d2;if(!a->active||a->hp==0u)continue;d2=odg_dist2(e->x,e->z,a->x,a->z);if(d2<threat_d2){threat_d2=d2;threat=i;}}
    if(threat!=UINT32_MAX){int32_t nx,nz;odg_normalize_q15(e->x-g_odg.actors[threat].x,e->z-g_odg.actors[threat].z,&nx,&nz);e->face_x_q15=nx;e->face_z_q15=nz;e->state=ODG_FAUNA_STATE_FLEE;e->decision_ticks=ODG_TICK_RATE;return;}
    if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_HERD)!=0u&&fauna_seek_herd(e)){e->state=ODG_FAUNA_STATE_GROUND;e->decision_ticks=2u*ODG_TICK_RATE;return;}
    {int32_t rx=odg_rand_range_fx(&e->rng,-ODG_Q15_ONE,ODG_Q15_ONE);int32_t rz=odg_rand_range_fx(&e->rng,-ODG_Q15_ONE,ODG_Q15_ONE);odg_normalize_q15(rx,rz,&e->face_x_q15,&e->face_z_q15);if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)!=0u&&e->life_stage!=ODG_FAUNA_STAGE_YOUNG&&odg_rand_bounded(&e->rng,5u)==0u)e->state=ODG_FAUNA_STATE_FLIGHT;else e->state=ODG_FAUNA_STATE_GROUND;e->decision_ticks=(2u+odg_rand_bounded(&e->rng,5u))*ODG_TICK_RATE;}
}

static void fauna_clear_combat_state(uint32_t fauna_id){
    if(fauna_id>=ODG_FAUNA_MAX_ENTRIES)return;
    g_odg_persistent_runtime.fauna_target_actor[fauna_id]=UINT32_MAX;
    g_odg_persistent_runtime.fauna_aggro_ticks[fauna_id]=0u;
}

static int fauna_target_allowed(const odg_fauna_entity *e,const odg_fauna_species_definition *d,uint32_t actor_id){
    const odg_actor *actor;
    if(e==NULL||d==NULL||actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];
    if(!actor->active||actor->hp==0u||!actor->local_resident)return 0;
    if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_NOCTURNAL)!=0u&&!odg_is_night())return 0;
    if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_OUTSIDE_TERRITORY_HUNTER)!=0u&&
       odg_territory_actor_controls_position(actor_id,actor->x,actor->z))return 0;
    return 1;
}

static uint32_t fauna_find_hostile_target(const odg_fauna_entity *e,const odg_fauna_species_definition *d,
                                          const odg_fauna_aggression_profile *profile){
    uint32_t i,best=UINT32_MAX;
    int32_t range_fx;int64_t best_d2;
    if(e==NULL||d==NULL||profile==NULL||(d->behavior_flags&ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS)==0u)return UINT32_MAX;
    range_fx=(int32_t)(((uint64_t)profile->aggro_range_milli*ODG_FX_ONE)/1000u);best_d2=(int64_t)range_fx*range_fx;
    for(i=0u;i<ODG_MAX_ACTORS;++i){
        int64_t d2;
        if(!fauna_target_allowed(e,d,i))continue;
        d2=odg_dist2(e->x,e->z,g_odg.actors[i].x,g_odg.actors[i].z);
        if(d2<best_d2){best_d2=d2;best=i;}
    }
    return best;
}

static int fauna_tick_aggression(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    const odg_fauna_aggression_profile *profile;
    uint32_t target;int32_t attack_range_fx,aggro_range_fx;int64_t d2;
    if(e==NULL||d==NULL||e->id>=ODG_FAUNA_MAX_ENTRIES)return 0;
    profile=aggression_profile(e->species_id);
    if(g_odg_persistent_runtime.fauna_attack_cooldown[e->id]>0u)--g_odg_persistent_runtime.fauna_attack_cooldown[e->id];
    if(profile==NULL){fauna_clear_combat_state(e->id);return 0;}
    if(g_odg_persistent_runtime.fauna_aggro_ticks[e->id]>0u){
        --g_odg_persistent_runtime.fauna_aggro_ticks[e->id];
        if(g_odg_persistent_runtime.fauna_aggro_ticks[e->id]==0u&&
           (d->behavior_flags&ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS)==0u)fauna_clear_combat_state(e->id);
    }
    target=g_odg_persistent_runtime.fauna_target_actor[e->id];
    if(!fauna_target_allowed(e,d,target)){
        target=fauna_find_hostile_target(e,d,profile);
        g_odg_persistent_runtime.fauna_target_actor[e->id]=target;
    }
    if(target==UINT32_MAX)return 0;
    d2=odg_dist2(e->x,e->z,g_odg.actors[target].x,g_odg.actors[target].z);
    aggro_range_fx=(int32_t)(((uint64_t)profile->aggro_range_milli*ODG_FX_ONE)/1000u);
    if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS)!=0u&&d2>(int64_t)aggro_range_fx*aggro_range_fx){
        fauna_clear_combat_state(e->id);return 0;
    }
    odg_normalize_q15(g_odg.actors[target].x-e->x,g_odg.actors[target].z-e->z,&e->face_x_q15,&e->face_z_q15);
    e->state=ODG_FAUNA_STATE_AGGRO;e->decision_ticks=1u;
    attack_range_fx=(int32_t)(((uint64_t)profile->attack_range_milli*ODG_FX_ONE)/1000u);
    if(d2<=(int64_t)attack_range_fx*attack_range_fx&&g_odg_persistent_runtime.fauna_attack_cooldown[e->id]==0u){
        uint32_t reason=e->family==ODG_FAUNA_FAMILY_MONSTER?ODG_DEATH_MONSTER:ODG_DEATH_FAUNA;
        g_odg_persistent_runtime.fauna_attack_cooldown[e->id]=profile->attack_cooldown_ticks;
        odg_actor_apply_damage_internal(target,UINT32_MAX,profile->attack_damage,reason);
        if(g_odg.actors[target].hp==0u)fauna_clear_combat_state(e->id);
    }
    return 1;
}

static int fauna_nocturnal_retreat(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    uint32_t i,nearest=UINT32_MAX;int64_t best=(int64_t)(14*ODG_FX_ONE)*(14*ODG_FX_ONE);
    if(e==NULL||d==NULL||(d->behavior_flags&ODG_FAUNA_BEHAVIOR_NOCTURNAL)==0u||odg_is_night())return 0;
    fauna_clear_combat_state(e->id);
    for(i=0u;i<ODG_MAX_ACTORS;++i){
        int64_t d2;const odg_actor *actor=&g_odg.actors[i];
        if(!actor->active||actor->hp==0u||!actor->local_resident)continue;
        d2=odg_dist2(e->x,e->z,actor->x,actor->z);if(d2<best){best=d2;nearest=i;}
    }
    if(nearest==UINT32_MAX){uint32_t fauna_id=e->id;odg_fauna_deactivate_internal(fauna_id);return 2;}
    odg_normalize_q15(e->x-g_odg.actors[nearest].x,e->z-g_odg.actors[nearest].z,&e->face_x_q15,&e->face_z_q15);
    e->state=ODG_FAUNA_STATE_FLEE;e->decision_ticks=ODG_TICK_RATE/2u;return 1;
}

static uint32_t fauna_species_near_actor(uint32_t species_id,const odg_actor *actor,int32_t radius_fx){
    uint32_t i,count=0u;int64_t limit=(int64_t)radius_fx*radius_fx;
    if(actor==NULL)return 0u;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){const odg_fauna_entity *e=&g_odg.fauna[i];if(e->active&&e->local_resident&&e->species_id==species_id&&odg_dist2(actor->x,actor->z,e->x,e->z)<limit)++count;}
    return count;
}

static int fauna_spawn_night_stalker_for(uint32_t actor_id){
    odg_actor *actor;uint32_t attempt;const int32_t min_fx=10*ODG_FX_ONE,max_fx=14*ODG_FX_ONE;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];
    if(!actor->active||actor->hp==0u||!actor->local_resident||odg_territory_actor_controls_position(actor_id,actor->x,actor->z))return 0;
    if(fauna_species_near_actor(ODG_FAUNA_SPECIES_NIGHT_STALKER,actor,22*ODG_FX_ONE)>=2u)return 0;
    for(attempt=0u;attempt<64u;++attempt){
        int32_t x=actor->x+odg_rand_range_fx(&g_odg.ecology_rng,-max_fx,max_fx);
        int32_t z=actor->z+odg_rand_range_fx(&g_odg.ecology_rng,-max_fx,max_fx);
        int64_t d2=odg_dist2(actor->x,actor->z,x,z);
        if(d2<(int64_t)min_fx*min_fx||d2>(int64_t)max_fx*max_fx)continue;
        if(odg_territory_actor_controls_position(actor_id,x,z)||odg_artifact_light_permille_internal(x,z)>180u)continue;
        if(!odg_position_clear_internal(x,z,ODG_FX_ONE/2))continue;
        if(spawn_fauna(ODG_FAUNA_SPECIES_NIGHT_STALKER,ODG_FAUNA_STAGE_ADULT,x,z,ODG_FAUNA_SEX_NONE))return 1;
    }
    return 0;
}

static void fauna_tick_hostile_spawns(void){
    uint32_t i;
    if(!odg_is_night()||(g_odg.tick%(uint64_t)(4u*ODG_TICK_RATE))!=0u)return;
    for(i=0u;i<ODG_MAX_ACTORS;++i)(void)fauna_spawn_night_stalker_for(i);
}

typedef struct {
    uint32_t substrate;
    uint64_t host_resource_stable_id;
    int32_t x,z;
    int64_t global_fx_x,global_fx_z;
    uint32_t local_resident;
} fauna_nest_site;

static uint32_t surface_slope_permille(const odg_surface_sample *sample){
    uint64_t horizontal2;uint32_t horizontal;int32_t ny;uint64_t slope;
    if(sample==NULL)return 1000u;
    ny=sample->normal_y_q15;
    if(ny<=0)return 1000u;
    horizontal2=(uint64_t)((int64_t)sample->normal_x_q15*sample->normal_x_q15)+
                (uint64_t)((int64_t)sample->normal_z_q15*sample->normal_z_q15);
    horizontal=odg_isqrt_u64(horizontal2);
    slope=((uint64_t)horizontal*UINT64_C(1000))/(uint32_t)ny;
    return slope>1000u?1000u:(uint32_t)slope;
}

static int nest_position_clear(int32_t x,int32_t z,uint32_t spacing_milli){
    uint32_t i;int32_t spacing_fx=(int32_t)(((uint64_t)spacing_milli*ODG_FX_ONE)/1000u);
    int64_t min_d2=(int64_t)spacing_fx*spacing_fx;
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i){const odg_fauna_nest *n=&g_odg.fauna_nests[i];if(!n->active||!n->local_resident)continue;if(odg_dist2(x,z,n->x,n->z)<min_d2)return 0;}
    return 1;
}

static void tree_nest_branch_offset(const odg_resource_node *host,const odg_flora_species_definition *flora,
                                    int32_t *out_x,int32_t *out_z){
    static const int32_t dirs_q15[8][2]={{32767,0},{23170,23170},{0,32767},{-23170,23170},
                                         {-32767,0},{-23170,-23170},{0,-32767},{23170,-23170}};
    uint32_t host_radius_milli,branch_milli,dir;int32_t radius_fx,host_radius_fx;uint64_t mixed;
    if(host==NULL||flora==NULL||out_x==NULL||out_z==NULL){if(out_x!=NULL)*out_x=0;if(out_z!=NULL)*out_z=0;return;}
    host_radius_fx=odg_flora_collision_radius_fx_internal(flora,host->flora_stage);
    host_radius_milli=(uint32_t)(((uint64_t)(host_radius_fx>0?host_radius_fx:0)*1000u)/(uint32_t)ODG_FX_ONE);
    branch_milli=(uint32_t)(((uint64_t)host_radius_milli*850u)/1000u);
    if(branch_milli<360u)branch_milli=360u;
    if(branch_milli>700u)branch_milli=700u;
    radius_fx=(int32_t)(((uint64_t)branch_milli*ODG_FX_ONE)/1000u);
    mixed=host->stable_id^(host->stable_id>>32u)^UINT64_C(0x4252414e43484e53);
    dir=(uint32_t)(mixed&UINT64_C(7));
    *out_x=(int32_t)(((int64_t)dirs_q15[dir][0]*radius_fx)/ODG_Q15_ONE);
    *out_z=(int32_t)(((int64_t)dirs_q15[dir][1]*radius_fx)/ODG_Q15_ONE);
}

int odg_fauna_tree_nest_position_internal(const odg_resource_node *host,int32_t *out_x,int32_t *out_z){
    const odg_flora_species_definition *flora;int32_t ox,oz;
    if(host==NULL||out_x==NULL||out_z==NULL)return 0;
    flora=odg_resource_flora_definition_internal(host);
    if(flora==NULL||flora->growth_form!=ODG_FLORA_GROWTH_TREE)return 0;
    tree_nest_branch_offset(host,flora,&ox,&oz);
    *out_x=host->x+ox;*out_z=host->z+oz;return 1;
}

static int find_tree_nest_site(const odg_fauna_entity *e,const odg_fauna_nesting_definition *profile,fauna_nest_site *out){
    uint32_t i,best=UINT32_MAX;int32_t range_fx,best_x=0,best_z=0;int64_t best_d2;
    if(e==NULL||profile==NULL||out==NULL)return 0;
    range_fx=(int32_t)(((uint64_t)profile->search_range_milli*ODG_FX_ONE)/1000u);
    best_d2=(int64_t)range_fx*range_fx;
    for(i=0u;i<g_odg.resource_count;++i){
        const odg_resource_node *r=&g_odg_resources[i];const odg_flora_species_definition *flora;uint32_t j;int occupied=0;int64_t d2;int32_t nx,nz;
        if(!r->active||!r->local_resident||r->state!=ODG_RESOURCE_STATE_AVAILABLE||r->flora_stage<profile->min_host_flora_stage)continue;
        flora=odg_resource_flora_definition_internal(r);if(flora==NULL||flora->growth_form!=ODG_FLORA_GROWTH_TREE)continue;
        for(j=0u;j<ODG_FAUNA_MAX_NESTS;++j)if(g_odg.fauna_nests[j].active&&g_odg.fauna_nests[j].host_resource_stable_id==r->stable_id){occupied=1;break;}
        if(occupied)continue;
        if(!odg_fauna_tree_nest_position_internal(r,&nx,&nz))continue;
        if(!nest_position_clear(nx,nz,profile->nest_spacing_milli))continue;
        d2=odg_dist2(e->x,e->z,nx,nz);if(d2<best_d2){best_d2=d2;best=i;best_x=nx;best_z=nz;}
    }
    if(best==UINT32_MAX)return 0;
    out->substrate=ODG_NEST_SUBSTRATE_TREE;out->host_resource_stable_id=g_odg_resources[best].stable_id;
    out->x=best_x;out->z=best_z;
    out->global_fx_x=g_odg_resources[best].global_fx_x+(int64_t)(best_x-g_odg_resources[best].x);
    out->global_fx_z=g_odg_resources[best].global_fx_z+(int64_t)(best_z-g_odg_resources[best].z);
    out->local_resident=g_odg_resources[best].local_resident;return 1;
}

static int find_ground_nest_site(const odg_fauna_entity *e,const odg_fauna_nesting_definition *profile,fauna_nest_site *out){
    static const int32_t dirs[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    uint32_t radius_m,di,max_radius_m;
    if(e==NULL||profile==NULL||out==NULL)return 0;
    max_radius_m=profile->search_range_milli/1000u;if(max_radius_m<2u)max_radius_m=2u;if(max_radius_m>16u)max_radius_m=16u;
    for(radius_m=2u;radius_m<=max_radius_m;radius_m+=2u){
        for(di=0u;di<8u;++di){
            int32_t tx=e->x+dirs[di][0]*(int32_t)radius_m*ODG_FX_ONE;
            int32_t tz=e->z+dirs[di][1]*(int32_t)radius_m*ODG_FX_ONE;odg_surface_sample surface;
            if(!habitat_matches(e->species_id,tx,tz)||!odg_environment_surface_local(tx,tz,&surface))continue;
            if((surface.flags&ODG_SURFACE_FLAG_WATER)!=0u||surface_slope_permille(&surface)>profile->max_ground_slope_permille)continue;
            if(!nest_position_clear(tx,tz,profile->nest_spacing_milli))continue;
            if(odg_chunk_procedural_turret_reserves_local_circle_internal(tx,tz,ODG_FX_ONE/4))continue;
            if(!odg_position_clear_internal(tx,tz,ODG_FX_ONE/4))continue;
            if(!odg_dynamic_position_clear_internal(tx,tz,ODG_FX_ONE/4,UINT32_MAX,UINT32_MAX))continue;
            out->substrate=ODG_NEST_SUBSTRATE_GROUND;out->host_resource_stable_id=0u;out->x=tx;out->z=tz;out->local_resident=1u;
            odg_local_fx_to_global_fx_internal(tx,tz,&out->global_fx_x,&out->global_fx_z);return 1;
        }
    }
    return 0;
}

static int find_nest_site(const odg_fauna_entity *e,const odg_fauna_nesting_definition *profile,fauna_nest_site *out){
    if(profile==NULL||out==NULL)return 0;
    if((profile->substrate_mask&ODG_NEST_SUBSTRATE_TREE)!=0u&&find_tree_nest_site(e,profile,out))return 1;
    if((profile->substrate_mask&ODG_NEST_SUBSTRATE_GROUND)!=0u&&find_ground_nest_site(e,profile,out))return 1;
    return 0;
}

static int create_nest(odg_fauna_entity *female,odg_fauna_entity *male,const odg_fauna_species_definition *d){
    const odg_fauna_nesting_definition *profile;fauna_nest_site site;uint32_t nid,eggs;odg_fauna_nest *n;
    if(female==NULL||male==NULL||d==NULL||(d->behavior_flags&ODG_FAUNA_BEHAVIOR_NESTING)==0u)return 0;
    profile=odg_fauna_nesting_internal(d->species_id);if(profile==NULL||!find_nest_site(female,profile,&site))return 0;
    nid=alloc_nest();if(nid==UINT32_MAX)return 0;n=&g_odg.fauna_nests[nid];odg_memset(n,0,sizeof(*n));eggs=d->offspring_min;
    if(d->offspring_max>d->offspring_min)eggs+=odg_rand_bounded(&female->rng,d->offspring_max-d->offspring_min+1u);
    n->active=1u;n->id=nid;n->stable_id=g_odg.seed^female->stable_id^(male->stable_id<<1u)^g_odg.tick^UINT64_C(0x4e4553545f454747);
    n->species_id=d->species_id;n->substrate=site.substrate;n->egg_count=eggs;n->hatch_ticks=d->gestation_or_incubation_ticks;
    n->parent_a=female->id;n->parent_b=male->id;n->host_resource_stable_id=site.host_resource_stable_id;n->x=site.x;n->z=site.z;
    n->global_fx_x=site.global_fx_x;n->global_fx_z=site.global_fx_z;n->local_resident=site.local_resident;
    female->nest_id=nid;male->nest_id=nid;female->state=ODG_FAUNA_STATE_NEST;male->state=ODG_FAUNA_STATE_NEST;++g_odg.fauna_nest_count;return 1;
}

static void fauna_try_breed(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    uint32_t i;if(d==NULL||d->reproduction_mode==ODG_FAUNA_REPRO_NONE)return;if(e->life_stage!=ODG_FAUNA_STAGE_ADULT||e->sex!=ODG_FAUNA_SEX_FEMALE||e->breeding_cooldown!=0u||e->pregnancy_ticks!=0u||nest_active_for_entity(e)||e->satiety_permille<d->breeding_min_satiety_permille||e->hydration_permille<d->breeding_min_hydration_permille||fauna_population(e->species_id)>=d->wild_population_hard_cap)return;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){odg_fauna_entity *m=&g_odg.fauna[i];if(!m->active||m->species_id!=e->species_id||m->sex!=ODG_FAUNA_SEX_MALE||m->life_stage!=ODG_FAUNA_STAGE_ADULT||m->breeding_cooldown!=0u||nest_active_for_entity(m)||m->satiety_permille<d->breeding_min_satiety_permille||m->hydration_permille<d->breeding_min_hydration_permille)continue;if(odg_dist2(e->x,e->z,m->x,m->z)>(int64_t)(5*ODG_FX_ONE)*(5*ODG_FX_ONE))continue;if(d->reproduction_mode==ODG_FAUNA_REPRO_EGG){if(!create_nest(e,m,d))continue;}else e->pregnancy_ticks=d->gestation_or_incubation_ticks;e->breeding_cooldown=d->breeding_cooldown_ticks;m->breeding_cooldown=d->breeding_cooldown_ticks;break;}
}

static void fauna_birth(odg_fauna_entity *e,const odg_fauna_species_definition *d){
    uint32_t n;
    if(e==NULL||d==NULL)return;
    n=d->offspring_min;
    if(d->offspring_max>d->offspring_min)n+=odg_rand_bounded(&e->rng,d->offspring_max-d->offspring_min+1u);
    while(n-->0u&&fauna_population(e->species_id)<d->wild_population_hard_cap){
        uint32_t sex=odg_rand_bounded(&e->rng,2u)?ODG_FAUNA_SEX_MALE:ODG_FAUNA_SEX_FEMALE;
        (void)spawn_fauna_offspring_near(e->species_id,e->x,e->z,sex,&e->rng);
    }
}

static int nest_host_is_valid(const odg_fauna_nest *n){
    const odg_fauna_nesting_definition *profile;uint32_t i;int64_t player_d2;
    if(n==NULL)return 0;
    profile=odg_fauna_nesting_internal(n->species_id);
    if(profile==NULL||(profile->substrate_mask&n->substrate)==0u)return 0;
    if(n->substrate==ODG_NEST_SUBSTRATE_GROUND){
        odg_surface_sample surface;if(!n->local_resident)return 1;
        if(!habitat_matches(n->species_id,n->x,n->z)||!odg_environment_surface_local(n->x,n->z,&surface))return 0;
        if((surface.flags&ODG_SURFACE_FLAG_WATER)!=0u||surface_slope_permille(&surface)>profile->max_ground_slope_permille)return 0;
        /* A runtime nest is lightweight/non-solid to movement, but it may not survive
         * after solid world geometry or a sleeping natural turret claims its footprint.
         * This prevents eggs from visually living inside a newly streamed tree/wall. */
        if(odg_chunk_procedural_turret_reserves_local_circle_internal(n->x,n->z,ODG_FX_ONE/4))return 0;
        return odg_position_clear_internal(n->x,n->z,ODG_FX_ONE/4);
    }
    if(n->substrate!=ODG_NEST_SUBSTRATE_TREE||n->host_resource_stable_id==0u)return 0;
    for(i=0u;i<g_odg.resource_count;++i){
        const odg_resource_node *r=&g_odg_resources[i];const odg_flora_species_definition *flora;
        if(!r->active||r->stable_id!=n->host_resource_stable_id||r->state!=ODG_RESOURCE_STATE_AVAILABLE)continue;
        flora=odg_resource_flora_definition_internal(r);
        if(flora!=NULL&&flora->growth_form==ODG_FLORA_GROWTH_TREE&&r->flora_stage>=profile->min_host_flora_stage)return 1;
    }
    player_d2=odg_dist2(g_odg.actors[ODG_PLAYER_ID].x,g_odg.actors[ODG_PLAYER_ID].z,n->x,n->z);
    return player_d2>(int64_t)FAUNA_NEST_VISIBLE_VALIDATE_RANGE_FX*FAUNA_NEST_VISIBLE_VALIDATE_RANGE_FX;
}

static void clear_nest_parent_links(uint32_t nest_id){uint32_t i;for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i)if(g_odg.fauna[i].active&&g_odg.fauna[i].nest_id==nest_id)g_odg.fauna[i].nest_id=UINT32_MAX;}
static void destroy_nest(uint32_t nest_id){odg_fauna_nest *n;if(nest_id>=ODG_FAUNA_MAX_NESTS)return;n=&g_odg.fauna_nests[nest_id];if(!n->active)return;clear_nest_parent_links(nest_id);odg_memset(n,0,sizeof(*n));if(g_odg.fauna_nest_count>0u)--g_odg.fauna_nest_count;}

static void fauna_tick_nests(void){
    uint32_t i;
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i){
        odg_fauna_nest *n=&g_odg.fauna_nests[i];const odg_fauna_species_definition *d;uint32_t eggs;
        if(!n->active)continue;
        d=odg_fauna_species_internal(n->species_id);
        if(d==NULL||d->reproduction_mode!=ODG_FAUNA_REPRO_EGG||!nest_host_is_valid(n)){destroy_nest(i);continue;}
        if(n->hatch_ticks>0u)--n->hatch_ticks;
        if(n->hatch_ticks!=0u)continue;
        eggs=n->egg_count;
        {
            uint32_t remaining=0u;
            while(eggs-->0u){
                uint32_t sex;
                if(fauna_population(n->species_id)>=d->wild_population_hard_cap){remaining+=eggs+1u;break;}
                sex=odg_rand_bounded(&g_odg.ecology_rng,2u)?ODG_FAUNA_SEX_MALE:ODG_FAUNA_SEX_FEMALE;
                if(!spawn_fauna_offspring_near(n->species_id,n->x,n->z,sex,&g_odg.ecology_rng))++remaining;
            }
            if(remaining==0u){destroy_nest(i);continue;}
            /* Physical congestion must not silently delete incubated offspring. Keep only
             * eggs that did not hatch and retry at one-second cadence until space/capacity
             * exists or the nest becomes invalid by the normal host rules. */
            n->egg_count=remaining;n->hatch_ticks=ODG_TICK_RATE;
        }
    }
}

static void fauna_try_migration(void){
    uint32_t si;
    for(si=0u;si<table_count_species();++si){const odg_fauna_species_definition *d=&g_species[si];const odg_fauna_habitat_definition *h=odg_fauna_habitat_internal(d->species_id);if(h==NULL||h->migration_interval_ticks==0u||fauna_population(d->species_id)>=d->wild_population_target)continue;if((uint32_t)(g_odg.tick%(uint64_t)h->migration_interval_ticks)!=(uint32_t)(d->species_id%(h->migration_interval_ticks)))continue;(void)spawn_nearby_habitat(d->species_id,ODG_FAUNA_STAGE_ADULT,odg_rand_bounded(&g_odg.ecology_rng,2u)?ODG_FAUNA_SEX_MALE:ODG_FAUNA_SEX_FEMALE,1);}
}

void odg_fauna_tick(void){
    uint32_t i;fauna_tick_nests();fauna_try_migration();fauna_tick_hostile_spawns();
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){odg_fauna_entity *e=&g_odg.fauna[i];const odg_fauna_species_definition *d;if(!e->active)continue;d=odg_fauna_species_internal(e->species_id);if(d==NULL){if(nest_active_for_entity(e))destroy_nest(e->nest_id);odg_fauna_deactivate_internal(i);continue;}
        ++e->age_ticks;fauna_update_stage(e,d);if(e->age_ticks>=d->lifespan_ticks){if(nest_active_for_entity(e))destroy_nest(e->nest_id);odg_fauna_deactivate_internal(i);continue;}
        if(e->breeding_cooldown>0u)--e->breeding_cooldown;
        if(e->pregnancy_ticks>0u&&--e->pregnancy_ticks==0u)fauna_birth(e,d);
        if(++e->satiety_decay_accum>=d->satiety_decay_ticks){e->satiety_decay_accum=0u;if(e->satiety_permille>0u)--e->satiety_permille;}
        if((d->behavior_flags&ODG_FAUNA_BEHAVIOR_AQUATIC)!=0u){e->hydration_permille=1000u;e->hydration_decay_accum=0u;e->dehydration_accum=0u;}
        else if(d->hydration_decay_ticks>0u&&++e->hydration_decay_accum>=d->hydration_decay_ticks){e->hydration_decay_accum=0u;if(e->hydration_permille>0u)--e->hydration_permille;}
        if(e->satiety_permille==0u){if(++e->starvation_accum>=ODG_FAUNA_STARVATION_DAMAGE_TICKS){e->starvation_accum=0u;if(e->hp>1u)--e->hp;}}else e->starvation_accum=0u;
        if(e->hydration_permille==0u){if(++e->dehydration_accum>=ODG_FAUNA_DEHYDRATION_DAMAGE_TICKS){e->dehydration_accum=0u;if(e->hp>1u)--e->hp;}}else e->dehydration_accum=0u;
        if((g_odg.tick%ODG_TICK_RATE)==0u){int seeking_water=fauna_try_water(e,d);if(!seeking_water){fauna_try_feed(e,d);fauna_try_ambient_forage(e,d);}fauna_try_breed(e,d);}
        {int retreat=fauna_nocturnal_retreat(e,d);if(retreat==2)continue;if(retreat==1){fauna_move(e,d);continue;}}
        {int predation=fauna_try_predation(e,d);if(predation==2)continue;if(predation==1){fauna_move(e,d);continue;}}
        if(fauna_tick_aggression(e,d)){fauna_move(e,d);continue;}
        if(e->decision_ticks>0u)--e->decision_ticks;else fauna_choose_direction(e,d);fauna_move(e,d);
    }
}

void odg_fauna_refresh_local_cache(void){
    uint32_t i;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        odg_fauna_entity *e=&g_odg.fauna[i];int32_t x,z;
        if(!e->active)continue;
        if(odg_global_fx_to_local_internal(e->global_fx_x,e->global_fx_z,&x,&z)){
            const odg_fauna_species_definition *d=odg_fauna_species_internal(e->species_id);
            e->x=x;e->z=z;e->local_resident=1u;fauna_sync_aquatic_height(e,d);
            if(d!=NULL&&(d->behavior_flags&ODG_FAUNA_BEHAVIOR_AMPHIBIOUS)!=0u)fauna_sync_surface_swim_height(e,d);
        }else e->local_resident=0u;
    }
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i){
        odg_fauna_nest *n=&g_odg.fauna_nests[i];int32_t x,z;
        if(!n->active)continue;
        if(odg_global_fx_to_local_internal(n->global_fx_x,n->global_fx_z,&x,&z)){n->x=x;n->z=z;n->local_resident=1u;}
        else n->local_resident=0u;
    }
}
void odg_fauna_shift_local(int32_t shift_x,int32_t shift_z){uint32_t i;for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i)if(g_odg.fauna[i].active&&g_odg.fauna[i].local_resident){g_odg.fauna[i].x-=shift_x;g_odg.fauna[i].z-=shift_z;}for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i)if(g_odg.fauna_nests[i].active&&g_odg.fauna_nests[i].local_resident){g_odg.fauna_nests[i].x-=shift_x;g_odg.fauna_nests[i].z-=shift_z;}}

static int fauna_actor_may_attack(uint32_t actor_id,const odg_fauna_entity *e,const odg_fauna_species_definition *species){
    if(e==NULL||species==NULL||actor_id>=ODG_MAX_ACTORS)return 0;
    /* Territory protects wild resources from opportunistic harvesting, but it must never
     * make an actively hostile/retaliating animal invulnerable. Self-defense is a combat
     * right, not environmental harvesting. */
    if((species->behavior_flags&ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS)!=0u)return 1;
    if(e->id<ODG_FAUNA_MAX_ENTRIES&&g_odg_persistent_runtime.fauna_target_actor[e->id]==actor_id)return 1;
    return odg_territory_allows_environment_action(actor_id,e->x,e->z);
}

int odg_fauna_build_hint(const odg_actor *actor,const odg_item_stack *selected,odg_interaction_hint *hint){
    uint32_t i,best=UINT32_MAX,damage,hits;int64_t best_d2=(int64_t)(2*ODG_FX_ONE)*(2*ODG_FX_ONE);
    const odg_item_definition *def;
    if(actor==NULL||selected==NULL||hint==NULL)return 0;
    def=odg_item_definition_internal(selected->type_id);
    if(def==NULL||(def->capability_bits&(ODG_ITEM_CAP_HUNT|ODG_ITEM_CAP_ATTACK))==0u)return 0;
    damage=odg_item_attack_damage_internal(selected);if(damage==0u)return 0;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        odg_fauna_entity *e=&g_odg.fauna[i];const odg_fauna_species_definition *species;int64_t d2;
        if(!e->active||!e->local_resident)continue;
        species=odg_fauna_species_internal(e->species_id);
        if((def->capability_bits&ODG_ITEM_CAP_HUNT)==0u&&(species==NULL||(species->behavior_flags&ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS)==0u))continue;
        d2=odg_dist2(actor->x,actor->z,e->x,e->z);if(d2<best_d2){best_d2=d2;best=i;}
    }
    if(best==UINT32_MAX)return 0;
    hits=(g_odg.fauna[best].hp+damage-1u)/damage;if(hits==0u)hits=1u;
    hint->action=ODG_INTERACTION_HUNT_FAUNA;hint->target_kind=ODG_INTERACTION_TARGET_FAUNA;hint->target_id=best;
    {
        const odg_fauna_species_definition *target_species=odg_fauna_species_internal(g_odg.fauna[best].species_id);
        hint->valid=fauna_actor_may_attack(actor->id,&g_odg.fauna[best],target_species)?1u:0u;
    }
    hint->requires_hold=1u;
    /* The UI progress estimate now reflects actual strikes/cooldown, not raw animal HP
     * interpreted as frame count. First strike can occur immediately. */
    hint->threshold_ticks=1u+(hits-1u)*ODG_MELEE_COOLDOWN_TICKS;
    hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_TERRITORY_REQUIRED;
    return 1;
}

static int spawn_loot(const odg_fauna_entity *entity,const odg_loot_table_definition *table){
    odg_item_stack planned[ODG_LOOT_MAX_ENTRIES];
    odm_rng staged_rng;
    uint32_t i,planned_count=0u;
    if(entity==NULL)return 0;
    if(table==NULL)return 1;
    staged_rng=g_odg.ecology_rng;
    for(i=0u;i<table->entry_count&&i<ODG_LOOT_MAX_ENTRIES;++i){
        const odg_loot_entry *entry=&table->entries[i];
        const odg_item_definition *item;
        odg_item_stack stack;
        uint32_t quantity;
        if(entry->item_type==ODG_ITEM_NONE||
           odg_rand_bounded(&staged_rng,1000u)>=entry->chance_permille)continue;
        item=odg_item_definition_internal(entry->item_type);
        if(item==NULL)continue;
        quantity=entry->quantity_min;
        if(entry->quantity_max>entry->quantity_min){
            quantity+=odg_rand_bounded(&staged_rng,entry->quantity_max-entry->quantity_min+1u);
        }
        odg_memset(&stack,0,sizeof(stack));
        stack.type_id=item->type_id;stack.quantity=quantity;stack.flags=item->flags;stack.material_tier=item->default_material_tier;
        if(!odg_item_stack_normalize_internal(&stack))return 0;
        planned[planned_count++]=stack;
    }
    /* Keep kill + loot atomic under allocation pressure. RNG is staged too, so a failed
     * capacity reservation neither kills the animal nor consumes different future rolls. */
    if(!odg_world_pickups_prepare_internal(planned_count))return 0;
    for(i=0u;i<planned_count;++i){
        if(!odg_spawn_world_pickup(&planned[i],entity->x,entity->z,20u))return 0;
    }
    /* RNG is part of the loot transaction too. Publish the staged stream only after all
     * planned pickups exist; a rejected kill cannot consume a different future loot roll. */
    g_odg.ecology_rng=staged_rng;
    return 1;
}

int odg_fauna_hunt(uint32_t actor_id,uint32_t fauna_id){
    odg_actor *a;odg_fauna_entity *e;odg_item_stack *tool_stack;
    const odg_item_definition *tool;const odg_fauna_species_definition *d;uint32_t damage;
    if(actor_id>=ODG_MAX_ACTORS||fauna_id>=ODG_FAUNA_MAX_ENTRIES)return 0;
    a=&g_odg.actors[actor_id];e=&g_odg.fauna[fauna_id];
    if(!e->active||!e->local_resident||a->melee_cooldown_ticks!=0u||
       odg_dist2(a->x,a->z,e->x,e->z)>(int64_t)(2*ODG_FX_ONE)*(2*ODG_FX_ONE))return 0;
    tool_stack=odg_inventory_selected(&a->inventory);
    tool=tool_stack!=NULL?odg_item_definition_internal(tool_stack->type_id):NULL;
    d=odg_fauna_species_internal(e->species_id);
    if(d==NULL||!fauna_actor_may_attack(actor_id,e,d))return 0;
    if(tool==NULL||((tool->capability_bits&ODG_ITEM_CAP_HUNT)==0u&&
       (d==NULL||(d->behavior_flags&ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS)==0u||(tool->capability_bits&ODG_ITEM_CAP_ATTACK)==0u)))return 0;
    damage=odg_item_attack_damage_internal(tool_stack);if(damage==0u)return 0;
    if(damage>=e->hp&&!spawn_loot(e,loot_internal(d->loot_table_id)))return 0;
    a->melee_cooldown_ticks=ODG_MELEE_COOLDOWN_TICKS;
    odg_item_wear_internal(tool_stack,1u);
    if(damage<e->hp){
        e->hp-=damage;
        if(d!=NULL&&(d->behavior_flags&ODG_FAUNA_BEHAVIOR_DEFENSIVE_ATTACK)!=0u&&e->life_stage==ODG_FAUNA_STAGE_ADULT&&e->sex==ODG_FAUNA_SEX_MALE){
            const odg_fauna_aggression_profile *profile=aggression_profile(e->species_id);
            g_odg_persistent_runtime.fauna_target_actor[e->id]=actor_id;
            g_odg_persistent_runtime.fauna_aggro_ticks[e->id]=profile!=NULL?profile->retaliation_ticks:4u*ODG_TICK_RATE;
            e->state=ODG_FAUNA_STATE_AGGRO;
        }else{e->state=ODG_FAUNA_STATE_FLEE;e->decision_ticks=0u;}
        return 1;
    }
    if(nest_active_for_entity(e))destroy_nest(e->nest_id);
    odg_fauna_deactivate_internal(fauna_id);
    return 2;
}


int32_t odg_copy_fauna(odg_fauna_snapshot *out,uint64_t capacity,uint64_t *required){uint32_t i,n=0u;if(required!=NULL)*required=sizeof(*out);if(!g_odg.initialized)return ODG_STATUS_INVALID_STATE;if(out==NULL||capacity<sizeof(*out))return ODG_STATUS_BUFFER_TOO_SMALL;odg_memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);for(i=0u;i<ODG_FAUNA_MAX_ENTRIES&&n<ODG_FAUNA_MAX_ENTRIES;++i){const odg_fauna_entity *e=&g_odg.fauna[i];odg_fauna_entry *v;int64_t xm,zm;if(!e->active)continue;v=&out->entries[n++];xm=(e->global_fx_x*1000)/(int64_t)ODG_FX_ONE;zm=(e->global_fx_z*1000)/(int64_t)ODG_FX_ONE;v->stable_id=e->stable_id;v->fauna_id=e->id;v->species_id=e->species_id;v->family=e->family;v->variant=e->variant;v->state=e->state;v->tame=e->tame;v->owner_actor_id=e->owner_actor_id;v->health=e->hp;v->max_health=e->max_hp;v->satiety_permille=e->satiety_permille;v->hydration_permille=e->hydration_permille;v->life_stage=e->life_stage;v->sex=e->sex;v->nest_id=e->nest_id;v->age_ticks=e->age_ticks;v->x_milli=xm<INT32_MIN?INT32_MIN:(xm>INT32_MAX?INT32_MAX:(int32_t)xm);v->z_milli=zm<INT32_MIN?INT32_MIN:(zm>INT32_MAX?INT32_MAX:(int32_t)zm);v->y_milli=(odg_terrain_height_fx(e->x,e->z)*1000)/(int32_t)ODG_FX_ONE+(e->y_offset_fx*1000)/(int32_t)ODG_FX_ONE;if(e->family==ODG_FAUNA_FAMILY_BIRD)++out->bird_count;else if(e->family==ODG_FAUNA_FAMILY_MAMMAL)++out->mammal_count;}out->count=n;return ODG_STATUS_OK;}

int32_t odg_copy_fauna_nests(odg_fauna_nest_snapshot *out,uint64_t capacity,uint64_t *required){uint32_t i,n=0u;if(required!=NULL)*required=sizeof(*out);if(!g_odg.initialized)return ODG_STATUS_INVALID_STATE;if(out==NULL||capacity<sizeof(*out))return ODG_STATUS_BUFFER_TOO_SMALL;odg_memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);for(i=0u;i<ODG_FAUNA_MAX_NESTS&&n<ODG_FAUNA_MAX_NESTS;++i){const odg_fauna_nest *src=&g_odg.fauna_nests[i];const odg_fauna_nesting_definition *profile;odg_fauna_nest_entry *dst;int64_t xm,zm;if(!src->active)continue;profile=odg_fauna_nesting_internal(src->species_id);dst=&out->entries[n++];xm=(src->global_fx_x*1000)/(int64_t)ODG_FX_ONE;zm=(src->global_fx_z*1000)/(int64_t)ODG_FX_ONE;dst->stable_id=src->stable_id;dst->nest_id=src->id;dst->species_id=src->species_id;dst->state=ODG_FAUNA_NEST_STATE_INCUBATING;dst->substrate=src->substrate;dst->egg_count=src->egg_count;dst->hatch_ticks=src->hatch_ticks;dst->parent_a=src->parent_a;dst->parent_b=src->parent_b;dst->host_resource_stable_id=src->host_resource_stable_id;dst->x_milli=xm<INT32_MIN?INT32_MIN:(xm>INT32_MAX?INT32_MAX:(int32_t)xm);dst->z_milli=zm<INT32_MIN?INT32_MIN:(zm>INT32_MAX?INT32_MAX:(int32_t)zm);dst->y_milli=(odg_terrain_height_fx(src->x,src->z)*1000)/(int32_t)ODG_FX_ONE+(profile!=NULL?profile->height_offset_milli:120);}out->count=n;return ODG_STATUS_OK;}
