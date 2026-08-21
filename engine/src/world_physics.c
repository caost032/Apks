#include "game_internal.h"

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Shared world-space physical authority
 * -------------------------------------------------------------------------
 * Terrain height, obstacle overlap, ground occupancy and vertical airspace all live in
 * one module. Simulation, fauna, construction, artifacts and camera probes consume the
 * same queries instead of maintaining subtly different geometry rules.
 */

int odg_world_cell_safe_ground_internal(int64_t gx,int64_t gz){
    odg_surface_sample surface;uint64_t required=0u;
    if(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)!=ODG_STATUS_OK)return 0;
    return (surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u;
}

int odg_world_position_safe_ground_internal(int32_t x,int32_t z){
    odg_surface_sample surface;
    if(!odg_environment_surface_local(x,z,&surface))return 0;
    return (surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u;
}

int32_t odg_terrain_height_fx(int32_t x,int32_t z){
    /* Floating-render-origin coordinates are converted back to the global fixed-point
     * world before sampling. Integer global-cell heights come from chunks.c and are
     * bilinearly interpolated here, so terrain remains continuous across both cell and
     * chunk boundaries and after floating-origin recentering. */
    int64_t global_fx_x=odg_global_center_cell_x_internal()*(int64_t)ODG_FX_ONE+(int64_t)x;
    int64_t global_fx_z=odg_global_center_cell_z_internal()*(int64_t)ODG_FX_ONE+(int64_t)z;
    int64_t gx=odg_floor_div_i64_internal(global_fx_x,(int64_t)ODG_FX_ONE);
    int64_t gz=odg_floor_div_i64_internal(global_fx_z,(int64_t)ODG_FX_ONE);
    int64_t ox=global_fx_x-gx*(int64_t)ODG_FX_ONE;
    int64_t oz=global_fx_z-gz*(int64_t)ODG_FX_ONE;
    int32_t hm00=420,hm10=420,hm01=420,hm11=420;
    int64_t h0,h1,h;
    (void)odg_world_height_milli64(gx,gz,&hm00);
    (void)odg_world_height_milli64(gx+1,gz,&hm10);
    (void)odg_world_height_milli64(gx,gz+1,&hm01);
    (void)odg_world_height_milli64(gx+1,gz+1,&hm11);
    h0=(int64_t)hm00+(((int64_t)hm10-hm00)*ox)/(int64_t)ODG_FX_ONE;
    h1=(int64_t)hm01+(((int64_t)hm11-hm01)*ox)/(int64_t)ODG_FX_ONE;
    h=h0+((h1-h0)*oz)/(int64_t)ODG_FX_ONE;
    return (int32_t)((h*(int64_t)ODG_FX_ONE)/INT64_C(1000));
}

int odg_world_circle_aabb_overlap_internal(int32_t x,int32_t z,int32_t radius,const odg_obstacle *obstacle){
    int32_t cx,cz;int64_t dx,dz;
    if(obstacle==NULL||radius<=0)return 0;
    cx=odg_clamp_i32(x,obstacle->x-obstacle->hx,obstacle->x+obstacle->hx);
    cz=odg_clamp_i32(z,obstacle->z-obstacle->hz,obstacle->z+obstacle->hz);
    dx=(int64_t)x-cx;dz=(int64_t)z-cz;
    return dx*dx+dz*dz<(int64_t)radius*radius;
}

typedef struct {
    int64_t global_x,global_z,limit;
} odg_turret_collision_query;

static int turret_collision_visit(uint32_t id,void *context){
    odg_turret_collision_query *q=(odg_turret_collision_query *)context;
    const odg_turret *turret;int64_t dx,dz;
    if(q==NULL||id>=g_odg.turret_count)return 0;
    turret=&g_odg_turrets[id];
    if(!turret->active||turret->carried_by!=ODG_TURRET_NONE)return 0;
    dx=turret->global_fx_x-q->global_x;dz=turret->global_fx_z-q->global_z;
    return dx*dx+dz*dz<q->limit;
}

static int turret_position_blocked(int32_t x,int32_t z,int32_t radius){
    odg_turret_collision_query q;int32_t combined=radius+ODG_TURRET_COLLISION_RADIUS_FX;
    odg_local_fx_to_global_fx_internal(x,z,&q.global_x,&q.global_z);
    q.limit=(int64_t)combined*combined;
    return odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_TURRET,q.global_x,q.global_z,
                                                   combined,turret_collision_visit,&q);
}

static int position_clear_ignoring_internal(int32_t x,int32_t z,int32_t radius,
                                            uint32_t ignore_resource_id,uint32_t ignore_artifact_id){
    uint32_t i;
    /* Open Domain has no finite local-map wall. Every local coordinate maps back to a
     * valid global world location, so occupancy is purely physical rather than bounded
     * by the resident cache window. */
    for(i=0u;i<g_odg.obstacle_count;++i){
        if(odg_world_circle_aabb_overlap_internal(x,z,radius,&g_odg.obstacles[i]))return 0;
    }
    if(odg_resource_position_blocked_ignoring_internal(x,z,radius,ignore_resource_id))return 0;
    if(odg_artifact_position_blocked(x,z,radius,ignore_artifact_id))return 0;
    if(odg_construction_position_blocked_internal(x,z,radius,UINT32_MAX))return 0;
    if(turret_position_blocked(x,z,radius))return 0;
    return 1;
}

int odg_position_clear_ignoring_resource_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_resource_id){
    return position_clear_ignoring_internal(x,z,radius,ignore_resource_id,UINT32_MAX);
}

int odg_position_clear_ignoring_artifact_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_artifact_id){
    return position_clear_ignoring_internal(x,z,radius,UINT32_MAX,ignore_artifact_id);
}

int odg_position_clear_internal(int32_t x,int32_t z,int32_t radius){
    return position_clear_ignoring_internal(x,z,radius,UINT32_MAX,UINT32_MAX);
}

int odg_actor_bodies_clear_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_actor_id){
    uint32_t i;
    if(radius<0)return 0;
    for(i=0u;i<ODG_MAX_ACTORS;++i){
        const odg_actor *actor=&g_odg.actors[i];int32_t combined;
        if(i==ignore_actor_id||!actor->active||actor->hp==0u||actor->local_resident==0u)continue;
        combined=radius+actor->radius;
        if(odg_dist2(x,z,actor->x,actor->z)<(int64_t)combined*combined)return 0;
    }
    return 1;
}

int odg_fauna_bodies_clear_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_fauna_id){
    uint32_t i;
    if(radius<0)return 0;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        const odg_fauna_entity *entity=&g_odg.fauna[i];
        const odg_fauna_species_definition *definition;uint64_t body;int32_t body_fx,combined;
        if(i==ignore_fauna_id||!entity->active||entity->local_resident==0u)continue;
        definition=odg_fauna_species_internal(entity->species_id);if(definition==NULL)continue;
        body=((uint64_t)definition->body_radius_milli*(uint64_t)ODG_FX_ONE)/UINT64_C(1000);
        if(body<(uint64_t)(ODG_FX_ONE/10))body=(uint64_t)(ODG_FX_ONE/10);
        body_fx=body>(uint64_t)INT32_MAX?INT32_MAX:(int32_t)body;
        /* Ground-footprint queries must not turn a bird cruising several metres above
         * the world into an invisible wall. During the last part of a landing it starts
         * occupying ground again; aquatic vertical offsets are deliberately unaffected. */
        if((definition->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)!=0u&&
           entity->y_offset_fx>body_fx+ODG_FX_ONE/4)continue;
        if(radius>INT32_MAX-body_fx)return 0;
        combined=radius+body_fx;
        if(odg_dist2(x,z,entity->x,entity->z)<(int64_t)combined*combined)return 0;
    }
    return 1;
}

int odg_dynamic_position_clear_internal(int32_t x,int32_t z,int32_t radius,
                                        uint32_t ignore_actor_id,uint32_t ignore_fauna_id){
    return odg_actor_bodies_clear_internal(x,z,radius,ignore_actor_id)&&
           odg_fauna_bodies_clear_internal(x,z,radius,ignore_fauna_id);
}

typedef struct {
    int64_t global_x,global_z;
    int32_t radius_fx;
    int32_t base_height_milli;
    uint32_t clearance_milli;
    uint32_t required_offset_milli;
} odg_airspace_query;

static void airspace_accumulate_top(odg_airspace_query *q,int32_t obstacle_x,int32_t obstacle_z,
                                    int32_t obstacle_radius_fx,uint32_t obstacle_height_milli,
                                    uint32_t support_water){
    odg_surface_sample surface;int64_t gx,gz,dx,dz;int32_t sum;int64_t top,required;
    if(q==NULL||obstacle_radius_fx<=0||obstacle_height_milli==0u)return;
    odg_local_fx_to_global_fx_internal(obstacle_x,obstacle_z,&gx,&gz);
    dx=gx-q->global_x;dz=gz-q->global_z;sum=q->radius_fx+obstacle_radius_fx;
    if(dx*dx+dz*dz>=(int64_t)sum*sum)return;
    if(!odg_environment_surface_local(obstacle_x,obstacle_z,&surface))return;
    top=(int64_t)surface.height_milli+(int64_t)obstacle_height_milli;
    if(support_water!=0u&&(surface.flags&ODG_SURFACE_FLAG_WATER)!=0u)
        top+=(int64_t)surface.water_depth_milli;
    required=top+(int64_t)q->clearance_milli-(int64_t)q->base_height_milli;
    if(required>0&&required>(int64_t)q->required_offset_milli)
        q->required_offset_milli=required>(int64_t)UINT32_MAX?UINT32_MAX:(uint32_t)required;
}

static int airspace_resource_visit(uint32_t id,void *context){
    odg_airspace_query *q=(odg_airspace_query *)context;const odg_resource_node *resource;
    if(q==NULL||id>=g_odg.resource_count)return 0;
    resource=&g_odg_resources[id];
    if(resource->active&&resource->local_resident)
        airspace_accumulate_top(q,resource->x,resource->z,
                                odg_resource_collision_radius_fx_internal(resource),
                                odg_resource_physical_height_milli_internal(resource),0u);
    return 0;
}

static int airspace_artifact_visit(uint32_t id,void *context){
    odg_airspace_query *q=(odg_airspace_query *)context;const odg_artifact *artifact;
    if(q==NULL||id>=g_odg.artifact_count)return 0;
    artifact=&g_odg_artifacts[id];
    if(artifact->active&&artifact->local_resident)
        airspace_accumulate_top(q,artifact->x,artifact->z,
                                odg_artifact_collision_radius_fx_internal(artifact),
                                odg_artifact_physical_height_milli_internal(artifact),
                                (artifact->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)!=0u?1u:0u);
    return 0;
}

static int airspace_construction_visit(uint32_t id,void *context){
    odg_airspace_query *q=(odg_airspace_query *)context;const odg_construction_block *block;
    if(q==NULL||id>=g_odg_construction_count)return 0;
    block=&g_odg_construction_blocks[id];
    if(block->active&&block->local_resident)
        airspace_accumulate_top(q,block->x,block->z,
                                odg_construction_airspace_radius_fx_internal(block),
                                odg_construction_physical_height_milli_internal(block),0u);
    return 0;
}

static int airspace_turret_visit(uint32_t id,void *context){
    odg_airspace_query *q=(odg_airspace_query *)context;const odg_turret *turret;
    if(q==NULL||id>=g_odg.turret_count)return 0;
    turret=&g_odg_turrets[id];
    if(turret->active&&turret->carried_by==ODG_TURRET_NONE)
        airspace_accumulate_top(q,turret->x,turret->z,ODG_TURRET_COLLISION_RADIUS_FX,1250u,0u);
    return 0;
}

uint32_t odg_airspace_required_offset_milli_internal(int32_t x,int32_t z,uint32_t radius_milli,
                                                      uint32_t clearance_milli){
    odg_airspace_query q;odg_surface_sample surface;uint32_t i;int32_t query_radius;
    if(!odg_environment_surface_local(x,z,&surface))return UINT32_MAX;
    odg_memset(&q,0,sizeof(q));
    odg_local_fx_to_global_fx_internal(x,z,&q.global_x,&q.global_z);
    q.radius_fx=(int32_t)(((uint64_t)radius_milli*ODG_FX_ONE)/1000u);
    if(q.radius_fx<ODG_FX_ONE/12)q.radius_fx=ODG_FX_ONE/12;
    q.base_height_milli=surface.height_milli;q.clearance_milli=clearance_milli;
    query_radius=q.radius_fx+2*ODG_FX_ONE;
    (void)odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_RESOURCE,q.global_x,q.global_z,
                                                  query_radius,airspace_resource_visit,&q);
    (void)odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_ARTIFACT,q.global_x,q.global_z,
                                                  query_radius,airspace_artifact_visit,&q);
    (void)odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_CONSTRUCTION,q.global_x,q.global_z,
                                                  query_radius,airspace_construction_visit,&q);
    (void)odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_TURRET,q.global_x,q.global_z,
                                                  query_radius,airspace_turret_visit,&q);
    for(i=0u;i<g_odg.obstacle_count;++i){
        const odg_obstacle *obstacle=&g_odg.obstacles[i];
        int32_t rx=q.radius_fx+obstacle->hx,rz=q.radius_fx+obstacle->hz;
        if(odg_abs_i32(x-obstacle->x)>rx||odg_abs_i32(z-obstacle->z)>rz)continue;
        airspace_accumulate_top(&q,obstacle->x,obstacle->z,
                                obstacle->hx>obstacle->hz?obstacle->hx:obstacle->hz,
                                (uint32_t)(((uint64_t)(obstacle->height_fx>0?obstacle->height_fx:0)*1000u)/
                                           (uint32_t)ODG_FX_ONE),0u);
    }
    return q.required_offset_milli;
}
