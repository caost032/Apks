#include "game_internal.h"

#include <stddef.h>
#include <stdint.h>

static uint32_t nearest_enemy_actor(const odg_actor *actor){
    uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)ODG_MELEE_RANGE_FX*ODG_MELEE_RANGE_FX;
    if(actor==NULL)return UINT32_MAX;
    for(i=0u;i<ODG_MAX_ACTORS;++i){const odg_actor *other=&g_odg.actors[i];int64_t d2;if(i==actor->id||!other->active||other->hp==0u||!other->local_resident)continue;d2=odg_dist2(actor->x,actor->z,other->x,other->z);if(d2<best_d2){best_d2=d2;best=i;}}
    return best;
}


static int melee_attack_actor(uint32_t attacker_id,uint32_t victim_id){
    odg_actor *attacker;odg_item_stack *tool;const odg_item_definition *def;uint32_t damage;
    if(attacker_id>=ODG_MAX_ACTORS||victim_id>=ODG_MAX_ACTORS||attacker_id==victim_id)return 0;
    attacker=&g_odg.actors[attacker_id];if(attacker->melee_cooldown_ticks!=0u)return 0;
    if(!g_odg.actors[victim_id].active||g_odg.actors[victim_id].hp==0u||odg_dist2(attacker->x,attacker->z,g_odg.actors[victim_id].x,g_odg.actors[victim_id].z)>(int64_t)ODG_MELEE_RANGE_FX*ODG_MELEE_RANGE_FX)return 0;
    tool=odg_inventory_selected(&attacker->inventory);
    def=tool!=NULL?odg_item_definition_internal(tool->type_id):NULL;
    if(def==NULL||(def->capability_bits&ODG_ITEM_CAP_ATTACK)==0u)return 0;
    damage=odg_item_attack_damage_internal(tool);
    if(damage==0u)return 0;
    odg_actor_apply_damage_internal(victim_id,attacker_id,damage,ODG_DEATH_COMBAT);
    odg_item_wear_internal(tool,1u);
    attacker->melee_cooldown_ticks=ODG_MELEE_COOLDOWN_TICKS;
    return 1;
}

static int melee_attack_construction(uint32_t attacker_id,uint32_t construction_id){
    odg_actor *attacker;odg_item_stack *tool;const odg_item_definition *def;uint32_t damage;
    if(attacker_id>=ODG_MAX_ACTORS)return 0;
    attacker=&g_odg.actors[attacker_id];if(attacker->melee_cooldown_ticks!=0u)return 0;
    tool=odg_inventory_selected(&attacker->inventory);def=tool!=NULL?odg_item_definition_internal(tool->type_id):NULL;
    if(def==NULL||(def->capability_bits&ODG_ITEM_CAP_ATTACK)==0u)return 0;
    damage=odg_item_attack_damage_internal(tool);if(damage==0u)return 0;
    if(!odg_construction_apply_damage_internal(attacker_id,construction_id,damage))return 0;
    odg_item_wear_internal(tool,1u);attacker->melee_cooldown_ticks=ODG_MELEE_COOLDOWN_TICKS;return 1;
}

static int selected_fluid_container(odg_actor *actor,odg_item_stack **out_stack,
                                    const odg_fluid_container_definition **out_container){
    odg_item_stack *stack;const odg_fluid_container_definition *container;
    if(actor==NULL)return 0;
    stack=odg_inventory_selected(&actor->inventory);
    if(stack==NULL||stack->quantity==0u)return 0;
    container=odg_fluid_container_definition_internal(stack->type_id);
    if(container==NULL)return 0;
    if(out_stack!=NULL)*out_stack=stack;
    if(out_container!=NULL)*out_container=container;
    return 1;
}

static int container_accepts_fluid(const odg_fluid_container_definition *container,uint32_t fluid_id){
    const odg_fluid_definition *fluid=odg_fluid_definition_internal(fluid_id);
    return container!=NULL&&fluid!=NULL&&(fluid->flags&container->accepted_fluid_flags)!=0u;
}

static int fill_container_from_environment(uint32_t actor_id){
    odg_actor *a;odg_item_stack *stack;const odg_fluid_container_definition *container;odg_surface_sample surface;
    uint32_t fluid_id,units;int32_t tx,tz;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    a=&g_odg.actors[actor_id];
    if(!selected_fluid_container(a,&stack,&container)||!container_accepts_fluid(container,ODG_FLUID_WATER))return 0;
    fluid_id=odg_fluid_payload_id_internal(stack->payload_id);units=odg_fluid_payload_units_internal(stack->payload_id);
    if(units>=container->capacity_units||(fluid_id!=ODG_FLUID_NONE&&fluid_id!=ODG_FLUID_WATER))return 0;
    tx=a->x+(int32_t)(((int64_t)a->face_x_q15*ODG_FX_ONE)/ODG_Q15_ONE);
    tz=a->z+(int32_t)(((int64_t)a->face_z_q15*ODG_FX_ONE)/ODG_Q15_ONE);
    if(!odg_environment_surface_local(tx,tz,&surface)||(surface.flags&ODG_SURFACE_FLAG_WATER)==0u||
       !odg_territory_allows_environment_action(actor_id,tx,tz))return 0;
    stack->payload_id=odg_fluid_payload_make_internal(ODG_FLUID_WATER,container->capacity_units);return 1;
}

static int fill_container_from_barrel(uint32_t actor_id){
    odg_actor *a;odg_item_stack *stack;const odg_fluid_container_definition *container;uint32_t fluid_id,units,i,best=UINT32_MAX;
    int64_t best_d2=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    a=&g_odg.actors[actor_id];
    if(!selected_fluid_container(a,&stack,&container)||!container_accepts_fluid(container,ODG_FLUID_WATER))return 0;
    fluid_id=odg_fluid_payload_id_internal(stack->payload_id);units=odg_fluid_payload_units_internal(stack->payload_id);
    if(units>=container->capacity_units||(fluid_id!=ODG_FLUID_NONE&&fluid_id!=ODG_FLUID_WATER))return 0;
    for(i=0u;i<g_odg.artifact_count;++i){
        odg_artifact *barrel=&g_odg_artifacts[i];int64_t d2;
        if(!barrel->active||barrel->local_resident==0u||(barrel->capability_bits&ODG_ARTIFACT_CAP_COLLECT_RAIN)==0u||barrel->aux_u32==0u||
           (barrel->fluid_type_id!=ODG_FLUID_NONE&&barrel->fluid_type_id!=ODG_FLUID_WATER))continue;
        if(barrel->owner_actor_id!=actor_id&&!odg_territory_actor_controls_position(actor_id,barrel->x,barrel->z))continue;
        d2=odg_dist2(a->x,a->z,barrel->x,barrel->z);if(d2<best_d2){best_d2=d2;best=i;}
    }
    if(best==UINT32_MAX)return 0;
    {
        odg_artifact *barrel=&g_odg_artifacts[best];uint32_t need=container->capacity_units-units;
        uint32_t take=barrel->aux_u32<need?barrel->aux_u32:need;barrel->aux_u32-=take;units+=take;
        stack->payload_id=odg_fluid_payload_make_internal(ODG_FLUID_WATER,units);
        if(barrel->aux_u32==0u)barrel->fluid_type_id=ODG_FLUID_NONE;
    }
    return 1;
}

static int irrigate_from_container(uint32_t actor_id){
    odg_actor *a;odg_item_stack *stack;const odg_fluid_container_definition *container;const odg_fluid_definition *fluid;
    uint32_t used=0u,units,fluid_id;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    a=&g_odg.actors[actor_id];
    if(!selected_fluid_container(a,&stack,&container))return 0;
    fluid_id=odg_fluid_payload_id_internal(stack->payload_id);units=odg_fluid_payload_units_internal(stack->payload_id);
    fluid=odg_fluid_definition_internal(fluid_id);if(fluid==NULL||units==0u||(fluid->flags&ODG_FLUID_FLAG_IRRIGATION)==0u)return 0;
    if(!odg_ecology_irrigate_nearest(actor_id,units,&used)||used==0u)return 0;
    units-=used;stack->payload_id=odg_fluid_payload_make_internal(units==0u?ODG_FLUID_NONE:fluid_id,units);return 1;
}

int odg_actor_drink_environment_internal(uint32_t actor_id){
    odg_actor *actor;odg_surface_sample surface;int32_t tx,tz;uint32_t used=0u;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];
    if(!actor->active||actor->hp==0u||actor->hydration_permille>=ODG_ACTOR_HYDRATION_MAX)return 0;
    tx=actor->x+(int32_t)(((int64_t)actor->face_x_q15*ODG_FX_ONE)/ODG_Q15_ONE);
    tz=actor->z+(int32_t)(((int64_t)actor->face_z_q15*ODG_FX_ONE)/ODG_Q15_ONE);
    if(!odg_environment_surface_local(tx,tz,&surface)||(surface.flags&ODG_SURFACE_FLAG_WATER)==0u||
       !odg_territory_allows_environment_action(actor_id,tx,tz))return 0;
    return odg_actor_drink_fluid_internal(actor_id,ODG_FLUID_WATER,100u,&used);
}

typedef struct {
    uint32_t material_tier;
    uint32_t max_ammo;
    uint32_t fire_period;
    uint32_t aim_required;
    int32_t range_fx;
} odg_turret_tier_profile;

static const odg_turret_tier_profile g_turret_tier_profiles[]={
    {ODG_MATERIAL_WOOD,  32u,504u,288u,11*ODG_FX_ONE}, /* 4.2s fire, 2.4s lock */
    {ODG_MATERIAL_STONE, 48u,408u,240u,14*ODG_FX_ONE}, /* 3.4s fire, 2.0s lock */
    {ODG_MATERIAL_IRON,  72u,336u,204u,17*ODG_FX_ONE}  /* 2.8s fire, 1.7s lock */
};

static const odg_turret_tier_profile *turret_tier_profile(uint32_t tier){
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_turret_tier_profiles)/sizeof(g_turret_tier_profiles[0]));++i)
        if(g_turret_tier_profiles[i].material_tier==tier)return &g_turret_tier_profiles[i];
    return NULL;
}

int odg_turret_persisted_profile_validate_internal(uint32_t material_tier,uint32_t mode,uint32_t ammo){
    const odg_turret_tier_profile *profile=turret_tier_profile(material_tier);
    return profile!=NULL&&mode<=ODG_TURRET_MODE_HARVEST&&ammo<=profile->max_ammo;
}

int odg_turret_state_validate_internal(const odg_turret *turret,uint32_t expected_id){
    odg_turret canonical;
    if(turret==NULL||turret->id!=expected_id||turret->active>1u||turret->procedural>1u||
       turret->local_resident>1u)return 0;
    if(!turret->active&&turret->instance_id==0u){
        /* Free dynamic slots are canonical tombstones. Do not serialize stale combat or
         * floating-origin history into an object the allocator may later recycle. */
        return turret->procedural==0u&&turret->source_chunk_x==0&&turret->source_chunk_z==0&&
               turret->material_tier==0u&&turret->owner==0u&&turret->x==0&&turret->z==0&&
               turret->global_fx_x==0&&turret->global_fx_z==0&&turret->local_resident==0u&&
               turret->ammo==0u&&turret->max_ammo==0u&&turret->fire_cd==0u&&turret->fire_period==0u&&
               turret->range_fx==0&&turret->carried_by==0u&&turret->shots_fired==0u&&
               turret->cells_conquered==0u&&turret->last_target_cell==0u&&
               turret->target_global_cell_x==0&&turret->target_global_cell_z==0&&turret->beam_ticks==0u&&
               turret->target_kind==0u&&turret->aim_ticks==0u&&turret->aim_required==0u&&
               turret->target_actor_id==0u&&turret->retarget_cd==0u&&turret->mode==0u&&
               turret->target_resource_stable_id==0u&&turret->head_x_q15==0&&turret->head_z_q15==0&&
               turret->head_turn_rate_q15==0&&turret->head_turn_sign==0;
    }
    if(turret->instance_id==0u||turret->owner>ODG_MAX_ACTORS||turret->carried_by!=ODG_TURRET_NONE||
       turret->mode>ODG_TURRET_MODE_HARVEST||turret->target_kind>ODG_TURRET_TARGET_RESOURCE||
       (turret->last_target_cell!=UINT32_MAX&&turret->last_target_cell>=ODG_CELL_COUNT)||
       (turret->target_actor_id!=UINT32_MAX&&turret->target_actor_id>=ODG_MAX_ACTORS))return 0;
    if(!odg_turret_persisted_profile_validate_internal(turret->material_tier,turret->mode,turret->ammo))return 0;
    canonical=*turret;odg_apply_turret_tier(&canonical,turret->material_tier,1);
    if(turret->max_ammo!=canonical.max_ammo||turret->fire_period!=canonical.fire_period||
       turret->range_fx!=canonical.range_fx||turret->aim_required!=canonical.aim_required||
       turret->fire_cd>turret->fire_period||turret->aim_ticks>turret->aim_required||
       turret->retarget_cd>ODG_TURRET_RETARGET_GRACE_TICKS||turret->beam_ticks>14u)return 0;
    if(turret->local_resident!=0u){
        int32_t x=0,z=0;
        if(!odg_global_fx_to_local_internal(turret->global_fx_x,turret->global_fx_z,&x,&z)||
           x!=turret->x||z!=turret->z)return 0;
    }else if(turret->x!=0||turret->z!=0)return 0;
    if(turret->procedural!=0u){
        odg_chunk_descriptor descriptor;uint64_t required=0u;int64_t gx=0,gz=0;
        uint64_t expected_instance;
        if(!turret->active||odg_chunk_descriptor_get(turret->source_chunk_x,turret->source_chunk_z,
              &descriptor,sizeof(descriptor),&required)!=ODG_STATUS_OK||descriptor.has_procedural_turret==0u||
           !odg_chunk_procedural_turret_cell(turret->source_chunk_x,turret->source_chunk_z,&gx,&gz))return 0;
        expected_instance=ODG_INSTANCE_ID_PROCEDURAL_BIT|(descriptor.stable_id&ODG_INSTANCE_ID_SEQUENTIAL_MAX);
        if(turret->instance_id!=expected_instance||
           turret->global_fx_x!=gx*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2||
           turret->global_fx_z!=gz*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2)return 0;
    }else if(turret->source_chunk_x!=0||turret->source_chunk_z!=0||
             (turret->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u)return 0;
    if(!turret->active){
        if(turret->local_resident!=0u||turret->global_fx_x!=0||turret->global_fx_z!=0)return 0;
    }
    if(turret->target_kind==ODG_TURRET_TARGET_NONE){
        if(turret->target_actor_id!=UINT32_MAX||turret->target_resource_stable_id!=0u||
           turret->target_global_cell_x!=INT64_MIN||turret->target_global_cell_z!=INT64_MIN||turret->aim_ticks!=0u)return 0;
    }else{
        if(turret->owner==ODG_TURRET_NEUTRAL||turret->target_global_cell_x==INT64_MIN||
           turret->target_global_cell_z==INT64_MIN)return 0;
        if(turret->target_kind==ODG_TURRET_TARGET_RESOURCE){
            if(turret->mode!=ODG_TURRET_MODE_HARVEST||turret->target_resource_stable_id==0u||
               turret->target_actor_id!=UINT32_MAX)return 0;
        }else if(turret->target_resource_stable_id!=0u)return 0;
        if(turret->target_kind==ODG_TURRET_TARGET_TERRITORY&&turret->target_actor_id!=UINT32_MAX)return 0;
    }
    return 1;
}

int odg_turret_profiles_validate_internal(void){
    uint32_t i,j;
    const odg_turret_tier_profile *previous=NULL;
    if((uint32_t)(sizeof(g_turret_tier_profiles)/sizeof(g_turret_tier_profiles[0]))!=3u)return 0;
    for(i=0u;i<3u;++i){
        const odg_turret_tier_profile *profile=&g_turret_tier_profiles[i];
        if(profile->material_tier<ODG_MATERIAL_WOOD||profile->material_tier>ODG_MATERIAL_IRON||
           profile->max_ammo==0u||profile->fire_period==0u||profile->aim_required==0u||profile->range_fx<=0)return 0;
        for(j=i+1u;j<3u;++j)if(profile->material_tier==g_turret_tier_profiles[j].material_tier)return 0;
        if(previous!=NULL){
            /* Material progression is a gameplay invariant: upgrades may not secretly
             * reduce capacity/range or make firing/locking slower. */
            if(profile->material_tier!=previous->material_tier+1u||profile->max_ammo<previous->max_ammo||
               profile->range_fx<previous->range_fx||profile->fire_period>previous->fire_period||
               profile->aim_required>previous->aim_required)return 0;
        }
        previous=profile;
    }
    return g_turret_tier_profiles[0].material_tier==ODG_MATERIAL_WOOD&&
           g_turret_tier_profiles[1].material_tier==ODG_MATERIAL_STONE&&
           g_turret_tier_profiles[2].material_tier==ODG_MATERIAL_IRON;
}

void odg_apply_turret_tier(odg_turret *turret,uint32_t tier,int preserve_ammo) {
    const odg_turret_tier_profile *profile;uint32_t ammo;
    if (turret==NULL) return;
    profile=turret_tier_profile(tier);
    if(profile==NULL)profile=turret_tier_profile(ODG_MATERIAL_WOOD);
    if(profile==NULL)return; /* validation and static table make this unreachable; fail closed. */
    ammo=preserve_ammo?turret->ammo:profile->max_ammo;
    turret->material_tier=profile->material_tier;
    turret->max_ammo=profile->max_ammo;
    turret->ammo=ammo>turret->max_ammo?turret->max_ammo:ammo;
    turret->fire_period=profile->fire_period;
    turret->range_fx=profile->range_fx;
    turret->aim_required=profile->aim_required;
    if(turret->procedural!=0u){
        odg_chunk_descriptor descriptor;uint64_t required=0u;
        /* Natural-turret personality is derived from immutable chunk identity, never
         * stored as a second authority. Re-applying a tier after upgrade or streaming
         * therefore reproduces exactly the same cadence/lock offsets. */
        if(odg_chunk_descriptor_get(turret->source_chunk_x,turret->source_chunk_z,
                                    &descriptor,sizeof(descriptor),&required)==ODG_STATUS_OK){
            turret->fire_period+=(uint32_t)(descriptor.stable_id%UINT64_C(11));
            turret->aim_required+=(uint32_t)((descriptor.stable_id>>8u)%UINT64_C(9));
        }
    }
    if (turret->fire_cd>turret->fire_period) turret->fire_cd=turret->fire_period;
}

static uint32_t nearest_turret(const odg_actor *actor,uint32_t relationship,int64_t max_d2) {
    uint32_t i,best=UINT32_MAX;
    int64_t best_d2=max_d2;
    uint8_t own;
    if (actor==NULL) return UINT32_MAX;
    own=ODG_OWNER_FROM_ID(actor->id);
    for (i=0u;i<g_odg.turret_count;++i) {
        const odg_turret *turret=&g_odg_turrets[i];
        int64_t d2;
        int matches=0;
        if (!turret->active || turret->local_resident==0u || turret->carried_by!=ODG_TURRET_NONE) continue;
        if (relationship==1u) matches=turret->owner==own;
        else if (relationship==2u) matches=turret->owner!=ODG_TURRET_NEUTRAL && turret->owner!=own;
        else if (relationship==3u) matches=turret->owner==ODG_TURRET_NEUTRAL;
        else matches=1;
        if (!matches) continue;
        d2=odg_dist2(actor->x,actor->z,turret->x,turret->z);
        if (d2<=best_d2) { best_d2=d2; best=i; }
    }
    return best;
}

static uint32_t nearest_pickup(const odg_actor *actor,int64_t max_d2) {
    uint32_t i,best=UINT32_MAX;
    int64_t best_d2=max_d2;
    if (actor==NULL) return UINT32_MAX;
    for (i=0u;i<g_odg.pickup_count;++i) {
        const odg_world_pickup *pickup=&g_odg_pickups[i];
        int64_t d2;
        if (!pickup->active || pickup->local_resident==0u || pickup->pickup_cd!=0u) continue;
        d2=odg_dist2(actor->x,actor->z,pickup->x,pickup->z);
        if (d2<=best_d2) {best_d2=d2;best=i;}
    }
    return best;
}

static int pickup_world_item(uint32_t actor_id,uint32_t pickup_id) {
    odg_actor *actor;
    odg_world_pickup *pickup;
    if (actor_id>=ODG_MAX_ACTORS || pickup_id>=g_odg.pickup_count) return 0;
    actor=&g_odg.actors[actor_id]; pickup=&g_odg_pickups[pickup_id];
    if (!actor->active || actor->hp==0u || !pickup->active || pickup->local_resident==0u || pickup->pickup_cd!=0u) return 0;
    if (odg_dist2(actor->x,actor->z,pickup->x,pickup->z)>(int64_t)ODG_PICKUP_RANGE_FX*ODG_PICKUP_RANGE_FX) return 0;
    {
        const odg_item_definition *definition=odg_item_definition_internal(pickup->stack.type_id);
        if(definition!=NULL && ((definition->flags&(ODG_ITEM_FLAG_RESOURCE|ODG_ITEM_FLAG_FOOD|ODG_ITEM_FLAG_SEED))!=0u) &&
           !odg_territory_allows_environment_action(actor_id,pickup->x,pickup->z))return 0;
    }
    if (!odg_inventory_add(&actor->inventory,&pickup->stack)) return 0;
    {
        const odg_item_definition *definition=odg_item_definition_internal(pickup->stack.type_id);
        int32_t px=pickup->x,pz=pickup->z;
        uint32_t color=definition!=NULL&&(definition->capability_bits&ODG_ITEM_CAP_REFILL_TURRET)!=0u?0xffdf72ffu:0xc983ffffu;
        odg_world_pickup_deactivate_internal(pickup);
        odg_emit_particles(px,pz,color,8u);
    }
    return 1;
}

static int pickup_owned_turret(uint32_t actor_id,uint32_t turret_id) {
    odg_actor *actor;
    odg_turret *turret;
    odg_item_stack stack;
    odg_inventory staged;
    uint64_t manual_instance_id;
    uint8_t own;
    int64_t gx,gz;
    if (actor_id>=ODG_MAX_ACTORS || turret_id>=g_odg.turret_count) return 0;
    actor=&g_odg.actors[actor_id];turret=&g_odg_turrets[turret_id];own=ODG_OWNER_FROM_ID(actor_id);
    if (!actor->active || actor->hp==0u || !turret->active || turret->local_resident==0u || turret->owner!=own) return 0;
    if (odg_dist2(actor->x,actor->z,turret->x,turret->z)>(int64_t)(3*ODG_FX_ONE)*(3*ODG_FX_ONE)) return 0;
    odg_local_fx_to_global_cell_internal(turret->x,turret->z,&gx,&gz);
    if (odg_chunk_owner_at_global_cell(gx,gz)!=own) return 0;
    odg_memset(&stack,0,sizeof(stack));
    stack.type_id=ODG_ITEM_TURRET; stack.quantity=1u; stack.material_tier=turret->material_tier;
    stack.instance_id=turret->instance_id; stack.payload_id=(uint64_t)turret_id+UINT64_C(1);
    stack.flags=ODG_ITEM_FLAG_ARTIFACT;
    if(turret->procedural!=0u&&!odg_turret_prepare_procedural_persist(turret))return 0;
    /* Capacity preflight before consuming a sequential identity. A conquered natural
     * turret leaves the deterministic procedural namespace when it becomes portable;
     * its worldgen identity is retired with the chunk override, while the carried/manual
     * object receives a normal sequential id. */
    staged=actor->inventory;if(!odg_inventory_add(&staged,&stack))return 0;
    if(turret->procedural!=0u){
        manual_instance_id=odg_next_instance_id();if(manual_instance_id==0u)return 0;
        stack.instance_id=manual_instance_id;staged=actor->inventory;
        if(!odg_inventory_add(&staged,&stack))return 0;
    }
    actor->inventory=staged;
    if(turret->procedural!=0u){
        if(!odg_chunk_mark_procedural_turret_removed(turret->source_chunk_x,turret->source_chunk_z))return 0;
        turret->procedural=0u;turret->source_chunk_x=0;turret->source_chunk_z=0;turret->instance_id=stack.instance_id;
        /* Once portable, it belongs to the manual namespace/profile. Strip procedural
         * cadence/lock offsets now so dormant SAVE state and future redeploy agree. */
        odg_apply_turret_tier(turret,turret->material_tier,1);
    }
    turret->active=0u;turret->x=0;turret->z=0;turret->global_fx_x=0;turret->global_fx_z=0;turret->local_resident=0u;
    turret->carried_by=ODG_TURRET_NONE;
    odg_entities_spatial_mark_dirty();
    turret->target_kind=ODG_TURRET_TARGET_NONE;turret->aim_ticks=0u;turret->last_target_cell=UINT32_MAX;
    turret->target_global_cell_x=INT64_MIN;turret->target_global_cell_z=INT64_MIN;turret->target_actor_id=UINT32_MAX;turret->target_resource_stable_id=0u;
    odg_emit_particles(actor->x,actor->z,0x8ce8ffffu,10u);
    return 1;
}

static int selected_turret_index(const odg_item_stack *stack,uint32_t *out_index,uint32_t *out_append) {
    uint32_t index;
    if(stack==NULL||out_index==NULL||out_append==NULL||stack->type_id!=ODG_ITEM_TURRET||stack->quantity==0u)return 0;
    *out_append=0u;
    if(stack->payload_id!=0u){
        uint64_t raw=stack->payload_id-UINT64_C(1);const odg_turret *existing;
        if(raw>=g_odg.turret_count||stack->instance_id==0u)return 0;
        index=(uint32_t)raw;existing=&g_odg_turrets[index];
        /* A stateful turret item is a capability handle to exactly one dormant manual
         * instance. Sleeping procedural slots are world-stream cache, never inventory
         * payloads; an active or identity-mismatched handle is corrupt/duplicated. */
        if(existing->active||existing->procedural!=0u||existing->instance_id==0u||
           stack->instance_id!=existing->instance_id||stack->material_tier!=existing->material_tier)return 0;
    }else{
        /* Inactive manual turrets with a live instance id are carried state and therefore
         * reserved. Only an empty slot or a sleeping procedural cache may be recycled. */
        for(index=0u;index<g_odg.turret_count;++index){
            const odg_turret *existing=&g_odg_turrets[index];
            if(!existing->active&&(existing->procedural!=0u||existing->instance_id==0u))break;
        }
        if(index>=g_odg.turret_count){
            if(!odg_entities_reserve_turrets(g_odg.turret_count+1u))return 0;
            *out_append=1u;
        }
    }
    *out_index=index;return 1;
}

int odg_turret_place_selected(uint32_t actor_id) {
    odg_actor *actor;
    odg_item_stack *stack,item;
    odg_turret *turret;
    uint64_t new_instance_id=0u;uint32_t index,append=0u,slot;
    int32_t x,z;
    if (actor_id>=ODG_MAX_ACTORS) return 0;
    actor=&g_odg.actors[actor_id]; stack=odg_inventory_selected(&actor->inventory);
    if(stack==NULL)return 0;
    item=*stack;slot=actor->inventory.selected_slot;
    /* Validate the world before allocating a turret slot. Repeated invalid placement must
     * never grow turret_count or consume instance IDs. */
    if (!odg_turret_drop_candidate_internal(actor,&x,&z)) return 0;
    if (!selected_turret_index(&item,&index,&append)) return 0;
    if(item.payload_id==0u){
        new_instance_id=item.instance_id!=0u?item.instance_id:odg_next_instance_id();
        if(new_instance_id==0u)return 0;
    }else if(g_odg_turrets[index].instance_id==0u){
        new_instance_id=item.instance_id!=0u?item.instance_id:odg_next_instance_id();
        if(new_instance_id==0u)return 0;
    }
    /* Inventory is the transaction gate. After this succeeds every remaining operation is
     * infallible state assignment; no placed turret can exist without consuming its item. */
    if (!odg_inventory_remove_from_slot(&actor->inventory,slot,1u,NULL)) return 0;
    if(append!=0u)++g_odg.turret_count;
    turret=&g_odg_turrets[index];
    if(item.payload_id==0u){
        odg_memset(turret,0,sizeof(*turret));turret->id=index;
        turret->instance_id=new_instance_id;turret->mode=ODG_TURRET_MODE_DEFENSE;
    }else if(turret->instance_id==0u){
        turret->instance_id=new_instance_id;
    }
    turret->active=1u;turret->id=index;turret->owner=ODG_OWNER_FROM_ID(actor_id);turret->x=x;turret->z=z;turret->local_resident=1u;
    odg_local_fx_to_global_fx_internal(x,z,&turret->global_fx_x,&turret->global_fx_z);turret->carried_by=ODG_TURRET_NONE;
    if (turret->head_z_q15==0 && turret->head_x_q15==0) turret->head_z_q15=ODG_Q15_ONE;
    turret->last_target_cell=UINT32_MAX;turret->target_global_cell_x=INT64_MIN;turret->target_global_cell_z=INT64_MIN;
    turret->target_actor_id=UINT32_MAX;turret->target_resource_stable_id=0u;turret->target_kind=ODG_TURRET_TARGET_NONE;
    odg_apply_turret_tier(turret,item.material_tier,item.payload_id!=0u);
    turret->fire_cd=turret->fire_period;turret->aim_ticks=0u;turret->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;
    odg_entities_spatial_mark_dirty();
    odg_emit_particles(x,z,0x8ce8ffffu,12u);
    return 1;
}

static int use_reprogram_chip(uint32_t actor_id,uint32_t turret_id) {
    odg_actor *actor;odg_item_stack *stack;odg_turret *turret;
    if (actor_id>=ODG_MAX_ACTORS || turret_id>=g_odg.turret_count) return 0;
    actor=&g_odg.actors[actor_id];stack=odg_inventory_selected(&actor->inventory);turret=&g_odg_turrets[turret_id];
    {const odg_item_definition *definition=stack!=NULL?odg_item_definition_internal(stack->type_id):NULL;
    if (stack==NULL || stack->quantity==0u || definition==NULL || (definition->capability_bits&ODG_ITEM_CAP_REPROGRAM)==0u ||
        turret->local_resident==0u || turret->owner==ODG_TURRET_NEUTRAL || turret->owner==ODG_OWNER_FROM_ID(actor_id) ||
        stack->material_tier!=turret->material_tier) return 0;}
    if (odg_dist2(actor->x,actor->z,turret->x,turret->z)>(int64_t)ODG_CHIP_HACK_RANGE_FX*ODG_CHIP_HACK_RANGE_FX) return 0;
    if(!odg_turret_prepare_procedural_persist(turret))return 0;
    if (!odg_inventory_remove_from_slot(&actor->inventory,actor->inventory.selected_slot,1u,NULL)) return 0;
    turret->owner=ODG_OWNER_FROM_ID(actor_id);turret->fire_cd=turret->fire_period;turret->target_kind=ODG_TURRET_TARGET_NONE;
    turret->last_target_cell=UINT32_MAX;turret->target_global_cell_x=INT64_MIN;turret->target_global_cell_z=INT64_MIN;
    turret->target_actor_id=UINT32_MAX;turret->aim_ticks=0u;turret->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;
    odg_turret_persist_procedural(turret);
    odg_emit_particles(turret->x,turret->z,0xc983ffffu,24u);
    return 1;
}

static int use_ascension_chip(uint32_t actor_id,uint32_t turret_id) {
    odg_actor *actor;odg_item_stack *stack,consumed;odg_turret *turret;uint32_t expected_from;
    if (actor_id>=ODG_MAX_ACTORS || turret_id>=g_odg.turret_count) return 0;
    actor=&g_odg.actors[actor_id];stack=odg_inventory_selected(&actor->inventory);turret=&g_odg_turrets[turret_id];
    {const odg_item_definition *definition=stack!=NULL?odg_item_definition_internal(stack->type_id):NULL;
    if (stack==NULL || stack->quantity==0u || definition==NULL || (definition->capability_bits&ODG_ITEM_CAP_UPGRADE)==0u ||
        turret->local_resident==0u || turret->owner!=ODG_OWNER_FROM_ID(actor_id)) return 0;}
    expected_from=stack->material_tier==ODG_MATERIAL_STONE?ODG_MATERIAL_WOOD:
                  (stack->material_tier==ODG_MATERIAL_IRON?ODG_MATERIAL_STONE:ODG_MATERIAL_NONE);
    if (expected_from==ODG_MATERIAL_NONE || turret->material_tier!=expected_from) return 0;
    if (odg_dist2(actor->x,actor->z,turret->x,turret->z)>(int64_t)ODG_CHIP_HACK_RANGE_FX*ODG_CHIP_HACK_RANGE_FX) return 0;
    if(!odg_turret_prepare_procedural_persist(turret))return 0;
    if (!odg_inventory_remove_from_slot(&actor->inventory,actor->inventory.selected_slot,1u,&consumed)) return 0;
    /* Never read the selected-slot pointer after mutation: consuming the final chip clears
     * that slot. The removed value is the immutable transaction record. */
    odg_apply_turret_tier(turret,consumed.material_tier,1);
    turret->fire_cd=turret->fire_period;turret->target_kind=ODG_TURRET_TARGET_NONE;turret->last_target_cell=UINT32_MAX;
    turret->target_global_cell_x=INT64_MIN;turret->target_global_cell_z=INT64_MIN;turret->target_actor_id=UINT32_MAX;turret->target_resource_stable_id=0u;turret->aim_ticks=0u;
    odg_turret_persist_procedural(turret);
    odg_emit_particles(turret->x,turret->z,0xffdf72ffu,22u);
    return 1;
}

static int refill_owned_turret(uint32_t actor_id,uint32_t turret_id) {
    odg_actor *actor;odg_item_stack *stack;odg_turret *turret;uint32_t give,need;
    if (actor_id>=ODG_MAX_ACTORS || turret_id>=g_odg.turret_count) return 0;
    actor=&g_odg.actors[actor_id];stack=odg_inventory_selected(&actor->inventory);turret=&g_odg_turrets[turret_id];
    {const odg_item_definition *definition=stack!=NULL?odg_item_definition_internal(stack->type_id):NULL;
    if (stack==NULL || stack->quantity==0u || definition==NULL || (definition->capability_bits&ODG_ITEM_CAP_REFILL_TURRET)==0u ||
        turret->local_resident==0u || turret->owner!=ODG_OWNER_FROM_ID(actor_id) || turret->ammo>=turret->max_ammo) return 0;}
    if (odg_dist2(actor->x,actor->z,turret->x,turret->z)>(int64_t)ODG_AMMO_DELIVERY_RANGE_FX*ODG_AMMO_DELIVERY_RANGE_FX) return 0;
    need=turret->max_ammo-turret->ammo;give=stack->quantity<need?stack->quantity:need;
    if(give==0u||!odg_turret_prepare_procedural_persist(turret))return 0;
    if (!odg_inventory_remove_from_slot(&actor->inventory,actor->inventory.selected_slot,give,NULL)) return 0;
    turret->ammo+=give;
    odg_turret_persist_procedural(turret);
    odg_emit_particles(turret->x,turret->z,0xffdf72ffu,odg_min_u32(give,10u));
    return give!=0u;
}

int odg_drop_inventory_slot(uint32_t actor_id,uint32_t slot,uint32_t quantity,uint32_t cooldown) {
    odg_actor *actor;odg_inventory staged;odg_item_stack removed;
    if (actor_id>=ODG_MAX_ACTORS) return 0;
    actor=&g_odg.actors[actor_id];staged=actor->inventory;
    /* Dropping is a two-domain transaction. Never mutate the live inventory and then
     * attempt to repair it after a world-allocation failure: stage the removal, validate
     * the exact removed stack, reserve the pickup slot, then commit world -> inventory.
     * Once pickup capacity exists and the stack is normalized, spawn has no remaining
     * fallible allocation step. */
    if (!odg_inventory_remove_from_slot(&staged,slot,quantity,&removed)) return 0;
    if (!odg_item_stack_normalize_internal(&removed)) return 0;
    if (!odg_world_pickups_prepare_internal(1u)) return 0;
    if (!odg_spawn_world_pickup(&removed,actor->x,actor->z,cooldown)) return 0;
    actor->inventory=staged;
    odg_emit_particles(actor->x,actor->z,0xb7c4caffu,6u);
    return 1;
}

void odg_rebuild_interaction_hint(void) {
    odg_actor *player=&g_odg.actors[ODG_PLAYER_ID];
    const odg_item_stack *selected=odg_inventory_selected_const(&player->inventory);
    const odg_item_definition *selected_def=(selected!=NULL&&selected->quantity!=0u)?odg_item_definition_internal(selected->type_id):NULL;
    odg_interaction_hint *hint=&g_odg.interaction_hint;
    uint32_t turret,pickup;
    odg_memset(hint,0,sizeof(*hint));
    hint->struct_size=(uint32_t)sizeof(*hint);
    hint->progress_ticks=g_odg.interact_ticks;
    if (!player->active || player->hp==0u) return;
    if(selected_def!=NULL){
        if((selected_def->capability_bits&ODG_ITEM_CAP_ATTACK)!=0u){
            uint32_t enemy=nearest_enemy_actor(player);if(enemy<ODG_MAX_ACTORS){hint->action=ODG_INTERACTION_ATTACK_ACTOR;hint->target_kind=ODG_INTERACTION_TARGET_ACTOR;hint->target_id=enemy;hint->valid=player->melee_cooldown_ticks==0u?1u:0u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;return;}
        }
        {
            const odg_fluid_container_definition *container=odg_fluid_container_definition_internal(selected->type_id);
            uint32_t fluid_id=odg_fluid_payload_id_internal(selected->payload_id);
            uint32_t units=odg_fluid_payload_units_internal(selected->payload_id);
            if((selected_def->capability_bits&ODG_ITEM_CAP_DRINK)!=0u&&units>0u&&player->hydration_permille<ODG_ACTOR_HYDRATION_MAX){
                const odg_fluid_definition *fluid=odg_fluid_definition_internal(fluid_id);
                if(fluid!=NULL&&(fluid->flags&ODG_FLUID_FLAG_POTABLE)!=0u){hint->action=ODG_INTERACTION_DRINK_WATER;hint->target_kind=ODG_INTERACTION_TARGET_NONE;hint->valid=1u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;return;}
            }
            if(container!=NULL&&(selected_def->capability_bits&ODG_ITEM_CAP_COLLECT_WATER)!=0u&&units<container->capacity_units&&
               (fluid_id==ODG_FLUID_NONE||fluid_id==ODG_FLUID_WATER)&&container_accepts_fluid(container,ODG_FLUID_WATER)){
                uint32_t ai;for(ai=0u;ai<g_odg.artifact_count;++ai){const odg_artifact *barrel=&g_odg_artifacts[ai];if(barrel->active&&barrel->local_resident&&(barrel->capability_bits&ODG_ARTIFACT_CAP_COLLECT_RAIN)!=0u&&barrel->aux_u32>0u&&(barrel->fluid_type_id==ODG_FLUID_NONE||barrel->fluid_type_id==ODG_FLUID_WATER)&&odg_dist2(player->x,player->z,barrel->x,barrel->z)<=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX&&(barrel->owner_actor_id==ODG_PLAYER_ID||odg_territory_actor_controls_position(ODG_PLAYER_ID,barrel->x,barrel->z))){hint->action=ODG_INTERACTION_COLLECT_WATER;hint->target_kind=ODG_INTERACTION_TARGET_ARTIFACT;hint->target_id=ai;hint->valid=1u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;return;}}
                {int32_t tx=player->x+(int32_t)(((int64_t)player->face_x_q15*ODG_FX_ONE)/ODG_Q15_ONE);int32_t tz=player->z+(int32_t)(((int64_t)player->face_z_q15*ODG_FX_ONE)/ODG_Q15_ONE);odg_surface_sample surface;if(odg_environment_surface_local(tx,tz,&surface)&&(surface.flags&ODG_SURFACE_FLAG_WATER)!=0u){hint->action=ODG_INTERACTION_COLLECT_WATER;hint->target_kind=ODG_INTERACTION_TARGET_SURFACE;hint->valid=odg_territory_allows_environment_action(ODG_PLAYER_ID,tx,tz)?1u:0u;hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_TERRITORY_REQUIRED;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;return;}}
            }
            if(container!=NULL&&(selected_def->capability_bits&ODG_ITEM_CAP_IRRIGATE)!=0u&&units>0u){
                const odg_fluid_definition *fluid=odg_fluid_definition_internal(fluid_id);
                if(fluid!=NULL&&(fluid->flags&ODG_FLUID_FLAG_IRRIGATION)!=0u){uint32_t ri;int64_t best=(int64_t)(3*ODG_FX_ONE)*(3*ODG_FX_ONE);for(ri=0u;ri<g_odg.resource_count;++ri){const odg_resource_node *r=&g_odg_resources[ri];int64_t d2;if(!r->active||r->local_resident==0u||r->state!=ODG_RESOURCE_STATE_AVAILABLE||!odg_resource_is_flora_internal(r))continue;d2=odg_dist2(player->x,player->z,r->x,r->z);if(d2<best){best=d2;hint->action=ODG_INTERACTION_IRRIGATE;hint->target_kind=ODG_INTERACTION_TARGET_RESOURCE;hint->target_id=ri;hint->valid=odg_territory_allows_environment_action(ODG_PLAYER_ID,r->x,r->z)?1u:0u;hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_TERRITORY_REQUIRED;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;}}if(hint->action==ODG_INTERACTION_IRRIGATE)return;}
            }
        }
    }
    if (selected!=NULL && selected->type_id==ODG_ITEM_TURRET && selected->quantity!=0u) {
        int32_t x,z;
        hint->action=ODG_INTERACTION_PLACE;hint->target_kind=ODG_INTERACTION_TARGET_ARTIFACT;hint->valid=odg_turret_drop_candidate_internal(player,&x,&z)?1u:0u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;
        return;
    }
    if (selected_def!=NULL && (selected_def->capability_bits&ODG_ITEM_CAP_CONSTRUCT)!=0u) {
        if (odg_construction_build_hint_internal(player,selected,hint)) return;
    }
    if (selected!=NULL && selected->quantity!=0u &&
        odg_artifact_item_deployable_internal(selected->type_id)) {
        if (odg_artifact_build_hint(player,selected,hint)) return;
    }
    if (selected_def!=NULL && (selected_def->capability_bits&ODG_ITEM_CAP_REPROGRAM)!=0u) {
        turret=nearest_turret(player,2u,(int64_t)ODG_CHIP_HACK_RANGE_FX*ODG_CHIP_HACK_RANGE_FX);
        if (turret<g_odg.turret_count) {
            hint->action=ODG_INTERACTION_REPROGRAM;hint->target_kind=ODG_INTERACTION_TARGET_TURRET;hint->target_id=turret;hint->valid=g_odg_turrets[turret].material_tier==selected->material_tier?1u:0u;hint->message_code=hint->valid?0u:1u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;return;
        }
    }
    if (selected_def!=NULL && (selected_def->capability_bits&ODG_ITEM_CAP_UPGRADE)!=0u) {
        turret=nearest_turret(player,1u,(int64_t)ODG_CHIP_HACK_RANGE_FX*ODG_CHIP_HACK_RANGE_FX);
        if (turret<g_odg.turret_count) {
            uint32_t from=selected->material_tier==ODG_MATERIAL_STONE?ODG_MATERIAL_WOOD:(selected->material_tier==ODG_MATERIAL_IRON?ODG_MATERIAL_STONE:0u);
            hint->action=ODG_INTERACTION_UPGRADE;hint->target_kind=ODG_INTERACTION_TARGET_TURRET;hint->target_id=turret;hint->valid=g_odg_turrets[turret].material_tier==from?1u:0u;hint->message_code=hint->valid?0u:2u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;return;
        }
    }
    if (selected_def!=NULL && (selected_def->capability_bits&ODG_ITEM_CAP_REFILL_TURRET)!=0u) {
        turret=nearest_turret(player,1u,(int64_t)ODG_AMMO_DELIVERY_RANGE_FX*ODG_AMMO_DELIVERY_RANGE_FX);
        if (turret<g_odg.turret_count) {
            hint->action=ODG_INTERACTION_REFILL;hint->target_kind=ODG_INTERACTION_TARGET_TURRET;hint->target_id=turret;hint->valid=g_odg_turrets[turret].ammo<g_odg_turrets[turret].max_ammo?1u:0u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;return;
        }
    }
    /* A deployed owned turret is itself an artifact interaction. Resolve it before
     * stations/resources so standing on the turret cannot accidentally target a nearby
     * workbench or tree. Item-specific chip/ammo actions above still have priority. */
    turret=nearest_turret(player,1u,(int64_t)(3*ODG_FX_ONE)*(3*ODG_FX_ONE));
    if (turret<g_odg.turret_count) {
        int64_t gx,gz;
        odg_local_fx_to_global_cell_internal(g_odg_turrets[turret].x,g_odg_turrets[turret].z,&gx,&gz);
        hint->action=ODG_INTERACTION_PICKUP_ARTIFACT;hint->target_kind=ODG_INTERACTION_TARGET_TURRET;hint->target_id=turret;hint->valid=odg_chunk_owner_at_global_cell(gx,gz)==ODG_OWNER_FROM_ID(ODG_PLAYER_ID)?1u:0u;hint->requires_hold=1u;hint->threshold_ticks=ODG_INTERACT_HOLD_TICKS;return;
    }
    if (odg_fauna_build_hint(player,selected,hint)) return;
    if (odg_construction_build_hint_internal(player,selected,hint)) return;
    if (odg_artifact_build_hint(player,selected,hint)) return;
    if (odg_resource_build_hint(player,hint)) return;
    if(player->hydration_permille<ODG_ACTOR_HYDRATION_MAX){
        int32_t tx=player->x+(int32_t)(((int64_t)player->face_x_q15*ODG_FX_ONE)/ODG_Q15_ONE);
        int32_t tz=player->z+(int32_t)(((int64_t)player->face_z_q15*ODG_FX_ONE)/ODG_Q15_ONE);odg_surface_sample surface;
        if(odg_environment_surface_local(tx,tz,&surface)&&(surface.flags&ODG_SURFACE_FLAG_WATER)!=0u){hint->action=ODG_INTERACTION_DRINK_WATER;hint->target_kind=ODG_INTERACTION_TARGET_SURFACE;hint->valid=odg_territory_allows_environment_action(ODG_PLAYER_ID,tx,tz)?1u:0u;hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_TERRITORY_REQUIRED;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;return;}
    }
    pickup=nearest_pickup(player,(int64_t)ODG_PICKUP_RANGE_FX*ODG_PICKUP_RANGE_FX);
    if (pickup<g_odg.pickup_count) {
        const odg_item_definition *definition=odg_item_definition_internal(g_odg_pickups[pickup].stack.type_id);
        hint->action=ODG_INTERACTION_PICKUP;hint->target_kind=ODG_INTERACTION_TARGET_PICKUP;hint->target_id=pickup;hint->valid=1u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;
        if(definition!=NULL&&((definition->flags&(ODG_ITEM_FLAG_RESOURCE|ODG_ITEM_FLAG_FOOD|ODG_ITEM_FLAG_SEED))!=0u)&&!odg_territory_allows_environment_action(ODG_PLAYER_ID,g_odg_pickups[pickup].x,g_odg_pickups[pickup].z)){hint->valid=0u;hint->message_code=ODG_MESSAGE_TERRITORY_REQUIRED;}
    }
}

int32_t odg_copy_interaction_hint(odg_interaction_hint *out_hint,uint64_t capacity,uint64_t *out_required) {
    if (out_required!=NULL) *out_required=(uint64_t)sizeof(odg_interaction_hint);
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (out_hint==NULL || capacity<(uint64_t)sizeof(*out_hint)) return ODG_STATUS_BUFFER_TOO_SMALL;
    *out_hint=g_odg.interaction_hint;
    return ODG_STATUS_OK;
}

static void execute_tap(void) {
    odg_actor *player=&g_odg.actors[ODG_PLAYER_ID];
    odg_interaction_hint hint=g_odg.interaction_hint;
    if (!hint.valid) return;
    if (hint.action==ODG_INTERACTION_PLACE) (void)odg_turret_place_selected(ODG_PLAYER_ID);
    else if (hint.action==ODG_INTERACTION_REPROGRAM) (void)use_reprogram_chip(ODG_PLAYER_ID,hint.target_id);
    else if (hint.action==ODG_INTERACTION_UPGRADE) (void)use_ascension_chip(ODG_PLAYER_ID,hint.target_id);
    else if (hint.action==ODG_INTERACTION_REFILL) (void)refill_owned_turret(ODG_PLAYER_ID,hint.target_id);
    else if (hint.action==ODG_INTERACTION_PICKUP) (void)pickup_world_item(ODG_PLAYER_ID,hint.target_id);
    else if (hint.action==ODG_INTERACTION_GATHER_FRUIT) (void)odg_ecology_gather_fruit(ODG_PLAYER_ID,hint.target_id);
    else if (hint.action==ODG_INTERACTION_ATTACK_ACTOR) (void)melee_attack_actor(ODG_PLAYER_ID,hint.target_id);
    else if (hint.action==ODG_INTERACTION_ATTACK_CONSTRUCTION) (void)melee_attack_construction(ODG_PLAYER_ID,hint.target_id);
    else if (hint.action==ODG_INTERACTION_COLLECT_WATER) {if(!fill_container_from_barrel(ODG_PLAYER_ID))(void)fill_container_from_environment(ODG_PLAYER_ID);}
    else if (hint.action==ODG_INTERACTION_IRRIGATE) (void)irrigate_from_container(ODG_PLAYER_ID);
    else if (hint.action==ODG_INTERACTION_DRINK_WATER) {if(hint.target_kind==ODG_INTERACTION_TARGET_SURFACE)(void)odg_actor_drink_environment_internal(ODG_PLAYER_ID);else (void)odg_actor_drink_selected_internal(ODG_PLAYER_ID);}
    else if (hint.action==ODG_INTERACTION_PLACE_CONSTRUCTION)
        (void)odg_construction_execute_tap_internal(ODG_PLAYER_ID,&hint);
    else if (hint.action==ODG_INTERACTION_OPEN_ARTIFACT || hint.action==ODG_INTERACTION_PLACE_ARTIFACT ||
             hint.action==ODG_INTERACTION_USE_VEHICLE)
        (void)odg_artifact_execute_tap(ODG_PLAYER_ID,&hint);
    (void)player;
}

static void execute_hold(void) {
    odg_interaction_hint hint=g_odg.interaction_hint;
    if (!hint.valid || !hint.requires_hold) return;
    if (hint.action==ODG_INTERACTION_PICKUP_ARTIFACT) (void)pickup_owned_turret(ODG_PLAYER_ID,hint.target_id);
    else if (hint.action==ODG_INTERACTION_HUNT_FAUNA) (void)odg_fauna_hunt(ODG_PLAYER_ID,hint.target_id);
    else if (hint.action==ODG_INTERACTION_DISMANTLE_CONSTRUCTION || hint.action==ODG_INTERACTION_REPAIR_CONSTRUCTION)
        (void)odg_construction_execute_hold_internal(ODG_PLAYER_ID,&hint);
    else if (hint.action==ODG_INTERACTION_OPEN_ARTIFACT || hint.action==ODG_INTERACTION_USE_VEHICLE)
        (void)odg_artifact_execute_hold(ODG_PLAYER_ID,&hint);
}

void odg_handle_interaction(void) {
    uint32_t pressed=(g_odg.input.buttons&ODG_BUTTON_INTERACT)!=0u?1u:0u;
    odg_rebuild_interaction_hint();
    if (pressed) {
        if (!g_odg.interact_pressed_prev) {
            g_odg.interact_ticks=1u;
            g_odg.interact_hold_fired=0u;
        } else if (g_odg.interact_ticks<UINT32_MAX) {
            ++g_odg.interact_ticks;
        }
        odg_rebuild_interaction_hint();
        if (g_odg.interaction_hint.action==ODG_INTERACTION_HARVEST && g_odg.interaction_hint.valid) {
            int result=odg_resource_hold_tick(ODG_PLAYER_ID,g_odg.interaction_hint.target_id);
            if (result==2) g_odg.interact_hold_fired=1u;
        } else if (g_odg.interaction_hint.action==ODG_INTERACTION_HUNT_FAUNA && g_odg.interaction_hint.valid) {
            int result=odg_fauna_hunt(ODG_PLAYER_ID,g_odg.interaction_hint.target_id);
            if(result==2)g_odg.interact_hold_fired=1u;
        } else if (!g_odg.interact_hold_fired && g_odg.interaction_hint.requires_hold!=0u &&
                   g_odg.interaction_hint.threshold_ticks!=0u &&
                   g_odg.interact_ticks>=g_odg.interaction_hint.threshold_ticks) {
            execute_hold();
            g_odg.interact_hold_fired=1u;
        }
    } else if (g_odg.interact_pressed_prev) {
        if (!g_odg.interact_hold_fired && g_odg.interact_ticks<=ODG_INTERACT_TAP_MAX_TICKS) {
            odg_rebuild_interaction_hint();
            execute_tap();
        }
        g_odg.interact_ticks=0u;
        g_odg.interact_hold_fired=0u;
    }
    g_odg.interact_pressed_prev=pressed;
    odg_rebuild_interaction_hint();
}

int odg_command_validate_internal(const odg_command *command) {
    if(command==NULL||command->struct_size!=sizeof(*command))return 0;
    switch(command->type){
        case ODG_COMMAND_SELECT_SLOT:
        case ODG_COMMAND_DROP_SELECTED:
        case ODG_COMMAND_PLACE_SELECTED:
        case ODG_COMMAND_USE_SELECTED:
        case ODG_COMMAND_EQUIP_BACKPACK:
        case ODG_COMMAND_REQUEST_RESPAWN:
        case ODG_COMMAND_MOVE_SLOT:
        case ODG_COMMAND_CRAFT:
        case ODG_COMMAND_REPAIR_SELECTED:
        case ODG_COMMAND_CLOSE_ARTIFACT:
        case ODG_COMMAND_STORAGE_DEPOSIT:
        case ODG_COMMAND_STORAGE_WITHDRAW:
        case ODG_COMMAND_SET_TURRET_MODE:
        case ODG_COMMAND_CONSUME_SELECTED:
        case ODG_COMMAND_PLANT_SELECTED:
        case ODG_COMMAND_DRINK_SELECTED:
        case ODG_COMMAND_SET_CONSTRUCTION_SHAPE:
            return 1;
        default:return 0;
    }
}

int odg_command_queue_state_validate_internal(const odg_command_queue *queue) {
    uint32_t i,index;
    if(queue==NULL||queue->count>ODG_COMMAND_QUEUE_CAPACITY||
       queue->read_index>=ODG_COMMAND_QUEUE_CAPACITY||queue->write_index>=ODG_COMMAND_QUEUE_CAPACITY)return 0;
    if(queue->write_index!=(queue->read_index+queue->count)%ODG_COMMAND_QUEUE_CAPACITY)return 0;
    index=queue->read_index;
    for(i=0u;i<queue->count;++i){
        if(!odg_command_validate_internal(&queue->entries[index]))return 0;
        index=(index+1u)%ODG_COMMAND_QUEUE_CAPACITY;
    }
    return 1;
}

int32_t odg_command_submit(const odg_command *command,uint64_t capacity) {
    odg_command_queue *queue=&g_odg.commands;
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (command==NULL || capacity<(uint64_t)sizeof(*command) || !odg_command_validate_internal(command)) return ODG_STATUS_INVALID_ARGUMENT;
    if (!odg_command_queue_state_validate_internal(queue)) return ODG_STATUS_INVALID_STATE;
    if (queue->count>=ODG_COMMAND_QUEUE_CAPACITY) return ODG_STATUS_INVALID_STATE;
    queue->entries[queue->write_index]=*command;
    queue->write_index=(queue->write_index+1u)%ODG_COMMAND_QUEUE_CAPACITY;
    ++queue->count;
    return ODG_STATUS_OK;
}

void odg_process_commands(void) {
    odg_command_queue *queue=&g_odg.commands;
    odg_actor *player=&g_odg.actors[ODG_PLAYER_ID];
    if(!odg_command_queue_state_validate_internal(queue)){
        /* A corrupt in-memory ring must never become an out-of-bounds read. SAVE24
         * rejects this state; runtime hardening additionally drops the unusable queue. */
        odg_memset(queue,0,sizeof(*queue));return;
    }
    while (queue->count!=0u) {
        odg_command command=queue->entries[queue->read_index];
        queue->read_index=(queue->read_index+1u)%ODG_COMMAND_QUEUE_CAPACITY;--queue->count;
        if (command.type==ODG_COMMAND_SELECT_SLOT) {
            if (command.arg0<odg_inventory_capacity(&player->inventory)) player->inventory.selected_slot=command.arg0;
        } else if (command.type==ODG_COMMAND_DROP_SELECTED) {
            (void)odg_drop_inventory_slot(ODG_PLAYER_ID,player->inventory.selected_slot,1u,ODG_MANUAL_DROP_REPICKUP_TICKS);
        } else if (command.type==ODG_COMMAND_PLACE_SELECTED) {
            const odg_item_stack *selected=odg_inventory_selected_const(&player->inventory);
            const odg_item_definition *definition=selected!=NULL?odg_item_definition_internal(selected->type_id):NULL;
            if (selected!=NULL && selected->type_id==ODG_ITEM_TURRET) (void)odg_turret_place_selected(ODG_PLAYER_ID);
            else if (definition!=NULL && (definition->capability_bits&ODG_ITEM_CAP_CONSTRUCT)!=0u)
                (void)odg_construction_place_selected_internal(ODG_PLAYER_ID);
            else (void)odg_artifact_place_selected(ODG_PLAYER_ID);
        } else if (command.type==ODG_COMMAND_REQUEST_RESPAWN) {
            (void)odg_actor_request_ready_respawn_internal(ODG_PLAYER_ID);
        } else if (command.type==ODG_COMMAND_MOVE_SLOT) {
            uint32_t capacity=odg_inventory_capacity(&player->inventory);
            if (command.arg0<capacity && command.arg1<capacity && command.arg0!=command.arg1) {
                odg_item_stack tmp=player->inventory.slots[command.arg0];
                player->inventory.slots[command.arg0]=player->inventory.slots[command.arg1];
                player->inventory.slots[command.arg1]=tmp;
            }
        } else if (command.type==ODG_COMMAND_CRAFT) {
            (void)odg_craft(ODG_PLAYER_ID,command.arg0,command.arg1==0u?1u:command.arg1);
        } else if (command.type==ODG_COMMAND_CLOSE_ARTIFACT) {
            g_odg.opened_artifact_id=UINT32_MAX;
        } else if (command.type==ODG_COMMAND_STORAGE_DEPOSIT) {
            (void)odg_artifact_storage_deposit(ODG_PLAYER_ID,command.arg0,command.arg1,command.arg2==0u?1u:command.arg2);
        } else if (command.type==ODG_COMMAND_STORAGE_WITHDRAW) {
            (void)odg_artifact_storage_withdraw(ODG_PLAYER_ID,command.arg0,command.arg1,command.arg2==0u?1u:command.arg2);
        } else if (command.type==ODG_COMMAND_REPAIR_SELECTED) {
            (void)odg_repair_selected(ODG_PLAYER_ID);
        } else if (command.type==ODG_COMMAND_SET_TURRET_MODE) {
            if(command.arg0<g_odg.turret_count && command.arg1<=ODG_TURRET_MODE_HARVEST){
                odg_turret *turret=&g_odg_turrets[command.arg0];
                if(turret->active && turret->carried_by==ODG_TURRET_NONE && turret->owner==ODG_OWNER_FROM_ID(ODG_PLAYER_ID)&&
                   odg_turret_prepare_procedural_persist(turret)){
                    turret->mode=command.arg1;turret->target_kind=ODG_TURRET_TARGET_NONE;turret->target_actor_id=UINT32_MAX;
                    turret->target_resource_stable_id=0u;turret->target_global_cell_x=INT64_MIN;turret->target_global_cell_z=INT64_MIN;
                    turret->aim_ticks=0u;turret->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;turret->last_target_cell=UINT32_MAX;
                    odg_turret_persist_procedural(turret);
                }
            }
        } else if(command.type==ODG_COMMAND_CONSUME_SELECTED){
            (void)odg_actor_consume_food_internal(ODG_PLAYER_ID,player->inventory.selected_slot);
        } else if(command.type==ODG_COMMAND_PLANT_SELECTED){
            (void)odg_ecology_plant_selected(ODG_PLAYER_ID);
        } else if(command.type==ODG_COMMAND_DRINK_SELECTED){
            (void)odg_actor_drink_selected_internal(ODG_PLAYER_ID);
        } else if(command.type==ODG_COMMAND_SET_CONSTRUCTION_SHAPE){
            (void)odg_construction_set_shape_internal(ODG_PLAYER_ID,command.arg0);
        } else if (command.type==ODG_COMMAND_EQUIP_BACKPACK || command.type==ODG_COMMAND_USE_SELECTED) {
            (void)odg_inventory_equip_first_expander_internal(&player->inventory);
        }
    }
}

static int bot_should_auto_pickup(const odg_actor *actor,const odg_world_pickup *pickup) {
    const odg_item_definition *definition;
    if(actor==NULL||pickup==NULL)return 0;
    definition=odg_item_definition_internal(pickup->stack.type_id);
    if(definition==NULL)return 0;
    /* Organic items are intentional inventory choices. Food is collected when useful;
     * seeds remain in the world for ecology until the bot has an explicit planting plan.
     * Structural resources/chips/ammo keep the lightweight logistics behaviour. */
    if((definition->flags&ODG_ITEM_FLAG_SEED)!=0u)return 0;
    if((definition->flags&ODG_ITEM_FLAG_FOOD)!=0u){
        if(actor->satiety_permille>=760u&&actor->hydration_permille>=760u)return 0;
        if(odg_inventory_total(&actor->inventory,pickup->stack.type_id,pickup->stack.material_tier)>=3u)return 0;
    }
    return 1;
}

static uint32_t nearest_bot_auto_pickup(const odg_actor *actor,int64_t max_d2) {
    uint32_t i,best=UINT32_MAX;int64_t best_d2=max_d2;
    if(actor==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg.pickup_count;++i){
        const odg_world_pickup *pickup=&g_odg_pickups[i];const odg_item_definition *definition;int64_t d2;
        if(!pickup->active||pickup->local_resident==0u||pickup->pickup_cd!=0u||!bot_should_auto_pickup(actor,pickup))continue;
        definition=odg_item_definition_internal(pickup->stack.type_id);
        if(definition==NULL)continue;
        /* Match the real pickup transaction here. An unreachable enemy-land resource
         * must not shadow a useful pickup a few centimetres farther away. */
        if((definition->flags&(ODG_ITEM_FLAG_RESOURCE|ODG_ITEM_FLAG_FOOD|ODG_ITEM_FLAG_SEED))!=0u&&
           !odg_territory_allows_environment_action(actor->id,pickup->x,pickup->z))continue;
        d2=odg_dist2(actor->x,actor->z,pickup->x,pickup->z);
        if(d2<=best_d2){best_d2=d2;best=i;}
    }
    return best;
}

void odg_update_world_pickups(void) {
    uint32_t i,actor_id;
    for (i=0u;i<g_odg.pickup_count;++i) {
        odg_world_pickup *pickup=&g_odg_pickups[i];
        const odg_food_definition *food;
        if(!pickup->active)continue;
        if(pickup->pickup_cd>0u)--pickup->pickup_cd;
        food=odg_food_definition_internal(pickup->stack.type_id);
        if(food==NULL)continue;
        /* Save 14-17 already contain age/lifetime fields. Old worlds may have zeroes
         * because the lifetime data used to be declared but never consumed. Hydrate
         * those zeroes from the authoritative Food Registry instead of changing schema. */
        if(pickup->lifetime_ticks==0u)pickup->lifetime_ticks=food->ground_lifetime_ticks;
        if(pickup->age_ticks<UINT32_MAX)++pickup->age_ticks;
        if(pickup->lifetime_ticks!=0u&&pickup->age_ticks>=pickup->lifetime_ticks){
            odg_world_pickup_deactivate_internal(pickup);
        }
    }
    /* Bots use the same inventory capacity and item metadata. Their current v15 planner
     * keeps lightweight auto-acquisition while the human uses explicit INTERACT. */
    for (actor_id=1u;actor_id<ODG_MAX_ACTORS;++actor_id) {
        odg_actor *actor=&g_odg.actors[actor_id];
        uint32_t pickup,turret;
        if (!actor->active || actor->hp==0u) continue;
        pickup=nearest_bot_auto_pickup(actor,(int64_t)ODG_PICKUP_RANGE_FX*ODG_PICKUP_RANGE_FX);
        if(pickup<g_odg.pickup_count)(void)pickup_world_item(actor_id,pickup);
        turret=nearest_turret(actor,2u,(int64_t)ODG_CHIP_HACK_RANGE_FX*ODG_CHIP_HACK_RANGE_FX);
        if (turret<g_odg.turret_count) {
            uint32_t slot;
            if (odg_inventory_find_capability_internal(&actor->inventory,ODG_ITEM_CAP_REPROGRAM,g_odg_turrets[turret].material_tier,&slot)) {
                uint32_t old=actor->inventory.selected_slot;actor->inventory.selected_slot=slot;(void)use_reprogram_chip(actor_id,turret);actor->inventory.selected_slot=old;
            }
        }
        /* Upgrades use the exact same ascension-chip transaction as the player. */
        {
            uint32_t ti,best_upgrade=UINT32_MAX,slot=UINT32_MAX;int64_t best_d2=(int64_t)ODG_CHIP_HACK_RANGE_FX*ODG_CHIP_HACK_RANGE_FX;
            for(ti=0u;ti<g_odg.turret_count;++ti){
                odg_turret *candidate=&g_odg_turrets[ti];uint32_t wanted;int64_t d2;
                if(!candidate->active||candidate->local_resident==0u||candidate->carried_by!=ODG_TURRET_NONE||candidate->owner!=ODG_OWNER_FROM_ID(actor_id))continue;
                wanted=candidate->material_tier==ODG_MATERIAL_WOOD?ODG_MATERIAL_STONE:
                       (candidate->material_tier==ODG_MATERIAL_STONE?ODG_MATERIAL_IRON:ODG_MATERIAL_NONE);
                if(wanted==ODG_MATERIAL_NONE||!odg_inventory_find_capability_internal(&actor->inventory,ODG_ITEM_CAP_UPGRADE,wanted,&slot))continue;
                d2=odg_dist2(actor->x,actor->z,candidate->x,candidate->z);
                if(d2<=best_d2){best_d2=d2;best_upgrade=ti;}
            }
            if(best_upgrade<g_odg.turret_count){
                uint32_t wanted=g_odg_turrets[best_upgrade].material_tier==ODG_MATERIAL_WOOD?ODG_MATERIAL_STONE:ODG_MATERIAL_IRON;
                if(odg_inventory_find_capability_internal(&actor->inventory,ODG_ITEM_CAP_UPGRADE,wanted,&slot)){
                    uint32_t old=actor->inventory.selected_slot;actor->inventory.selected_slot=slot;
                    (void)use_ascension_chip(actor_id,best_upgrade);actor->inventory.selected_slot=old;
                }
            }
        }
        turret=nearest_turret(actor,1u,(int64_t)ODG_AMMO_DELIVERY_RANGE_FX*ODG_AMMO_DELIVERY_RANGE_FX);
        if (turret<g_odg.turret_count && g_odg_turrets[turret].ammo<g_odg_turrets[turret].max_ammo) {
            uint32_t slot;
            if (odg_inventory_find_capability_internal(&actor->inventory,ODG_ITEM_CAP_REFILL_TURRET,ODG_MATERIAL_NONE,&slot)) {
                uint32_t old=actor->inventory.selected_slot;actor->inventory.selected_slot=slot;(void)refill_owned_turret(actor_id,turret);actor->inventory.selected_slot=old;
            }
        }
    }
}
