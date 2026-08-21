#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

static int failures=0;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);++failures;} } while(0)

static odg_artifact *owned_artifact(uint32_t actor_id,uint32_t item_type) {
    uint32_t i;
    for(i=0u;i<g_odg.artifact_count;++i){
        odg_artifact *a=&g_odg_artifacts[i];
        if(a->active&&a->owner_actor_id==actor_id&&a->item_type==item_type)return a;
    }
    return NULL;
}

static void move_actor_to_artifact(odg_actor *actor,odg_artifact *artifact) {
    CHECK(actor!=NULL);CHECK(artifact!=NULL);
    if(actor==NULL||artifact==NULL)return;
    actor->x=artifact->x;actor->z=artifact->z;
    odg_local_fx_to_global_fx_internal(actor->x,actor->z,&actor->global_fx_x,&actor->global_fx_z);
}

static void add_stack(odg_actor *actor,uint32_t type,uint32_t tier,uint32_t quantity) {
    odg_item_stack s;
    odg_memset(&s,0,sizeof(s));s.type_id=type;s.material_tier=tier;s.quantity=quantity;
    {const odg_item_definition *d=odg_item_definition_internal(type);if(d!=NULL)s.flags=d->flags;}
    if(type==ODG_ITEM_AXE||type==ODG_ITEM_PICKAXE){s.instance_id=odg_next_instance_id();s.max_durability=odg_item_max_durability_internal(type,tier);s.durability=s.max_durability;}
    CHECK(odg_inventory_add(&actor->inventory,&s));
}

static void reset_actor(odg_actor *actor) {
    odg_inventory_init(&actor->inventory);
    actor->hp=actor->max_hp;actor->satiety_permille=1000u;actor->hydration_permille=1000u;
    actor->trail_active=0u;actor->trail_broken=0u;
    actor->bot_economy_item_type=0u;actor->bot_economy_target_id=UINT32_MAX;
}

static uint32_t active_raft_count(void) {
    uint32_t i,count=0u;
    for(i=0u;i<g_odg.artifact_count;++i)
        if(g_odg_artifacts[i].active&&g_odg_artifacts[i].item_type==ODG_ITEM_RAFT)++count;
    return count;
}

static int dry_surface(int32_t x,int32_t z) {
    odg_surface_sample sample;
    return odg_environment_surface_local(x,z,&sample)!=0&&
           (sample.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u;
}

static void own_launch_fan(uint32_t actor_id,int32_t sx,int32_t sz,int32_t dir_x,int32_t dir_z) {
    int32_t forward,lateral;uint8_t owner=ODG_OWNER_FROM_ID(actor_id);
    for(forward=0;forward<=5;++forward){
        for(lateral=-3;lateral<=3;++lateral){
            int32_t rx=-dir_z,rz=dir_x;
            int32_t x=sx+dir_x*forward*ODG_FX_ONE+rx*lateral*ODG_FX_ONE;
            int32_t z=sz+dir_z*forward*ODG_FX_ONE+rz*lateral*ODG_FX_ONE;
            int64_t gx,gz;odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
            odg_chunk_set_owner_at_global_cell(gx,gz,owner);
        }
    }
}

static int find_raft_corridor(odg_actor *bot,int32_t *out_sx,int32_t *out_sz,int32_t *out_tx,int32_t *out_tz) {
    static const int32_t dirs[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    int32_t mx,mz,dist;uint32_t di;
    if(bot==NULL||out_sx==NULL||out_sz==NULL||out_tx==NULL||out_tz==NULL)return 0;
    /* Search by the same route predicate used by the production planner.  The previous
     * regression assumed a very specific 2 m / 4 m shoreline shape, which made the test
     * depend on incidental worldgen morphology instead of the actual logistics contract. */
    for(mz=-60;mz<=60;++mz){
        for(mx=-60;mx<=60;++mx){
            int32_t sx=mx*ODG_FX_ONE,sz=mz*ODG_FX_ONE;
            if(!dry_surface(sx,sz)||!odg_position_clear_internal(sx,sz,bot->radius))continue;
            bot->x=sx;bot->z=sz;bot->vertical_offset_fx=0;bot->vertical_velocity_fx=0;bot->local_resident=1u;
            odg_local_fx_to_global_fx_internal(sx,sz,&bot->global_fx_x,&bot->global_fx_z);
            for(di=0u;di<4u;++di){
                int32_t dx=dirs[di][0],dz=dirs[di][1];
                bot->face_x_q15=dx*ODG_Q15_ONE;bot->face_z_q15=dz*ODG_Q15_ONE;
                for(dist=8;dist<=32;dist+=2){
                    int32_t tx=sx+dx*dist*ODG_FX_ONE,tz=sz+dz*dist*ODG_FX_ONE,px,pz;
                    /* The target may itself be water: this regression certifies the
                     * production/deployment/boarding transition. Cross-bank dismount and
                     * bank selection are covered by the vehicle world-system tests. */
                    if(!odg_bot_route_requires_raft_internal(bot,tx,tz))continue;
                    own_launch_fan(bot->id,sx,sz,dx,dz);
                    if(!odg_artifact_placement_candidate_for_item_internal(bot,ODG_ITEM_RAFT,&px,&pz))continue;
                    *out_sx=sx;*out_sz=sz;*out_tx=tx;*out_tz=tz;return 1;
                }
            }
        }
    }
    return 0;
}

int main(void) {
    odg_actor *bot;
    odg_artifact *bench,*smithy;
    int32_t dx=0,dz=0;
    uint32_t i;

    CHECK(odg_init(UINT64_C(0xB07EC0A1),320u,180u)==ODG_STATUS_OK);
    bot=&g_odg.actors[1];bench=owned_artifact(1u,ODG_ITEM_WORKBENCH);
    CHECK(bench!=NULL);

    /* An ignored organic pickup cannot shadow a useful logistics pickup. This used to
     * stall a hungry/economic bot when a seed happened to be a few pixels closer. */
    reset_actor(bot);move_actor_to_artifact(bot,bench);
    {
        odg_item_stack seed,wood;uint32_t j,seed_id=UINT32_MAX,wood_id=UINT32_MAX;
        odg_memset(&seed,0,sizeof(seed));seed.type_id=ODG_ITEM_APPLE_SEED;seed.quantity=1u;
        odg_memset(&wood,0,sizeof(wood));wood.type_id=ODG_ITEM_WOOD;wood.quantity=1u;wood.material_tier=ODG_MATERIAL_WOOD;
        {const odg_item_definition *d=odg_item_definition_internal(seed.type_id);if(d!=NULL)seed.flags=d->flags;}
        {const odg_item_definition *d=odg_item_definition_internal(wood.type_id);if(d!=NULL)wood.flags=d->flags;}
        CHECK(odg_spawn_world_pickup(&seed,bot->x,bot->z,0u)!=0);
        CHECK(odg_spawn_world_pickup(&wood,bot->x+ODG_FX_ONE/4,bot->z,0u)!=0);
        for(j=0u;j<g_odg.pickup_count;++j){
            if(!g_odg_pickups[j].active)continue;
            if(g_odg_pickups[j].stack.type_id==ODG_ITEM_APPLE_SEED&&g_odg_pickups[j].x==bot->x&&g_odg_pickups[j].z==bot->z)seed_id=j;
            if(g_odg_pickups[j].stack.type_id==ODG_ITEM_WOOD&&g_odg_pickups[j].x==bot->x+ODG_FX_ONE/4&&g_odg_pickups[j].z==bot->z)wood_id=j;
        }
        CHECK(seed_id!=UINT32_MAX&&wood_id!=UINT32_MAX);
        odg_update_world_pickups();
        if(seed_id<g_odg.pickup_count)CHECK(g_odg_pickups[seed_id].active!=0u);
        if(wood_id<g_odg.pickup_count)CHECK(g_odg_pickups[wood_id].active==0u);
        CHECK(odg_inventory_total(&bot->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_NONE)>=1u);
    }

    /* Empty economy asks the registry for a real source of WOOD, not for TREE. */
    reset_actor(bot);move_actor_to_artifact(bot,bench);
    CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
    CHECK(bot->bot_economy_item_type==ODG_ITEM_WOOD);
    CHECK(bot->bot_economy_target_id<g_odg.resource_count);
    CHECK(odg_resource_harvest_item_type_internal(&g_odg_resources[bot->bot_economy_target_id])==ODG_ITEM_WOOD);

    /* Bootstrap invests immediately: six real wood at the real bench buys the axe. */
    reset_actor(bot);move_actor_to_artifact(bot,bench);add_stack(bot,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,6u);
    CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
    CHECK(odg_inventory_find_type(&bot->inventory,ODG_ITEM_AXE,ODG_MATERIAL_WOOD,NULL));

    /* Then the pick, still through the ordinary recipe/station transaction. */
    add_stack(bot,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,6u);
    CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
    CHECK(odg_inventory_find_type(&bot->inventory,ODG_ITEM_PICKAXE,ODG_MATERIAL_WOOD,NULL));

    /* Backpack is equipment: it expands capacity atomically instead of consuming a slot. */
    add_stack(bot,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,12u);add_stack(bot,ODG_ITEM_STONE,ODG_MATERIAL_STONE,4u);
    CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
    CHECK(bot->inventory.equipped_backpack_type==ODG_ITEM_BACKPACK);
    CHECK(odg_inventory_capacity(&bot->inventory)==ODG_INVENTORY_MAX_SLOTS);

    /* Productivity-first stone upgrade: axe precedes pick, avoiding a 14-stone batch. */
    add_stack(bot,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,4u);add_stack(bot,ODG_ITEM_STONE,ODG_MATERIAL_STONE,6u);
    CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
    CHECK(odg_inventory_find_type(&bot->inventory,ODG_ITEM_AXE,ODG_MATERIAL_STONE,NULL));
    add_stack(bot,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,4u);add_stack(bot,ODG_ITEM_STONE,ODG_MATERIAL_STONE,8u);
    CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
    CHECK(odg_inventory_find_type(&bot->inventory,ODG_ITEM_PICKAXE,ODG_MATERIAL_STONE,NULL));

    /* Build and place a smithy through normal artifact placement. */
    add_stack(bot,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,12u);add_stack(bot,ODG_ITEM_STONE,ODG_MATERIAL_STONE,20u);
    CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
    CHECK(odg_inventory_find_type(&bot->inventory,ODG_ITEM_SMITHY,ODG_MATERIAL_NONE,NULL));
    for(i=0u;i<16u && owned_artifact(1u,ODG_ITEM_SMITHY)==NULL;++i){
        (void)odg_bot_economy_direction_internal(bot,&dx,&dz);
        if(dx!=0||dz!=0){bot->x+=dx/64;bot->z+=dz/64;odg_local_fx_to_global_fx_internal(bot->x,bot->z,&bot->global_fx_x,&bot->global_fx_z);}
    }
    smithy=owned_artifact(1u,ODG_ITEM_SMITHY);CHECK(smithy!=NULL);
    if(smithy!=NULL)move_actor_to_artifact(bot,smithy);

    /* Iron tier also invests first: ten iron buys the pick; turret ore comes after. */
    add_stack(bot,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,4u);add_stack(bot,ODG_ITEM_IRON,ODG_MATERIAL_IRON,10u);
    CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
    CHECK(odg_inventory_find_type(&bot->inventory,ODG_ITEM_PICKAXE,ODG_MATERIAL_IRON,NULL));

    /* Mature bots participate in construction through the same craft/place transactions.
     * A sparse home perimeter is deliberate utility, not random wall spam. */
    reset_actor(bot);move_actor_to_artifact(bot,bench);add_stack(bot,ODG_ITEM_BACKPACK,ODG_MATERIAL_NONE,1u);
    CHECK(odg_inventory_equip_first_expander_internal(&bot->inventory)!=0);
    add_stack(bot,ODG_ITEM_AXE,ODG_MATERIAL_IRON,1u);add_stack(bot,ODG_ITEM_PICKAXE,ODG_MATERIAL_IRON,1u);
    add_stack(bot,ODG_ITEM_AMMO,ODG_MATERIAL_NONE,24u);add_stack(bot,ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_IRON,1u);
    add_stack(bot,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,5u);
    {
        uint32_t turret_id=g_odg.turret_count,before=g_odg_construction_count;int32_t hx,hz;
        CHECK(odg_entities_reserve_turrets(turret_id+1u)!=0);
        if(g_odg_turrets!=NULL){
            odg_turret *t=&g_odg_turrets[turret_id];odg_memset(t,0,sizeof(*t));t->active=1u;t->id=turret_id;
            t->owner=ODG_OWNER_FROM_ID(bot->id);t->material_tier=ODG_MATERIAL_IRON;t->carried_by=ODG_TURRET_NONE;
            ++g_odg.turret_count;
        }
        CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
        CHECK(odg_inventory_total(&bot->inventory,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_WOOD)>=1u);
        CHECK(odg_global_cell_center_to_local_fx_internal(bot->home_global_cell_x,bot->home_global_cell_z,&hx,&hz)!=0);
        bot->x=hx;bot->z=hz;odg_local_fx_to_global_fx_internal(hx,hz,&bot->global_fx_x,&bot->global_fx_z);
        for(i=0u;i<8u&&g_odg_construction_count==before;++i)(void)odg_bot_economy_direction_internal(bot,&dx,&dz);
        CHECK(g_odg_construction_count>before);
        if(g_odg_construction_count>before)CHECK(g_odg_construction_blocks[g_odg_construction_count-1u].owner_actor_id==bot->id);
    }

    /* A mature bot preserves its own infrastructure before expanding it. Damage a real
     * home-perimeter block, give exactly one matching repair module and prove the economy
     * planner spends it through the shared repair transaction instead of placing a new wall. */
    {
        uint32_t cid=UINT32_MAX,before_hp,before_blocks,ci;
        for(ci=0u;ci<g_odg_construction_count;++ci){
            if(g_odg_construction_blocks[ci].active&&g_odg_construction_blocks[ci].owner_actor_id==bot->id){cid=ci;break;}
        }
        CHECK(cid!=UINT32_MAX);
        if(cid!=UINT32_MAX){
            odg_construction_block *b=&g_odg_construction_blocks[cid];int64_t cgx,cgz;
            b->health=b->max_health/2u;before_hp=b->health;
            odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&cgx,&cgz);
            odg_chunk_set_owner_at_global_cell(cgx,cgz,ODG_OWNER_FROM_ID(bot->id));
            add_stack(bot,ODG_ITEM_BUILDING_BLOCK,b->material_tier,1u);
            before_blocks=odg_inventory_total(&bot->inventory,ODG_ITEM_BUILDING_BLOCK,b->material_tier);
            bot->x=b->x;bot->z=b->z;odg_local_fx_to_global_fx_internal(bot->x,bot->z,&bot->global_fx_x,&bot->global_fx_z);
            CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
            CHECK(b->health>before_hp);
            CHECK(odg_inventory_total(&bot->inventory,ODG_ITEM_BUILDING_BLOCK,b->material_tier)+1u==before_blocks);
            b->health=b->max_health; /* isolate later salvage tests from this repair scenario */
        }
    }

    /* Conquest closes the reverse construction cycle too: a mature bot reuses a hostile
     * module on controlled land before harvesting fresh wood for its own perimeter. */
    {
        uint32_t slot;int32_t sx,sz;int64_t sgx=bot->home_global_cell_x+5,sgz=bot->home_global_cell_z;uint32_t before;
        while(odg_inventory_find_type(&bot->inventory,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_NONE,&slot)){
            uint32_t q=bot->inventory.slots[slot].quantity;CHECK(q!=0u);CHECK(odg_inventory_remove_from_slot(&bot->inventory,slot,q,NULL)!=0);
        }
        CHECK(odg_global_cell_center_to_local_fx_internal(sgx,sgz,&sx,&sz)!=0);
        {
            odg_artifact old;odg_memset(&old,0,sizeof(old));old.active=1u;old.instance_id=odg_next_instance_id();
            old.item_type=ODG_ITEM_BUILDING_BLOCK;old.owner_actor_id=2u;old.material_tier=ODG_MATERIAL_STONE;
            old.x=sx;old.z=sz;old.local_resident=1u;odg_local_fx_to_global_fx_internal(sx,sz,&old.global_fx_x,&old.global_fx_z);
            CHECK(odg_construction_import_legacy_artifact_internal(&old)!=0);
        }
        odg_chunk_set_owner_at_global_cell(sgx,sgz,ODG_OWNER_FROM_ID(bot->id));before=g_odg_construction_count;
        bot->x=sx;bot->z=sz;odg_local_fx_to_global_fx_internal(sx,sz,&bot->global_fx_x,&bot->global_fx_z);
        CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
        CHECK(g_odg_construction_count+1u==before);
        CHECK(odg_inventory_total(&bot->inventory,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_STONE)==1u);

        /* Layered construction must not deadlock salvage. A supporting wall is not
         * dismantleable while its roof exists, so candidate selection must choose the
         * roof first, then expose the wall on the next economic pass. Both modules are
         * placed through the real construction transaction before the cell is conquered. */
        {
            odg_actor *builder=&g_odg.actors[2];uint32_t wall_seen=0u,roof_seen=0u;
            while(odg_inventory_find_type(&bot->inventory,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_NONE,&slot)){
                uint32_t q=bot->inventory.slots[slot].quantity;CHECK(q!=0u);CHECK(odg_inventory_remove_from_slot(&bot->inventory,slot,q,NULL)!=0);
            }
            /* The salvage bot was intentionally standing on the target cell above. Move
             * that body out while the hostile builder creates the fixture: current
             * construction physics correctly rejects spawning a solid through another
             * actor, and this test is about layer-aware salvage rather than clipping. */
            bot->active=0u;bot->local_resident=0u;
            reset_actor(builder);builder->x=sx;builder->z=sz;builder->local_resident=1u;
            odg_local_fx_to_global_fx_internal(sx,sz,&builder->global_fx_x,&builder->global_fx_z);
            add_stack(builder,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_STONE,2u);builder->inventory.selected_slot=0u;
            odg_chunk_set_owner_at_global_cell(sgx,sgz,ODG_OWNER_FROM_ID(builder->id));
            CHECK(odg_construction_set_shape_internal(builder->id,ODG_CONSTRUCTION_SHAPE_WALL)!=0);
            CHECK(odg_construction_place_selected_at_global_cell_internal(builder->id,sgx,sgz)!=0);
            CHECK(odg_construction_set_shape_internal(builder->id,ODG_CONSTRUCTION_SHAPE_ROOF)!=0);
            CHECK(odg_construction_place_selected_at_global_cell_internal(builder->id,sgx,sgz)!=0);
            odg_chunk_set_owner_at_global_cell(sgx,sgz,ODG_OWNER_FROM_ID(bot->id));
            bot->active=1u;bot->local_resident=1u;bot->x=sx;bot->z=sz;odg_local_fx_to_global_fx_internal(sx,sz,&bot->global_fx_x,&bot->global_fx_z);
            CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
            for(i=0u;i<g_odg_construction_count;++i){
                int64_t cgx,cgz;const odg_construction_block *b=&g_odg_construction_blocks[i];
                if(!b->active)continue;
                odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&cgx,&cgz);
                if(cgx!=sgx||cgz!=sgz)continue;
                if(b->shape==ODG_CONSTRUCTION_SHAPE_WALL)++wall_seen;
                if(b->shape==ODG_CONSTRUCTION_SHAPE_ROOF)++roof_seen;
            }
            CHECK(wall_seen==1u&&roof_seen==0u);
            while(odg_inventory_find_type(&bot->inventory,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_NONE,&slot)){
                uint32_t q=bot->inventory.slots[slot].quantity;CHECK(q!=0u);CHECK(odg_inventory_remove_from_slot(&bot->inventory,slot,q,NULL)!=0);
            }
            CHECK(odg_bot_economy_direction_internal(bot,&dx,&dz)==1);
            wall_seen=0u;
            for(i=0u;i<g_odg_construction_count;++i){
                int64_t cgx,cgz;const odg_construction_block *b=&g_odg_construction_blocks[i];
                if(!b->active)continue;
                odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&cgx,&cgz);
                if(cgx==sgx&&cgz==sgz&&b->shape==ODG_CONSTRUCTION_SHAPE_WALL)++wall_seen;
            }
            CHECK(wall_seen==0u);
        }
    }


    /* Multimodal logistics is a closed real economy cycle, not a pathfinding cheat:
     * when its route enters navigable water, the bot spends sixteen wood at
     * its actual workbench, places the resulting raft only on owned navigable water and
     * boards that exact world artifact. No free raft and no teleport are permitted. */
    {
        int32_t sx=0,sz=0,tx=0,tz=0;uint32_t before_rafts,after_rafts,mounted;
        reset_actor(bot);CHECK(find_raft_corridor(bot,&sx,&sz,&tx,&tz)!=0);
        if(bench!=NULL){
            bench->active=1u;bench->owner_actor_id=bot->id;bench->item_type=ODG_ITEM_WORKBENCH;
            bench->x=sx;bench->z=sz;bench->local_resident=1u;
            odg_local_fx_to_global_fx_internal(sx,sz,&bench->global_fx_x,&bench->global_fx_z);
        }
        bot->x=sx;bot->z=sz;odg_local_fx_to_global_fx_internal(sx,sz,&bot->global_fx_x,&bot->global_fx_z);
        add_stack(bot,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,16u);before_rafts=active_raft_count();
        CHECK(odg_bot_logistics_prepare_vehicle_internal(bot,tx,tz,&dx,&dz)==1);
        CHECK(odg_inventory_total(&bot->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD)==0u);
        CHECK(odg_inventory_total(&bot->inventory,ODG_ITEM_RAFT,ODG_MATERIAL_WOOD)==1u);
        CHECK(active_raft_count()==before_rafts);
        CHECK(odg_bot_logistics_prepare_vehicle_internal(bot,tx,tz,&dx,&dz)==1);
        after_rafts=active_raft_count();
        CHECK(after_rafts==before_rafts+1u);
        CHECK(odg_inventory_total(&bot->inventory,ODG_ITEM_RAFT,ODG_MATERIAL_WOOD)==0u);
        CHECK(odg_bot_logistics_prepare_vehicle_internal(bot,tx,tz,&dx,&dz)==1);
        mounted=odg_artifact_actor_vehicle_internal(bot->id);CHECK(mounted<g_odg.artifact_count);
        if(mounted<g_odg.artifact_count){
            CHECK(g_odg_artifacts[mounted].item_type==ODG_ITEM_RAFT);
            CHECK(g_odg_artifacts[mounted].owner_actor_id==bot->id);
            CHECK(g_odg_artifacts[mounted].aux_u32==bot->id+1u);
        }
    }

    if(failures!=0){fprintf(stderr,"%d bot economy test(s) failed\n",failures);return 1;}
    printf("BOT ECONOMY OK stages=wood-axe,wood-pick,backpack,stone-axe,stone-pick,smithy,iron-pick,home-fortification,salvage-reuse,layered-salvage,structure-repair,raft-logistics\n");
    return 0;
}
