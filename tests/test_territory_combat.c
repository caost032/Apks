#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

static int failures=0;
#define CHECK(expr) do{if(!(expr)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);++failures;}}while(0)

static void place_actor_cell(odg_actor *actor,uint32_t cell){
    int64_t gx,gz;
    actor->x=odg_cell_center_x(cell);actor->z=odg_cell_center_z(cell);
    actor->vx=0;actor->vz=0;actor->vertical_offset_fx=0;actor->vertical_velocity_fx=0;
    actor->last_cell=cell;actor->local_resident=1u;
    gx=g_odg.world_origin_cell_x+(int64_t)(cell&(ODG_GRID_SIZE-1u));
    gz=g_odg.world_origin_cell_z+(int64_t)(cell>>ODG_GRID_SHIFT);
    actor->last_global_cell_x=gx;actor->last_global_cell_z=gz;
    odg_local_fx_to_global_fx_internal(actor->x,actor->z,&actor->global_fx_x,&actor->global_fx_z);
}

static void set_territory_owner(uint32_t cell,uint8_t owner){
    int64_t gx,gz;
    if(cell>=ODG_CELL_COUNT)return;
    gx=g_odg.world_origin_cell_x+(int64_t)(cell&(ODG_GRID_SIZE-1u));
    gz=g_odg.world_origin_cell_z+(int64_t)(cell>>ODG_GRID_SHIFT);
    odg_chunk_set_owner_at_global_cell(gx,gz,owner);
}

static void set_trail_owner(uint32_t cell,uint8_t owner){
    int64_t gx=g_odg.world_origin_cell_x+(int64_t)(cell&(ODG_GRID_SIZE-1u));
    int64_t gz=g_odg.world_origin_cell_z+(int64_t)(cell>>ODG_GRID_SHIFT);
    odg_chunk_set_trail_at_global_cell(gx,gz,owner);
}

static uint32_t neighbor_cell(uint32_t cell){
    uint32_t x=cell&(ODG_GRID_SIZE-1u),z=cell>>ODG_GRID_SHIFT;
    if(x+1u<ODG_GRID_SIZE)return cell+1u;
    if(x>0u)return cell-1u;
    if(z+1u<ODG_GRID_SIZE)return cell+ODG_GRID_SIZE;
    return cell-ODG_GRID_SIZE;
}

static void isolate_two_actors(void){
    uint32_t i;
    for(i=2u;i<ODG_MAX_ACTORS;++i){g_odg.actors[i].active=0u;g_odg.actors[i].hp=0u;}
    g_odg.actors[0].active=1u;g_odg.actors[0].hp=g_odg.actors[0].max_hp;
    g_odg.actors[1].active=1u;g_odg.actors[1].hp=g_odg.actors[1].max_hp;
    g_odg.actors[0].vx=g_odg.actors[0].vz=0;g_odg.actors[1].vx=g_odg.actors[1].vz=0;
}

static void clear_actor_territory(uint32_t actor_id){
    uint8_t owner=ODG_OWNER_FROM_ID(actor_id);uint32_t i;
    for(i=0u;i<g_odg.chunk_cache_used;++i){
        odg_chunk_runtime *record=&g_odg_chunk_cache[i];uint32_t ordinal;
        if(record->used==0u||record->territory_cells[actor_id]==0u)continue;
        for(ordinal=0u;ordinal<ODG_CHUNK_CELL_COUNT;++ordinal){
            int64_t gx,gz;
            if(odg_chunk_runtime_owner_at_ordinal_internal(record,ordinal)!=owner)continue;
            gx=record->chunk_x*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)(ordinal%(uint32_t)ODG_CHUNK_SIZE_CELLS);
            gz=record->chunk_z*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)(ordinal/(uint32_t)ODG_CHUNK_SIZE_CELLS);
            odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_NONE);
        }
    }
    g_odg.territory_count[actor_id]=0u;
}


static int find_far_unrepresentable_safe_cell(int64_t *out_gx,int64_t *out_gz){
    int64_t base_x=odg_global_center_cell_x_internal(),base_z=odg_global_center_cell_z_internal();uint32_t i;
    for(i=0u;i<256u;++i){
        int64_t gx=base_x+INT64_C(3000000)+(int64_t)i,gz=base_z+INT64_C(3000000);
        odg_surface_sample surface;uint64_t required=0u;int32_t lx,lz;
        if(odg_global_cell_center_to_local_fx_internal(gx,gz,&lx,&lz)!=0)continue;
        if(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)!=ODG_STATUS_OK)continue;
        if((surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)continue;
        *out_gx=gx;*out_gz=gz;return 1;
    }
    return 0;
}

static int find_local_water_cell(int64_t *out_gx,int64_t *out_gz){
    int64_t base_x=odg_global_center_cell_x_internal(),base_z=odg_global_center_cell_z_internal();int32_t oz,ox;
    for(oz=-60;oz<=60;++oz)for(ox=-60;ox<=60;++ox){
        int64_t gx=base_x+(int64_t)ox,gz=base_z+(int64_t)oz;odg_surface_sample surface;uint64_t required=0u;
        if(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)!=ODG_STATUS_OK)continue;
        if((surface.flags&ODG_SURFACE_FLAG_WATER)==0u)continue;
        *out_gx=gx;*out_gz=gz;return 1;
    }
    return 0;
}

int main(void){
    odg_actor *player,*bot;uint32_t cell,away;uint8_t player_owner=ODG_OWNER_FROM_ID(0u),bot_owner=ODG_OWNER_FROM_ID(1u);

    /* Regression for the historical identity/territory confusion: a bot standing on its
     * own ground must still cut the PLAYER'S exposed trail. Ground owner never becomes
     * actor identity, and cutting disrupts the trail without killing its owner. */
    CHECK(odg_init(UINT64_C(0x54524944454e5431),320u,180u)==ODG_STATUS_OK);isolate_two_actors();
    player=&g_odg.actors[0];bot=&g_odg.actors[1];cell=player->last_cell;away=neighbor_cell(cell);
    place_actor_cell(player,away);place_actor_cell(bot,cell);set_territory_owner(cell,bot_owner);
    CHECK(odg_chunk_owner_at_global_cell(g_odg.world_origin_cell_x+(int64_t)(cell&(ODG_GRID_SIZE-1u)),
                                         g_odg.world_origin_cell_z+(int64_t)(cell>>ODG_GRID_SHIFT))==bot_owner);
    odg_chunk_clear_trail_owner(player_owner);player->trail_active=1u;player->trail_len=1u;player->trail_broken=0u;set_trail_owner(cell,player_owner);
    odg_set_input(0,0,0,ODG_Q15_ONE,0u);odg_step_ticks(1u);
    CHECK(player->hp==player->max_hp);CHECK(player->trail_active==0u&&player->trail_len==0u&&player->trail_broken==1u);

    /* Symmetric regression: the player can cut a BOT trail while standing in player
     * territory. The cutter is the body actor, never the territorial owner lookup. */
    CHECK(odg_init(UINT64_C(0x54524944454e5432),320u,180u)==ODG_STATUS_OK);isolate_two_actors();
    player=&g_odg.actors[0];bot=&g_odg.actors[1];cell=player->last_cell;away=neighbor_cell(cell);
    place_actor_cell(player,cell);place_actor_cell(bot,away);set_territory_owner(cell,player_owner);
    odg_chunk_clear_trail_owner(bot_owner);bot->trail_active=1u;bot->trail_len=1u;bot->trail_broken=0u;set_trail_owner(cell,bot_owner);
    odg_set_input(0,0,0,ODG_Q15_ONE,0u);odg_step_ticks(1u);
    CHECK(bot->hp==bot->max_hp);CHECK(bot->trail_active==0u&&bot->trail_len==0u&&bot->trail_broken==1u);

    /* Self-contact remains harmless even on foreign ground: only a distinct cutter actor
     * can break a trail. This guards against reintroducing self-cross death semantics. */
    CHECK(odg_init(UINT64_C(0x54524944454e5433),320u,180u)==ODG_STATUS_OK);
    for(uint32_t i=1u;i<ODG_MAX_ACTORS;++i){g_odg.actors[i].active=0u;g_odg.actors[i].hp=0u;}
    player=&g_odg.actors[0];cell=player->last_cell;place_actor_cell(player,cell);set_territory_owner(cell,bot_owner);
    odg_chunk_clear_trail_owner(player_owner);player->trail_active=1u;player->trail_len=1u;player->trail_broken=0u;set_trail_owner(cell,player_owner);
    odg_set_input(0,0,0,ODG_Q15_ONE,0u);odg_step_ticks(1u);
    CHECK(player->hp==player->max_hp);CHECK(player->trail_active!=0u&&player->trail_broken==0u);

    /* Respawn safety must use physical ground authority, not "territory_count > 0" as
     * a proxy for land. Keep one owned WATER cell, make it the old home, then verify the
     * recovery path creates a neutral dry enclave and resurrects there instead of in water. */
    CHECK(odg_init(UINT64_C(0x45434f5359533231),320u,180u)==ODG_STATUS_OK);
    player=&g_odg.actors[0];
    {
        int64_t water_gx=0,water_gz=0,spawn_gx=0,spawn_gz=0;odg_surface_sample surface;uint64_t required=0u;uint32_t mapped=UINT32_MAX;
        CHECK(find_local_water_cell(&water_gx,&water_gz)!=0);clear_actor_territory(ODG_PLAYER_ID);
        odg_chunk_set_owner_at_global_cell(water_gx,water_gz,player_owner);g_odg.territory_count[ODG_PLAYER_ID]=1u;
        player->home_global_cell_x=water_gx;player->home_global_cell_z=water_gz;
        (void)odg_global_cell_to_local_internal(water_gx,water_gz,&mapped);player->home_cell=mapped;
        player->hp=0u;player->respawn_ticks=0u;player->active=1u;odg_set_input(0,0,0,ODG_Q15_ONE,0u);
        odg_step_ticks(1u);
        CHECK(player->hp==player->max_hp);
        odg_global_fx_to_global_cell_internal(player->global_fx_x,player->global_fx_z,&spawn_gx,&spawn_gz);
        CHECK(!(spawn_gx==water_gx&&spawn_gz==water_gz));
        CHECK(odg_world_surface_sample64(spawn_gx,spawn_gz,&surface,sizeof(surface),&required)==ODG_STATUS_OK);
        CHECK((surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u);
        CHECK(odg_chunk_owner_at_global_cell(spawn_gx,spawn_gz)==player_owner);
        CHECK(player->home_global_cell_x==spawn_gx&&player->home_global_cell_z==spawn_gz);
    }

    /* A safe owned cell that is too far from the current floating origin must never be
     * replaced by an unchecked centre spawn. The actor remains dead and retries later. */
    CHECK(odg_init(UINT64_C(0x464152525350574e),320u,180u)==ODG_STATUS_OK);
    player=&g_odg.actors[0];
    {
        int64_t far_gx=0,far_gz=0;
        CHECK(find_far_unrepresentable_safe_cell(&far_gx,&far_gz)!=0);clear_actor_territory(ODG_PLAYER_ID);
        odg_chunk_set_owner_at_global_cell(far_gx,far_gz,player_owner);g_odg.territory_count[ODG_PLAYER_ID]=1u;
        player->home_global_cell_x=far_gx;player->home_global_cell_z=far_gz;player->home_cell=UINT32_MAX;
        player->hp=0u;player->respawn_ticks=0u;player->active=1u;odg_set_input(0,0,0,ODG_Q15_ONE,0u);
        odg_step_ticks(1u);
        CHECK(player->hp==0u);CHECK(player->respawn_ticks==ODG_TICK_RATE);
        CHECK(player->home_global_cell_x==far_gx&&player->home_global_cell_z==far_gz);
    }

    /* Death is a hard locomotion boundary. A jump/swim/dash/slide cannot leak through
     * the three-second dead state and make the actor respawn airborne or propelled.
     * REQUEST_RESPAWN is ABI-visible: it never skips the timer, but once ready it uses
     * exactly the same safe-ground authority as automatic respawn. */
    CHECK(odg_init(UINT64_C(0x5245535041574e51),320u,180u)==ODG_STATUS_OK);
    player=&g_odg.actors[ODG_PLAYER_ID];
    {
        odg_command command;
        player->vertical_offset_fx=2*ODG_FX_ONE;player->vertical_velocity_fx=ODG_FX_ONE/5;player->grounded=0u;
        player->dash_ticks=7u;player->dash_cd=31u;player->slide_lock_ticks=9u;player->slide_axis=1u;
        player->speed_fx=120;player->steer_q15=9000;player->ai_commit_ticks=22u;player->ai_plan_cell=10u;
        odg_actor_apply_damage_internal(ODG_PLAYER_ID,1u,player->hp,ODG_DEATH_COMBAT);
        CHECK(player->hp==0u&&player->respawn_ticks==3u*ODG_TICK_RATE);
        CHECK(player->vertical_offset_fx==0&&player->vertical_velocity_fx==0&&player->grounded==1u);
        CHECK(player->dash_ticks==0u&&player->dash_cd==0u&&player->slide_lock_ticks==0u&&player->speed_fx==0);
        CHECK(player->ai_commit_ticks==0u&&player->ai_plan_cell==UINT32_MAX);
        odg_memset(&command,0,sizeof(command));command.struct_size=sizeof(command);command.type=ODG_COMMAND_REQUEST_RESPAWN;
        player->respawn_ticks=1u;CHECK(odg_command_submit(&command,sizeof(command))==ODG_STATUS_OK);odg_process_commands();
        CHECK(player->hp==0u&&player->respawn_ticks==1u);
        player->respawn_ticks=0u;CHECK(odg_command_submit(&command,sizeof(command))==ODG_STATUS_OK);odg_process_commands();
        CHECK(player->hp==player->max_hp&&player->death_reason==ODG_DEATH_NONE);
        CHECK(player->vertical_offset_fx==0&&player->vertical_velocity_fx==0&&player->grounded==1u);
        CHECK(player->dash_ticks==0u&&player->slide_lock_ticks==0u&&player->speed_fx==0);
    }

    if(failures!=0){fprintf(stderr,"territory combat: %d failure(s)\n",failures);return 2;}
    printf("TERRITORY COMBAT OK identity=actor trail-vs-ground=symmetric self-trail=nonlethal respawn=safe-ground+fail-closed+transient-reset api=%u save=%u\n",
           odg_api_version(),odg_save_schema_version());
    return 0;
}
