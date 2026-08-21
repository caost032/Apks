#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

static int failures=0;
#define CHECK(expr) do { if(!(expr)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);++failures;} } while(0)

static uint32_t first_active_fauna(void){
    uint32_t i;for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i)if(g_odg.fauna[i].active)return i;return UINT32_MAX;
}
static odg_resource_node *find_resource_by_stable_id(uint64_t stable_id){
    uint32_t i;for(i=0u;i<g_odg.resource_count;++i)if(g_odg_resources[i].active&&g_odg_resources[i].stable_id==stable_id)return &g_odg_resources[i];return NULL;
}

int main(void){
    odg_actor *p;
    odg_world_pickup *pickup;
    odg_resource_node *flora;
    uint32_t fauna_id;
    int64_t pickup_gx,pickup_gz,flora_gx,flora_gz,fauna_gx,fauna_gz;
    uint64_t flora_stable_id;
    uint32_t pass;

    CHECK(odg_init(UINT64_C(0x434f4e54494e5555),320u,180u)==ODG_STATUS_OK);
    p=&g_odg.actors[ODG_PLAYER_ID];
    CHECK(p->active!=0u && p->local_resident!=0u);

    /* Put representative persistent entities close to the player. Their global position is
     * authoritative; recentering may only rebuild the 32-bit local cache. */
    {
        odg_item_stack wood;
        odg_memset(&wood,0,sizeof(wood));wood.type_id=ODG_ITEM_WOOD;wood.quantity=2u;
        wood.material_tier=ODG_MATERIAL_WOOD;wood.flags=ODG_ITEM_FLAG_RESOURCE;
        CHECK(odg_spawn_world_pickup(&wood,p->x+ODG_FX_ONE,p->z,ODG_MANUAL_DROP_REPICKUP_TICKS)!=0);
    }
    pickup=&g_odg_pickups[g_odg.pickup_count-1u];
    CHECK(odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_STAGE_MATURE,0u,
                                   p->x+3*ODG_FX_ONE,p->z+ODG_FX_ONE,UINT32_MAX)!=0);
    flora=&g_odg_resources[g_odg.resource_count-1u];
    fauna_id=first_active_fauna();CHECK(fauna_id!=UINT32_MAX);
    pickup_gx=pickup->global_fx_x;pickup_gz=pickup->global_fx_z;
    flora_gx=flora->global_fx_x;flora_gz=flora->global_fx_z;flora_stable_id=flora->stable_id;
    fauna_gx=g_odg.fauna[fauna_id].global_fx_x;fauna_gz=g_odg.fauna[fauna_id].global_fx_z;

    for(pass=0u;pass<48u;++pass){
        int64_t cell_x_before,cell_z_before,cell_x_after,cell_z_after;
        odg_surface_sample before,after;
        /* Simulate legitimate travel: local movement is committed to the actor's global
         * coordinate before the floating-origin maintenance step. */
        p->x=ODG_FLOATING_ORIGIN_TRIGGER_FX+ODG_FX_ONE;
        odg_local_fx_to_global_fx_internal(p->x,p->z,&p->global_fx_x,&p->global_fx_z);
        odg_local_fx_to_global_cell_internal(p->x,p->z,&cell_x_before,&cell_z_before);
        CHECK(odg_environment_surface_local(p->x,p->z,&before)!=0);
        odg_chunks_maybe_recenter();
        CHECK(p->active!=0u && p->local_resident!=0u);
        odg_local_fx_to_global_cell_internal(p->x,p->z,&cell_x_after,&cell_z_after);
        CHECK(cell_x_after==cell_x_before && cell_z_after==cell_z_before);
        CHECK(odg_environment_surface_local(p->x,p->z,&after)!=0);
        CHECK(after.height_milli==before.height_milli);
        CHECK(after.biome==before.biome && after.flags==before.flags);
        CHECK(after.normal_x_q15==before.normal_x_q15 && after.normal_y_q15==before.normal_y_q15 &&
              after.normal_z_q15==before.normal_z_q15);
        CHECK(after.water_depth_milli==before.water_depth_milli);
        CHECK(p->vertical_offset_fx>=0);
        /* The local bot graph is derived data and must be rebuilt for the new global
         * window. Sample every rebase so stale-window bugs fail at the exact transition;
         * the exhaustive comparison after all 48 rebases still proves every stored edge. */
        {
            uint32_t sample;
            for(sample=0u;sample<96u;++sample){
                uint32_t nav_cell=(pass*UINT32_C(977)+sample*UINT32_C(251))%ODG_CELL_COUNT;
                CHECK(g_odg.bot_nav_edges[nav_cell]==odg_bot_navigation_edge_mask_internal(nav_cell));
            }
        }
    }

    {
        uint32_t nav_cell;
        for(nav_cell=0u;nav_cell<ODG_CELL_COUNT;++nav_cell)
            CHECK(g_odg.bot_nav_edges[nav_cell]==odg_bot_navigation_edge_mask_internal(nav_cell));
    }

    pickup=&g_odg_pickups[g_odg.pickup_count-1u];
    flora=find_resource_by_stable_id(flora_stable_id);
    CHECK(pickup->active!=0u && pickup->global_fx_x==pickup_gx && pickup->global_fx_z==pickup_gz);
    CHECK(flora!=NULL);
    if(flora!=NULL)CHECK(flora->active!=0u && flora->global_fx_x==flora_gx && flora->global_fx_z==flora_gz);
    CHECK(g_odg.fauna[fauna_id].active!=0u && g_odg.fauna[fauna_id].global_fx_x==fauna_gx &&
          g_odg.fauna[fauna_id].global_fx_z==fauna_gz);
    /* After travelling hundreds of cells these old entities may intentionally sleep; they
     * must never be deleted or teleported just because their local cache is unavailable. */
    CHECK(g_odg.chunk_recenter_count>=48u);

    if(failures!=0){fprintf(stderr,"world-continuity: %d failure(s)\n",failures);return 1;}
    printf("WORLD CONTINUITY OK recenters=%u origin=(%lld,%lld)\n",g_odg.chunk_recenter_count,
           (long long)g_odg.world_origin_cell_x,(long long)g_odg.world_origin_cell_z);
    return 0;
}
