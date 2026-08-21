#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures=0;
#define CHECK(expr) do { if(!(expr)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);++failures;} } while(0)

static uint32_t get_u32_le(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}
static void put_u32_le(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);p[2]=(uint8_t)(v>>16u);p[3]=(uint8_t)(v>>24u);}
static void put_u64_le(uint8_t *p,uint64_t v){uint32_t i;for(i=0u;i<8u;++i)p[i]=(uint8_t)(v>>(i*8u));}
static uint64_t payload_checksum(const uint8_t *p,size_t n){uint64_t h=UINT64_C(1469598103934665603);size_t i;for(i=0u;i<n;++i){h^=p[i];h*=UINT64_C(1099511628211);}return h;}

static int skip_section(uint8_t **cursor,uint8_t *end,size_t elem_size){
    uint32_t count;uint64_t bytes;
    if(cursor==NULL||*cursor==NULL||(size_t)(end-*cursor)<4u)return 0;
    count=get_u32_le(*cursor);*cursor+=4u;bytes=(uint64_t)count*(uint64_t)elem_size;
    if (bytes > (uint64_t)(end - *cursor)) return 0;
    *cursor += (size_t)bytes;
    return 1;
}

/* A schema-17 payload is the current layout without the compact construction section.
 * Runtime state remains present. This helper does not reinterpret any frozen struct. */
static int rewrite_current_as_17(uint8_t *blob,uint64_t *bytes,const odg_artifact *legacy_block_record){
    const size_t header=40u;
    const size_t prefix=offsetof(odg_world,playable)-offsetof(odg_world,seed);
    const size_t before_artifacts[3]={sizeof(odg_turret),sizeof(odg_world_pickup),sizeof(odg_resource_node)};
    uint32_t payload,i,count;uint8_t *cursor,*end,*construction,*artifact_data;size_t remove;
    if(blob==NULL||bytes==NULL||legacy_block_record==NULL||*bytes<header)return 0;
    payload=get_u32_le(blob+20u);if(*bytes!=header+(uint64_t)payload||payload<prefix)return 0;
    cursor=blob+header+prefix;end=blob+header+payload;
    for(i=0u;i<3u;++i)if(!skip_section(&cursor,end,before_artifacts[i]))return 0;
    if((size_t)(end-cursor)<4u)return 0;
    count=get_u32_le(cursor);cursor+=4u;
    if(count==0u||(uint64_t)count*(uint64_t)sizeof(odg_artifact)>(uint64_t)(end-cursor))return 0;
    artifact_data=cursor;cursor+=(size_t)count*sizeof(odg_artifact);
    {
        odg_artifact patched=*legacy_block_record;
        patched.id=count-1u;
        memcpy(artifact_data+(size_t)(count-1u)*sizeof(odg_artifact),&patched,sizeof(patched));
    }
    construction=cursor;if((size_t)(end-cursor)<4u)return 0;count=get_u32_le(cursor);
    remove=4u+(size_t)count*sizeof(odg_construction_block);if(remove>(size_t)(end-construction))return 0;
    memmove(construction,construction+remove,(size_t)(end-(construction+remove)));
    payload-=(uint32_t)remove;*bytes-=remove;put_u32_le(blob+8u,17u);put_u32_le(blob+20u,payload);
    put_u64_le(blob+32u,payload_checksum(blob+header,payload));return 1;
}

/* SAVE19 already persisted structural shape, but the three trailing integrity words were
 * reserved zeroes. Recreate that exact contract from a current SAVE20 payload so migration
 * coverage cannot accidentally rely on current health bytes. */
static int rewrite_current_as_19(uint8_t *blob,uint64_t bytes){
    const size_t header=40u;
    const size_t prefix=offsetof(odg_world,playable)-offsetof(odg_world,seed);
    const size_t before[4]={sizeof(odg_turret),sizeof(odg_world_pickup),sizeof(odg_resource_node),sizeof(odg_artifact)};
    uint32_t payload,i,count;uint8_t *cursor,*end;
    if(blob==NULL||bytes<header)return 0;
    payload=get_u32_le(blob+20u);if(bytes!=header+(uint64_t)payload||payload<prefix)return 0;
    cursor=blob+header+prefix;end=blob+header+payload;
    for(i=0u;i<4u;++i)if(!skip_section(&cursor,end,before[i]))return 0;
    if((size_t)(end-cursor)<4u)return 0;
    count=get_u32_le(cursor);cursor+=4u;
    if((uint64_t)count*(uint64_t)sizeof(odg_construction_block)>(uint64_t)(end-cursor))return 0;
    for(i=0u;i<count;++i){
        uint8_t *b=cursor+(size_t)i*sizeof(odg_construction_block);
        put_u32_le(b+offsetof(odg_construction_block,health),0u);
        put_u32_le(b+offsetof(odg_construction_block,max_health),0u);
        put_u32_le(b+offsetof(odg_construction_block,reserved_u32),0u);
    }
    put_u32_le(blob+8u,19u);put_u64_le(blob+32u,payload_checksum(blob+header,payload));return 1;
}

static int give_building_blocks(odg_actor *actor,uint32_t material,uint32_t quantity){
    odg_item_stack stack;
    if(actor==NULL||quantity==0u)return 0;
    odg_memset(&stack,0,sizeof(stack));stack.type_id=ODG_ITEM_BUILDING_BLOCK;stack.quantity=quantity;stack.material_tier=material;
    if(!odg_item_stack_normalize_internal(&stack))return 0;
    actor->inventory.selected_slot=0u;actor->inventory.slots[0]=stack;return 1;
}


static int give_attack_tool(odg_actor *actor,uint32_t material){
    odg_item_stack stack;
    if(actor==NULL)return 0;
    odg_memset(&stack,0,sizeof(stack));stack.type_id=ODG_ITEM_SWORD;stack.quantity=1u;stack.material_tier=material;
    if(!odg_item_stack_normalize_internal(&stack))return 0;
    odg_memset(&actor->inventory.slots[0],0,sizeof(actor->inventory.slots[0]));actor->inventory.slots[0]=stack;
    actor->inventory.selected_slot=0u;return 1;
}

static int find_clear_owned_cell(odg_actor *actor,int64_t *out_gx,int64_t *out_gz,int32_t *out_x,int32_t *out_z){
    int64_t cgx,cgz;int32_t dx,dz;
    if(actor==NULL||out_gx==NULL||out_gz==NULL||out_x==NULL||out_z==NULL)return 0;
    odg_local_fx_to_global_cell_internal(actor->x,actor->z,&cgx,&cgz);
    for(dz=-2;dz<=2;++dz)for(dx=-2;dx<=2;++dx){
        int64_t gx=cgx+dx,gz=cgz+dz;int32_t x,z;odg_surface_sample surface;
        if(!odg_global_cell_center_to_local_fx_internal(gx,gz,&x,&z))continue;
        if(!odg_environment_surface_local(x,z,&surface)||(surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)continue;
        if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_FROM_ID(actor->id))continue;
        if(!odg_position_clear_internal(x,z,18*ODG_FX_ONE/25))continue;
        *out_gx=gx;*out_gz=gz;*out_x=x;*out_z=z;return 1;
    }
    return 0;
}

static odg_artifact legacy_block(uint32_t material,int32_t x,int32_t z,uint64_t instance){
    odg_artifact a;odg_memset(&a,0,sizeof(a));a.active=1u;a.instance_id=instance;a.item_type=ODG_ITEM_BUILDING_BLOCK;
    a.owner_actor_id=ODG_PLAYER_ID;a.material_tier=material;a.capability_bits=ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_CONSTRUCTION;
    a.x=x;a.z=z;a.local_resident=1u;odg_local_fx_to_global_fx_internal(x,z,&a.global_fx_x,&a.global_fx_z);return a;
}

int main(void){
    const odg_item_definition *definition;uint32_t artifact_before;
    CHECK(odg_init(UINT64_C(0xc01757a5),640u,360u)==ODG_STATUS_OK);
    CHECK(odg_api_version()==ODG_API_VERSION&&odg_save_schema_version()==ODG_SAVE_SCHEMA_VERSION);
    definition=odg_item_definition_internal(ODG_ITEM_BUILDING_BLOCK);
    CHECK(definition!=NULL&&(definition->capability_bits&ODG_ITEM_CAP_CONSTRUCT)!=0u);
    CHECK(definition!=NULL&&(definition->flags&ODG_ITEM_FLAG_ARTIFACT)==0u&&definition->max_stack>1u);
    CHECK(odg_artifact_item_deployable_internal(ODG_ITEM_BUILDING_BLOCK)==0);
    CHECK(sizeof(odg_construction_block)<sizeof(odg_artifact)/8u);

    /* Flutter reaches shape selection only through the public command queue. Prove the
     * command path and prove unknown append-only values fail closed without corrupting mode. */
    {
        odg_command command;odg_memset(&command,0,sizeof(command));command.struct_size=(uint32_t)sizeof(command);
        command.type=ODG_COMMAND_SET_CONSTRUCTION_SHAPE;command.arg0=ODG_CONSTRUCTION_SHAPE_FLOOR;
        CHECK(odg_command_submit(&command,sizeof(command))==ODG_STATUS_OK);odg_process_commands();
        CHECK(odg_construction_selected_shape_internal(ODG_PLAYER_ID)==ODG_CONSTRUCTION_SHAPE_FLOOR);
        command.arg0=UINT32_C(0x7fffffff);CHECK(odg_command_submit(&command,sizeof(command))==ODG_STATUS_OK);odg_process_commands();
        CHECK(odg_construction_selected_shape_internal(ODG_PLAYER_ID)==ODG_CONSTRUCTION_SHAPE_FLOOR);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_WALL)!=0);
    }

    /* A wall is a real solid at placement time. Static-world clearance alone is not
     * enough: another actor standing in the target cell must block both the ghost's
     * authoritative candidate and the actual placement path. Once that body leaves,
     * the exact same owned/safe cell becomes legal without changing any other rule. */
    {
        odg_actor player_backup=g_odg.actors[ODG_PLAYER_ID],bot_backup=g_odg.actors[1u];
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID],*bot=&g_odg.actors[1u];
        int64_t gx=0,gz=0;int32_t x=0,z=0;
        odg_construction_reset_runtime_internal();odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
        CHECK(find_clear_owned_cell(p,&gx,&gz,&x,&z)!=0);p->x=x;p->z=z;
        odg_local_fx_to_global_fx_internal(x,z,&p->global_fx_x,&p->global_fx_z);
        CHECK(give_building_blocks(p,ODG_MATERIAL_WOOD,2u)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_WALL)!=0);
        bot->active=1u;bot->hp=bot->max_hp!=0u?bot->max_hp:100u;bot->local_resident=1u;bot->x=x;bot->z=z;
        odg_local_fx_to_global_fx_internal(x,z,&bot->global_fx_x,&bot->global_fx_z);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)==0);
        bot->active=0u;bot->local_resident=0u;
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        odg_construction_reset_runtime_internal();g_odg.actors[ODG_PLAYER_ID]=player_backup;g_odg.actors[1u]=bot_backup;
        odg_entities_spatial_mark_dirty();
    }

    /* Legacy schema migration: an old heavyweight building artifact becomes one compact
     * block and disappears from artifact storage. */
    artifact_before=g_odg.artifact_count;
    {
        uint64_t bytes=odg_save_blob_size(),written=0u,legacy_bytes,legacy_instance=0u;
        uint8_t *blob=(uint8_t *)malloc((size_t)bytes);odg_artifact old;
        CHECK(blob!=NULL&&artifact_before!=0u);
        if(blob!=NULL&&artifact_before!=0u){
            legacy_instance=g_odg_artifacts[artifact_before-1u].instance_id;
            old=legacy_block(ODG_MATERIAL_STONE,9*ODG_FX_ONE,7*ODG_FX_ONE,legacy_instance);
            CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
            legacy_bytes=bytes;CHECK(rewrite_current_as_17(blob,&legacy_bytes,&old)!=0);
            CHECK(odg_reset(UINT64_C(0xc01757a5))==ODG_STATUS_OK);CHECK(odg_save_load(blob,legacy_bytes)==ODG_STATUS_OK);
            CHECK(g_odg.artifact_count==artifact_before-1u);
            CHECK(g_odg_construction_count==1u&&g_odg_construction_blocks[0].material_tier==ODG_MATERIAL_STONE);
            CHECK(g_odg_construction_blocks[0].instance_id==legacy_instance);
            CHECK(g_odg_construction_blocks[0].owner_actor_id==ODG_PLAYER_ID);
            free(blob);
        }
    }

    /* SAVE18 remains readable. Its construction section is byte-compatible, but its only
     * legal shape was WALL; SAVE19 is the semantic activation boundary for other shapes. */
    {
        uint64_t bytes=odg_save_blob_size(),written=0u;uint8_t *blob=(uint8_t *)malloc((size_t)bytes);
        CHECK(blob!=NULL);if(blob!=NULL){CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
        put_u32_le(blob+8u,18u);CHECK(odg_reset(UINT64_C(7))==ODG_STATUS_OK);CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);
        CHECK(g_odg_construction_count==1u&&g_odg_construction_blocks[0].shape==ODG_CONSTRUCTION_SHAPE_WALL);free(blob);}
    }

    /* SAVE19 shape data migrates through SAVE20/21 at full health. The old integrity words must
     * be treated as reserved zeroes rather than trusted as invented damage history. */
    {
        uint64_t bytes=odg_save_blob_size(),written=0u;uint8_t *blob=(uint8_t *)malloc((size_t)bytes);
        CHECK(blob!=NULL);if(blob!=NULL){CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
        CHECK(rewrite_current_as_19(blob,bytes)!=0);CHECK(odg_reset(UINT64_C(8))==ODG_STATUS_OK);CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);
        CHECK(g_odg_construction_count==1u&&g_odg_construction_blocks[0].health==g_odg_construction_blocks[0].max_health);
        CHECK(g_odg_construction_blocks[0].max_health==odg_construction_max_health_internal(ODG_MATERIAL_STONE,ODG_CONSTRUCTION_SHAPE_WALL));free(blob);}
    }

    /* Current SAVE22 round-trip includes SAVE20 construction integrity in the authoritative hash. */
    {
        uint64_t hash=odg_state_hash(),bytes=odg_save_blob_size(),written=0u;uint8_t *blob=(uint8_t *)malloc((size_t)bytes);
        CHECK(blob!=NULL);if(blob!=NULL){CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
        CHECK(odg_reset(UINT64_C(7))==ODG_STATUS_OK);CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);CHECK(odg_state_hash()==hash);
        /* Chunk summaries are derived caches: refreshing them must never mutate authoritative
         * world state or change a freshly loaded save's deterministic hash. */
        odg_chunks_refresh_summaries();CHECK(odg_state_hash()==hash);
        odg_chunks_refresh_summaries();CHECK(odg_state_hash()==hash);
        free(blob);}
    }

    /* 64 is a transfer page, never a world cap. Store 80 modules while artifact_count
     * remains unchanged, then prove 64+16 pagination. */
    odg_construction_reset_runtime_internal();artifact_before=g_odg.artifact_count;
    {
        uint32_t i;odg_construction_snapshot page;uint64_t required=0u;
        for(i=0u;i<80u;++i){odg_artifact old=legacy_block((i%3u)+1u,(int32_t)i*ODG_FX_ONE,20*ODG_FX_ONE,UINT64_C(0x90000000)+i);old.id=i;CHECK(odg_construction_import_legacy_artifact_internal(&old)!=0);}
        CHECK(g_odg.artifact_count==artifact_before&&odg_construction_count()==80u);
        CHECK(odg_copy_construction_page(0u,&page,sizeof(page),&required)==ODG_STATUS_OK);
        CHECK(required==sizeof(page)&&page.total_count==80u&&page.count==64u);
        CHECK(odg_copy_construction_page(64u,&page,sizeof(page),&required)==ODG_STATUS_OK&&page.count==16u&&page.total_count==80u);
    }

    /* Material dismantle time is real interaction data, not visual metadata. */
    {
        const uint32_t materials[3]={ODG_MATERIAL_WOOD,ODG_MATERIAL_STONE,ODG_MATERIAL_IRON};
        const uint32_t expected[3]={ODG_INTERACT_HOLD_TICKS,(3u*ODG_INTERACT_HOLD_TICKS)/2u,2u*ODG_INTERACT_HOLD_TICKS};
        uint32_t i;odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
        for(i=0u;i<3u;++i){odg_artifact old;odg_interaction_hint hint;int64_t gx,gz;
            odg_construction_reset_runtime_internal();old=legacy_block(materials[i],p->x,p->z,UINT64_C(0xa0000000)+i);
            CHECK(odg_construction_import_legacy_artifact_internal(&old)!=0);
            odg_local_fx_to_global_cell_internal(p->x,p->z,&gx,&gz);odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_NONE);
            odg_memset(&hint,0,sizeof(hint));CHECK(odg_construction_build_hint_internal(p,NULL,&hint)!=0);
            CHECK(hint.action==ODG_INTERACTION_DISMANTLE_CONSTRUCTION&&hint.requires_hold!=0u&&hint.threshold_ticks==expected[i]);
        }
    }

    /* SAVE19 turned the pre-existing shape field into a real structural graph. SAVE20 adds
     * durability without changing its topology. A cell may
     * carry floor + wall/doorway + roof layers, but never two modules in one layer. Roofs
     * require support, walls block ground motion, and doorway centers remain traversable. */
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];int64_t gx=0,gz=0;int32_t x=0,z=0;
        uint32_t wall_id=UINT32_MAX,roof_id=UINT32_MAX,i;odg_construction_snapshot page;uint64_t required=0u;
        odg_construction_reset_runtime_internal();CHECK(find_clear_owned_cell(p,&gx,&gz,&x,&z)!=0);p->x=x;p->z=z;
        odg_local_fx_to_global_fx_internal(p->x,p->z,&p->global_fx_x,&p->global_fx_z);
        CHECK(give_building_blocks(p,ODG_MATERIAL_STONE,8u)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_FLOOR)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_WALL)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_ROOF)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        CHECK(g_odg_construction_count==3u);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_DOORWAY)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)==0); /* wall layer occupied */
        CHECK(odg_copy_construction_page(0u,&page,sizeof(page),&required)==ODG_STATUS_OK);
        CHECK(page.selected_shape==ODG_CONSTRUCTION_SHAPE_DOORWAY&&page.total_count==3u);
        for(i=0u;i<g_odg_construction_count;++i){
            if(g_odg_construction_blocks[i].shape==ODG_CONSTRUCTION_SHAPE_WALL)wall_id=i;
            if(g_odg_construction_blocks[i].shape==ODG_CONSTRUCTION_SHAPE_ROOF)roof_id=i;
        }
        CHECK(wall_id!=UINT32_MAX&&roof_id!=UINT32_MAX);
        CHECK(odg_construction_collision_radius_fx_internal(&g_odg_construction_blocks[wall_id])>0);
        CHECK(odg_construction_airspace_radius_fx_internal(&g_odg_construction_blocks[roof_id])>0);
        CHECK(odg_position_clear_internal(x,z,p->radius)==0);
        CHECK(odg_construction_dismantle_internal(ODG_PLAYER_ID,wall_id)==0); /* roof dependency */
        CHECK(odg_construction_dismantle_internal(ODG_PLAYER_ID,roof_id)!=0);
        wall_id=UINT32_MAX;for(i=0u;i<g_odg_construction_count;++i)if(g_odg_construction_blocks[i].shape==ODG_CONSTRUCTION_SHAPE_WALL)wall_id=i;
        CHECK(wall_id!=UINT32_MAX&&odg_construction_dismantle_internal(ODG_PLAYER_ID,wall_id)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_DOORWAY)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_ROOF)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        CHECK(odg_position_clear_internal(x,z,p->radius)!=0); /* doorway + floor + roof keep center open */
        {
            uint64_t hash=odg_state_hash(),bytes=odg_save_blob_size(),written=0u;uint8_t *blob=(uint8_t *)malloc((size_t)bytes);
            CHECK(blob!=NULL);if(blob!=NULL){CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                CHECK(odg_reset(UINT64_C(9))==ODG_STATUS_OK);CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);CHECK(odg_state_hash()==hash);
                put_u32_le(blob+8u,18u);CHECK(odg_reset(UINT64_C(10))==ODG_STATUS_OK);CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
                put_u32_le(blob+8u,ODG_SAVE_SCHEMA_VERSION);CHECK(odg_reset(UINT64_C(11))==ODG_STATUS_OK);CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);free(blob);}
        }
    }

    /* SAVE20 structural integrity remains authoritative gameplay under SAVE22: enemy damage reduces HP,
     * repair consumes a real matching-material block, and destroying a support collapses
     * its dependent roof while preserving the independent floor layer. */
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];int64_t gx=0,gz=0;int32_t x=0,z=0;
        uint32_t wall_id=UINT32_MAX,roof_id=UINT32_MAX,floor_id=UINT32_MAX,i,before_hp,before_qty;
        odg_interaction_hint hint;odg_construction_snapshot page;uint64_t required=0u;
        odg_construction_reset_runtime_internal();odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
        CHECK(find_clear_owned_cell(p,&gx,&gz,&x,&z)!=0);p->x=x;p->z=z;
        odg_local_fx_to_global_fx_internal(p->x,p->z,&p->global_fx_x,&p->global_fx_z);
        CHECK(give_building_blocks(p,ODG_MATERIAL_STONE,8u)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_FLOOR)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_WALL)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_ROOF)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        for(i=0u;i<g_odg_construction_count;++i){
            if(g_odg_construction_blocks[i].shape==ODG_CONSTRUCTION_SHAPE_FLOOR)floor_id=i;
            else if(g_odg_construction_blocks[i].shape==ODG_CONSTRUCTION_SHAPE_WALL)wall_id=i;
            else if(g_odg_construction_blocks[i].shape==ODG_CONSTRUCTION_SHAPE_ROOF)roof_id=i;
        }
        CHECK(floor_id!=UINT32_MAX&&wall_id!=UINT32_MAX&&roof_id!=UINT32_MAX);
        CHECK(g_odg_construction_blocks[wall_id].health==g_odg_construction_blocks[wall_id].max_health);
        before_hp=g_odg_construction_blocks[wall_id].health;
        odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_FROM_ID(1u));
        CHECK(odg_construction_apply_damage_internal(ODG_PLAYER_ID,wall_id,31u)!=0);
        CHECK(g_odg_construction_blocks[wall_id].health==before_hp-31u);
        CHECK(give_attack_tool(p,ODG_MATERIAL_STONE)!=0);odg_memset(&hint,0,sizeof(hint));
        CHECK(odg_construction_build_hint_internal(p,odg_inventory_selected(&p->inventory),&hint)!=0);
        CHECK(hint.action==ODG_INTERACTION_ATTACK_CONSTRUCTION&&hint.target_id<g_odg_construction_count&&hint.valid!=0u);
        odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_FROM_ID(ODG_PLAYER_ID));
        odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
        CHECK(give_building_blocks(p,ODG_MATERIAL_STONE,2u)!=0);before_qty=p->inventory.slots[0].quantity;before_hp=g_odg_construction_blocks[wall_id].health;
        odg_memset(&hint,0,sizeof(hint));CHECK(odg_construction_build_hint_internal(p,odg_inventory_selected(&p->inventory),&hint)!=0);
        CHECK(hint.action==ODG_INTERACTION_REPAIR_CONSTRUCTION&&hint.target_id==wall_id&&hint.valid!=0u);
        CHECK(odg_construction_repair_internal(ODG_PLAYER_ID,wall_id)!=0);
        CHECK(p->inventory.slots[0].quantity==before_qty-1u&&g_odg_construction_blocks[wall_id].health>before_hp);
        {
            uint64_t save_required=0u;uint32_t saved_health=g_odg_construction_blocks[wall_id].health;
            CHECK(odg_save_write(NULL,0u,&save_required)==ODG_STATUS_BUFFER_TOO_SMALL);
            g_odg_construction_blocks[wall_id].health=0u;
            CHECK(odg_save_write(NULL,0u,&save_required)==ODG_STATUS_INVALID_STATE);
            g_odg_construction_blocks[wall_id].health=saved_health;
        }
        CHECK(odg_copy_construction_page(0u,&page,sizeof(page),&required)==ODG_STATUS_OK);
        for(i=0u;i<page.count;++i)if(page.entries[i].construction_id==wall_id){CHECK(page.entries[i].health==g_odg_construction_blocks[wall_id].health);CHECK(page.entries[i].max_health==g_odg_construction_blocks[wall_id].max_health);}
        odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_FROM_ID(1u));before_hp=g_odg_construction_blocks[wall_id].health;
        CHECK(odg_construction_apply_damage_internal(ODG_PLAYER_ID,wall_id,before_hp)!=0);
        CHECK(g_odg_construction_count==1u&&g_odg_construction_blocks[0].shape==ODG_CONSTRUCTION_SHAPE_FLOOR);
    }

    /* Dismantling must never be a free repair path. An intact stackable module is reusable,
     * but once damaged it is converted into recipe-derived raw salvage because a stack item
     * cannot preserve per-module integrity truthfully. */
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];int64_t gx=0,gz=0;int32_t x=0,z=0;
        uint32_t wall_id=UINT32_MAX,i,pickups_before,blocks_before;
        odg_construction_reset_runtime_internal();odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
        CHECK(find_clear_owned_cell(p,&gx,&gz,&x,&z)!=0);p->x=x;p->z=z;
        odg_local_fx_to_global_fx_internal(p->x,p->z,&p->global_fx_x,&p->global_fx_z);
        CHECK(give_building_blocks(p,ODG_MATERIAL_STONE,2u)!=0);
        CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_WALL)!=0);
        CHECK(odg_construction_place_selected_at_global_cell_internal(ODG_PLAYER_ID,gx,gz)!=0);
        for(i=0u;i<g_odg_construction_count;++i)if(g_odg_construction_blocks[i].shape==ODG_CONSTRUCTION_SHAPE_WALL)wall_id=i;
        CHECK(wall_id!=UINT32_MAX);
        odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_FROM_ID(1u));
        CHECK(odg_construction_apply_damage_internal(ODG_PLAYER_ID,wall_id,1u)!=0);
        odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_FROM_ID(ODG_PLAYER_ID));
        pickups_before=g_odg.pickup_count;blocks_before=odg_inventory_total(&p->inventory,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_STONE);
        CHECK(odg_construction_dismantle_internal(ODG_PLAYER_ID,wall_id)!=0);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_STONE)==blocks_before);
        CHECK(g_odg.pickup_count==pickups_before+1u);
        if(g_odg.pickup_count>pickups_before){
            const odg_world_pickup *salvage=&g_odg_pickups[g_odg.pickup_count-1u];
            CHECK(salvage->stack.type_id==ODG_ITEM_STONE&&salvage->stack.material_tier==ODG_MATERIAL_STONE);
            CHECK(salvage->stack.quantity==2u); /* floor(6 * (384-1) / (2 * 384)) */
        }
    }

    /* The strategic map must expose lightweight structures as their own semantic marker,
     * not hide them behind an artifact fallback. Controller follows land authority. */
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];odg_artifact old;odg_map_query_desc q;
        odg_map_sample samples[64];odg_map_marker markers[ODG_MAP_MAX_MARKERS];
        uint64_t required=0u;uint32_t marker_count=0u,i;int found=0;int64_t gx,gz;
        odg_construction_reset_runtime_internal();old=legacy_block(ODG_MATERIAL_IRON,p->x,p->z,UINT64_C(0xb0000001));
        CHECK(odg_construction_import_legacy_artifact_internal(&old)!=0);odg_entities_spatial_mark_dirty();
        odg_local_fx_to_global_cell_internal(p->x,p->z,&gx,&gz);odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_FROM_ID(1u));
        odg_memset(&q,0,sizeof(q));q.struct_size=(uint32_t)sizeof(q);
        {
            int32_t pxm=(int32_t)((g_odg_construction_blocks[0].global_fx_x*INT64_C(1000))/(int64_t)ODG_FX_ONE);
            int32_t pzm=(int32_t)((g_odg_construction_blocks[0].global_fx_z*INT64_C(1000))/(int64_t)ODG_FX_ONE);
            q.min_x_milli=pxm-4000;q.min_z_milli=pzm-4000;q.max_x_milli=pxm+4000;q.max_z_milli=pzm+4000;
        }
        q.width=8u;q.height=8u;
        CHECK(odg_map_query(&q,samples,64u,&required,markers,ODG_MAP_MAX_MARKERS,&marker_count)==ODG_STATUS_OK);
        CHECK(required==64u);
        for(i=0u;i<marker_count;++i)if(markers[i].kind==ODG_MAP_MARKER_CONSTRUCTION){
            CHECK(markers[i].material_tier==ODG_MATERIAL_IRON);CHECK(markers[i].owner_actor_id==1u);
            CHECK(markers[i].state==ODG_CONSTRUCTION_SHAPE_WALL);found=1;break;
        }
        CHECK(found!=0);
    }

    if(failures!=0){fprintf(stderr,"construction: %d failure(s)\n",failures);return 1;}
    printf("CONSTRUCTION OK store=%zuB artifact=%zuB paging=64+16 migration=17->23 shapes=floor+wall+doorway+roof durability=damage+repair+collapse salvage=recipe-driven dynamic=body-safe api=%u save=%u\n",
           sizeof(odg_construction_block),sizeof(odg_artifact),odg_api_version(),odg_save_schema_version());
    return 0;
}
