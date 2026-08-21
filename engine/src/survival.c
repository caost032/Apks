#include "game_internal.h"

#include <stdint.h>

#define ODG_ACTOR_BREATH_HEIGHT_MILLI UINT32_C(850)
#define ODG_FAUNA_BREATH_HEIGHT_MILLI UINT32_C(420)
#define ODG_OXYGEN_LOSS_INTERVAL_TICKS UINT32_C(4) /* ~33.3 s from full to empty */
#define ODG_OXYGEN_RECOVERY_PER_TICK UINT32_C(2)   /* ~4.2 s from empty to full */
#define ODG_DROWNING_DAMAGE_TICKS (ODG_TICK_RATE * UINT32_C(3) / UINT32_C(2))
#define ODG_DROWNING_DAMAGE UINT32_C(8)

odg_persistent_runtime_state g_odg_persistent_runtime;

static void respiration_full(odg_respiration_state *state){
    if(state==NULL)return;
    state->oxygen_permille=1000u;
    state->oxygen_loss_accum=0u;
    state->drowning_accum=0u;
}

static void persistent_runtime_reset(uint32_t worldgen_version){
    uint32_t i;
    odg_memset(&g_odg_persistent_runtime,0,sizeof(g_odg_persistent_runtime));
    g_odg_persistent_runtime.struct_size=(uint32_t)sizeof(g_odg_persistent_runtime);
    g_odg_persistent_runtime.worldgen_version=worldgen_version;
    for(i=0u;i<ODG_MAX_ACTORS;++i)respiration_full(&g_odg_persistent_runtime.actors[i]);
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        respiration_full(&g_odg_persistent_runtime.fauna[i]);
        g_odg_persistent_runtime.fauna_target_actor[i]=UINT32_MAX;
    }
}

void odg_survival_reset_new_world(void){persistent_runtime_reset(ODG_WORLDGEN_VERSION_CURRENT);}
void odg_survival_reset_legacy_world(void){persistent_runtime_reset(ODG_WORLDGEN_VERSION_LEGACY);}
void odg_survival_reset_actor(uint32_t actor_id){if(actor_id<ODG_MAX_ACTORS)respiration_full(&g_odg_persistent_runtime.actors[actor_id]);}
void odg_survival_reset_fauna(uint32_t fauna_id){
    if(fauna_id>=ODG_FAUNA_MAX_ENTRIES)return;
    respiration_full(&g_odg_persistent_runtime.fauna[fauna_id]);
    g_odg_persistent_runtime.fauna_attack_cooldown[fauna_id]=0u;
    g_odg_persistent_runtime.fauna_target_actor[fauna_id]=UINT32_MAX;
    g_odg_persistent_runtime.fauna_aggro_ticks[fauna_id]=0u;
}

static int respiration_state_validate_internal(const odg_respiration_state *state){
    if(state==NULL||state->oxygen_permille>1000u||
       state->oxygen_loss_accum>=ODG_OXYGEN_LOSS_INTERVAL_TICKS||
       state->drowning_accum>=ODG_DROWNING_DAMAGE_TICKS)return 0;
    /* Drowning only accumulates after oxygen is exactly zero; oxygen loss is reset on
     * the tick that reaches zero. These phase constraints prevent huge/stale timers from
     * becoming hidden persistent gameplay state. */
    if(state->oxygen_permille>0u&&state->drowning_accum!=0u)return 0;
    if(state->oxygen_permille==0u&&state->oxygen_loss_accum!=0u)return 0;
    return 1;
}

int odg_survival_state_validate(const odg_persistent_runtime_state *state){
    uint32_t i;
    if(state==NULL||state->struct_size!=(uint32_t)sizeof(*state))return 0;
    if(state->worldgen_version<ODG_WORLDGEN_VERSION_LEGACY||state->worldgen_version>ODG_WORLDGEN_VERSION_CURRENT)return 0;
    for(i=0u;i<ODG_MAX_ACTORS;++i)if(!respiration_state_validate_internal(&state->actors[i]))return 0;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        /* This is intentionally structural only. During save load the runtime section is
         * parsed before the saved fauna prefix is installed, so consulting g_odg.fauna
         * here would make file validity depend on whichever world happened to be open.
         * Species-specific combat semantics are checked after the loaded fauna exists. */
        if(!respiration_state_validate_internal(&state->fauna[i])||
           (state->fauna_target_actor[i]!=UINT32_MAX&&state->fauna_target_actor[i]>=ODG_MAX_ACTORS))return 0;
    }
    return 1;
}

int odg_survival_loaded_fauna_state_validate_internal(uint32_t fauna_id){
    const odg_respiration_state *respiration;
    if(fauna_id>=ODG_FAUNA_MAX_ENTRIES)return 0;
    respiration=&g_odg_persistent_runtime.fauna[fauna_id];
    if(!respiration_state_validate_internal(respiration))return 0;
    if(!g_odg.fauna[fauna_id].active){
        if(respiration->oxygen_permille!=1000u||respiration->oxygen_loss_accum!=0u||respiration->drowning_accum!=0u)return 0;
    }
    return odg_fauna_runtime_combat_state_validate_internal(fauna_id,
        g_odg_persistent_runtime.fauna_attack_cooldown[fauna_id],
        g_odg_persistent_runtime.fauna_target_actor[fauna_id],
        g_odg_persistent_runtime.fauna_aggro_ticks[fauna_id]);
}

uint32_t odg_worldgen_version(void){
    return g_odg.initialized?g_odg_persistent_runtime.worldgen_version:0u;
}

int odg_actor_is_swimming_internal(const odg_actor *actor){
    odg_surface_sample surface;
    if(actor==NULL||!actor->active||actor->hp==0u||actor->local_resident==0u)return 0;
    if(odg_artifact_actor_vehicle_internal(actor->id)!=UINT32_MAX)return 0;
    if(!odg_environment_surface_local(actor->x,actor->z,&surface))return 0;
    return (surface.flags&ODG_SURFACE_FLAG_WATER)!=0u&&surface.water_depth_milli>=ODG_SWIM_MIN_DEPTH_MILLI;
}

int32_t odg_actor_swim_target_offset_fx_internal(const odg_actor *actor){
    odg_surface_sample surface;
    uint32_t lift_milli;
    uint64_t lift_fx;
    if(actor==NULL||!odg_environment_surface_local(actor->x,actor->z,&surface) ||
       (surface.flags&ODG_SURFACE_FLAG_WATER)==0u||surface.water_depth_milli<ODG_SWIM_MIN_DEPTH_MILLI)return 0;
    lift_milli=surface.water_depth_milli>ODG_SWIM_DRAFT_MILLI?surface.water_depth_milli-ODG_SWIM_DRAFT_MILLI:0u;
    lift_fx=((uint64_t)lift_milli*(uint64_t)ODG_FX_ONE)/1000u;
    return lift_fx>(uint64_t)INT32_MAX?INT32_MAX:(int32_t)lift_fx;
}

static int actor_head_submerged(const odg_actor *actor){
    odg_surface_sample surface;
    int32_t vertical_milli;
    int64_t breath_height;
    if(actor==NULL||!actor->active||!actor->local_resident||actor->hp==0u)return 0;
    if(!odg_environment_surface_local(actor->x,actor->z,&surface)||(surface.flags&ODG_SURFACE_FLAG_WATER)==0u)return 0;
    vertical_milli=(actor->vertical_offset_fx*1000)/ODG_FX_ONE;
    breath_height=(int64_t)ODG_ACTOR_BREATH_HEIGHT_MILLI+(int64_t)vertical_milli;
    if(breath_height<100)breath_height=100;
    return (uint64_t)surface.water_depth_milli>=(uint64_t)breath_height;
}

static int fauna_is_aquatic(const odg_fauna_entity *entity){
    const odg_fauna_species_definition *def;
    if(entity==NULL)return 0;
    def=odg_fauna_species_internal(entity->species_id);
    return def!=NULL&&(def->behavior_flags&ODG_FAUNA_BEHAVIOR_AQUATIC)!=0u;
}

static int fauna_head_submerged(const odg_fauna_entity *entity){
    odg_surface_sample surface;
    int64_t breath_height;
    const odg_fauna_species_definition *def;
    if(entity==NULL||!entity->active||!entity->local_resident||entity->hp==0u)return 0;
    if(!odg_environment_surface_local(entity->x,entity->z,&surface)||(surface.flags&ODG_SURFACE_FLAG_WATER)==0u)return 0;
    def=odg_fauna_species_internal(entity->species_id);
    breath_height=def!=NULL?(int64_t)def->body_radius_milli*2:ODG_FAUNA_BREATH_HEIGHT_MILLI;
    breath_height+=(int64_t)entity->y_offset_fx*1000/ODG_FX_ONE;
    if(breath_height<120)breath_height=120;
    return (uint64_t)surface.water_depth_milli>=(uint64_t)breath_height;
}

static int respiration_update(odg_respiration_state *state,int submerged){
    if(state==NULL)return 0;
    if(!submerged){
        state->oxygen_loss_accum=0u;
        state->drowning_accum=0u;
        if(state->oxygen_permille<1000u){
            uint32_t restored=state->oxygen_permille+ODG_OXYGEN_RECOVERY_PER_TICK;
            state->oxygen_permille=restored>1000u?1000u:restored;
        }
        return 0;
    }
    if(state->oxygen_permille>0u){
        if(++state->oxygen_loss_accum>=ODG_OXYGEN_LOSS_INTERVAL_TICKS){
            state->oxygen_loss_accum=0u;
            --state->oxygen_permille;
        }
        state->drowning_accum=0u;
        return 0;
    }
    if(++state->drowning_accum>=ODG_DROWNING_DAMAGE_TICKS){
        state->drowning_accum=0u;
        return 1;
    }
    return 0;
}

void odg_survival_tick(void){
    uint32_t i;
    for(i=0u;i<ODG_MAX_ACTORS;++i){
        odg_actor *actor=&g_odg.actors[i];
        odg_respiration_state *resp=&g_odg_persistent_runtime.actors[i];
        if(!actor->active||actor->hp==0u){respiration_full(resp);continue;}
        if(respiration_update(resp,actor_head_submerged(actor)))
            odg_actor_apply_damage_internal(i,i,ODG_DROWNING_DAMAGE,ODG_DEATH_DROWNING);
    }
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        odg_fauna_entity *entity=&g_odg.fauna[i];
        odg_respiration_state *resp=&g_odg_persistent_runtime.fauna[i];
        if(!entity->active||entity->hp==0u){respiration_full(resp);continue;}
        if(fauna_is_aquatic(entity)){respiration_full(resp);continue;}
        if(respiration_update(resp,fauna_head_submerged(entity))){
            if(entity->hp>ODG_DROWNING_DAMAGE)entity->hp-=ODG_DROWNING_DAMAGE;
            else {odg_fauna_deactivate_internal(i);}
        }
    }
}

uint32_t odg_player_oxygen_permille(void){
    if(!g_odg.initialized)return 0u;
    return g_odg_persistent_runtime.actors[ODG_PLAYER_ID].oxygen_permille;
}
