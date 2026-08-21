#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

static int failures=0;
#define CHECK(expr) do{if(!(expr)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);++failures;}}while(0)

static void check_seed(uint64_t seed){
    uint32_t actor,i,j,chips=0u;
    CHECK(odg_init(seed,320u,180u)==ODG_STATUS_OK);
    if(!g_odg.initialized)return;

    for(actor=0u;actor<ODG_MAX_ACTORS;++actor){
        const odg_actor *a=&g_odg.actors[actor];
        int64_t gx,gz;odg_surface_sample surface;uint64_t required=0u;uint32_t trees=0u,stone=0u,workbenches=0u;
        CHECK(a->active!=0u&&a->hp==a->max_hp&&a->max_hp!=0u&&a->local_resident!=0u);
        odg_global_fx_to_global_cell_internal(a->global_fx_x,a->global_fx_z,&gx,&gz);
        CHECK(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)==ODG_STATUS_OK);
        CHECK((surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u);
        CHECK(odg_chunk_owner_at_global_cell(a->home_global_cell_x,a->home_global_cell_z)==ODG_OWNER_FROM_ID(actor));
        for(i=0u;i<g_odg.resource_count;++i){
            const odg_resource_node *r=&g_odg_resources[i];
            if(!r->active||r->bootstrap_actor_id!=actor)continue;
            if(r->kind==ODG_RESOURCE_TREE)++trees;
            else if(r->kind==ODG_RESOURCE_STONE)++stone;
        }
        for(i=0u;i<g_odg.artifact_count;++i){
            const odg_artifact *artifact=&g_odg_artifacts[i];
            if(!artifact->active||artifact->owner_actor_id!=actor||artifact->item_type!=ODG_ITEM_WORKBENCH)continue;
            ++workbenches;CHECK((artifact->state&ODG_ARTIFACT_STATE_PROTECTED)!=0u);
            CHECK(odg_artifact_surface_allows_item_internal(artifact->item_type,artifact->x,artifact->z)!=0);
        }
        CHECK(trees>=3u);CHECK(stone>=2u);CHECK(workbenches==1u);
    }

    /* Bootstrap bodies may relax the aesthetic 17 m spacing only on an exceptional
     * deterministic fallback, but physical colliders must never overlap. */
    for(i=0u;i<ODG_MAX_ACTORS;++i)for(j=i+1u;j<ODG_MAX_ACTORS;++j){
        const odg_actor *a=&g_odg.actors[i],*b=&g_odg.actors[j];int32_t sum=a->radius+b->radius;
        CHECK(odg_dist2(a->x,a->z,b->x,b->z)>=(int64_t)sum*sum);
    }

    for(i=0u;i<g_odg.resource_count;++i){
        const odg_resource_node *r=&g_odg_resources[i];int64_t gx,gz;odg_surface_sample surface;uint64_t required=0u;
        if(!r->active)continue;
        odg_global_fx_to_global_cell_internal(r->global_fx_x,r->global_fx_z,&gx,&gz);
        CHECK(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)==ODG_STATUS_OK);
        CHECK((surface.flags&ODG_SURFACE_FLAG_WATER)==0u);
        if(r->kind!=ODG_RESOURCE_IRON&&r->kind!=ODG_RESOURCE_COAL)CHECK((surface.flags&ODG_SURFACE_FLAG_STEEP)==0u);
    }

    for(i=0u;i<g_odg.pickup_count;++i){
        const odg_world_pickup *p=&g_odg_pickups[i];int64_t gx,gz;odg_surface_sample surface;uint64_t required=0u;
        if(!p->active||p->stack.type_id!=ODG_ITEM_REPROGRAM_CHIP)continue;
        ++chips;odg_global_fx_to_global_cell_internal(p->global_fx_x,p->global_fx_z,&gx,&gz);
        CHECK(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)==ODG_STATUS_OK);
        CHECK((surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u);
    }
    CHECK(chips==4u);

    /* Natural turrets are reserved before bootstrap entities exist. Materialization must
     * therefore never create two physical solids in the same place, regardless of whether
     * the other object is an actor, guaranteed resource, protected station or salvage chip. */
    for(i=0u;i<g_odg.turret_count;++i){
        const odg_turret *t=&g_odg_turrets[i];
        if(!t->active||t->procedural==0u||t->local_resident==0u)continue;
        CHECK(odg_world_position_safe_ground_internal(t->x,t->z)!=0);
        for(j=0u;j<ODG_MAX_ACTORS;++j){
            const odg_actor *a=&g_odg.actors[j];int32_t sum;
            if(!a->active||a->local_resident==0u)continue;
            sum=ODG_TURRET_COLLISION_RADIUS_FX+a->radius;
            CHECK(odg_dist2(t->x,t->z,a->x,a->z)>=(int64_t)sum*sum);
        }
        for(j=0u;j<g_odg.resource_count;++j){
            const odg_resource_node *r=&g_odg_resources[j];int32_t sum;
            if(!r->active||r->local_resident==0u)continue;
            sum=ODG_TURRET_COLLISION_RADIUS_FX+odg_resource_collision_radius_fx_internal(r);
            CHECK(odg_dist2(t->x,t->z,r->x,r->z)>=(int64_t)sum*sum);
        }
        for(j=0u;j<g_odg.artifact_count;++j){
            const odg_artifact *a=&g_odg_artifacts[j];int32_t sum;
            if(!a->active||a->local_resident==0u)continue;
            sum=ODG_TURRET_COLLISION_RADIUS_FX+odg_artifact_collision_radius_fx_internal(a);
            CHECK(odg_dist2(t->x,t->z,a->x,a->z)>=(int64_t)sum*sum);
        }
        for(j=0u;j<g_odg.pickup_count;++j){
            const odg_world_pickup *p=&g_odg_pickups[j];int32_t sum=ODG_TURRET_COLLISION_RADIUS_FX+ODG_FX_ONE/2;
            if(!p->active||p->local_resident==0u)continue;
            CHECK(odg_dist2(t->x,t->z,p->x,p->z)>=(int64_t)sum*sum);
        }
    }
}

int main(void){
    static const uint64_t seeds[]={UINT64_C(2),UINT64_C(3),UINT64_C(17),UINT64_C(19),UINT64_C(31),UINT64_C(41)};
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(seeds)/sizeof(seeds[0]));++i)check_seed(seeds[i]);
    if(failures!=0){fprintf(stderr,"bootstrap: %d failure(s)\n",failures);return 2;}
    printf("BOOTSTRAP OK seeds=2,3,17,19,31,41 actors=safe+nonoverlap resources=3wood+2stone+surface workbench=1+safe chips=4+safe turrets=safe+reserved api=%u save=%u\n",
           odg_api_version(),odg_save_schema_version());
    return 0;
}
