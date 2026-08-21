#include "game_internal.h"

#include <stddef.h>
#include <stdint.h>

#define ODG_ARTIFACT_RADIUS_FX (4 * ODG_FX_ONE / 5)
#define ODG_ARTIFACT_PLACE_LAND UINT32_C(1)
#define ODG_ARTIFACT_PLACE_WATER UINT32_C(2)
#define ODG_RAFT_MIN_DEPTH_MILLI UINT32_C(200)
#define ODG_RAFT_DECK_ABOVE_WATER_MILLI INT32_C(170)

typedef struct {
    uint32_t item_type;
    uint32_t capability_bits;
    uint32_t fluid_capacity_units;
    uint32_t deployable;
    uint32_t placement_medium_mask;
    uint32_t min_water_depth_milli;
    uint32_t light_radius_milli;
    uint32_t light_peak_permille;
    uint32_t physical_height_milli;
} odg_artifact_type_profile;

/* Generic artifact behavior is declared here rather than reconstructed by chains of
 * concrete item comparisons throughout placement, storage and weather code. */
static const odg_artifact_type_profile g_artifact_type_profiles[] = {
    {ODG_ITEM_WORKBENCH,ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_MOVE,0u,1u,ODG_ARTIFACT_PLACE_LAND,0u,0u,0u,700u},
    {ODG_ITEM_SMITHY,ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_MOVE,0u,1u,ODG_ARTIFACT_PLACE_LAND,0u,0u,0u,1200u},
    {ODG_ITEM_CHEST,ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_STORE,0u,1u,ODG_ARTIFACT_PLACE_LAND,0u,0u,0u,750u},
    {ODG_ITEM_BIRD_TRAP,ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_TAME,0u,1u,ODG_ARTIFACT_PLACE_LAND,0u,0u,0u,520u},
    {ODG_ITEM_RAIN_BARREL,ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_STORE|ODG_ARTIFACT_CAP_COLLECT_RAIN,100u,1u,ODG_ARTIFACT_PLACE_LAND,0u,0u,0u,1100u},
    {ODG_ITEM_TORCH,ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_LIGHT,0u,1u,ODG_ARTIFACT_PLACE_LAND,0u,8000u,1000u,900u},
    /* Night Stalker loot closes its gameplay cycle as a reusable ward-light. It uses
     * the same LIGHT authority as torches, so hostile spawning and rendering agree. */
    {ODG_ITEM_NIGHT_SHARD,ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_LIGHT,0u,1u,ODG_ARTIFACT_PLACE_LAND,0u,12000u,900u,850u},
    {ODG_ITEM_RAFT,ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_VEHICLE,0u,1u,ODG_ARTIFACT_PLACE_WATER,ODG_RAFT_MIN_DEPTH_MILLI,0u,0u,260u},
    /* Backpacks are artifact-shaped only while dropped/recoverable; equipped backpacks
     * are not directly placeable through the normal artifact placement action. */
    {ODG_ITEM_BACKPACK,ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_STORE,0u,0u,0u,0u,0u,0u,700u}
};

static const odg_artifact_type_profile *artifact_type_profile(uint32_t item_type) {
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_artifact_type_profiles)/sizeof(g_artifact_type_profiles[0]));++i){
        if(g_artifact_type_profiles[i].item_type==item_type)return &g_artifact_type_profiles[i];
    }
    return NULL;
}

static uint32_t artifact_caps(uint32_t item_type) {
    const odg_artifact_type_profile *profile=artifact_type_profile(item_type);
    return profile!=NULL?profile->capability_bits:0u;
}

int odg_artifact_profiles_validate_internal(void) {
    uint32_t i,j;
    const uint32_t count=(uint32_t)(sizeof(g_artifact_type_profiles)/sizeof(g_artifact_type_profiles[0]));
    for(i=0u;i<count;++i){
        const odg_artifact_type_profile *profile=&g_artifact_type_profiles[i];
        const odg_item_definition *item=odg_item_definition_internal(profile->item_type);
        if(item==NULL||profile->item_type==ODG_ITEM_NONE||profile->capability_bits==0u)return 0;
        for(j=i+1u;j<count;++j)if(g_artifact_type_profiles[j].item_type==profile->item_type)return 0;
        if(profile->deployable!=0u){
            if(item->category!=ODG_ITEM_CATEGORY_DEPLOYABLE||(item->flags&ODG_ITEM_FLAG_ARTIFACT)==0u||
               (item->capability_bits&ODG_ITEM_CAP_PLACE)==0u||profile->placement_medium_mask==0u)return 0;
            if((profile->placement_medium_mask&~(ODG_ARTIFACT_PLACE_LAND|ODG_ARTIFACT_PLACE_WATER))!=0u)return 0;
            if((profile->placement_medium_mask&ODG_ARTIFACT_PLACE_WATER)!=0u&&profile->min_water_depth_milli==0u)return 0;
            if((profile->capability_bits&ODG_ARTIFACT_CAP_STORE)!=0u&&
               (item->capability_bits&ODG_ITEM_CAP_STORE)==0u)return 0;
        }
        if(profile->physical_height_milli==0u)return 0;
        if((profile->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)!=0u){
            if(profile->deployable==0u||(profile->capability_bits&ODG_ARTIFACT_CAP_MOVE)==0u||
               (profile->placement_medium_mask&ODG_ARTIFACT_PLACE_WATER)==0u)return 0;
        }
        if((profile->capability_bits&ODG_ARTIFACT_CAP_COLLECT_RAIN)!=0u){
            if((profile->capability_bits&ODG_ARTIFACT_CAP_STORE)==0u||profile->fluid_capacity_units==0u)return 0;
        }
        if((profile->capability_bits&ODG_ARTIFACT_CAP_LIGHT)!=0u){
            if(profile->light_radius_milli==0u||profile->light_peak_permille==0u||profile->light_peak_permille>1000u)return 0;
        }else if(profile->light_radius_milli!=0u||profile->light_peak_permille!=0u)return 0;
        if(odg_item_inventory_expander_recovery_internal(profile->item_type)&&
           (profile->capability_bits&(ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_STORE))!=
           (ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_STORE))return 0;
    }
    /* CAP_PLACE is a contract: every placeable item is either the dedicated turret
     * system or has a declarative artifact profile. CAP_STORE placeables must expose
     * storage on that profile too. This makes missing content wiring fail at init. */
    for(i=1u;i<ODG_ITEM_TYPE_COUNT;++i){
        const odg_item_definition *item=odg_item_definition_internal(i);
        const odg_artifact_type_profile *profile;
        if(item==NULL)return 0;
        profile=artifact_type_profile(i);
        if(odg_item_inventory_expander_recovery_internal(i)){
            if(profile==NULL||(profile->capability_bits&ODG_ARTIFACT_CAP_STORE)==0u)return 0;
        }
        if((item->capability_bits&ODG_ITEM_CAP_PLACE)==0u)continue;
        if(i==ODG_ITEM_TURRET||(item->capability_bits&ODG_ITEM_CAP_CONSTRUCT)!=0u)continue;
        if(profile==NULL||profile->deployable==0u)return 0;
        if((item->capability_bits&ODG_ITEM_CAP_STORE)!=0u&&
           (profile->capability_bits&ODG_ARTIFACT_CAP_STORE)==0u)return 0;
            /* Stackable deployables cannot carry a per-instance payload handle: the
             * inventory stacking authority intentionally erases instance_id. Therefore
             * a stackable artifact must be stateless enough to collapse back into a
             * plain item on recovery. Current torch/ward lights satisfy this; a future
             * container/vehicle/UI artifact must remain max_stack=1 (or introduce an
             * explicit different persistence model) instead of silently losing state. */
            if(item->max_stack>1u&&
               (profile->capability_bits&~(ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_LIGHT))!=0u)return 0;
    }
    return 1;
}

static uint32_t storage_used(const odg_storage *storage) {
    uint32_t i,used=0u;
    if (storage==NULL) return 0u;
    for (i=0u;i<ODG_CHEST_SLOTS;++i) if (storage->slots[i].type_id!=ODG_ITEM_NONE && storage->slots[i].quantity!=0u) ++used;
    return used;
}

int odg_artifact_state_validate_internal(const odg_artifact *artifact,uint32_t expected_id){
    const odg_artifact_type_profile *profile;
    const odg_item_definition *definition;
    uint32_t allowed_state=ODG_ARTIFACT_STATE_PROTECTED|ODG_ARTIFACT_STATE_DEATH_CACHE;
    int32_t local_x=0,local_z=0;
    if(artifact==NULL||artifact->id!=expected_id||artifact->active>1u||artifact->local_resident>1u)return 0;
    if(!artifact->active&&artifact->instance_id==0u){
        /* A free slot is a canonical tombstone. Persisting old payload bytes here makes
         * SAVE/hash depend on dead history and lets a future allocator inherit garbage. */
        return artifact->item_type==0u&&artifact->owner_actor_id==0u&&artifact->material_tier==0u&&
               artifact->capability_bits==0u&&artifact->state==0u&&artifact->x==0&&artifact->z==0&&
               artifact->global_fx_x==0&&artifact->global_fx_z==0&&artifact->local_resident==0u&&
               artifact->aux_tick==0u&&artifact->aux_u32==0u&&artifact->fluid_type_id==ODG_FLUID_NONE&&
               storage_used(&artifact->storage)==0u;
    }
    if(artifact->instance_id==0u||(artifact->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u)return 0;
    definition=odg_item_definition_internal(artifact->item_type);
    profile=artifact_type_profile(artifact->item_type);
    if(definition==NULL||profile==NULL||!odg_item_material_variant_valid_internal(artifact->item_type,artifact->material_tier))return 0;
    if(artifact->owner_actor_id>=ODG_MAX_ACTORS||artifact->capability_bits!=profile->capability_bits||
       (artifact->state&~allowed_state)!=0u)return 0;
    if(artifact->active){
        if(artifact->local_resident){
            if(!odg_global_fx_to_local_internal(artifact->global_fx_x,artifact->global_fx_z,&local_x,&local_z)||
               local_x!=artifact->x||local_z!=artifact->z)return 0;
        }else if(artifact->x!=0||artifact->z!=0)return 0;
    }else{
        if(artifact->x!=0||artifact->z!=0||artifact->global_fx_x!=0||artifact->global_fx_z!=0||artifact->local_resident!=0u)return 0;
        if(definition->max_stack!=1u||(artifact->state&ODG_ARTIFACT_STATE_DEATH_CACHE)!=0u)return 0;
    }

    if((artifact->state&ODG_ARTIFACT_STATE_DEATH_CACHE)!=0u){
        if(!artifact->active||(artifact->state&ODG_ARTIFACT_STATE_PROTECTED)!=0u||
           !odg_item_inventory_expander_recovery_internal(artifact->item_type)||artifact->aux_tick==0u||
           artifact->aux_u32!=0u||artifact->fluid_type_id!=ODG_FLUID_NONE)return 0;
        /* Death caches are tick-bounded runtime objects, not arbitrary scheduled
         * artifacts. A current SAVE can only observe one strictly in the future and
         * no farther away than the authoritative lifetime chosen at creation. */
        if(artifact->aux_tick<=g_odg.tick||
           artifact->aux_tick-g_odg.tick>(uint64_t)ODG_DEATH_CACHE_LIFETIME_TICKS)return 0;
        if((profile->capability_bits&(ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_STORE))!=
           (ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_STORE))return 0;
        return 1;
    }
    if(profile->deployable==0u||artifact->aux_tick!=0u)return 0;
    if((profile->capability_bits&ODG_ARTIFACT_CAP_STORE)==0u&&storage_used(&artifact->storage)!=0u)return 0;
    if(!artifact->active&&storage_used(&artifact->storage)!=0u)return 0;

    if((profile->capability_bits&ODG_ARTIFACT_CAP_COLLECT_RAIN)!=0u){
        if(profile->fluid_capacity_units==0u||artifact->aux_u32>profile->fluid_capacity_units)return 0;
        if(artifact->fluid_type_id!=ODG_FLUID_NONE&&artifact->fluid_type_id!=ODG_FLUID_WATER)return 0;
        if(artifact->aux_u32==0u&&artifact->fluid_type_id!=ODG_FLUID_NONE)return 0;
    }else if((profile->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)!=0u){
        if(artifact->fluid_type_id!=ODG_FLUID_NONE||artifact->aux_u32>ODG_MAX_ACTORS)return 0;
        if(!artifact->active&&artifact->aux_u32!=0u)return 0;
    }else if(artifact->aux_u32!=0u||artifact->fluid_type_id!=ODG_FLUID_NONE)return 0;
    return 1;
}

int odg_artifact_cross_reference_validate_internal(void){
    uint8_t rider_seen[ODG_MAX_ACTORS];uint32_t i;
    odg_memset(rider_seen,0,sizeof(rider_seen));
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *artifact=&g_odg_artifacts[i];uint32_t rider;const odg_actor *actor;
        if(!artifact->active||(artifact->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)==0u||artifact->aux_u32==0u)continue;
        rider=artifact->aux_u32-1u;if(rider>=ODG_MAX_ACTORS||rider_seen[rider]!=0u)return 0;
        actor=&g_odg.actors[rider];
        if(!actor->active||actor->hp==0u||actor->local_resident!=artifact->local_resident||
           actor->global_fx_x!=artifact->global_fx_x||actor->global_fx_z!=artifact->global_fx_z||
           actor->vertical_velocity_fx!=0||actor->grounded!=0u)return 0;
        if(actor->local_resident!=0u&&(actor->x!=artifact->x||actor->z!=artifact->z))return 0;
        rider_seen[rider]=1u;
    }
    return 1;
}

static void artifact_set_dormant_portable(odg_artifact *artifact){
    if(artifact==NULL)return;
    /* A carried artifact keeps its persistent object state, not a stale floating-origin
     * cache from the last deployment. Placement reconstructs both coordinate spaces. */
    artifact->active=0u;artifact->x=0;artifact->z=0;artifact->global_fx_x=0;artifact->global_fx_z=0;
    artifact->local_resident=0u;
}

int odg_artifact_item_deployable_internal(uint32_t item_type) {
    const odg_artifact_type_profile *profile=artifact_type_profile(item_type);
    return profile!=NULL&&profile->deployable!=0u;
}

typedef struct {
    int64_t global_x,global_z;
    uint32_t best;
} odg_artifact_light_query;

static int artifact_light_visit(uint32_t id,void *context) {
    odg_artifact_light_query *q=(odg_artifact_light_query *)context;
    const odg_artifact *artifact;const odg_artifact_type_profile *profile;
    int64_t radius,dx,dz;uint64_t d2,limit;uint32_t distance,intensity;
    if(q==NULL||id>=g_odg.artifact_count)return 0;
    artifact=&g_odg_artifacts[id];if(!artifact->active)return 0;
    profile=artifact_type_profile(artifact->item_type);
    if(profile==NULL||(profile->capability_bits&ODG_ARTIFACT_CAP_LIGHT)==0u)return 0;
    radius=((int64_t)profile->light_radius_milli*ODG_FX_ONE)/1000;
    dx=artifact->global_fx_x-q->global_x;dz=artifact->global_fx_z-q->global_z;
    d2=(uint64_t)(dx*dx+dz*dz);limit=(uint64_t)(radius*radius);
    if(d2>=limit)return 0;
    distance=odg_isqrt_u64(d2);
    intensity=profile->light_peak_permille-(uint32_t)(((uint64_t)distance*profile->light_peak_permille)/(uint64_t)radius);
    if(intensity>q->best)q->best=intensity;
    return 0;
}

uint32_t odg_artifact_light_permille_internal(int32_t x,int32_t z){
    uint32_t i,max_radius_milli=0u;odg_artifact_light_query q;int64_t radius_fx;
    for(i=0u;i<(uint32_t)(sizeof(g_artifact_type_profiles)/sizeof(g_artifact_type_profiles[0]));++i)
        if(g_artifact_type_profiles[i].light_radius_milli>max_radius_milli)max_radius_milli=g_artifact_type_profiles[i].light_radius_milli;
    if(max_radius_milli==0u)return 0u;
    odg_local_fx_to_global_fx_internal(x,z,&q.global_x,&q.global_z);q.best=0u;
    radius_fx=((int64_t)max_radius_milli*ODG_FX_ONE)/1000;
    if(radius_fx>INT32_MAX)radius_fx=INT32_MAX;
    (void)odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_ARTIFACT,q.global_x,q.global_z,
                                                  (int32_t)radius_fx,artifact_light_visit,&q);
    return q.best;
}

typedef struct {
    int64_t global_x,global_z,limit;
    uint32_t ignore_id;
} odg_artifact_collision_query;

static int artifact_collision_visit(uint32_t id,void *context) {
    odg_artifact_collision_query *q=(odg_artifact_collision_query *)context;
    const odg_artifact *artifact;int64_t dx,dz;
    if(q==NULL||id>=g_odg.artifact_count||id==q->ignore_id)return 0;
    artifact=&g_odg_artifacts[id];if(!artifact->active)return 0;
    dx=artifact->global_fx_x-q->global_x;dz=artifact->global_fx_z-q->global_z;
    return dx*dx+dz*dz<q->limit;
}

int odg_artifact_position_blocked(int32_t x,int32_t z,int32_t radius,uint32_t ignore_id) {
    odg_artifact_collision_query q;int32_t sum=radius+ODG_ARTIFACT_RADIUS_FX;
    odg_local_fx_to_global_fx_internal(x,z,&q.global_x,&q.global_z);q.limit=(int64_t)sum*sum;q.ignore_id=ignore_id;
    return odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_ARTIFACT,q.global_x,q.global_z,
                                                   sum,artifact_collision_visit,&q);
}

static uint32_t nearest_artifact(const odg_actor *actor) {
    uint32_t i,best=UINT32_MAX;
    int64_t best_d2=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX;
    if (actor==NULL) return UINT32_MAX;
    for (i=0u;i<g_odg.artifact_count;++i) {
        const odg_artifact *artifact=&g_odg_artifacts[i];
        int64_t d2;
        if (!artifact->active || artifact->local_resident==0u) continue;
        d2=odg_dist2(actor->x,actor->z,artifact->x,artifact->z);
        if (d2<=best_d2) {best_d2=d2;best=i;}
    }
    return best;
}


int odg_artifact_actor_can_access_internal(uint32_t actor_id,const odg_artifact *artifact);

uint32_t odg_artifact_actor_vehicle_internal(uint32_t actor_id) {
    uint32_t i,rider=actor_id+1u;
    if(actor_id>=ODG_MAX_ACTORS)return UINT32_MAX;
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *a=&g_odg_artifacts[i];
        if(a->active&&a->local_resident&&(a->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)!=0u&&a->aux_u32==rider)return i;
    }
    return UINT32_MAX;
}

static int vehicle_dismount_candidate(const odg_artifact *vehicle,int32_t *out_x,int32_t *out_z){
    static const int32_t dirs[][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    static const int32_t radii[]={2*ODG_FX_ONE,5*ODG_FX_ONE/2,3*ODG_FX_ONE};
    uint32_t ri,di,ignore_actor_id=UINT32_MAX;
    if(vehicle==NULL||out_x==NULL||out_z==NULL)return 0;
    if(vehicle->aux_u32!=0u&&vehicle->aux_u32<=ODG_MAX_ACTORS)ignore_actor_id=vehicle->aux_u32-1u;
    /* Prefer a safe dry shore, but allow bailing into open water if no bank is reachable.
     * The latter remains safe because the actor immediately enters the swimming model. */
    for(ri=0u;ri<(uint32_t)(sizeof(radii)/sizeof(radii[0]));++ri){
        for(di=0u;di<(uint32_t)(sizeof(dirs)/sizeof(dirs[0]));++di){
            int32_t x=vehicle->x+dirs[di][0]*radii[ri];
            int32_t z=vehicle->z+dirs[di][1]*radii[ri];
            odg_surface_sample surface;
            if(!odg_environment_surface_local(x,z,&surface)||(surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)continue;
            if(!odg_position_clear_internal(x,z,ODG_FX_ONE/3))continue;
            if(!odg_dynamic_position_clear_internal(x,z,ODG_FX_ONE/3,ignore_actor_id,UINT32_MAX))continue;
            *out_x=x;*out_z=z;return 1;
        }
    }
    for(ri=0u;ri<(uint32_t)(sizeof(radii)/sizeof(radii[0]));++ri){
        for(di=0u;di<(uint32_t)(sizeof(dirs)/sizeof(dirs[0]));++di){
            int32_t x=vehicle->x+dirs[di][0]*radii[ri];
            int32_t z=vehicle->z+dirs[di][1]*radii[ri];
            odg_surface_sample surface;
            if(!odg_environment_surface_local(x,z,&surface)||(surface.flags&ODG_SURFACE_FLAG_WATER)==0u)continue;
            if(!odg_position_clear_internal(x,z,ODG_FX_ONE/3))continue;
            if(!odg_dynamic_position_clear_internal(x,z,ODG_FX_ONE/3,ignore_actor_id,UINT32_MAX))continue;
            *out_x=x;*out_z=z;return 1;
        }
    }
    return 0;
}

static int vehicle_toggle_mount(uint32_t actor_id,odg_artifact *vehicle){
    odg_actor *actor;uint32_t rider;int32_t x,z;
    if(actor_id>=ODG_MAX_ACTORS||vehicle==NULL||!vehicle->active||
       (vehicle->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)==0u)return 0;
    actor=&g_odg.actors[actor_id];rider=actor_id+1u;
    if(vehicle->aux_u32==rider){
        if(!vehicle_dismount_candidate(vehicle,&x,&z))return 0;
        vehicle->aux_u32=0u;actor->x=x;actor->z=z;actor->vertical_offset_fx=0;
        actor->vertical_velocity_fx=0;actor->grounded=1u;actor->vx=0;actor->vz=0;
        odg_local_fx_to_global_fx_internal(x,z,&actor->global_fx_x,&actor->global_fx_z);
        odg_entities_spatial_mark_dirty();return 1;
    }
    if(vehicle->aux_u32!=0u||odg_artifact_actor_vehicle_internal(actor_id)!=UINT32_MAX)return 0;
    if(vehicle->local_resident==0u||actor->local_resident==0u||
       odg_dist2(actor->x,actor->z,vehicle->x,vehicle->z)>
       (int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX)return 0;
    if(!odg_artifact_actor_can_access_internal(actor_id,vehicle))return 0;
    vehicle->aux_u32=rider;actor->x=vehicle->x;actor->z=vehicle->z;actor->vx=0;actor->vz=0;
    actor->vertical_velocity_fx=0;actor->grounded=0u;
    (void)odg_artifact_vehicle_move_actor_internal(actor,0,0);
    return 1;
}

int odg_artifact_vehicle_toggle_internal(uint32_t actor_id,uint32_t artifact_id){
    if(artifact_id>=g_odg.artifact_count)return 0;
    return vehicle_toggle_mount(actor_id,&g_odg_artifacts[artifact_id]);
}

static int vehicle_target_valid(const odg_actor *actor,uint32_t vehicle_id,int32_t nx,int32_t nz,odg_surface_sample *out_surface){
    const odg_artifact *vehicle;const odg_artifact_type_profile *profile;odg_surface_sample surface;
    if(actor==NULL||vehicle_id>=g_odg.artifact_count)return 0;
    vehicle=&g_odg_artifacts[vehicle_id];profile=artifact_type_profile(vehicle->item_type);
    if(!vehicle->active||profile==NULL||(profile->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)==0u)return 0;
    if(!odg_environment_surface_local(nx,nz,&surface)||(surface.flags&ODG_SURFACE_FLAG_WATER)==0u||
       surface.water_depth_milli<profile->min_water_depth_milli)return 0;
    if(!odg_position_clear_ignoring_artifact_internal(nx,nz,ODG_ARTIFACT_RADIUS_FX,vehicle_id))return 0;
    if(!odg_dynamic_position_clear_internal(nx,nz,ODG_ARTIFACT_RADIUS_FX,actor->id,UINT32_MAX))return 0;
    if(out_surface!=NULL)*out_surface=surface;
    return 1;
}

int odg_artifact_vehicle_can_move_actor_internal(const odg_actor *actor,int32_t dx,int32_t dz){
    uint32_t id;
    if(actor==NULL)return 0;
    id=odg_artifact_actor_vehicle_internal(actor->id);
    if(id>=g_odg.artifact_count)return 0;
    return vehicle_target_valid(actor,id,g_odg_artifacts[id].x+dx,g_odg_artifacts[id].z+dz,NULL);
}

int odg_artifact_vehicle_move_actor_internal(odg_actor *actor,int32_t dx,int32_t dz){
    uint32_t id;odg_artifact *vehicle;const odg_artifact_type_profile *profile;
    odg_surface_sample surface;int32_t nx,nz;uint64_t lift_fx;
    if(actor==NULL)return 0;
    id=odg_artifact_actor_vehicle_internal(actor->id);if(id>=g_odg.artifact_count)return 0;
    vehicle=&g_odg_artifacts[id];profile=artifact_type_profile(vehicle->item_type);
    if(profile==NULL||(profile->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)==0u)return 0;
    nx=vehicle->x+dx;nz=vehicle->z+dz;
    if(!vehicle_target_valid(actor,id,nx,nz,&surface))return 0;
    vehicle->x=nx;vehicle->z=nz;actor->x=nx;actor->z=nz;
    odg_local_fx_to_global_fx_internal(nx,nz,&vehicle->global_fx_x,&vehicle->global_fx_z);
    actor->global_fx_x=vehicle->global_fx_x;actor->global_fx_z=vehicle->global_fx_z;
    vehicle->local_resident=1u;actor->local_resident=1u;
    lift_fx=((uint64_t)(surface.water_depth_milli+(uint32_t)ODG_RAFT_DECK_ABOVE_WATER_MILLI)*(uint64_t)ODG_FX_ONE)/1000u;
    actor->vertical_offset_fx=lift_fx>(uint64_t)INT32_MAX?INT32_MAX:(int32_t)lift_fx;
    actor->vertical_velocity_fx=0;actor->grounded=0u;
    odg_entities_spatial_mark_dirty();return 1;
}

int odg_artifact_actor_can_access_internal(uint32_t actor_id,const odg_artifact *artifact) {
    if(actor_id>=ODG_MAX_ACTORS||artifact==NULL||!artifact->active)return 0;
    if(artifact->owner_actor_id==actor_id)return 1;
    if((artifact->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)!=0u){
        int64_t gx,gz;uint8_t land_owner;
        odg_local_fx_to_global_cell_internal(artifact->x,artifact->z,&gx,&gz);
        land_owner=odg_chunk_owner_at_global_cell(gx,gz);
        /* Vehicles follow territorial control like salvageable construction: an owner
         * keeps authority while the craft remains on owned water; neutral or conquered
         * water permits another actor to board/recover an abandoned craft. */
        if(land_owner==ODG_OWNER_NONE||land_owner==ODG_OWNER_FROM_ID(actor_id))return 1;
    }
    /* Storage left on conquered land is lootable by the current controller, while
     * natural/neutral ground does not magically transfer ownership. */
    if((artifact->capability_bits&ODG_ARTIFACT_CAP_STORE)!=0u &&
       odg_territory_actor_controls_position(actor_id,artifact->x,artifact->z))return 1;
    return 0;
}

static int artifact_actor_can_dismantle(uint32_t actor_id,const odg_artifact *artifact) {
    return odg_artifact_actor_can_access_internal(actor_id,artifact);
}

static int artifact_surface_allows(const odg_artifact_type_profile *profile,int32_t x,int32_t z) {
    odg_surface_sample surface;
    if(profile==NULL||!odg_environment_surface_local(x,z,&surface))return 0;
    if((surface.flags&ODG_SURFACE_FLAG_WATER)!=0u){
        return (profile->placement_medium_mask&ODG_ARTIFACT_PLACE_WATER)!=0u&&
               surface.water_depth_milli>=profile->min_water_depth_milli;
    }
    if((surface.flags&ODG_SURFACE_FLAG_STEEP)!=0u)return 0;
    return (profile->placement_medium_mask&ODG_ARTIFACT_PLACE_LAND)!=0u;
}

int odg_artifact_surface_allows_item_internal(uint32_t item_type,int32_t x,int32_t z){
    return artifact_surface_allows(artifact_type_profile(item_type),x,z);
}

uint32_t odg_artifact_physical_height_milli_internal(const odg_artifact *artifact){
    const odg_artifact_type_profile *profile;
    if(artifact==NULL||!artifact->active)return 0u;
    profile=artifact_type_profile(artifact->item_type);
    return profile!=NULL?profile->physical_height_milli:650u;
}

int32_t odg_artifact_collision_radius_fx_internal(const odg_artifact *artifact){
    return artifact!=NULL&&artifact->active?ODG_ARTIFACT_RADIUS_FX:0;
}

int odg_artifact_placement_candidate_for_item_internal(const odg_actor *actor,uint32_t item_type,
                                                        int32_t *out_x,int32_t *out_z) {
    /* Placement is a capability query, not a single fragile point in front of the
     * actor. Search a compact deterministic fan. The selected artifact profile owns the
     * physical medium: land infrastructure never materializes underwater, and a water
     * vehicle never spawns beached on dry terrain. Gameplay + ghost use this authority. */
    static const int32_t distance_num[] = {9,12,15,18,21}; /* /5 metres */
    static const int32_t lateral_num[] = {0,3,-3,6,-6,9,-9}; /* /5 metres */
    const odg_artifact_type_profile *profile=artifact_type_profile(item_type);
    uint32_t di,li;
    int32_t fx,fz,rx,rz;
    if(actor==NULL||out_x==NULL||out_z==NULL||profile==NULL||profile->deployable==0u)return 0;
    fx=actor->face_x_q15;fz=actor->face_z_q15;
    if(fx==0&&fz==0){fx=0;fz=ODG_Q15_ONE;}
    rx=-fz;rz=fx;
    for(di=0u;di<(uint32_t)(sizeof(distance_num)/sizeof(distance_num[0]));++di){
        int32_t distance=(distance_num[di]*ODG_FX_ONE)/5;
        for(li=0u;li<(uint32_t)(sizeof(lateral_num)/sizeof(lateral_num[0]));++li){
            int32_t lateral=(lateral_num[li]*ODG_FX_ONE)/5;
            int32_t x=actor->x+(int32_t)(((int64_t)fx*distance+(int64_t)rx*lateral)/ODG_Q15_ONE);
            int32_t z=actor->z+(int32_t)(((int64_t)fz*distance+(int64_t)rz*lateral)/ODG_Q15_ONE);
            int64_t gx,gz;
            odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
            if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_FROM_ID(actor->id))continue;
            if(!artifact_surface_allows(profile,x,z))continue;
            if(odg_chunk_procedural_turret_reserves_local_circle_internal(x,z,ODG_ARTIFACT_RADIUS_FX))continue;
            if(!odg_position_clear_internal(x,z,ODG_ARTIFACT_RADIUS_FX))continue;
            if(!odg_dynamic_position_clear_internal(x,z,ODG_ARTIFACT_RADIUS_FX,actor->id,UINT32_MAX))continue;
            *out_x=x;*out_z=z;return 1;
        }
    }
    return 0;
}

static uint32_t alloc_artifact_system_slot(void) {
    uint32_t i;
    /* Inactive artifacts with a live instance id are not free: their state can be
     * referenced by an artifact item currently carried, stored, or dropped in the
     * world. Reusing such a slot would make two different items share one payload. */
    for(i=0u;i<g_odg.artifact_count;++i)
        if(!g_odg_artifacts[i].active&&g_odg_artifacts[i].instance_id==0u)return i;
    if(!odg_entities_reserve_artifacts(g_odg.artifact_count+1u))return UINT32_MAX;
    i=g_odg.artifact_count++;
    odg_memset(&g_odg_artifacts[i],0,sizeof(g_odg_artifacts[i]));
    g_odg_artifacts[i].id=i;
    return i;
}

static int artifact_slot_for_item(const odg_item_stack *item,uint32_t *out_index,uint32_t *out_append){
    uint32_t i;
    if(item==NULL||out_index==NULL||out_append==NULL)return 0;
    *out_index=UINT32_MAX;*out_append=0u;
    if(item->payload_id!=0u){
        uint64_t raw=item->payload_id-UINT64_C(1);const odg_artifact *existing;
        if(raw>=g_odg.artifact_count||item->instance_id==0u)return 0;
        existing=&g_odg_artifacts[(uint32_t)raw];
        if(existing->active||existing->instance_id==0u||existing->instance_id!=item->instance_id)return 0;
        if(existing->item_type!=item->type_id||existing->material_tier!=item->material_tier)return 0;
        if((existing->state&ODG_ARTIFACT_STATE_DEATH_CACHE)!=0u)return 0;
        *out_index=(uint32_t)raw;return 1;
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        if(!g_odg_artifacts[i].active&&g_odg_artifacts[i].instance_id==0u){*out_index=i;return 1;}
    }
    if(!odg_entities_reserve_artifacts(g_odg.artifact_count+1u))return 0;
    *out_index=g_odg.artifact_count;*out_append=1u;return 1;
}

static int add_initial_workbench_at(uint32_t actor_id,int32_t x,int32_t z){
    int64_t gx,gz;uint64_t instance_id;uint32_t index;odg_artifact *artifact;
    odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
    if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_FROM_ID(actor_id))return 0;
    if(!odg_artifact_surface_allows_item_internal(ODG_ITEM_WORKBENCH,x,z))return 0;
    if(odg_chunk_procedural_turret_reserves_local_circle_internal(x,z,ODG_ARTIFACT_RADIUS_FX))return 0;
    if(!odg_position_clear_internal(x,z,ODG_ARTIFACT_RADIUS_FX))return 0;
    if(!odg_dynamic_position_clear_internal(x,z,ODG_ARTIFACT_RADIUS_FX,UINT32_MAX,UINT32_MAX))return 0;
    if(!odg_entities_reserve_artifacts(g_odg.artifact_count+1u))return 0;
    instance_id=odg_next_instance_id();if(instance_id==0u)return 0;
    index=g_odg.artifact_count++;artifact=&g_odg_artifacts[index];
    odg_memset(artifact,0,sizeof(*artifact));
    artifact->active=1u;artifact->id=index;artifact->instance_id=instance_id;
    artifact->item_type=ODG_ITEM_WORKBENCH;artifact->owner_actor_id=actor_id;artifact->material_tier=ODG_MATERIAL_WOOD;
    artifact->capability_bits=artifact_caps(artifact->item_type);artifact->state=ODG_ARTIFACT_STATE_PROTECTED;
    artifact->x=x;artifact->z=z;artifact->local_resident=1u;
    odg_local_fx_to_global_fx_internal(x,z,&artifact->global_fx_x,&artifact->global_fx_z);
    odg_entities_spatial_mark_dirty();return 1;
}

static int add_initial_workbench(uint32_t actor_id) {
    static const int32_t offsets[][2]={{2,0},{-2,0},{0,2},{0,-2},{2,2},{-2,2},{2,-2},{-2,-2},{3,0},{0,3}};
    odg_actor *actor;uint32_t j,radius;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];
    for(j=0u;j<(uint32_t)(sizeof(offsets)/sizeof(offsets[0]));++j)
        if(add_initial_workbench_at(actor_id,actor->x+offsets[j][0]*ODG_FX_ONE,
                                    actor->z+offsets[j][1]*ODG_FX_ONE))return 1;
    /* Same physical/territorial rules as player placement, but no silent missing station:
     * exhaust the small initial nation footprint in deterministic rings. */
    for(radius=1u;radius<=4u;++radius){
        int32_t r=(int32_t)radius,side;
        for(side=-r;side<=r;++side){
            int32_t offsets2[4][2]={{side,-r},{r,side},{-side,r},{-r,-side}};uint32_t q;
            for(q=0u;q<4u;++q)
                if(add_initial_workbench_at(actor_id,actor->x+offsets2[q][0]*ODG_FX_ONE,
                                            actor->z+offsets2[q][1]*ODG_FX_ONE))return 1;
        }
    }
    return 0;
}

void odg_artifacts_build_initial(void) {
    uint32_t actor;
    g_odg.artifact_count=0u;g_odg.opened_artifact_id=UINT32_MAX;
    for (actor=0u;actor<ODG_MAX_ACTORS;++actor) (void)add_initial_workbench(actor);
}

void odg_artifacts_tick(void) {
    uint32_t i;
    for(i=0u;i<g_odg.artifact_count;++i){
        odg_artifact *a=&g_odg_artifacts[i];
        if(!a->active)continue;
        if((a->state&ODG_ARTIFACT_STATE_DEATH_CACHE)!=0u && a->aux_tick!=0u && g_odg.tick>=a->aux_tick){
            uint32_t id=a->id;odg_memset(a,0,sizeof(*a));a->id=id;continue;
        }
        if((a->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)!=0u&&a->aux_u32!=0u){
            uint32_t rider=a->aux_u32-1u;
            if(rider>=ODG_MAX_ACTORS||!g_odg.actors[rider].active||g_odg.actors[rider].hp==0u)a->aux_u32=0u;
        }
        if((a->capability_bits&ODG_ARTIFACT_CAP_COLLECT_RAIN)!=0u &&
           (g_odg.tick%ODG_TICK_RATE)==0u && g_odg.weather_rain_permille>0u){
            const odg_artifact_type_profile *profile=artifact_type_profile(a->item_type);
            uint32_t capacity=profile!=NULL?profile->fluid_capacity_units:0u;
            if(capacity!=0u){
                uint32_t add=1u+g_odg.weather_rain_permille/250u;
                a->aux_u32=a->aux_u32+add>capacity?capacity:a->aux_u32+add;
                if(a->aux_u32!=0u)a->fluid_type_id=ODG_FLUID_WATER;
            }
        }
    }
    if (g_odg.opened_artifact_id<g_odg.artifact_count && !g_odg_artifacts[g_odg.opened_artifact_id].active)
        g_odg.opened_artifact_id=UINT32_MAX;
}

int odg_artifact_place_selected(uint32_t actor_id) {
    odg_actor *actor;odg_item_stack *stack,item;odg_inventory staged;odg_artifact *artifact;
    uint64_t new_instance_id=0u;uint32_t index,append=0u,selected_slot;int32_t x,z;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];stack=odg_inventory_selected(&actor->inventory);
    if(stack==NULL||stack->quantity==0u||!odg_artifact_item_deployable_internal(stack->type_id))return 0;
    item=*stack;selected_slot=actor->inventory.selected_slot;
    if(!odg_artifact_placement_candidate_for_item_internal(actor,item.type_id,&x,&z))return 0;
    if(!artifact_slot_for_item(&item,&index,&append))return 0;

    /* Stage the inventory mutation before touching world state. After slot capacity has
     * been reserved, artifact commit has no fallible operation left, so placement is
     * atomic: either the item remains untouched or exactly one world artifact appears. */
    staged=actor->inventory;
    if(!odg_inventory_remove_from_slot(&staged,selected_slot,1u,NULL))return 0;
    if(item.payload_id==0u){
        new_instance_id=item.instance_id!=0u?item.instance_id:odg_next_instance_id();
        if(new_instance_id==0u)return 0;
    }
    actor->inventory=staged;
    if(append)++g_odg.artifact_count;
    artifact=&g_odg_artifacts[index];
    if(item.payload_id==0u){
        odg_memset(artifact,0,sizeof(*artifact));artifact->id=index;artifact->instance_id=new_instance_id;
    }
    artifact->active=1u;artifact->item_type=item.type_id;artifact->owner_actor_id=actor_id;
    artifact->material_tier=item.material_tier;artifact->capability_bits=artifact_caps(item.type_id);
    if((item.flags&ODG_ITEM_FLAG_PROTECTED)!=0u)artifact->state|=ODG_ARTIFACT_STATE_PROTECTED;
    artifact->x=x;artifact->z=z;artifact->local_resident=1u;
    odg_local_fx_to_global_fx_internal(x,z,&artifact->global_fx_x,&artifact->global_fx_z);
    odg_entities_spatial_mark_dirty();odg_emit_particles(x,z,0x8ce8ffffu,10u);return 1;
}

int odg_artifact_build_hint(const odg_actor *actor,const odg_item_stack *selected,odg_interaction_hint *hint) {
    uint32_t id;
    if (actor==NULL || hint==NULL) return 0;
    if (selected!=NULL && selected->quantity!=0u && odg_artifact_item_deployable_internal(selected->type_id)) {
        int32_t x,z;
        hint->action=ODG_INTERACTION_PLACE_ARTIFACT;hint->target_kind=ODG_INTERACTION_TARGET_ARTIFACT;
        hint->target_id=UINT32_MAX;hint->valid=odg_artifact_placement_candidate_for_item_internal(actor,selected->type_id,&x,&z)?1u:0u;
        hint->requires_hold=0u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;
        hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_INVALID_PLACEMENT;
        return 1;
    }
    id=nearest_artifact(actor);if (id>=g_odg.artifact_count) return 0;
    hint->action=ODG_INTERACTION_OPEN_ARTIFACT;hint->target_kind=ODG_INTERACTION_TARGET_ARTIFACT;hint->target_id=id;
    if((g_odg_artifacts[id].capability_bits&ODG_ARTIFACT_CAP_VEHICLE)!=0u){
        uint32_t rider=g_odg_artifacts[id].aux_u32;
        hint->action=ODG_INTERACTION_USE_VEHICLE;
        hint->valid=(odg_artifact_actor_can_access_internal(actor->id,&g_odg_artifacts[id])&&
                    (rider==0u||rider==actor->id+1u))?1u:0u;
        /* Tap boards/leaves; holding an empty craft recovers it as an item. */
        hint->requires_hold=1u;hint->threshold_ticks=ODG_INTERACT_HOLD_TICKS;
        hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_OWNER_ONLY;
    }else{
        hint->valid=odg_artifact_actor_can_access_internal(actor->id,&g_odg_artifacts[id])?1u:0u;
        hint->requires_hold=1u;hint->threshold_ticks=ODG_INTERACT_HOLD_TICKS;
        hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_OWNER_ONLY;
    }
    return 1;
}

int odg_artifact_execute_tap(uint32_t actor_id,const odg_interaction_hint *hint) {
    odg_artifact *artifact;
    if (actor_id>=ODG_MAX_ACTORS || hint==NULL || !hint->valid) return 0;
    if (hint->action==ODG_INTERACTION_PLACE_ARTIFACT) return odg_artifact_place_selected(actor_id);
    if(hint->action==ODG_INTERACTION_USE_VEHICLE){
        if(hint->target_id>=g_odg.artifact_count)return 0;
        return odg_artifact_vehicle_toggle_internal(actor_id,hint->target_id);
    }
    if (hint->action!=ODG_INTERACTION_OPEN_ARTIFACT || hint->target_id>=g_odg.artifact_count) return 0;
    artifact=&g_odg_artifacts[hint->target_id];
    if (!artifact->active || !odg_artifact_actor_can_access_internal(actor_id,artifact) || (artifact->capability_bits&ODG_ARTIFACT_CAP_OPEN_UI)==0u) return 0;
    g_odg.opened_artifact_id=hint->target_id;return 1;
}

int odg_artifact_execute_hold(uint32_t actor_id,const odg_interaction_hint *hint) {
    odg_actor *actor;odg_artifact *artifact;odg_item_stack stack;const odg_item_definition *definition;
    if (actor_id>=ODG_MAX_ACTORS || hint==NULL || hint->target_id>=g_odg.artifact_count) return 0;
    actor=&g_odg.actors[actor_id];artifact=&g_odg_artifacts[hint->target_id];
    if(hint->action!=ODG_INTERACTION_OPEN_ARTIFACT&&hint->action!=ODG_INTERACTION_USE_VEHICLE)return 0;
    if (!artifact->active || !artifact_actor_can_dismantle(actor_id,artifact) || (artifact->capability_bits&ODG_ARTIFACT_CAP_MOVE)==0u) return 0;
    if((artifact->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)!=0u&&artifact->aux_u32!=0u)return 0;
    if(odg_artifact_is_death_cache(artifact))return odg_artifact_recover_death_cache(actor_id,hint->target_id);
    if ((artifact->capability_bits&ODG_ARTIFACT_CAP_STORE)!=0u && storage_used(&artifact->storage)!=0u) return 0;
    definition=odg_item_definition_internal(artifact->item_type);if(definition==NULL)return 0;
    odg_memset(&stack,0,sizeof(stack));stack.type_id=artifact->item_type;stack.quantity=1u;stack.material_tier=artifact->material_tier;
    stack.flags=definition->flags;
    if(definition->max_stack==1u){
        stack.instance_id=artifact->instance_id;stack.payload_id=(uint64_t)artifact->id+UINT64_C(1);
    }
    if ((artifact->state&ODG_ARTIFACT_STATE_PROTECTED)!=0u) stack.flags|=ODG_ITEM_FLAG_PROTECTED;
    if (!odg_inventory_add(&actor->inventory,&stack)) return 0;
    if(definition->max_stack>1u){
        /* Stateless stackables have no world payload to preserve. Release the slot
         * completely so allocator/save identity cannot retain a phantom reservation. */
        uint32_t id=artifact->id;odg_memset(artifact,0,sizeof(*artifact));artifact->id=id;
    }else artifact_set_dormant_portable(artifact);
    odg_entities_spatial_mark_dirty();if (g_odg.opened_artifact_id==hint->target_id)g_odg.opened_artifact_id=UINT32_MAX;
    odg_emit_particles(actor->x,actor->z,0x8ce8ffffu,10u);return 1;
}

int odg_crafting_station_near_actor(uint32_t actor_id,uint32_t station_item_type,uint32_t *out_artifact_id) {
    uint32_t i;int64_t max_d2=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX;
    if (actor_id>=ODG_MAX_ACTORS) return 0;
    for (i=0u;i<g_odg.artifact_count;++i) {
        const odg_artifact *artifact=&g_odg_artifacts[i];
        if (!artifact->active || artifact->local_resident==0u || artifact->owner_actor_id!=actor_id || artifact->item_type!=station_item_type) continue;
        if (odg_dist2(g_odg.actors[actor_id].x,g_odg.actors[actor_id].z,artifact->x,artifact->z)<=max_d2) {
            if (out_artifact_id!=NULL) *out_artifact_id=i;
            return 1;
        }
    }
    return 0;
}

uint32_t odg_opened_artifact_id(void) { return g_odg.opened_artifact_id; }

static uint32_t artifact_active_total(void) {
    uint32_t i,total=0u;
    for(i=0u;i<g_odg.artifact_count;++i) if(g_odg_artifacts[i].active) ++total;
    for(i=0u;i<g_odg.turret_count;++i) {
        const odg_turret *turret=&g_odg_turrets[i];
        if(turret->active && turret->carried_by==ODG_TURRET_NONE) ++total;
    }
    return total;
}

static void fill_artifact_entry(odg_artifact_entry *entry,const odg_artifact *artifact,uint32_t artifact_id) {
    int64_t gx_milli,gz_milli;
    entry->instance_id=artifact->instance_id;entry->artifact_id=artifact_id;entry->item_type=artifact->item_type;
    entry->owner_actor_id=artifact->owner_actor_id;entry->material_tier=artifact->material_tier;entry->capability_bits=artifact->capability_bits;
    gx_milli=(artifact->global_fx_x/(int64_t)ODG_FX_ONE)*INT64_C(1000)+((artifact->global_fx_x%(int64_t)ODG_FX_ONE)*INT64_C(1000))/(int64_t)ODG_FX_ONE;
    gz_milli=(artifact->global_fx_z/(int64_t)ODG_FX_ONE)*INT64_C(1000)+((artifact->global_fx_z%(int64_t)ODG_FX_ONE)*INT64_C(1000))/(int64_t)ODG_FX_ONE;
    entry->x_milli=gx_milli<INT32_MIN?INT32_MIN:(gx_milli>INT32_MAX?INT32_MAX:(int32_t)gx_milli);
    entry->z_milli=gz_milli<INT32_MIN?INT32_MIN:(gz_milli>INT32_MAX?INT32_MAX:(int32_t)gz_milli);
    entry->storage_used=storage_used(&artifact->storage);entry->state=artifact->state;
}

static void fill_turret_artifact_entry(odg_artifact_entry *entry,const odg_turret *turret) {
    int64_t gx_milli,gz_milli;
    entry->instance_id=turret->instance_id;entry->artifact_id=turret->id;entry->item_type=ODG_ITEM_TURRET;
    entry->owner_actor_id=turret->owner==ODG_TURRET_NEUTRAL?UINT32_MAX:(uint32_t)(turret->owner-1u);
    entry->material_tier=turret->material_tier;
    entry->capability_bits=ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_REMOTE_VIEW|ODG_ARTIFACT_CAP_FIRE|ODG_ARTIFACT_CAP_HARVEST|ODG_ARTIFACT_CAP_UPGRADE;
    gx_milli=(turret->global_fx_x/(int64_t)ODG_FX_ONE)*INT64_C(1000)+((turret->global_fx_x%(int64_t)ODG_FX_ONE)*INT64_C(1000))/(int64_t)ODG_FX_ONE;
    gz_milli=(turret->global_fx_z/(int64_t)ODG_FX_ONE)*INT64_C(1000)+((turret->global_fx_z%(int64_t)ODG_FX_ONE)*INT64_C(1000))/(int64_t)ODG_FX_ONE;
    entry->x_milli=gx_milli<INT32_MIN?INT32_MIN:(gx_milli>INT32_MAX?INT32_MAX:(int32_t)gx_milli);
    entry->z_milli=gz_milli<INT32_MIN?INT32_MIN:(gz_milli>INT32_MAX?INT32_MAX:(int32_t)gz_milli);
    entry->storage_used=turret->ammo;
    entry->state=(turret->max_ammo&ODG_TURRET_ARTIFACT_STATE_MAX_AMMO_MASK)|(turret->mode<<ODG_TURRET_ARTIFACT_STATE_MODE_SHIFT);
}

int32_t odg_copy_artifacts_page(uint32_t offset,odg_artifact_snapshot *out_artifacts,
                                uint64_t capacity,uint64_t *out_required) {
    uint32_t i,count=0u,logical=0u,total;
    if(out_required!=NULL)*out_required=(uint64_t)sizeof(odg_artifact_snapshot);
    if(!g_odg.initialized)return ODG_STATUS_INVALID_STATE;
    if(out_artifacts==NULL||capacity<(uint64_t)sizeof(*out_artifacts))return ODG_STATUS_BUFFER_TOO_SMALL;
    odg_memset(out_artifacts,0,sizeof(*out_artifacts));out_artifacts->struct_size=(uint32_t)sizeof(*out_artifacts);
    out_artifacts->opened_artifact_id=g_odg.opened_artifact_id;
    total=artifact_active_total();out_artifacts->total_count=total;
    if(offset>=total){out_artifacts->count=0u;return ODG_STATUS_OK;}
    for(i=0u;i<g_odg.artifact_count && count<ODG_ARTIFACT_MAX_ENTRIES;++i){
        const odg_artifact *artifact=&g_odg_artifacts[i];
        if(!artifact->active)continue;
        if(logical++<offset)continue;
        fill_artifact_entry(&out_artifacts->entries[count++],artifact,i);
    }
    for(i=0u;i<g_odg.turret_count && count<ODG_ARTIFACT_MAX_ENTRIES;++i){
        const odg_turret *turret=&g_odg_turrets[i];
        if(!turret->active||turret->carried_by!=ODG_TURRET_NONE)continue;
        if(logical++<offset)continue;
        fill_turret_artifact_entry(&out_artifacts->entries[count++],turret);
    }
    out_artifacts->count=count;return ODG_STATUS_OK;
}

int32_t odg_copy_artifacts(odg_artifact_snapshot *out_artifacts,uint64_t capacity,uint64_t *out_required) {
    return odg_copy_artifacts_page(0u,out_artifacts,capacity,out_required);
}

static odg_artifact *owned_storage_artifact(uint32_t actor_id,uint32_t artifact_id) {
    odg_artifact *artifact;
    if (actor_id>=ODG_MAX_ACTORS || artifact_id>=g_odg.artifact_count) return NULL;
    artifact=&g_odg_artifacts[artifact_id];
    if (!artifact->active || !odg_artifact_actor_can_access_internal(actor_id,artifact) ||
        (artifact->capability_bits&ODG_ARTIFACT_CAP_STORE)==0u) return NULL;
    return artifact;
}

int32_t odg_copy_artifact_storage(uint32_t actor_id,uint32_t artifact_id,
                                  odg_storage_snapshot *out_storage,
                                  uint64_t capacity,uint64_t *out_required) {
    odg_artifact *artifact;
    uint32_t i;
    if (out_required!=NULL) *out_required=(uint64_t)sizeof(odg_storage_snapshot);
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (out_storage==NULL || capacity<(uint64_t)sizeof(*out_storage)) return ODG_STATUS_BUFFER_TOO_SMALL;
    artifact=owned_storage_artifact(actor_id,artifact_id);
    if (artifact==NULL) return ODG_STATUS_INVALID_ARGUMENT;
    odg_memset(out_storage,0,sizeof(*out_storage));
    out_storage->struct_size=(uint32_t)sizeof(*out_storage);
    out_storage->artifact_id=artifact_id;
    out_storage->slot_count=ODG_CHEST_SLOTS;
    out_storage->used_slots=storage_used(&artifact->storage);
    for (i=0u;i<ODG_CHEST_SLOTS;++i) out_storage->slots[i]=artifact->storage.slots[i];
    return ODG_STATUS_OK;
}

int32_t odg_artifact_storage_deposit(uint32_t actor_id,uint32_t artifact_id,
                                     uint32_t inventory_slot,uint32_t quantity) {
    odg_actor *actor;
    odg_artifact *artifact;
    odg_item_stack removed;
    odg_inventory inventory_temp;
    odg_storage storage_temp;
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (quantity==0u || actor_id>=ODG_MAX_ACTORS) return ODG_STATUS_INVALID_ARGUMENT;
    actor=&g_odg.actors[actor_id];artifact=owned_storage_artifact(actor_id,artifact_id);
    if (artifact==NULL || artifact->local_resident==0u || odg_dist2(actor->x,actor->z,artifact->x,artifact->z)>
        (int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX) return ODG_STATUS_INVALID_STATE;
    if (inventory_slot>=odg_inventory_capacity(&actor->inventory)) return ODG_STATUS_INVALID_ARGUMENT;
    inventory_temp=actor->inventory;storage_temp=artifact->storage;
    if (!odg_inventory_remove_from_slot(&inventory_temp,inventory_slot,quantity,&removed))
        return ODG_STATUS_INVALID_ARGUMENT;
    if (!odg_slots_add(storage_temp.slots,ODG_CHEST_SLOTS,&removed)) return ODG_STATUS_INVALID_STATE;
    actor->inventory=inventory_temp;artifact->storage=storage_temp;
    return ODG_STATUS_OK;
}

int32_t odg_artifact_storage_withdraw(uint32_t actor_id,uint32_t artifact_id,
                                      uint32_t storage_slot,uint32_t quantity) {
    odg_actor *actor;
    odg_artifact *artifact;
    odg_item_stack removed;
    odg_inventory inventory_temp;
    odg_storage storage_temp;
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (quantity==0u || actor_id>=ODG_MAX_ACTORS || storage_slot>=ODG_CHEST_SLOTS) return ODG_STATUS_INVALID_ARGUMENT;
    actor=&g_odg.actors[actor_id];artifact=owned_storage_artifact(actor_id,artifact_id);
    if (artifact==NULL || artifact->local_resident==0u || odg_dist2(actor->x,actor->z,artifact->x,artifact->z)>
        (int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX) return ODG_STATUS_INVALID_STATE;
    inventory_temp=actor->inventory;storage_temp=artifact->storage;
    if (!odg_slots_remove(storage_temp.slots,ODG_CHEST_SLOTS,storage_slot,quantity,&removed)) return ODG_STATUS_INVALID_ARGUMENT;
    if (!odg_inventory_add(&inventory_temp,&removed)) return ODG_STATUS_INVALID_STATE;
    actor->inventory=inventory_temp;artifact->storage=storage_temp;
    return ODG_STATUS_OK;
}


int odg_artifact_is_death_cache(const odg_artifact *artifact){
    return artifact!=NULL && artifact->active && (artifact->state&ODG_ARTIFACT_STATE_DEATH_CACHE)!=0u;
}

int odg_artifact_create_death_cache(uint32_t actor_id){
    odg_actor *actor;odg_artifact *cache;const odg_item_definition *equipment_def;
    uint64_t instance_id;uint32_t id,slot,capacity,equipment_type;odg_storage cargo;odg_inventory survivors;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];equipment_type=actor->inventory.equipped_backpack_type;
    if(equipment_type==ODG_ITEM_NONE||!odg_item_inventory_expander_recovery_internal(equipment_type))return 0;
    equipment_def=odg_item_definition_internal(equipment_type);
    if(equipment_def==NULL||(artifact_caps(equipment_type)&ODG_ARTIFACT_CAP_STORE)==0u)return 0;

    /* Death splitting is transactional. Recovery cargo and protected survivors are built
     * in temporary destinations first. A protected item living in an expanded slot must
     * be compacted into the base inventory; merely shrinking slot_count would make that
     * item invisible until another expander was equipped. If either destination cannot
     * hold everything, preserve the original inventory and let the caller use fallback. */
    odg_memset(&cargo,0,sizeof(cargo));odg_inventory_init(&survivors);
    capacity=odg_inventory_capacity(&actor->inventory);
    for(slot=0u;slot<capacity;++slot){
        const odg_item_stack *stack=&actor->inventory.slots[slot];
        if(odg_item_stack_empty_internal(stack))continue;
        if(odg_item_stack_protected_internal(stack)){
            if(!odg_inventory_add(&survivors,stack))return 0;
        }else if(!odg_slots_add(cargo.slots,ODG_CHEST_SLOTS,stack))return 0;
    }

    instance_id=odg_next_instance_id();if(instance_id==0u)return 0;
    id=alloc_artifact_system_slot();if(id==UINT32_MAX)return 0;
    cache=&g_odg_artifacts[id];odg_memset(cache,0,sizeof(*cache));cache->active=1u;cache->id=id;
    cache->instance_id=instance_id;cache->item_type=equipment_type;cache->owner_actor_id=actor_id;
    cache->material_tier=equipment_def->default_material_tier;cache->capability_bits=artifact_caps(equipment_type);
    cache->state=ODG_ARTIFACT_STATE_DEATH_CACHE;cache->x=actor->x;cache->z=actor->z;cache->local_resident=1u;
    cache->storage=cargo;cache->aux_tick=g_odg.tick+ODG_DEATH_CACHE_LIFETIME_TICKS;
    odg_local_fx_to_global_fx_internal(cache->x,cache->z,&cache->global_fx_x,&cache->global_fx_z);

    actor->inventory=survivors;
    odg_entities_spatial_mark_dirty();
    return 1;
}

int odg_artifact_recover_death_cache(uint32_t actor_id,uint32_t artifact_id){
    odg_actor *actor;odg_artifact *cache;odg_inventory temp;uint32_t i;
    if (actor_id >= ODG_MAX_ACTORS || artifact_id >= g_odg.artifact_count) return 0;
    actor = &g_odg.actors[actor_id];
    cache = &g_odg_artifacts[artifact_id];
    if(!odg_artifact_is_death_cache(cache)||!odg_artifact_actor_can_access_internal(actor_id,cache))return 0;
    if(odg_dist2(actor->x,actor->z,cache->x,cache->z)>(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX)return 0;
    if(!odg_item_inventory_expander_recovery_internal(cache->item_type))return 0;
    temp=actor->inventory;
    if(temp.equipped_backpack_type==ODG_ITEM_NONE){
        if(!odg_inventory_equip_expander_type_internal(&temp,cache->item_type))return 0;
    }else{
        odg_item_stack recovered_container;
        odg_memset(&recovered_container,0,sizeof(recovered_container));recovered_container.type_id=cache->item_type;
        recovered_container.quantity=1u;recovered_container.material_tier=cache->material_tier;recovered_container.instance_id=cache->instance_id;
        if(!odg_item_stack_normalize_internal(&recovered_container)||!odg_inventory_add(&temp,&recovered_container))return 0;
    }
    for(i=0u;i<ODG_CHEST_SLOTS;++i)if(!odg_item_stack_empty_internal(&cache->storage.slots[i])&&!odg_inventory_add(&temp,&cache->storage.slots[i]))return 0;
    actor->inventory=temp;odg_memset(cache,0,sizeof(*cache));cache->id=artifact_id;
    if(g_odg.opened_artifact_id==artifact_id)g_odg.opened_artifact_id=UINT32_MAX;
    odg_entities_spatial_mark_dirty();odg_emit_particles(actor->x,actor->z,0x8ce8ffffu,12u);return 1;
}
