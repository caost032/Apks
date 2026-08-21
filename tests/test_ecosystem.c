#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr); ++failures; \
} } while (0)

static int habitat_accepts(const odg_fauna_entity *e) {
    const odg_fauna_species_definition *species;
    const odg_fauna_habitat_definition *habitat;
    odg_surface_sample surface;
    if (e == NULL || !e->active || !e->local_resident) return 1;
    species = odg_fauna_species_internal(e->species_id);
    habitat = odg_fauna_habitat_internal(e->species_id);
    if (species == NULL || habitat == NULL || !odg_environment_surface_local(e->x,e->z,&surface)) return 0;
    if ((habitat->biome_mask & ODG_BIOME_MASK(surface.biome)) == 0u) return 0;
    if (surface.height_milli < habitat->min_altitude_milli || surface.height_milli > habitat->max_altitude_milli) return 0;
    if (surface.moisture_permille < habitat->min_moisture_permille || surface.moisture_permille > habitat->max_moisture_permille) return 0;
    if ((species->behavior_flags & ODG_FAUNA_BEHAVIOR_AQUATIC) != 0u) {
        if ((surface.flags & ODG_SURFACE_FLAG_WATER) == 0u) return 0;
        if (surface.water_depth_milli < species->body_radius_milli + 120u) return 0;
    } else if ((species->behavior_flags & ODG_FAUNA_BEHAVIOR_AMPHIBIOUS) != 0u) {
        if ((surface.flags & ODG_SURFACE_FLAG_WATER) != 0u) {
            if (surface.water_depth_milli < 350u) return 0;
        } else {
            if ((surface.flags & ODG_SURFACE_FLAG_STEEP) != 0u) return 0;
            /* Shore proximity is intentionally validated by the engine's habitat registry;
             * this helper verifies the medium at the resident point without duplicating
             * the engine's radial search implementation. */
        }
    } else {
        if ((surface.flags & ODG_SURFACE_FLAG_WATER) != 0u) return 0;
        if ((species->behavior_flags & ODG_FAUNA_BEHAVIOR_CAN_FLY) == 0u &&
            (surface.flags & ODG_SURFACE_FLAG_STEEP) != 0u) return 0;
    }
    return 1;
}

static int32_t test_fauna_radius_fx(const odg_fauna_entity *e) {
    const odg_fauna_species_definition *species;uint64_t radius;
    if(e==NULL)return ODG_FX_ONE/10;
    species=odg_fauna_species_internal(e->species_id);
    if(species==NULL)return ODG_FX_ONE/10;
    radius=((uint64_t)species->body_radius_milli*(uint64_t)ODG_FX_ONE)/UINT64_C(1000);
    if(radius<(uint64_t)(ODG_FX_ONE/10))radius=(uint64_t)(ODG_FX_ONE/10);
    return radius>(uint64_t)INT32_MAX?INT32_MAX:(int32_t)radius;
}

static int fauna_spawn_layout_is_physical(void) {
    uint32_t i,j;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        const odg_fauna_entity *e=&g_odg.fauna[i];int32_t radius;
        if(!e->active||!e->local_resident)continue;
        radius=test_fauna_radius_fx(e);
        if(!odg_position_clear_internal(e->x,e->z,radius))return 0;
        for(j=0u;j<ODG_MAX_ACTORS;++j){
            const odg_actor *actor=&g_odg.actors[j];int32_t combined;
            if(!actor->active||actor->hp==0u||!actor->local_resident)continue;
            combined=radius+actor->radius;
            if(odg_dist2(e->x,e->z,actor->x,actor->z)<(int64_t)combined*combined)return 0;
        }
        for(j=0u;j<i;++j){
            const odg_fauna_entity *other=&g_odg.fauna[j];int32_t combined;
            if(!other->active||!other->local_resident)continue;
            combined=radius+test_fauna_radius_fx(other);
            if(odg_dist2(e->x,e->z,other->x,other->z)<(int64_t)combined*combined)return 0;
        }
    }
    return 1;
}

static int fauna_ground_occupant(const odg_fauna_entity *e){
    const odg_fauna_species_definition *species;int32_t radius;
    if(e==NULL||!e->active||!e->local_resident)return 0;
    species=odg_fauna_species_internal(e->species_id);if(species==NULL)return 0;
    radius=test_fauna_radius_fx(e);
    if((species->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)!=0u&&e->y_offset_fx>radius+ODG_FX_ONE/4)return 0;
    return 1;
}

static int fauna_runtime_layout_is_physical(void){
    uint32_t i,j;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        const odg_fauna_entity *e=&g_odg.fauna[i];int32_t radius;
        if(!fauna_ground_occupant(e))continue;
        radius=test_fauna_radius_fx(e);
        if(!odg_position_clear_internal(e->x,e->z,radius))return 0;
        for(j=0u;j<ODG_MAX_ACTORS;++j){
            const odg_actor *actor=&g_odg.actors[j];int32_t combined;
            if(!actor->active||actor->hp==0u||!actor->local_resident)continue;
            combined=radius+actor->radius;
            if(odg_dist2(e->x,e->z,actor->x,actor->z)<(int64_t)combined*combined)return 0;
        }
        for(j=0u;j<i;++j){
            const odg_fauna_entity *other=&g_odg.fauna[j];int32_t combined;
            if(!fauna_ground_occupant(other))continue;
            combined=radius+test_fauna_radius_fx(other);
            if(odg_dist2(e->x,e->z,other->x,other->z)<(int64_t)combined*combined)return 0;
        }
    }
    return 1;
}

static uint32_t find_active_fauna(uint32_t species_id,uint32_t sex,uint32_t skip) {
    uint32_t i;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i) {
        if(!g_odg.fauna[i].active || g_odg.fauna[i].species_id!=species_id || g_odg.fauna[i].sex!=sex) continue;
        if(skip!=0u){--skip;continue;}
        return i;
    }
    return UINT32_MAX;
}


typedef struct {
    uint32_t active,id;
    uint64_t stable_id;
    uint32_t species_id,egg_count,hatch_ticks,parent_a,parent_b;
    uint64_t host_resource_stable_id;
    int32_t x,z;
    int64_t global_fx_x,global_fx_z;
    uint32_t local_resident;
} legacy_nest15;

static uint32_t test_get_u32_le(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}
static void test_put_u32_le(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);p[2]=(uint8_t)(v>>16u);p[3]=(uint8_t)(v>>24u);}
static void test_put_u64_le(uint8_t *p,uint64_t v){uint32_t i;for(i=0u;i<8u;++i)p[i]=(uint8_t)(v>>(i*8u));}
static uint64_t test_payload_checksum(const uint8_t *p,size_t n){uint64_t h=UINT64_C(1469598103934665603);size_t i;for(i=0u;i<n;++i){h^=p[i];h*=UINT64_C(1099511628211);}return h;}


enum {
    TEST_SAVE_SECTION_TURRETS=0u,
    TEST_SAVE_SECTION_PICKUPS=1u,
    TEST_SAVE_SECTION_RESOURCES=2u,
    TEST_SAVE_SECTION_ARTIFACTS=3u,
    TEST_SAVE_SECTION_CONSTRUCTION=4u,
    TEST_SAVE_SECTION_CHUNKS=5u,
    TEST_SAVE_SECTION_RUNTIME=6u
};

/* Return the first entry of one current-schema dynamic section. This parser is kept in
 * the regression layer so corrupt-save tests can patch exactly one canonical section
 * while leaving transport/checksum/state-hash layers self-consistent. */
static uint8_t *test_current_save_section(uint8_t *blob,uint64_t bytes,uint32_t wanted,uint32_t *out_count){
    const size_t header=40u;const size_t prefix=offsetof(odg_world,playable)-offsetof(odg_world,seed);
    const size_t sizes[]={sizeof(odg_turret),sizeof(odg_world_pickup),sizeof(odg_resource_node),
                          sizeof(odg_artifact),sizeof(odg_construction_block),sizeof(odg_chunk_runtime),
                          sizeof(odg_persistent_runtime_state)};
    uint32_t payload_size,section,count;uint8_t *cursor,*end;uint64_t section_bytes;
    if(blob==NULL||wanted>TEST_SAVE_SECTION_RUNTIME||bytes<header||test_get_u32_le(blob+8u)!=ODG_SAVE_SCHEMA_VERSION)return NULL;
    payload_size=test_get_u32_le(blob+20u);
    if(bytes!=header+(uint64_t)payload_size||payload_size<prefix)return NULL;
    cursor=blob+header+prefix;end=blob+header+payload_size;
    for(section=0u;section<=TEST_SAVE_SECTION_RUNTIME;++section){
        if((size_t)(end-cursor)<4u)return NULL;
        count=test_get_u32_le(cursor);cursor+=4u;
        section_bytes=(uint64_t)count*(uint64_t)sizes[section];
        if(section_bytes>(uint64_t)(end-cursor))return NULL;
        if(section==wanted){if(out_count!=NULL)*out_count=count;return cursor;}
        cursor+=(size_t)section_bytes;
    }
    return NULL;
}

static uint8_t *test_current_save_suffix(uint8_t *blob,uint64_t bytes){
    uint32_t runtime_count=0u;
    uint8_t *runtime=test_current_save_section(blob,bytes,TEST_SAVE_SECTION_RUNTIME,&runtime_count);
    if(runtime==NULL||runtime_count!=1u)return NULL;
    return runtime+sizeof(odg_persistent_runtime_state);
}

static int test_bytes_are_zero(const uint8_t *p,size_t n){
    size_t i;if(p==NULL)return 0;for(i=0u;i<n;++i)if(p[i]!=0u)return 0;return 1;
}

static int rewrite_current_save_as_legacy_nesting(uint8_t *blob,uint64_t bytes,uint32_t schema){
    const size_t header=40u;const size_t nest_off=offsetof(odg_world,fauna_nests)-offsetof(odg_world,seed);
    uint32_t payload_size,i;uint8_t *base;
    if(blob==NULL||bytes<header||sizeof(legacy_nest15)!=sizeof(odg_fauna_nest))return 0;
    payload_size=test_get_u32_le(blob+20u);if(bytes!=header+(uint64_t)payload_size)return 0;base=blob+header+nest_off;
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i){odg_fauna_nest cur;legacy_nest15 old;memcpy(&cur,base+(size_t)i*sizeof(cur),sizeof(cur));memset(&old,0,sizeof(old));old.active=cur.active;old.id=cur.id;old.stable_id=cur.stable_id;old.species_id=cur.species_id;old.egg_count=cur.egg_count;old.hatch_ticks=cur.hatch_ticks;old.parent_a=cur.parent_a;old.parent_b=cur.parent_b;old.host_resource_stable_id=cur.host_resource_stable_id;old.x=cur.x;old.z=cur.z;old.global_fx_x=cur.global_fx_x;old.global_fx_z=cur.global_fx_z;old.local_resident=cur.local_resident;memcpy(base+(size_t)i*sizeof(cur),&old,sizeof(old));}
    test_put_u32_le(blob+8u,schema);test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));return 1;
}

static int set_save_worldgen_version(uint8_t *blob,uint64_t bytes,uint32_t schema,uint32_t worldgen_version){
    const size_t header=40u;
    const size_t prefix=offsetof(odg_world,playable)-offsetof(odg_world,seed);
    const size_t base_sections[4]={sizeof(odg_turret),sizeof(odg_world_pickup),sizeof(odg_resource_node),sizeof(odg_artifact)};
    uint32_t payload_size,i,count;uint8_t *cursor,*payload_end;uint64_t section_bytes;
    if(blob==NULL||bytes<header||schema<17u||schema>ODG_SAVE_SCHEMA_VERSION)return 0;
    payload_size=test_get_u32_le(blob+20u);if(bytes!=header+(uint64_t)payload_size||payload_size<prefix)return 0;
    cursor=blob+header+prefix;payload_end=blob+header+payload_size;
    for(i=0u;i<4u;++i){
        if((size_t)(payload_end-cursor)<4u)return 0;
        count=test_get_u32_le(cursor);cursor+=4u;
        section_bytes=(uint64_t)count*(uint64_t)base_sections[i];
        if(section_bytes>(uint64_t)(payload_end-cursor))return 0;
        cursor+=(size_t)section_bytes;
    }
    if(schema>=18u){
        if((size_t)(payload_end-cursor)<4u)return 0;
        count=test_get_u32_le(cursor);cursor+=4u;
        section_bytes=(uint64_t)count*(uint64_t)sizeof(odg_construction_block);
        if(section_bytes>(uint64_t)(payload_end-cursor))return 0;
        cursor+=(size_t)section_bytes;
    }
    if((size_t)(payload_end-cursor)<4u)return 0;
    count=test_get_u32_le(cursor);cursor+=4u;
    section_bytes=(uint64_t)count*(uint64_t)sizeof(odg_chunk_runtime);
    if(section_bytes>(uint64_t)(payload_end-cursor))return 0;
    cursor+=(size_t)section_bytes;
    if((size_t)(payload_end-cursor)<4u+sizeof(odg_persistent_runtime_state)||test_get_u32_le(cursor)!=1u)return 0;
    cursor+=4u;test_put_u32_le(cursor+offsetof(odg_persistent_runtime_state,worldgen_version),worldgen_version);
    test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));return 1;
}

static int rewrite_current_save_as_schema20_worldgen2(uint8_t *blob,uint64_t bytes){
    if(blob==NULL||bytes<40u)return 0;
    test_put_u32_le(blob+8u,20u);
    return set_save_worldgen_version(blob,bytes,20u,ODG_WORLDGEN_VERSION_BATHYMETRY);
}

static int rewrite_current_save_as_schema21_worldgen3(uint8_t *blob,uint64_t bytes){
    if(blob==NULL||bytes<40u)return 0;
    test_put_u32_le(blob+8u,21u);
    return set_save_worldgen_version(blob,bytes,21u,ODG_WORLDGEN_VERSION_SAFE_TURRETS);
}

static int rewrite_current_save_as_schema24_worldgen4(uint8_t *blob,uint64_t bytes){
    const size_t header=40u;const size_t nest_off=offsetof(odg_world,fauna_nests)-offsetof(odg_world,seed);
    uint32_t resource_count=0u,turret_count=0u,i,j;uint8_t *resources,*turrets,*nest_bytes;
    uint64_t *new_ids=NULL,*old_ids=NULL;uint32_t payload_size;
    if(blob==NULL||bytes<header||test_get_u32_le(blob+8u)!=ODG_SAVE_SCHEMA_VERSION)return 0;
    resources=test_current_save_section(blob,bytes,TEST_SAVE_SECTION_RESOURCES,&resource_count);
    turrets=test_current_save_section(blob,bytes,TEST_SAVE_SECTION_TURRETS,&turret_count);
    if(resources==NULL||turrets==NULL)return 0;
    if(resource_count!=0u){
        new_ids=(uint64_t *)malloc((size_t)resource_count*sizeof(uint64_t));
        old_ids=(uint64_t *)malloc((size_t)resource_count*sizeof(uint64_t));
        if(new_ids==NULL||old_ids==NULL){free(new_ids);free(old_ids);return 0;}
    }
    for(i=0u;i<resource_count;++i){
        odg_resource_node r;uint64_t old_id;memcpy(&r,resources+(size_t)i*sizeof(r),sizeof(r));
        new_ids[i]=r.stable_id;
        if(r.procedural!=0u){old_id=r.stable_id&~ODG_RESOURCE_STABLE_PROCEDURAL_BIT;if(old_id==0u)old_id=(uint64_t)i+UINT64_C(1);}
        else old_id=UINT64_C(0x00100000)+(uint64_t)i;
        old_ids[i]=old_id;r.stable_id=old_id;
        /* SAVE24 kept completed work fields on depleted nodes indefinitely. */
        if(r.state==ODG_RESOURCE_STATE_DEPLETED){r.harvest_progress=17u;r.harvest_required=17u;r.harvest_grace=0u;}
        memcpy(resources+(size_t)i*sizeof(r),&r,sizeof(r));
    }
    for(i=0u;i<turret_count;++i){
        odg_turret t;memcpy(&t,turrets+(size_t)i*sizeof(t),sizeof(t));
        for(j=0u;j<resource_count;++j)if(t.target_resource_stable_id==new_ids[j]){t.target_resource_stable_id=old_ids[j];break;}
        memcpy(turrets+(size_t)i*sizeof(t),&t,sizeof(t));
    }
    nest_bytes=blob+header+nest_off;
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i){
        odg_fauna_nest n;memcpy(&n,nest_bytes+(size_t)i*sizeof(n),sizeof(n));
        for(j=0u;j<resource_count;++j)if(n.host_resource_stable_id==new_ids[j]){n.host_resource_stable_id=old_ids[j];break;}
        memcpy(nest_bytes+(size_t)i*sizeof(n),&n,sizeof(n));
    }
    free(new_ids);free(old_ids);
    test_put_u32_le(blob+8u,24u);
    if(!set_save_worldgen_version(blob,bytes,24u,ODG_WORLDGEN_VERSION_CANONICAL_RESOURCES))return 0;
    payload_size=test_get_u32_le(blob+20u);test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));return 1;
}

static int perturb_schema21_procedural_resource(uint8_t *blob,uint64_t bytes,const odg_resource_node *expected,
                                                uint32_t harvest_progress){
    const size_t header=40u;const size_t prefix=offsetof(odg_world,playable)-offsetof(odg_world,seed);
    uint32_t payload_size,count,i;uint8_t *cursor,*payload_end;uint64_t section_bytes;
    const size_t before_resources[2]={sizeof(odg_turret),sizeof(odg_world_pickup)};
    if(blob==NULL||expected==NULL||bytes<header||test_get_u32_le(blob+8u)!=21u)return 0;
    payload_size=test_get_u32_le(blob+20u);if(bytes!=header+(uint64_t)payload_size||payload_size<prefix)return 0;
    cursor=blob+header+prefix;payload_end=blob+header+payload_size;
    for(i=0u;i<2u;++i){
        if((size_t)(payload_end-cursor)<4u)return 0;
        count=test_get_u32_le(cursor);cursor+=4u;
        section_bytes=(uint64_t)count*(uint64_t)before_resources[i];
        if(section_bytes>(uint64_t)(payload_end-cursor))return 0;
        cursor+=(size_t)section_bytes;
    }
    if((size_t)(payload_end-cursor)<4u)return 0;
    count=test_get_u32_le(cursor);cursor+=4u;
    if((uint64_t)count*(uint64_t)sizeof(odg_resource_node)>(uint64_t)(payload_end-cursor))return 0;
    for(i=0u;i<count;++i){
        odg_resource_node r;uint8_t *entry=cursor+(size_t)i*sizeof(r);memcpy(&r,entry,sizeof(r));
        if(r.procedural==0u||r.chunk_x!=expected->chunk_x||r.chunk_z!=expected->chunk_z||
           r.chunk_ordinal!=expected->chunk_ordinal||r.kind!=expected->kind)continue;
        r.x+=7*ODG_FX_ONE;r.global_fx_x+=7*(int64_t)ODG_FX_ONE;r.harvest_progress=harvest_progress;
        r.harvest_required=harvest_progress+100u;r.harvest_grace=harvest_progress;r.harvest_actor=ODG_PLAYER_ID;
        if(r.species_id!=0u)r.species_id=0u; /* prove v4 species identity is reconstructed too */
        memcpy(entry,&r,sizeof(r));test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));return 1;
    }
    return 0;
}

static int rewrite_current_save_for_legacy_schema(uint8_t *blob,uint64_t *bytes,uint32_t schema){
    const size_t header=40u;
    const size_t prefix=offsetof(odg_world,playable)-offsetof(odg_world,seed);
    const size_t before_construction[4]={sizeof(odg_turret),sizeof(odg_world_pickup),sizeof(odg_resource_node),sizeof(odg_artifact)};
    uint32_t payload_size,i,count;uint8_t *cursor,*payload_end,*construction_start,*runtime_start;uint64_t section_bytes;size_t remove_bytes;
    if(blob==NULL||bytes==NULL||*bytes<header||schema<14u||schema>17u)return 0;
    payload_size=test_get_u32_le(blob+20u);
    if(*bytes!=header+(uint64_t)payload_size||payload_size<prefix)return 0;
    cursor=blob+header+prefix;payload_end=blob+header+payload_size;
    for(i=0u;i<4u;++i){
        if((size_t)(payload_end-cursor)<4u)return 0;
        count=test_get_u32_le(cursor);cursor+=4u;section_bytes=(uint64_t)count*(uint64_t)before_construction[i];
        if(section_bytes>(uint64_t)(payload_end-cursor))return 0;
        cursor+=(size_t)section_bytes;
    }
    /* Schema 18 inserted compact construction here. Remove that whole section to
     * reconstruct the exact schema-17-and-older section order. */
    construction_start=cursor;if((size_t)(payload_end-cursor)<4u)return 0;
    count=test_get_u32_le(cursor);remove_bytes=4u+(size_t)count*sizeof(odg_construction_block);
    if(remove_bytes>(size_t)(payload_end-construction_start))return 0;
    memmove(construction_start,construction_start+remove_bytes,(size_t)(payload_end-(construction_start+remove_bytes)));
    payload_size-=(uint32_t)remove_bytes;*bytes-=remove_bytes;payload_end-=remove_bytes;cursor=construction_start;
    /* Skip the chunk section, which immediately precedes the schema-17 runtime state. */
    if((size_t)(payload_end-cursor)<4u)return 0;
    count=test_get_u32_le(cursor);
    cursor+=4u;
    section_bytes=(uint64_t)count*(uint64_t)sizeof(odg_chunk_runtime);
    if(section_bytes>(uint64_t)(payload_end-cursor))return 0;
    cursor+=(size_t)section_bytes;
    if(schema<17u){
        runtime_start=cursor;
        if((size_t)(payload_end-cursor)<4u||test_get_u32_le(cursor)!=1u)return 0;
        remove_bytes=4u+sizeof(odg_persistent_runtime_state);
        if(remove_bytes>(size_t)(payload_end-runtime_start))return 0;
        memmove(runtime_start,runtime_start+remove_bytes,(size_t)(payload_end-(runtime_start+remove_bytes)));
        payload_size-=(uint32_t)remove_bytes;*bytes-=remove_bytes;
    }
    test_put_u32_le(blob+8u,schema);test_put_u32_le(blob+20u,payload_size);
    test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
    return 1;
}

static uint32_t find_death_cache(uint32_t owner) {
    uint32_t i;
    for(i=0u;i<g_odg.artifact_count;++i)
        if(g_odg_artifacts[i].active && g_odg_artifacts[i].owner_actor_id==owner && odg_artifact_is_death_cache(&g_odg_artifacts[i])) return i;
    return UINT32_MAX;
}

static uint64_t active_pickup_quantity(uint32_t type_id,uint64_t payload_id,int match_payload) {
    uint32_t i;uint64_t total=0u;
    for(i=0u;i<g_odg.pickup_count;++i){
        const odg_world_pickup *pickup=&g_odg_pickups[i];
        if(!pickup->active||pickup->stack.type_id!=type_id)continue;
        if(match_payload!=0&&pickup->stack.payload_id!=payload_id)continue;
        total+=(uint64_t)pickup->stack.quantity;
    }
    return total;
}

static void test_command_queue_save_guard(void){
    const size_t header=40u;
    const size_t command_offset=header+(offsetof(odg_world,commands)-offsetof(odg_world,seed));
    odg_command command,bad_command;odg_command_queue clean_queue,bad_queue;
    uint8_t *blob;uint64_t bytes,written=0u,bad_hash,live_hash;uint32_t payload_size;
    CHECK(odg_init(UINT64_C(0x434f4d4d414e4451),320u,180u)==ODG_STATUS_OK);
    odg_memset(&command,0,sizeof(command));command.struct_size=sizeof(command);command.type=ODG_COMMAND_SELECT_SLOT;command.arg0=1u;
    CHECK(odg_command_submit(&command,sizeof(command))==ODG_STATUS_OK);
    CHECK(g_odg.commands.count==1u&&odg_command_queue_state_validate_internal(&g_odg.commands)!=0);
    bad_command=command;bad_command.type=UINT32_C(0x7fffffff);
    CHECK(odg_command_submit(&bad_command,sizeof(bad_command))==ODG_STATUS_INVALID_ARGUMENT);

    bytes=odg_save_blob_size();blob=(uint8_t *)malloc((size_t)bytes);CHECK(blob!=NULL);
    if(blob==NULL)return;
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
    payload_size=test_get_u32_le(blob+20u);clean_queue=g_odg.commands;
    CHECK(command_offset+sizeof(odg_command_queue)<=header+(size_t)payload_size);

    /* A self-checksummed SAVE must not be able to turn the ring indices into an OOB
     * command read. Writer and loader both reject the same impossible state. */
    bad_queue=clean_queue;bad_queue.read_index=ODG_COMMAND_QUEUE_CAPACITY;
    g_odg.commands=bad_queue;CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
    g_odg.commands=clean_queue;memcpy(blob+command_offset,&bad_queue,sizeof(bad_queue));
    test_put_u64_le(blob+24u,bad_hash);test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
    live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);

    /* Unknown pending command types are not persisted as silent no-ops. Historical
     * bytes in already-consumed ring slots remain allowed; only the active ring matters. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    bad_queue=clean_queue;bad_queue.entries[bad_queue.read_index].type=UINT32_C(0x7fffffff);
    g_odg.commands=bad_queue;CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
    g_odg.commands=clean_queue;memcpy(blob+command_offset,&bad_queue,sizeof(bad_queue));
    test_put_u64_le(blob+24u,bad_hash);test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
    live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);

    /* A valid pending command survives save/load and executes exactly once. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
    g_odg.actors[ODG_PLAYER_ID].inventory.selected_slot=0u;g_odg.commands.count=0u;g_odg.commands.read_index=g_odg.commands.write_index;
    CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);CHECK(g_odg.commands.count==1u);
    odg_process_commands();CHECK(g_odg.commands.count==0u);CHECK(g_odg.actors[ODG_PLAYER_ID].inventory.selected_slot==1u);
    /* Input reaches simulation through clamping/normalization APIs, so SAVE may not
     * smuggle an impossible q15 vector around those entry points. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        odg_input clean_input=g_odg.input,bad_input=clean_input;
        const size_t input_offset=header+(offsetof(odg_world,input)-offsetof(odg_world,seed));
        bad_input.move_x_q15=ODG_Q15_ONE+1;g_odg.input=bad_input;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.input=clean_input;
        memcpy(blob+input_offset,&bad_input,sizeof(bad_input));test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }
    /* Interaction hold bookkeeping is also a state machine: no previous press means
     * there can be neither progress nor a fired hold action. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        const size_t ticks_offset=header+(offsetof(odg_world,interact_ticks)-offsetof(odg_world,seed));
        uint32_t clean_ticks=g_odg.interact_ticks;
        CHECK(g_odg.interact_pressed_prev==0u);g_odg.interact_ticks=1u;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.interact_ticks=clean_ticks;
        test_put_u32_le(blob+ticks_offset,1u);test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }

    /* Fixed-world counts are bounds before they are loop bounds. A corrupt live count
     * must fail before save validation can index outside a fixed/dynamic array. */
    {
        uint32_t clean_obstacles=g_odg.obstacle_count,clean_pickups=g_odg.pickup_count;
        g_odg.obstacle_count=ODG_MAX_OBSTACLES+1u;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);g_odg.obstacle_count=clean_obstacles;
        if(g_odg_pickup_capacity<UINT32_MAX){
            g_odg.pickup_count=g_odg_pickup_capacity+1u;
            CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);g_odg.pickup_count=clean_pickups;
        }
    }

    /* A current SAVE with an oversized obstacle loop bound is rejected even when both
     * transport checksum and deterministic hash have been forged to match it. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        const size_t field_offset=header+(offsetof(odg_world,obstacle_count)-offsetof(odg_world,seed));
        uint32_t clean=g_odg.obstacle_count;g_odg.obstacle_count=ODG_MAX_OBSTACLES+1u;bad_hash=odg_state_hash();g_odg.obstacle_count=clean;
        test_put_u32_le(blob+field_offset,ODG_MAX_OBSTACLES+1u);test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }

    /* Floating-origin cells are chunk aligned by construction. An unaligned origin would
     * make local caches and chunk authority describe two different worlds. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        const size_t field_offset=header+(offsetof(odg_world,world_origin_cell_x)-offsetof(odg_world,seed));
        int64_t clean=g_odg.world_origin_cell_x,bad=clean+1;g_odg.world_origin_cell_x=bad;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.world_origin_cell_x=clean;
        test_put_u64_le(blob+field_offset,(uint64_t)bad);test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }

    /* The opened panel is a persistent interaction state, not a free integer. It must
     * reference an active OPEN_UI artifact or use UINT32_MAX. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        const size_t field_offset=header+(offsetof(odg_world,opened_artifact_id)-offsetof(odg_world,seed));
        uint32_t clean=g_odg.opened_artifact_id,bad=g_odg.artifact_count+7u;g_odg.opened_artifact_id=bad;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.opened_artifact_id=clean;
        test_put_u32_le(blob+field_offset,bad);test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }

    /* Open Domain has no terminal match bit. Current SAVE24 rejects it; legacy SAVE23
     * explicitly migrates it away so an old finite-arena victory cannot freeze a world. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        uint8_t *suffix=test_current_save_suffix(blob,bytes);
        const size_t match_offset=offsetof(odg_world,match_over)-offsetof(odg_world,playable);
        const size_t winner_offset=offsetof(odg_world,winner_id)-offsetof(odg_world,playable);
        uint32_t clean_match=g_odg.match_over,clean_winner=g_odg.winner_id;
        CHECK(suffix!=NULL);g_odg.match_over=1u;g_odg.winner_id=0u;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
        g_odg.match_over=clean_match;g_odg.winner_id=clean_winner;
        if(suffix!=NULL){
            test_put_u32_le(suffix+match_offset,1u);test_put_u32_le(suffix+winner_offset,0u);test_put_u64_le(blob+24u,bad_hash);
            test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
        }
    }
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        uint8_t *suffix=test_current_save_suffix(blob,bytes);
        const size_t match_offset=offsetof(odg_world,match_over)-offsetof(odg_world,playable);
        const size_t winner_offset=offsetof(odg_world,winner_id)-offsetof(odg_world,playable);
        CHECK(suffix!=NULL);
        if(suffix!=NULL){
            test_put_u32_le(blob+8u,23u);test_put_u32_le(suffix+match_offset,1u);test_put_u32_le(suffix+winner_offset,0u);
            test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);CHECK(g_odg.match_over==0u&&g_odg.winner_id==UINT32_MAX);
        }
    }

    /* tick_accum_scaled is drained below one fixed tick before every public call returns.
     * Persisting a full tick here would create a hidden extra simulation step after load. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        const size_t field_offset=header+(offsetof(odg_world,tick_accum_scaled)-offsetof(odg_world,seed));
        uint64_t clean=g_odg.tick_accum_scaled;g_odg.tick_accum_scaled=ODG_TICK_US_NUM;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.tick_accum_scaled=clean;
        test_put_u64_le(blob+field_offset,ODG_TICK_US_NUM);test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }

    /* Actor-local deterministic state uses the same RNG contract and normalized facing
     * vectors as the simulation. A bad per-actor RNG must not lurk until a bot decision. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        const size_t actor_offset=header+(offsetof(odg_world,actors)-offsetof(odg_world,seed));
        odg_actor clean=g_odg.actors[1],bad=clean;bad.rng.stream&=~UINT64_C(1);g_odg.actors[1]=bad;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.actors[1]=clean;
        memcpy(blob+actor_offset+sizeof(odg_actor),&bad,sizeof(bad));test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }

    /* RNG validity is part of deterministic authority. A correct-looking SAVE cannot
     * carry an even PCG stream/corrupt cookie and defer the failure until a future roll. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        const size_t rng_offset=header+(offsetof(odg_world,rng)-offsetof(odg_world,seed));
        odm_rng clean=g_odg.rng,bad=clean;bad.stream&=~UINT64_C(1);g_odg.rng=bad;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.rng=clean;
        memcpy(blob+rng_offset,&bad,sizeof(bad));test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }

    /* SAVE24 cannot claim a transitional worldgen that should already have migrated.
     * Legacy v1 remains supported; modern worlds must be exactly WORLDGEN_CURRENT. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        uint32_t runtime_count=0u;uint8_t *runtime=test_current_save_section(blob,bytes,TEST_SAVE_SECTION_RUNTIME,&runtime_count);
        uint32_t clean=g_odg_persistent_runtime.worldgen_version;
        CHECK(runtime!=NULL&&runtime_count==1u);g_odg_persistent_runtime.worldgen_version=ODG_WORLDGEN_VERSION_SAFE_TURRETS;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
        g_odg_persistent_runtime.worldgen_version=clean;
        if(runtime!=NULL&&runtime_count==1u){
            test_put_u32_le(runtime+offsetof(odg_persistent_runtime_state,worldgen_version),ODG_WORLDGEN_VERSION_SAFE_TURRETS);
            test_put_u64_le(blob+24u,bad_hash);test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
        }
    }

    /* Camera/control vectors are persisted because camera-relative input uses them.
     * They therefore must be normalized states that the runtime itself can produce. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        uint8_t *suffix=test_current_save_suffix(blob,bytes);
        const size_t camera_x_offset=offsetof(odg_world,camera_dir_x_q15)-offsetof(odg_world,playable);
        const size_t camera_z_offset=offsetof(odg_world,camera_dir_z_q15)-offsetof(odg_world,playable);
        int32_t clean_x=g_odg.camera_dir_x_q15,clean_z=g_odg.camera_dir_z_q15;
        CHECK(suffix!=NULL);g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=0;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
        g_odg.camera_dir_x_q15=clean_x;g_odg.camera_dir_z_q15=clean_z;
        if(suffix!=NULL){
            test_put_u32_le(suffix+camera_x_offset,0u);test_put_u32_le(suffix+camera_z_offset,0u);test_put_u64_le(blob+24u,bad_hash);
            test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
        }
    }

    /* Persisted timers are phase state, not arbitrary uint32 values. The simulation
     * consumes/reset them at exact thresholds, so values beyond those thresholds are
     * impossible and must fail semantically even with a valid transport checksum/hash. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        const size_t actor_offset=header+(offsetof(odg_world,actors)-offsetof(odg_world,seed));
        odg_actor clean=g_odg.actors[1],bad=clean;bad.dash_cd=ODG_DASH_COOLDOWN_TICKS+1u;g_odg.actors[1]=bad;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.actors[1]=clean;
        memcpy(blob+actor_offset+sizeof(odg_actor),&bad,sizeof(bad));test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        uint32_t fauna_id;
        for(fauna_id=0u;fauna_id<ODG_FAUNA_MAX_ENTRIES;++fauna_id)if(g_odg.fauna[fauna_id].active)break;
        CHECK(fauna_id<ODG_FAUNA_MAX_ENTRIES);
        if(fauna_id<ODG_FAUNA_MAX_ENTRIES){
            const size_t fauna_offset=header+(offsetof(odg_world,fauna)-offsetof(odg_world,seed))+(size_t)fauna_id*sizeof(odg_fauna_entity);
            odg_fauna_entity clean=g_odg.fauna[fauna_id],bad=clean;bad.decision_ticks=ODG_FAUNA_DECISION_MAX_TICKS+1u;g_odg.fauna[fauna_id]=bad;
            CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.fauna[fauna_id]=clean;
            memcpy(blob+fauna_offset,&bad,sizeof(bad));test_put_u64_le(blob+24u,bad_hash);
            test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
        }
    }

    /* Weather is a permille value everywhere else in the engine; SAVE cannot inject a
     * value above 1000 and make render/ecology arithmetic operate outside its contract. */
    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);payload_size=test_get_u32_le(blob+20u);
    {
        const size_t field_offset=header+(offsetof(odg_world,weather_rain_permille)-offsetof(odg_world,seed));
        uint32_t clean=g_odg.weather_rain_permille;g_odg.weather_rain_permille=1001u;
        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();g_odg.weather_rain_permille=clean;
        test_put_u32_le(blob+field_offset,1001u);test_put_u64_le(blob+24u,bad_hash);
        test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));live_hash=odg_state_hash();
        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
    }
    free(blob);
}

int main(void) {
    uint32_t i;
    {
        odg_ffi_abi_info abi_info;
        uint64_t abi_required=0u;
        CHECK(odg_api_version()==ODG_API_VERSION);
        CHECK(odg_ffi_abi_query(ODG_FFI_ABI_VERSION,&abi_info,sizeof(abi_info),&abi_required)==ODG_STATUS_OK);
        CHECK(abi_required==sizeof(abi_info));
        CHECK(abi_info.ffi_abi_version==ODG_FFI_ABI_VERSION);
        CHECK(abi_info.engine_api_version==ODG_API_VERSION);
    }
    CHECK(odg_save_schema_supported(ODG_SAVE_SCHEMA_VERSION)!=0);
    test_command_queue_save_guard();
    /* Regression seeds that previously produced each spawn defect independently:
     * 43 overlapped an actor, 63 spawned fauna inside static resources, and 67 produced
     * overlapping fauna bodies. Initial ecology must satisfy habitat and physical world
     * occupancy after the first resource/turret streaming pass. */
    {
        static const uint64_t physical_seeds[]={UINT64_C(43),UINT64_C(63),UINT64_C(67)};uint32_t si;
        for(si=0u;si<(uint32_t)(sizeof(physical_seeds)/sizeof(physical_seeds[0]));++si){
            CHECK(odg_init(physical_seeds[si],320u,180u)==ODG_STATUS_OK);
            CHECK(g_odg.fauna_count>0u);
            CHECK(fauna_spawn_layout_is_physical()!=0);
        }
    }
    /* Runtime regressions: seed 4 previously let two ground bodies intersect, while
     * seed 6 let a flying bird snap its altitude to zero while horizontally inside a
     * mature tree. Check the evolving world, not only birth-time placement. */
    {
        static const uint64_t motion_seeds[]={UINT64_C(4),UINT64_C(6)};uint32_t si,tick;
        for(si=0u;si<(uint32_t)(sizeof(motion_seeds)/sizeof(motion_seeds[0]));++si){
            CHECK(odg_init(motion_seeds[si],320u,180u)==ODG_STATUS_OK);
            for(tick=0u;tick<600u;++tick){
                odg_step_ticks(1u);
                if((tick%30u)==0u)CHECK(fauna_runtime_layout_is_physical()!=0);
            }
        }
    }
    /* Airborne fauna does not reserve a fake ground disc several metres below it. The
     * same bird becomes a ground blocker again as soon as it is actually landed. */
    CHECK(odg_init(UINT64_C(6),320u,180u)==ODG_STATUS_OK);
    {
        uint32_t bird=UINT32_MAX;
        for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
            if(g_odg.fauna[i].active&&g_odg.fauna[i].species_id==ODG_FAUNA_SPECIES_ORCHARD_BIRD){bird=i;break;}
        }
        CHECK(bird!=UINT32_MAX);
        if(bird!=UINT32_MAX){
            uint32_t j;odg_fauna_entity *e=&g_odg.fauna[bird];
            for(j=0u;j<ODG_FAUNA_MAX_ENTRIES;++j)if(j!=bird)g_odg.fauna[j].active=0u;
            e->y_offset_fx=3*ODG_FX_ONE;
            CHECK(odg_fauna_bodies_clear_internal(e->x,e->z,ODG_FX_ONE/8,UINT32_MAX)!=0);
            e->y_offset_fx=0;
            CHECK(odg_fauna_bodies_clear_internal(e->x,e->z,ODG_FX_ONE/8,UINT32_MAX)==0);
        }
    }
    CHECK(odg_init(UINT64_C(0x45434f5359533231),320u,180u)==ODG_STATUS_OK);
    CHECK(odg_content_registry_validate()!=0);
    CHECK(odg_food_definition_count()>=2u);
    CHECK(odg_fluid_definition_count()>=1u);
    CHECK(odg_fluid_container_definition_count()>=1u);
    CHECK(odg_flora_species_count()>=1u);
    CHECK(odg_fauna_species_count()==7u);
    CHECK(odg_fauna_habitat_count()==odg_fauna_species_count());
    CHECK(odg_fauna_nesting_count()==3u);
    CHECK(odg_loot_table_count()>=6u);

    /* Worldgen and migration may choose different exact coordinates, but every resident
     * animal must be born into a habitat accepted by the same authoritative surface API. */
    CHECK(g_odg.fauna_count>0u);
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i) CHECK(habitat_accepts(&g_odg.fauna[i]));

    /* Terrain/water/biome are one deterministic surface. Water always has a defined
     * terrain height underneath; there is no presentation-only hole to fall through. */
    {
        int32_t x,z;uint32_t sampled=0u,water=0u;
        for(z=-32;z<=32;z+=8) for(x=-32;x<=32;x+=8) {
            odg_surface_sample a,b;
            CHECK(odg_world_surface_sample64((int64_t)x,(int64_t)z,&a,sizeof(a),NULL)==ODG_STATUS_OK);
            CHECK(odg_world_surface_sample64((int64_t)x,(int64_t)z,&b,sizeof(b),NULL)==ODG_STATUS_OK);
            CHECK(a.height_milli==b.height_milli && a.biome==b.biome && a.flags==b.flags);
            CHECK(a.height_milli>=-1550 && a.height_milli<=6200);
            CHECK(a.normal_y_q15>0);
            CHECK(a.moisture_permille<=1000u && a.rain_permille<=1000u);
            if((a.flags&ODG_SURFACE_FLAG_WATER)!=0u){CHECK(a.water_depth_milli>0u);++water;}
            ++sampled;
        }
        CHECK(sampled>0u);
        (void)water; /* water occurrence depends on seed/region, validity does not. */
    }

    /* Food and water are independent registries. Hydration can be restored by a potable
     * fluid or partially by food, and drinking consumes only the units actually needed. */
    {
        const odg_fluid_definition *water=odg_fluid_definition_internal(ODG_FLUID_WATER);
        const odg_fluid_container_definition *flask=odg_fluid_container_definition_internal(ODG_ITEM_WATER_FLASK);
        const odg_food_definition *apple=odg_food_definition_internal(ODG_ITEM_APPLE);
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];uint32_t used=0u;odg_item_stack food;
        CHECK(water!=NULL && (water->flags&ODG_FLUID_FLAG_POTABLE)!=0u && water->hydration_restore_per_unit>0u);
        CHECK(flask!=NULL && flask->capacity_units==100u && (flask->accepted_fluid_flags&ODG_FLUID_FLAG_POTABLE)!=0u);
        CHECK(apple!=NULL && apple->hydration_restore>0u);
        p->hydration_permille=500u;
        CHECK(odg_actor_drink_fluid_internal(ODG_PLAYER_ID,ODG_FLUID_WATER,10u,&used)!=0);
        CHECK(used==10u && p->hydration_permille==500u+10u*water->hydration_restore_per_unit);
        odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
        odg_memset(&food,0,sizeof(food));food.type_id=ODG_ITEM_APPLE;food.quantity=1u;food.flags=ODG_ITEM_FLAG_RESOURCE;
        CHECK(odg_inventory_add(&p->inventory,&food)!=0);
        CHECK((p->inventory.slots[0].flags&ODG_ITEM_FLAG_FOOD)!=0u);
        CHECK((p->inventory.slots[0].flags&ODG_ITEM_FLAG_RESOURCE)==0u);
        /* A legacy stack with stale static flags is healed and can merge with a current
         * stack instead of occupying a second slot forever. */
        p->inventory.slots[0].flags=ODG_ITEM_FLAG_RESOURCE;
        food.flags=ODG_ITEM_FLAG_FOOD;food.quantity=1u;
        CHECK(odg_inventory_add(&p->inventory,&food)!=0);
        CHECK(p->inventory.slots[0].quantity==2u);
        CHECK((p->inventory.slots[0].flags&ODG_ITEM_FLAG_FOOD)!=0u);
        CHECK((p->inventory.slots[0].flags&ODG_ITEM_FLAG_RESOURCE)==0u);
        p->inventory.slots[0].quantity=1u;
        p->satiety_permille=ODG_ACTOR_SATIETY_MAX;p->hydration_permille=100u;p->hp=p->max_hp;
        CHECK(odg_actor_consume_food_internal(ODG_PLAYER_ID,0u)!=0);
        CHECK(p->satiety_permille==ODG_ACTOR_SATIETY_MAX);
        CHECK(p->hydration_permille==100u+apple->hydration_restore);
    }

    /* Fruit seed recovery is lossless even with a completely occupied inventory. The
     * recovered seed falls to the ground instead of disappearing, while the fruit stack
     * commits exactly one consumed unit. */
    CHECK(odg_init(UINT64_C(0x534545444c4f5353),320u,180u)==ODG_STATUS_OK);
    {
        const odg_flora_species_definition *flora=odg_flora_species_internal(ODG_FLORA_SPECIES_APPLE_TREE);
        const odg_item_definition *wood_def=odg_item_definition_internal(ODG_ITEM_WOOD);
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];odg_item_stack apple,wood;odm_rng candidate,probe;
        uint32_t recovery_i,capacity;uint64_t before_seed;int rng_found=0;
        CHECK(flora!=NULL&&wood_def!=NULL&&flora->fruit_seed_recovery_permille>0u);
        odg_inventory_init(&p->inventory);capacity=odg_inventory_capacity(&p->inventory);CHECK(capacity>=2u);
        odg_memset(&apple,0,sizeof(apple));apple.type_id=ODG_ITEM_APPLE;apple.quantity=2u;apple.payload_id=ODG_FLORA_SPECIES_APPLE_TREE;
        CHECK(odg_item_stack_normalize_internal(&apple)!=0);p->inventory.slots[0]=apple;
        odg_memset(&wood,0,sizeof(wood));wood.type_id=ODG_ITEM_WOOD;wood.quantity=wood_def!=NULL?wood_def->max_stack:1u;
        CHECK(odg_item_stack_normalize_internal(&wood)!=0);
        for(recovery_i=1u;recovery_i<capacity;++recovery_i)p->inventory.slots[recovery_i]=wood;
        for(recovery_i=1u;recovery_i<4096u&&rng_found==0;++recovery_i){
            CHECK(odm_rng_seed_derived(&candidate,(uint64_t)recovery_i,UINT64_C(0x534545445f544553),1u)==ODM_STATUS_OK);
            probe=candidate;
            if(odg_rand_bounded(&probe,1000u)<flora->fruit_seed_recovery_permille){g_odg.ecology_rng=candidate;rng_found=1;}
        }
        CHECK(rng_found!=0);p->satiety_permille=ODG_ACTOR_SATIETY_MAX;p->hydration_permille=100u;p->hp=p->max_hp;
        before_seed=active_pickup_quantity(ODG_ITEM_APPLE_SEED,ODG_FLORA_SPECIES_APPLE_TREE,1);
        CHECK(odg_actor_consume_food_internal(ODG_PLAYER_ID,0u)!=0);
        CHECK(p->inventory.slots[0].type_id==ODG_ITEM_APPLE&&p->inventory.slots[0].quantity==1u);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_APPLE_SEED,ODG_MATERIAL_NONE)==0u);
        CHECK(active_pickup_quantity(ODG_ITEM_APPLE_SEED,ODG_FLORA_SPECIES_APPLE_TREE,1)==before_seed+1u);
    }

    /* Tool repair is one inventory transaction. An underfunded attempt cannot consume
     * partial material or alter durability; a funded attempt consumes the authoritative
     * quote and restores exactly the selected tool. */
    CHECK(odg_init(UINT64_C(0x5245504149525458),320u,180u)==ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];odg_item_stack axe,stone;odg_repair_quote quote;
        uint32_t station=UINT32_MAX,slot=UINT32_MAX,before_durability,before_stone;uint64_t required=0u;
        for(uint32_t ai=0u;ai<g_odg.artifact_count;++ai){
            if(g_odg_artifacts[ai].active&&g_odg_artifacts[ai].owner_actor_id==ODG_PLAYER_ID&&
               g_odg_artifacts[ai].item_type==ODG_ITEM_WORKBENCH){station=ai;break;}
        }
        CHECK(station!=UINT32_MAX);
        if(station!=UINT32_MAX){p->x=g_odg_artifacts[station].x+ODG_FX_ONE;p->z=g_odg_artifacts[station].z;}
        odg_inventory_init(&p->inventory);
        odg_memset(&axe,0,sizeof(axe));axe.type_id=ODG_ITEM_AXE;axe.quantity=1u;axe.material_tier=ODG_MATERIAL_STONE;
        axe.max_durability=odg_item_max_durability_internal(ODG_ITEM_AXE,ODG_MATERIAL_STONE);axe.durability=axe.max_durability/2u;axe.instance_id=odg_next_instance_id();
        CHECK(odg_item_stack_normalize_internal(&axe)!=0);CHECK(odg_inventory_add(&p->inventory,&axe)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_AXE,ODG_MATERIAL_STONE,&slot)!=0);p->inventory.selected_slot=slot;
        CHECK(odg_repair_quote_selected(ODG_PLAYER_ID,&quote,sizeof(quote),&required)==ODG_STATUS_OK);CHECK(quote.cost_quantity>0u);
        odg_memset(&stone,0,sizeof(stone));stone.type_id=ODG_ITEM_STONE;stone.quantity=quote.cost_quantity>1u?quote.cost_quantity-1u:0u;stone.material_tier=ODG_MATERIAL_STONE;
        if(stone.quantity!=0u)CHECK(odg_inventory_add(&p->inventory,&stone)!=0);
        before_durability=p->inventory.slots[slot].durability;before_stone=odg_inventory_total(&p->inventory,ODG_ITEM_STONE,ODG_MATERIAL_STONE);
        CHECK(odg_repair_selected(ODG_PLAYER_ID)==ODG_STATUS_INVALID_STATE);
        CHECK(p->inventory.slots[slot].durability==before_durability);CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_STONE,ODG_MATERIAL_STONE)==before_stone);
        stone.quantity=quote.cost_quantity-before_stone;CHECK(stone.quantity>0u);CHECK(odg_inventory_add(&p->inventory,&stone)!=0);
        before_stone=odg_inventory_total(&p->inventory,ODG_ITEM_STONE,ODG_MATERIAL_STONE);
        CHECK(odg_repair_selected(ODG_PLAYER_ID)==ODG_STATUS_OK);
        CHECK(p->inventory.slots[slot].durability==p->inventory.slots[slot].max_durability);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_STONE,ODG_MATERIAL_STONE)==before_stone-quote.cost_quantity);
    }

    /* A failed final harvest commit is not work. Keep progress and tool durability frozen
     * until loot/depletion can commit, then charge exactly the final work unit once. This
     * prevents repeated HOLD retries from grinding durability against a full progress bar. */
    CHECK(odg_init(UINT64_C(0x4841525654584143),320u,180u)==ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];odg_item_stack axe;uint32_t id,slot=UINT32_MAX;
        uint32_t before_durability,required=192u;odg_resource_node *r;uint64_t depleted_stable_id=0u;
        odg_inventory_init(&p->inventory);
        odg_memset(&axe,0,sizeof(axe));axe.type_id=ODG_ITEM_AXE;axe.quantity=1u;axe.material_tier=ODG_MATERIAL_IRON;
        axe.max_durability=odg_item_max_durability_internal(ODG_ITEM_AXE,ODG_MATERIAL_IRON);axe.durability=axe.max_durability;
        axe.instance_id=odg_next_instance_id();CHECK(odg_item_stack_normalize_internal(&axe)!=0);CHECK(odg_inventory_add(&p->inventory,&axe)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_AXE,ODG_MATERIAL_IRON,&slot)!=0);p->inventory.selected_slot=slot;
        id=g_odg.resource_count;CHECK(odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_STAGE_MATURE,0u,
                                                               p->x+ODG_FX_ONE,p->z,ODG_PLAYER_ID)!=0);
        CHECK(id<g_odg.resource_count);
        if(id<g_odg.resource_count){
            r=&g_odg_resources[id];r->fruit_count=0u;r->harvest_actor=ODG_PLAYER_ID;r->harvest_progress=required-1u;
            r->harvest_required=required;r->harvest_grace=ODG_HARVEST_GRACE_TICKS;
            before_durability=p->inventory.slots[slot].durability;
            r->yield_min=7u;r->yield_max=6u; /* deterministic final-commit rejection */
            CHECK(odg_resource_hold_tick(ODG_PLAYER_ID,id)==0);
            CHECK(r->harvest_progress==required-1u);CHECK(r->state==ODG_RESOURCE_STATE_AVAILABLE);
            CHECK(p->inventory.slots[slot].durability==before_durability);
            r->yield_max=7u;
            CHECK(odg_resource_hold_tick(ODG_PLAYER_ID,id)==2);
            CHECK(r->state==ODG_RESOURCE_STATE_DEPLETED);CHECK(r->harvest_progress==0u);
            CHECK(r->harvest_required==0u&&r->harvest_grace==ODG_RESOURCE_DEPLETED_VISUAL_TICKS);
            CHECK(p->inventory.slots[slot].durability+1u==before_durability);

            /* WORLDGEN5 makes a manual resource's array index an ephemeral paging handle,
             * never its identity. The depleted remnant lives briefly for presentation,
             * then stream compaction may recycle the array slot without recycling the
             * low-bit stable ID. Newly spawned flora must also invalidate an already-built
             * spatial index immediately rather than remaining intangible until another
             * subsystem happens to dirty the cache. */
            depleted_stable_id=r->stable_id;
            CHECK(depleted_stable_id!=0u&&(depleted_stable_id&ODG_RESOURCE_STABLE_PROCEDURAL_BIT)==0u);
            r->harvest_grace=1u;odg_resources_tick();CHECK(r->harvest_grace==0u);
            odg_resources_stream_refresh();
            {
                uint32_t ri,old_found=0u,new_id;int32_t sx=0,sz=0;int found_clear=0;
                uint64_t replacement_stable_id=0u;
                for(ri=0u;ri<g_odg.resource_count;++ri)
                    if(g_odg_resources[ri].active&&g_odg_resources[ri].stable_id==depleted_stable_id){old_found=1u;break;}
                CHECK(old_found==0u);
                /* This query deliberately rebuilds the spatial index before the spawn. */
                for(ri=0u;ri<64u;++ri){
                    int32_t candidate_x=p->x+(int32_t)(12u+(ri%8u)*3u)*ODG_FX_ONE;
                    int32_t candidate_z=p->z+(int32_t)(12u+(ri/8u)*3u)*ODG_FX_ONE;
                    if(!odg_resource_position_blocked(candidate_x,candidate_z,ODG_FX_ONE/4)){
                        sx=candidate_x;sz=candidate_z;found_clear=1;break;
                    }
                }
                CHECK(found_clear!=0);
                new_id=g_odg.resource_count;
                if(found_clear!=0)CHECK(odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_STAGE_MATURE,0u,
                                                                  sx,sz,ODG_PLAYER_ID)!=0);
                CHECK(new_id<g_odg.resource_count);
                if(new_id<g_odg.resource_count){
                    replacement_stable_id=g_odg_resources[new_id].stable_id;
                    CHECK(replacement_stable_id!=0u&&replacement_stable_id!=depleted_stable_id);
                    CHECK((replacement_stable_id&ODG_RESOURCE_STABLE_PROCEDURAL_BIT)==0u);
                    CHECK(odg_resource_position_blocked(sx,sz,ODG_FX_ONE/4)!=0);
                }
            }
        }
    }

    /* Hunger is deliberately slow and non-binary. One starvation period removes one HP,
     * not an entire life, preserving time to recover food. */
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];uint32_t hp=p->hp;
        p->satiety_permille=0u;p->starvation_accum=ODG_STARVATION_DAMAGE_TICKS-1u;
        odg_nutrition_tick();
        CHECK(p->hp+1u==hp);
        CHECK(p->hp>0u);
    }

    /* Manual felling and TALA harvesting share one authoritative completion path. A
     * turret must preserve fruit, respect life-stage yield, and never use a second hidden
     * resource economy that disagrees with hand harvesting. */
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];uint32_t id;uint64_t sid,before_apple,before_wood,after_wood;
        id=g_odg.resource_count;
        CHECK(odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_STAGE_MATURE,0u,
                                       p->x+50*ODG_FX_ONE,p->z,UINT32_MAX)!=0);
        if(id<g_odg.resource_count){
            odg_resource_node *r=&g_odg_resources[id];
            r->fruit_count=2u;if(r->fruit_capacity<2u)r->fruit_capacity=2u;sid=r->stable_id;
            before_apple=active_pickup_quantity(ODG_ITEM_APPLE,ODG_FLORA_SPECIES_APPLE_TREE,1);
            before_wood=active_pickup_quantity(ODG_ITEM_WOOD,0u,0);
            CHECK(odg_resource_turret_hit(sid,ODG_MATERIAL_IRON)==1);
            CHECK(odg_resource_turret_hit(sid,ODG_MATERIAL_IRON)==2);
            CHECK(r->state==ODG_RESOURCE_STATE_DEPLETED && r->fruit_count==0u);
            CHECK(active_pickup_quantity(ODG_ITEM_APPLE,ODG_FLORA_SPECIES_APPLE_TREE,1)==before_apple+2u);
            CHECK(active_pickup_quantity(ODG_ITEM_WOOD,0u,0)>before_wood);
        }

        id=g_odg.resource_count;before_wood=active_pickup_quantity(ODG_ITEM_WOOD,0u,0);
        CHECK(odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_STAGE_SEEDLING,0u,
                                       p->x+54*ODG_FX_ONE,p->z,UINT32_MAX)!=0);
        if(id<g_odg.resource_count){
            odg_resource_node *r=&g_odg_resources[id];sid=r->stable_id;
            CHECK(odg_resource_turret_hit(sid,ODG_MATERIAL_IRON)==1);
            CHECK(odg_resource_turret_hit(sid,ODG_MATERIAL_IRON)==2);
            after_wood=active_pickup_quantity(ODG_ITEM_WOOD,0u,0);
            CHECK(r->state==ODG_RESOURCE_STATE_DEPLETED);
            CHECK(after_wood>=before_wood+1u && after_wood<=before_wood+2u);
        }
    }

    /* Oviparous reproduction must create a persistent tree-hosted nest, not another
     * magical spawn. Force only the biological preconditions; the normal fauna tick owns
     * the actual pairing/nest creation. */
    {
        const odg_fauna_species_definition *bird=odg_fauna_species_internal(ODG_FAUNA_SPECIES_ORCHARD_BIRD);
        uint32_t female=find_active_fauna(ODG_FAUNA_SPECIES_ORCHARD_BIRD,ODG_FAUNA_SEX_FEMALE,0u);
        uint32_t male=find_active_fauna(ODG_FAUNA_SPECIES_ORCHARD_BIRD,ODG_FAUNA_SEX_MALE,0u);
        uint32_t before_count=g_odg.fauna_count,nid=UINT32_MAX;
        CHECK(bird!=NULL && bird->reproduction_mode==ODG_FAUNA_REPRO_EGG);
        CHECK(female!=UINT32_MAX && male!=UINT32_MAX);
        if(bird!=NULL && female!=UINT32_MAX && male!=UINT32_MAX){
            odg_fauna_entity *f=&g_odg.fauna[female],*m=&g_odg.fauna[male];
            uint32_t j;
            /* Existing spawn is already habitat-valid; place a mature host exactly there. */
            CHECK(odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_STAGE_MATURE,0u,f->x,f->z,UINT32_MAX)!=0);
            for(j=0u;j<ODG_FAUNA_MAX_ENTRIES;++j) if(g_odg.fauna[j].active && g_odg.fauna[j].species_id==ODG_FAUNA_SPECIES_ORCHARD_BIRD) g_odg.fauna[j].breeding_cooldown=ODG_TICK_RATE;
            f->breeding_cooldown=0u;m->breeding_cooldown=0u;
            f->satiety_permille=1000u;m->satiety_permille=1000u;
            f->hydration_permille=0u;m->hydration_permille=1000u;
            f->age_ticks=(uint64_t)bird->young_ticks+bird->juvenile_ticks+1u;
            m->age_ticks=(uint64_t)bird->young_ticks+bird->juvenile_ticks+1u;
            f->life_stage=ODG_FAUNA_STAGE_ADULT;m->life_stage=ODG_FAUNA_STAGE_ADULT;
            m->x=f->x;m->z=f->z;m->global_fx_x=f->global_fx_x;m->global_fx_z=f->global_fx_z;
            g_odg.tick=((g_odg.tick/ODG_TICK_RATE)+1u)*ODG_TICK_RATE;
            odg_fauna_tick();
            CHECK(g_odg.fauna_nest_count==0u);
            /* Hydration is a real reproduction precondition, not decorative metadata. */
            f->hydration_permille=1000u;f->breeding_cooldown=0u;m->breeding_cooldown=0u;
            g_odg.tick=((g_odg.tick/ODG_TICK_RATE)+1u)*ODG_TICK_RATE;
            odg_fauna_tick();
            CHECK(g_odg.fauna_nest_count==1u);
            for(j=0u;j<ODG_FAUNA_MAX_NESTS;++j) if(g_odg.fauna_nests[j].active){nid=j;break;}
            CHECK(nid!=UINT32_MAX);
            if(nid!=UINT32_MAX){
                odg_fauna_nest_snapshot snapshot;uint64_t required=0u;
                CHECK(g_odg.fauna_nests[nid].host_resource_stable_id!=0u);
                CHECK(g_odg.fauna_nests[nid].substrate==ODG_NEST_SUBSTRATE_TREE);
                {
                    uint32_t hi=UINT32_MAX;
                    for(j=0u;j<g_odg.resource_count;++j)if(g_odg_resources[j].active&&g_odg_resources[j].stable_id==g_odg.fauna_nests[nid].host_resource_stable_id){hi=j;break;}
                    CHECK(hi!=UINT32_MAX);
                    if(hi!=UINT32_MAX){
                        int64_t branch_d2=odg_dist2(g_odg.fauna_nests[nid].x,g_odg.fauna_nests[nid].z,g_odg_resources[hi].x,g_odg_resources[hi].z);
                        CHECK(branch_d2>(int64_t)(ODG_FX_ONE/4)*(ODG_FX_ONE/4));
                        CHECK(branch_d2<(int64_t)ODG_FX_ONE*ODG_FX_ONE);
                    }
                }
                CHECK(g_odg.fauna_nests[nid].egg_count>=bird->offspring_min && g_odg.fauna_nests[nid].egg_count<=bird->offspring_max);
                CHECK(odg_copy_fauna_nests(&snapshot,sizeof(snapshot),&required)==ODG_STATUS_OK);
                CHECK(snapshot.count==1u && required==sizeof(snapshot));

                /* Save/load restores the current schema, while frozen schema 14/15 nest
                 * bytes are explicitly converted to substrate-aware schema 16. */
                {
                    uint64_t bytes=odg_save_blob_size(),written=0u,hash=odg_state_hash();
                    uint8_t *blob=(uint8_t *)malloc((size_t)bytes);
                    uint8_t *legacy=(uint8_t *)malloc((size_t)bytes);
                    odg_resource_node canonical_resource;int have_canonical_resource=0;
                    uint32_t resource_i;
                    memset(&canonical_resource,0,sizeof(canonical_resource));
                    for(resource_i=0u;resource_i<g_odg.resource_count;++resource_i){
                        const odg_resource_node *r=&g_odg_resources[resource_i];
                        if(r->active&&r->procedural!=0u&&r->species_id!=0u){canonical_resource=*r;have_canonical_resource=1;break;}
                    }
                    CHECK(blob!=NULL && legacy!=NULL);
                    CHECK(have_canonical_resource!=0);
                    if(blob!=NULL && legacy!=NULL){
                        CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK && written==bytes);
                        /* An active nest and its two parents are one referential unit.
                         * A checksum/hash-correct file may not keep the nest while silently
                         * dropping one parent's backlink; runtime destruction always clears
                         * both sides together. */
                        {
                            uint32_t pa=g_odg.fauna_nests[nid].parent_a;uint64_t bad_hash,live_hash;
                            odg_fauna_entity original,bad;uint32_t payload_size=test_get_u32_le(blob+20u);
                            const size_t fauna_offset=40u+(offsetof(odg_world,fauna)-offsetof(odg_world,seed))+
                                                      (size_t)pa*sizeof(odg_fauna_entity);
                            CHECK(pa<ODG_FAUNA_MAX_ENTRIES);
                            if(pa<ODG_FAUNA_MAX_ENTRIES){
                                original=g_odg.fauna[pa];bad=original;bad.nest_id=UINT32_MAX;g_odg.fauna[pa]=bad;
                                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
                                g_odg.fauna[pa]=original;memcpy(blob+fauna_offset,&bad,sizeof(bad));
                                test_put_u64_le(blob+24u,bad_hash);test_put_u64_le(blob+32u,test_payload_checksum(blob+40u,payload_size));
                                live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
                                CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                            }
                        }
                        /* SAVE24 keeps legacy raw byte widths but serializes pure presentation/
                         * derived caches canonically empty. They are reconstructed or restarted
                         * after load and never participate in logical state identity. */
                        {
                            const size_t header=40u;
                            const size_t particle_off=offsetof(odg_world,particles)-offsetof(odg_world,seed);
                            const size_t hint_off=offsetof(odg_world,interaction_hint)-offsetof(odg_world,seed);
                            const size_t nav_off=offsetof(odg_world,bot_nav_edges)-offsetof(odg_world,playable);
                            uint8_t *saved_suffix=test_current_save_suffix(blob,bytes);
                            CHECK(test_bytes_are_zero(blob+header+particle_off,sizeof(g_odg.particles)));
                            CHECK(test_bytes_are_zero(blob+header+hint_off,sizeof(g_odg.interaction_hint)));
                            CHECK(saved_suffix!=NULL&&test_bytes_are_zero(saved_suffix+nav_off,sizeof(g_odg.bot_nav_edges)));
                            if(saved_suffix!=NULL){
                                uint64_t live_hash=odg_state_hash();
                                blob[header+particle_off]=UINT8_C(1);
                                test_put_u64_le(blob+32u,test_payload_checksum(blob+header,test_get_u32_le(blob+20u)));
                                CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
                                CHECK(odg_state_hash()==live_hash);
                                blob[header+particle_off]=UINT8_C(0);
                                test_put_u64_le(blob+32u,test_payload_checksum(blob+header,test_get_u32_le(blob+20u)));
                            }
                        }
                        CHECK(odg_save_schema_supported(17u)!=0u && odg_save_schema_supported(16u)!=0u &&
                              odg_save_schema_supported(15u)!=0u && odg_save_schema_supported(14u)!=0u);
                        CHECK(odg_save_schema_supported(13u)==0u && odg_save_schema_supported(18u)!=0u &&
                              odg_save_schema_supported(19u)!=0u && odg_save_schema_supported(20u)!=0u &&
                              odg_save_schema_supported(21u)!=0u && odg_save_schema_supported(22u)!=0u && odg_save_schema_supported(23u)!=0u &&
                              odg_save_schema_supported(24u)!=0u && odg_save_schema_supported(25u)!=0u &&
                              odg_save_schema_supported(26u)==0u);
                        /* API/ABI bytes are provenance only. Same data schema stays loadable. */
                        blob[12]^=UINT8_C(0x31);blob[16]^=UINT8_C(0x27);
                        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);
                        blob[12]^=UINT8_C(0x31);blob[16]^=UINT8_C(0x27);

                        /* SAVE23 historically included these transient bytes. Migration to
                         * SAVE24 must ignore/discard them and rebuild nav/hint from authority. */
                        {
                            const size_t header=40u;
                            const size_t particle_off=offsetof(odg_world,particles)-offsetof(odg_world,seed);
                            const size_t hint_off=offsetof(odg_world,interaction_hint)-offsetof(odg_world,seed);
                            const size_t nav_off=offsetof(odg_world,bot_nav_edges)-offsetof(odg_world,playable);
                            uint8_t *saved_suffix=test_current_save_suffix(blob,bytes);uint32_t edge_count=0u,ci;
                            CHECK(saved_suffix!=NULL);
                            if(saved_suffix!=NULL){
                                test_put_u32_le(blob+8u,23u);
                                blob[header+particle_off]=UINT8_C(0x7f);
                                blob[header+hint_off]=UINT8_C(0x6e);
                                saved_suffix[nav_off]=UINT8_C(0xff);
                                test_put_u64_le(blob+32u,test_payload_checksum(blob+header,test_get_u32_le(blob+20u)));
                                CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);
                                CHECK(g_odg.interaction_hint.struct_size==sizeof(g_odg.interaction_hint));
                                for(ci=0u;ci<ODG_CELL_COUNT;++ci)if(g_odg.bot_nav_edges[ci]!=0u)++edge_count;
                                CHECK(edge_count!=0u);
                                CHECK(test_bytes_are_zero((const uint8_t *)g_odg.particles,sizeof(g_odg.particles)));
                                CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                            }
                        }

                        /* Rejection after destructive parsing is a strict no-op. Corrupt
                         * only the expected hash (outside the checksummed payload), first
                         * move the live world away from the saved state, and preserve a
                         * runtime-only construction mode too. A non-transactional loader
                         * would return INVALID but leave the old save copied into g_odg. */
                        {
                            uint64_t rejected_live_hash;
                            if(g_odg.actors[ODG_PLAYER_ID].satiety_permille>0u)--g_odg.actors[ODG_PLAYER_ID].satiety_permille;
                            else ++g_odg.actors[ODG_PLAYER_ID].satiety_permille;
                            CHECK(odg_construction_set_shape_internal(ODG_PLAYER_ID,ODG_CONSTRUCTION_SHAPE_FLOOR)!=0);
                            rejected_live_hash=odg_state_hash();CHECK(rejected_live_hash!=hash);
                            blob[24]^=UINT8_C(0x01);
                            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
                            blob[24]^=UINT8_C(0x01);
                            CHECK(odg_state_hash()==rejected_live_hash);
                            CHECK(odg_construction_selected_shape_internal(ODG_PLAYER_ID)==ODG_CONSTRUCTION_SHAPE_FLOOR);
                            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);CHECK(odg_state_hash()==hash);
                        }

                        /* SAVE21/worldgen-v3 -> SAVE22/worldgen-v4 is explicit. Existing
                         * terrain + safe-turret rules remain; procedural resources become canonical. */
                        {
                            uint64_t legacy_bytes=bytes;
                            memcpy(legacy,blob,(size_t)bytes);
                            CHECK(rewrite_current_save_as_schema21_worldgen3(legacy,legacy_bytes)!=0);
                            if(have_canonical_resource){
                                uint32_t found=0u;
                                CHECK(perturb_schema21_procedural_resource(legacy,legacy_bytes,&canonical_resource,17u)!=0);
                                CHECK(odg_save_load(legacy,legacy_bytes)==ODG_STATUS_OK);
                                CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_CURRENT);
                                for(resource_i=0u;resource_i<g_odg.resource_count;++resource_i){
                                    const odg_resource_node *r=&g_odg_resources[resource_i];
                                    if(r->procedural==0u||r->chunk_x!=canonical_resource.chunk_x||r->chunk_z!=canonical_resource.chunk_z||
                                       r->chunk_ordinal!=canonical_resource.chunk_ordinal||r->kind!=canonical_resource.kind)continue;
                                    CHECK(r->global_fx_x==canonical_resource.global_fx_x&&r->global_fx_z==canonical_resource.global_fx_z);
                                    CHECK(r->stable_id==canonical_resource.stable_id&&r->species_id==canonical_resource.species_id);
                                    CHECK(r->harvest_progress==17u&&r->harvest_required==117u&&r->harvest_grace==17u);found=1u;break;
                                }
                                CHECK(found!=0u);
                            }else{
                                CHECK(odg_save_load(legacy,legacy_bytes)==ODG_STATUS_OK);
                                CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_CURRENT);
                            }
                        }
                        /* SAVE24/worldgen-v4 -> SAVE25/worldgen-v5 separates resource
                         * identity from the compactable resource array index. Procedural
                         * nodes move to the high-bit worldgen namespace; manual/bootstrap
                         * nodes receive monotonic low-bit IDs and any depleted historical
                         * work record becomes a bounded visual remnant. */
                        {
                            uint64_t legacy_bytes=bytes,max_manual=0u;uint32_t ri;
                            memcpy(legacy,blob,(size_t)bytes);
                            CHECK(rewrite_current_save_as_schema24_worldgen4(legacy,legacy_bytes)!=0);
                            CHECK(odg_save_load(legacy,legacy_bytes)==ODG_STATUS_OK);
                            CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_RESOURCE_ID_NAMESPACES);
                            for(ri=0u;ri<g_odg.resource_count;++ri){
                                const odg_resource_node *r=&g_odg_resources[ri];
                                if(r->procedural!=0u)CHECK((r->stable_id&ODG_RESOURCE_STABLE_PROCEDURAL_BIT)!=0u);
                                else{
                                    CHECK(r->stable_id!=0u&&(r->stable_id&ODG_RESOURCE_STABLE_PROCEDURAL_BIT)==0u);
                                    if(r->stable_id>max_manual)max_manual=r->stable_id;
                                }
                                if(r->state==ODG_RESOURCE_STATE_DEPLETED){
                                    CHECK(r->harvest_progress==0u&&r->harvest_required==0u&&r->turret_hits==0u);
                                    CHECK(r->harvest_grace==(r->procedural!=0u?0u:ODG_RESOURCE_DEPLETED_VISUAL_TICKS));
                                }
                            }
                            CHECK(g_odg.next_instance_id>max_manual);
                            CHECK(odg_save_write(legacy,legacy_bytes,&written)==ODG_STATUS_OK&&written==legacy_bytes);
                            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);CHECK(odg_state_hash()==hash);
                        }
                        /* SAVE20/worldgen-v2 traverses both explicit worldgen migrations. */
                        {
                            uint64_t legacy_bytes=bytes;
                            memcpy(legacy,blob,(size_t)bytes);
                            CHECK(rewrite_current_save_as_schema20_worldgen2(legacy,legacy_bytes)!=0);
                            CHECK(odg_save_load(legacy,legacy_bytes)==ODG_STATUS_OK);
                            CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_CURRENT);
                        }
                        /* SAVE17 can contain either v2 new worlds or v1 worlds migrated from
                         * pre-runtime schemas. v2 advances through v3 to v4; genuine v1 remains frozen. */
                        {
                            uint64_t legacy_bytes=bytes;
                            memcpy(legacy,blob,(size_t)bytes);
                            CHECK(rewrite_current_save_for_legacy_schema(legacy,&legacy_bytes,17u)!=0);
                            CHECK(set_save_worldgen_version(legacy,legacy_bytes,17u,ODG_WORLDGEN_VERSION_BATHYMETRY)!=0);
                            CHECK(odg_save_load(legacy,legacy_bytes)==ODG_STATUS_OK);
                            CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_CURRENT);
                            CHECK(odg_player_oxygen_permille()<=1000u);
                        }
                        {
                            uint64_t legacy_bytes=bytes;
                            memcpy(legacy,blob,(size_t)bytes);
                            CHECK(rewrite_current_save_for_legacy_schema(legacy,&legacy_bytes,17u)!=0);
                            CHECK(set_save_worldgen_version(legacy,legacy_bytes,17u,ODG_WORLDGEN_VERSION_LEGACY)!=0);
                            CHECK(odg_save_load(legacy,legacy_bytes)==ODG_STATUS_OK);
                            CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_LEGACY);
                        }
                        {
                            uint64_t legacy_bytes=bytes;
                            memcpy(legacy,blob,(size_t)bytes);
                            CHECK(rewrite_current_save_for_legacy_schema(legacy,&legacy_bytes,16u)!=0);
                            CHECK(odg_save_load(legacy,legacy_bytes)==ODG_STATUS_OK);
                            CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_LEGACY);
                            CHECK(odg_player_oxygen_permille()==1000u);
                        }
                        {
                            uint64_t legacy_bytes=bytes;
                            memcpy(legacy,blob,(size_t)bytes);
                            CHECK(rewrite_current_save_for_legacy_schema(legacy,&legacy_bytes,15u)!=0);
                            CHECK(rewrite_current_save_as_legacy_nesting(legacy,legacy_bytes,15u)!=0);
                            CHECK(odg_save_load(legacy,legacy_bytes)==ODG_STATUS_OK);
                            CHECK(g_odg.fauna_nests[nid].substrate==ODG_NEST_SUBSTRATE_TREE);
                            CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_LEGACY);
                        }
                        {
                            uint64_t legacy_bytes=bytes;
                            memcpy(legacy,blob,(size_t)bytes);
                            CHECK(rewrite_current_save_for_legacy_schema(legacy,&legacy_bytes,14u)!=0);
                            CHECK(rewrite_current_save_as_legacy_nesting(legacy,legacy_bytes,14u)!=0);
                            CHECK(odg_save_load(legacy,legacy_bytes)==ODG_STATUS_OK);
                            CHECK(g_odg.fauna_nests[nid].substrate==ODG_NEST_SUBSTRATE_TREE);
                            CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_LEGACY);
                        }
                        /* Restore the current SAVE22 state before testing exact current round-trip. */
                        CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);
                        CHECK(odg_state_hash()==hash);

                        {
                            uint32_t saved_player_hydration=g_odg.actors[ODG_PLAYER_ID].hydration_permille;
                            uint32_t saved_fauna_hydration=g_odg.fauna[female].hydration_permille;
                            g_odg.fauna_nests[nid].egg_count=0u;g_odg.fauna[female].satiety_permille=1u;
                            g_odg.fauna[female].hydration_permille=1u;g_odg.actors[ODG_PLAYER_ID].hydration_permille=2u;
                            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);
                            CHECK(odg_state_hash()==hash);
                            CHECK(g_odg.actors[ODG_PLAYER_ID].hydration_permille==saved_player_hydration);
                            CHECK(g_odg.fauna[female].hydration_permille==saved_fauna_hydration);
                            CHECK(g_odg.fauna_nests[nid].active!=0u && g_odg.fauna_nests[nid].egg_count>0u);
                        }

                        /* A visible tree-hosted nest cannot float after its physical host
                         * is felled. Destroy the host, verify parent links are cleared, then
                         * reload the frozen snapshot so the ordinary hatch test can proceed. */
                        {
                            uint64_t host=g_odg.fauna_nests[nid].host_resource_stable_id;uint32_t host_index=UINT32_MAX;
                            uint32_t pa=g_odg.fauna_nests[nid].parent_a,pb=g_odg.fauna_nests[nid].parent_b;
                            for(j=0u;j<g_odg.resource_count;++j)if(g_odg_resources[j].active&&g_odg_resources[j].stable_id==host){host_index=j;break;}
                            CHECK(host_index!=UINT32_MAX);
                            if(host_index!=UINT32_MAX){
                                g_odg_resources[host_index].state=ODG_RESOURCE_STATE_DEPLETED;
                                odg_fauna_tick();
                                CHECK(g_odg.fauna_nests[nid].active==0u);
                                if(pa<ODG_FAUNA_MAX_ENTRIES&&g_odg.fauna[pa].active)CHECK(g_odg.fauna[pa].nest_id==UINT32_MAX);
                                if(pb<ODG_FAUNA_MAX_ENTRIES&&g_odg.fauna[pb].active)CHECK(g_odg.fauna[pb].nest_id==UINT32_MAX);
                                CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);
                                CHECK(g_odg.fauna_nests[nid].active!=0u && g_odg.fauna_nests[nid].substrate==ODG_NEST_SUBSTRATE_TREE);
                            }
                        }
                    }
                    free(legacy);free(blob);
                }

                /* Hatching is a physical state transition, not egg deletion. Force every
                 * nearby body candidate into a temporary obstacle: the nest must retain
                 * its eggs and retry. Removing the blocker then permits the normal hatch. */
                {
                    uint32_t obstacle_count=g_odg.obstacle_count;uint32_t eggs_before=g_odg.fauna_nests[nid].egg_count;
                    CHECK(obstacle_count<ODG_MAX_OBSTACLES);
                    if(obstacle_count<ODG_MAX_OBSTACLES){
                        odg_obstacle *o=&g_odg.obstacles[obstacle_count];
                        odg_memset(o,0,sizeof(*o));o->x=g_odg.fauna_nests[nid].x;o->z=g_odg.fauna_nests[nid].z;
                        o->hx=5*ODG_FX_ONE;o->hz=5*ODG_FX_ONE;o->height_fx=ODG_FX_ONE;g_odg.obstacle_count=obstacle_count+1u;
                        g_odg.fauna_nests[nid].hatch_ticks=1u;odg_fauna_tick();
                        CHECK(g_odg.fauna_nests[nid].active!=0u);
                        CHECK(g_odg.fauna_nests[nid].egg_count==eggs_before);
                        CHECK(g_odg.fauna_nests[nid].hatch_ticks==ODG_TICK_RATE);
                        CHECK(g_odg.fauna_count==before_count);
                        g_odg.obstacle_count=obstacle_count;
                    }
                }
                g_odg.fauna_nests[nid].hatch_ticks=1u;odg_fauna_tick();
                CHECK(g_odg.fauna_nests[nid].active==0u);
                CHECK(g_odg.fauna_count>before_count);
            }
        }
    }

    /* Egg reproduction is composable: a non-flying field fowl uses the same lifecycle
     * but places a ground nest, proving EGG != TREE and BIRD != CAN_FLY. */
    {
        const odg_fauna_species_definition *fowl=odg_fauna_species_internal(ODG_FAUNA_SPECIES_FIELD_FOWL);
        const odg_fauna_nesting_definition *nesting=odg_fauna_nesting_internal(ODG_FAUNA_SPECIES_FIELD_FOWL);
        uint32_t female=find_active_fauna(ODG_FAUNA_SPECIES_FIELD_FOWL,ODG_FAUNA_SEX_FEMALE,0u);
        uint32_t male=find_active_fauna(ODG_FAUNA_SPECIES_FIELD_FOWL,ODG_FAUNA_SEX_MALE,0u),nid=UINT32_MAX,j;
        CHECK(fowl!=NULL && nesting!=NULL);
        CHECK(fowl!=NULL && (fowl->behavior_flags&ODG_FAUNA_BEHAVIOR_CAN_FLY)==0u && fowl->flight_speed_milli_per_s==0u);
        CHECK(nesting!=NULL && nesting->substrate_mask==ODG_NEST_SUBSTRATE_GROUND);
        CHECK(female!=UINT32_MAX && male!=UINT32_MAX);
        if(fowl!=NULL&&female!=UINT32_MAX&&male!=UINT32_MAX){
            odg_fauna_entity *f=&g_odg.fauna[female],*m=&g_odg.fauna[male];
            f->breeding_cooldown=0u;m->breeding_cooldown=0u;f->satiety_permille=m->satiety_permille=1000u;f->hydration_permille=m->hydration_permille=1000u;
            f->age_ticks=(uint64_t)fowl->young_ticks+fowl->juvenile_ticks+1u;m->age_ticks=f->age_ticks;f->life_stage=m->life_stage=ODG_FAUNA_STAGE_ADULT;
            m->x=f->x;m->z=f->z;m->global_fx_x=f->global_fx_x;m->global_fx_z=f->global_fx_z;
            g_odg.tick=((g_odg.tick/ODG_TICK_RATE)+1u)*ODG_TICK_RATE;odg_fauna_tick();
            for(j=0u;j<ODG_FAUNA_MAX_NESTS;++j)if(g_odg.fauna_nests[j].active&&g_odg.fauna_nests[j].species_id==ODG_FAUNA_SPECIES_FIELD_FOWL){nid=j;break;}
            CHECK(nid!=UINT32_MAX);
            if(nid!=UINT32_MAX){CHECK(g_odg.fauna_nests[nid].substrate==ODG_NEST_SUBSTRATE_GROUND);CHECK(g_odg.fauna_nests[nid].host_resource_stable_id==0u);g_odg.fauna_nests[nid].hatch_ticks=1u;odg_fauna_tick();CHECK(g_odg.fauna_nests[nid].active==0u);}
        }
    }

    /* Planting and irrigation share the same flora authority. A workstation physically
     * occupying a valid patch blocks planting, and a nearer stone cannot steal an
     * irrigation action from the actual plant behind it. */
    CHECK(odg_init(UINT64_C(0x504c414e54574154),320u,180u)==ODG_STATUS_OK);
    {
        const odg_flora_species_definition *flora=odg_flora_species_internal(ODG_FLORA_SPECIES_APPLE_TREE);
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
        odg_item_stack seed;uint32_t ai,ri;int found=0;int32_t tx=0,tz=0;
        CHECK(flora!=NULL);CHECK(g_odg.artifact_count>0u);
        /* Remove unrelated bootstrap blockers while choosing a deterministic valid soil
         * patch, then put one real workstation exactly on that patch. */
        for(ai=0u;ai<g_odg.artifact_count;++ai)g_odg_artifacts[ai].active=0u;
        for(ri=0u;ri<g_odg.resource_count;++ri)g_odg_resources[ri].active=0u;
        for(int32_t dz=-30;dz<=30&&found==0;++dz)for(int32_t dx=-30;dx<=30&&found==0;++dx){
            odg_surface_sample surface;int32_t cx=p->x+dx*ODG_FX_ONE,cz=p->z+dz*ODG_FX_ONE;
            if(!odg_environment_surface_local(cx,cz,&surface)||surface.moisture_permille<flora->min_growth_moisture_permille||
               (surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)continue;
            if(!odg_position_clear_internal(cx,cz,ODG_FX_ONE/8))continue;
            tx=cx;tz=cz;found=1;
        }
        if(found!=0){int64_t gx,gz;odg_local_fx_to_global_cell_internal(tx,tz,&gx,&gz);odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_FROM_ID(ODG_PLAYER_ID));}
        if(found!=0&&g_odg.artifact_count>0u){odg_artifact *artifact=&g_odg_artifacts[0];artifact->active=1u;artifact->local_resident=1u;artifact->x=tx;artifact->z=tz;odg_local_fx_to_global_fx_internal(tx,tz,&artifact->global_fx_x,&artifact->global_fx_z);odg_entities_spatial_mark_dirty();}
        CHECK(found!=0);
        if(found!=0){
            uint32_t before_resources;
            odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;p->inventory.selected_slot=0u;
            odg_memset(&seed,0,sizeof(seed));seed.type_id=ODG_ITEM_APPLE_SEED;seed.quantity=1u;seed.payload_id=ODG_FLORA_SPECIES_APPLE_TREE;
            CHECK(odg_inventory_add(&p->inventory,&seed)!=0);
            p->x=tx-2*ODG_FX_ONE;p->z=tz;p->face_x_q15=ODG_Q15_ONE;p->face_z_q15=0;
            CHECK(odg_ecology_plant_selected(ODG_PLAYER_ID)==0);
            CHECK(p->inventory.slots[0].type_id==ODG_ITEM_APPLE_SEED&&p->inventory.slots[0].quantity==1u);
            if(g_odg.artifact_count>0u){g_odg_artifacts[0].active=0u;}
            odg_entities_spatial_mark_dirty();
            before_resources=g_odg.resource_count;CHECK(odg_ecology_plant_selected(ODG_PLAYER_ID)!=0);
            CHECK(g_odg.resource_count==before_resources+1u);CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_APPLE_SEED,ODG_MATERIAL_NONE)==0u);
        }
    }
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];uint32_t ri,tree_id;uint32_t used=0u;
        for(ri=0u;ri<g_odg.resource_count;++ri)g_odg_resources[ri].active=0u;
        CHECK(g_odg.resource_count>0u);
        if(g_odg.resource_count>0u){
            odg_resource_node *stone=&g_odg_resources[0];
            odg_memset(stone,0,sizeof(*stone));stone->active=1u;stone->id=0u;stone->kind=ODG_RESOURCE_STONE;stone->state=ODG_RESOURCE_STATE_AVAILABLE;
            stone->local_resident=1u;stone->x=p->x+ODG_FX_ONE/2;stone->z=p->z;stone->soil_moisture_permille=0u;
            odg_local_fx_to_global_fx_internal(stone->x,stone->z,&stone->global_fx_x,&stone->global_fx_z);
            tree_id=g_odg.resource_count;
            CHECK(odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_STAGE_SEEDLING,0u,p->x+2*ODG_FX_ONE,p->z,ODG_PLAYER_ID)!=0);
            if(tree_id<g_odg.resource_count){
                odg_resource_node *tree=&g_odg_resources[tree_id];
                tree->soil_moisture_permille=100u;
                CHECK(odg_ecology_irrigate_nearest(ODG_PLAYER_ID,1u,&used)!=0);
                CHECK(used==1u);
                CHECK(stone->soil_moisture_permille==0u);
                CHECK(tree->soil_moisture_permille==110u);
            }
        }
    }

    /* Growth is physical, not a timer-only visual mutation. A seedling old enough to
     * enlarge must wait while a living body occupies the new footprint, then catch up
     * once clear. The next stage is likewise blocked by static infrastructure, proving
     * that existing small flora cannot later expand its collider through the world. */
    CHECK(odg_init(UINT64_C(0x47524f5754485354),320u,180u)==ODG_STATUS_OK);
    {
        const odg_flora_species_definition *flora=odg_flora_species_internal(ODG_FLORA_SPECIES_APPLE_TREE);
        uint32_t ri,ai,ti,ci,tree_id;int found=0;int32_t tx=0,tz=0;
        CHECK(flora!=NULL);
        for(ri=0u;ri<g_odg.resource_count;++ri)g_odg_resources[ri].active=0u;
        for(ai=0u;ai<g_odg.artifact_count;++ai)g_odg_artifacts[ai].active=0u;
        for(ti=0u;ti<g_odg.turret_count;++ti)g_odg_turrets[ti].active=0u;
        for(ci=0u;ci<g_odg_construction_count;++ci)g_odg_construction_blocks[ci].active=0u;
        for(ai=0u;ai<ODG_FAUNA_MAX_ENTRIES;++ai)g_odg.fauna[ai].active=0u;
        odg_entities_spatial_mark_dirty();
        if(flora!=NULL){
            int32_t old_radius=odg_flora_collision_radius_fx_internal(flora,ODG_FLORA_STAGE_OLD);
            for(int32_t dz=-30;dz<=30&&found==0;++dz)for(int32_t dx=-30;dx<=30&&found==0;++dx){
                odg_surface_sample surface;int32_t cx=g_odg.actors[ODG_PLAYER_ID].x+dx*ODG_FX_ONE;
                int32_t cz=g_odg.actors[ODG_PLAYER_ID].z+dz*ODG_FX_ONE;
                if(!odg_environment_surface_local(cx,cz,&surface)||(surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)continue;
                if(odg_chunk_procedural_turret_reserves_local_circle_internal(cx,cz,old_radius))continue;
                if(!odg_position_clear_internal(cx,cz,old_radius))continue;
                tx=cx;tz=cz;found=1;
            }
        }
        CHECK(found!=0);
        tree_id=g_odg.resource_count;
        if(found!=0&&flora!=NULL)CHECK(odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_STAGE_SEEDLING,0u,tx,tz,UINT32_MAX)!=0);
        if(tree_id<g_odg.resource_count&&flora!=NULL){
            odg_resource_node *tree=&g_odg_resources[tree_id];odg_actor *body=&g_odg.actors[1];
            int32_t seed_r=odg_flora_collision_radius_fx_internal(flora,ODG_FLORA_STAGE_SEEDLING);
            int32_t sapling_r=odg_flora_collision_radius_fx_internal(flora,ODG_FLORA_STAGE_SAPLING);
            int32_t young_r=odg_flora_collision_radius_fx_internal(flora,ODG_FLORA_STAGE_YOUNG);
            int32_t body_distance=body->radius+(seed_r+sapling_r)/2;
            CHECK(seed_r<sapling_r&&sapling_r<young_r);
            body->active=1u;body->hp=100u;body->local_resident=1u;body->x=tx+body_distance;body->z=tz;
            odg_local_fx_to_global_fx_internal(body->x,body->z,&body->global_fx_x,&body->global_fx_z);
            tree->soil_moisture_permille=1000u;tree->age_ticks=flora->seedling_ticks-ODG_TICK_RATE;
            g_odg.tick=(g_odg.tick/ODG_TICK_RATE)*ODG_TICK_RATE;odg_ecology_tick();
            CHECK(tree->age_ticks>=flora->seedling_ticks);
            CHECK(tree->flora_stage==ODG_FLORA_STAGE_SEEDLING);
            body->x=tx+8*ODG_FX_ONE;body->z=tz;odg_local_fx_to_global_fx_internal(body->x,body->z,&body->global_fx_x,&body->global_fx_z);
            odg_ecology_tick();CHECK(tree->flora_stage==ODG_FLORA_STAGE_SAPLING);
            CHECK(g_odg.artifact_count>0u);
            if(g_odg.artifact_count>0u){
                odg_artifact *blocker=&g_odg_artifacts[0];int32_t ar,static_distance;
                odg_memset(blocker,0,sizeof(*blocker));blocker->active=1u;blocker->id=0u;blocker->item_type=ODG_ITEM_WORKBENCH;
                blocker->local_resident=1u;ar=odg_artifact_collision_radius_fx_internal(blocker);
                static_distance=ar+(sapling_r+young_r)/2;blocker->x=tx+static_distance;blocker->z=tz;
                odg_local_fx_to_global_fx_internal(blocker->x,blocker->z,&blocker->global_fx_x,&blocker->global_fx_z);odg_entities_spatial_mark_dirty();
                tree->age_ticks=(uint64_t)flora->seedling_ticks+flora->sapling_ticks-ODG_TICK_RATE;
                odg_ecology_tick();CHECK(tree->flora_stage==ODG_FLORA_STAGE_SAPLING);
                blocker->x=tx+8*ODG_FX_ONE;blocker->z=tz;odg_local_fx_to_global_fx_internal(blocker->x,blocker->z,&blocker->global_fx_x,&blocker->global_fx_z);odg_entities_spatial_mark_dirty();
                odg_ecology_tick();CHECK(tree->flora_stage==ODG_FLORA_STAGE_YOUNG);
            }
        }
    }

    /* Ground fauna shares physical occupancy with the rest of the world. An artifact
     * directly in its path blocks movement and forces a new decision rather than letting
     * the animal ghost through gameplay geometry. */
    CHECK(odg_init(UINT64_C(0x4641554e41434f4c),320u,180u)==ODG_STATUS_OK);
    {
        odg_surface_sample steep_oracle;uint64_t steep_required=0u;
        CHECK(odg_world_surface_sample64(-60,1632,&steep_oracle,sizeof(steep_oracle),&steep_required)==ODG_STATUS_OK);
        CHECK((steep_oracle.flags&ODG_SURFACE_FLAG_STEEP)!=0u);
    }
    {
        uint32_t rabbit=find_active_fauna(ODG_FAUNA_SPECIES_MEADOW_RABBIT,ODG_FAUNA_SEX_FEMALE,0u);uint32_t ai;
        CHECK(rabbit!=UINT32_MAX);
        for(ai=0u;ai<g_odg.artifact_count;++ai)g_odg_artifacts[ai].active=0u;
        if(rabbit!=UINT32_MAX&&g_odg.artifact_count>0u){
            odg_fauna_entity *animal=&g_odg.fauna[rabbit];odg_artifact *block=&g_odg_artifacts[0];int32_t before_x=animal->x,before_z=animal->z;
            animal->face_x_q15=ODG_Q15_ONE;animal->face_z_q15=0;animal->decision_ticks=1000u;animal->satiety_permille=1000u;animal->hydration_permille=1000u;
            odg_memset(block,0,sizeof(*block));block->active=1u;block->local_resident=1u;block->x=animal->x+ODG_FX_ONE/4;block->z=animal->z;
            odg_local_fx_to_global_fx_internal(block->x,block->z,&block->global_fx_x,&block->global_fx_z);
            odg_entities_spatial_mark_dirty();
            odg_fauna_tick();
            CHECK(animal->x==before_x&&animal->z==before_z);
            CHECK(animal->decision_ticks==0u);
            block->active=0u;odg_entities_spatial_mark_dirty();

        }
    }

    /* Flight is a real 3D locomotion mode. The visible tree height is the same physical
     * authority queried by airspace; a bird approaching a mature crown climbs before
     * advancing horizontally instead of ghosting through it. */
    CHECK(odg_init(UINT64_C(0x4149525350414345),320u,180u)==ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];uint32_t air_i,tree_id;odg_resource_node *tree;
        odg_fauna_entity *bird=&g_odg.fauna[0];const odg_fauna_species_definition *bird_def=odg_fauna_species_internal(ODG_FAUNA_SPECIES_ORCHARD_BIRD);
        int32_t tx=p->x+6*ODG_FX_ONE,tz=p->z,before_x;uint32_t tree_height,required;
        for(air_i=0u;air_i<g_odg.resource_count;++air_i)g_odg_resources[air_i].active=0u;
        for(air_i=0u;air_i<g_odg.artifact_count;++air_i)g_odg_artifacts[air_i].active=0u;
        for(air_i=0u;air_i<g_odg.turret_count;++air_i)g_odg_turrets[air_i].active=0u;
        for(air_i=0u;air_i<g_odg_construction_count;++air_i)g_odg_construction_blocks[air_i].active=0u;
        tree_id=g_odg.resource_count;
        CHECK(odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_STAGE_MATURE,0u,tx,tz,UINT32_MAX)!=0);
        CHECK(tree_id<g_odg.resource_count);tree=&g_odg_resources[tree_id];odg_entities_spatial_mark_dirty();
        tree_height=odg_resource_physical_height_milli_internal(tree);CHECK(tree_height>=2000u);
        required=odg_airspace_required_offset_milli_internal(tree->x,tree->z,180u,260u);
        CHECK(required>=tree_height+200u);
        for(air_i=0u;air_i<ODG_FAUNA_MAX_ENTRIES;++air_i)g_odg.fauna[air_i].active=0u;
        CHECK(bird_def!=NULL);odg_memset(bird,0,sizeof(*bird));bird->active=1u;bird->local_resident=1u;bird->id=0u;
        bird->stable_id=UINT64_C(0x4149524249524401);bird->species_id=ODG_FAUNA_SPECIES_ORCHARD_BIRD;bird->family=ODG_FAUNA_FAMILY_BIRD;
        bird->life_stage=ODG_FAUNA_STAGE_ADULT;bird->sex=ODG_FAUNA_SEX_FEMALE;bird->max_hp=bird_def!=NULL?bird_def->max_health:28u;bird->hp=bird->max_hp;
        bird->age_ticks=bird_def!=NULL?(uint64_t)bird_def->young_ticks+bird_def->juvenile_ticks:0u;
        bird->satiety_permille=1000u;bird->hydration_permille=1000u;bird->nest_id=UINT32_MAX;bird->legacy_target_pickup_id=UINT32_MAX;
        bird->x=tree->x-ODG_FX_ONE/2;bird->z=tree->z;bird->face_x_q15=ODG_Q15_ONE;bird->face_z_q15=0;bird->state=2u;bird->decision_ticks=1000u;
        odg_local_fx_to_global_fx_internal(bird->x,bird->z,&bird->global_fx_x,&bird->global_fx_z);g_odg.fauna_count=1u;before_x=bird->x;
        odg_fauna_tick();
        CHECK(bird->x==before_x);CHECK(bird->y_offset_fx>0);
    }

    /* Hunting is a real weapon interaction, not one HP removed per rendered/input
     * frame. Damage comes from the same item attack profile as melee, strikes obey the
     * shared cooldown, durability wears only on an actual strike, and lethal hits emit
     * species loot through the loot table. */
    CHECK(odg_init(UINT64_C(0x48554e5450524f46),320u,180u)==ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];odg_fauna_entity *rabbit=&g_odg.fauna[0];
        const odg_fauna_species_definition *species=odg_fauna_species_internal(ODG_FAUNA_SPECIES_MEADOW_RABBIT);
        odg_item_stack knife;odg_item_stack *selected;uint32_t damage,before_durability;uint64_t before_meat;int result;
        uint32_t fi;
        CHECK(species!=NULL);
        for(fi=0u;fi<ODG_FAUNA_MAX_ENTRIES;++fi)g_odg.fauna[fi].active=0u;
        g_odg.fauna_count=1u;odg_memset(rabbit,0,sizeof(*rabbit));rabbit->active=1u;rabbit->local_resident=1u;
        rabbit->id=0u;rabbit->stable_id=UINT64_C(0x5241424249540001);rabbit->species_id=ODG_FAUNA_SPECIES_MEADOW_RABBIT;
        rabbit->family=ODG_FAUNA_FAMILY_MAMMAL;rabbit->life_stage=ODG_FAUNA_STAGE_ADULT;rabbit->sex=ODG_FAUNA_SEX_FEMALE;
        rabbit->max_hp=species!=NULL?species->max_health:55u;rabbit->hp=rabbit->max_hp;rabbit->x=p->x+ODG_FX_ONE;rabbit->z=p->z;
        rabbit->satiety_permille=1000u;rabbit->hydration_permille=1000u;rabbit->nest_id=UINT32_MAX;
        odg_local_fx_to_global_fx_internal(rabbit->x,rabbit->z,&rabbit->global_fx_x,&rabbit->global_fx_z);
        odg_inventory_init(&p->inventory);odg_memset(&knife,0,sizeof(knife));knife.type_id=ODG_ITEM_HUNTING_KNIFE;knife.quantity=1u;
        knife.material_tier=ODG_MATERIAL_STONE;CHECK(odg_inventory_add(&p->inventory,&knife)!=0);p->inventory.selected_slot=0u;
        selected=odg_inventory_selected(&p->inventory);CHECK(selected!=NULL);damage=odg_item_attack_damage_internal(selected);CHECK(damage>1u);
        before_durability=selected!=NULL?selected->durability:0u;before_meat=active_pickup_quantity(ODG_ITEM_RAW_MEAT,0u,0);
        result=odg_fauna_hunt(ODG_PLAYER_ID,0u);CHECK(result==1);CHECK(rabbit->hp==rabbit->max_hp-damage);
        CHECK(p->melee_cooldown_ticks==ODG_MELEE_COOLDOWN_TICKS);CHECK(selected->durability+1u==before_durability);
        CHECK(odg_fauna_hunt(ODG_PLAYER_ID,0u)==0);CHECK(rabbit->hp==rabbit->max_hp-damage);
        while(rabbit->active){p->melee_cooldown_ticks=0u;result=odg_fauna_hunt(ODG_PLAYER_ID,0u);CHECK(result==1||result==2);}
        CHECK(rabbit->hp==0u);CHECK(active_pickup_quantity(ODG_ITEM_RAW_MEAT,0u,0)>before_meat);
    }

    /* Deployed turrets participate in the same physical occupancy authority used by
     * actor movement and artifact/flora placement. A turret cannot become a visual-only
     * object that actors walk through or new objects overlap. */
    CHECK(odg_init(UINT64_C(0x545552524554434f),320u,180u)==ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];uint32_t ti;int found=0;int32_t tx=0,tz=0;
        for(ti=0u;ti<g_odg.resource_count;++ti)g_odg_resources[ti].active=0u;
        for(ti=0u;ti<g_odg.artifact_count;++ti)g_odg_artifacts[ti].active=0u;
        for(ti=0u;ti<g_odg.turret_count;++ti)g_odg_turrets[ti].active=0u;
        CHECK(g_odg.turret_count>0u);
        for(int32_t dz=-8;dz<=8&&found==0;++dz)for(int32_t dx=-8;dx<=8&&found==0;++dx){
            int32_t cx=p->x+dx*ODG_FX_ONE/2,cz=p->z+dz*ODG_FX_ONE/2;
            if(odg_position_clear_internal(cx,cz,ODG_FX_ONE/4)){tx=cx;tz=cz;found=1;}
        }
        CHECK(found!=0);
        if(found!=0&&g_odg.turret_count>0u){
            odg_turret *turret=&g_odg_turrets[0];
            turret->active=1u;turret->local_resident=1u;turret->carried_by=ODG_TURRET_NONE;turret->x=tx;turret->z=tz;
            odg_local_fx_to_global_fx_internal(tx,tz,&turret->global_fx_x,&turret->global_fx_z);
            odg_entities_spatial_mark_dirty();
            CHECK(odg_position_clear_internal(tx,tz,ODG_FX_ONE/4)==0);
            turret->active=0u;odg_entities_spatial_mark_dirty();
            CHECK(odg_position_clear_internal(tx,tz,ODG_FX_ONE/4)!=0);
        }
    }

    /* Death without recovery equipment drops every ordinary stack but preserves protected
     * bootstrap infrastructure. The drop path is transactional per stack and payload/save
     * identity remains valid after the actor is dead. */
    CHECK(odg_init(UINT64_C(0x444541544844524f),320u,180u)==ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];odg_item_stack wood,bench;uint64_t before_wood;
        odg_inventory_init(&p->inventory);
        odg_memset(&wood,0,sizeof(wood));wood.type_id=ODG_ITEM_WOOD;wood.quantity=7u;CHECK(odg_inventory_add(&p->inventory,&wood)!=0);
        odg_memset(&bench,0,sizeof(bench));bench.type_id=ODG_ITEM_WORKBENCH;bench.quantity=1u;CHECK(odg_inventory_add(&p->inventory,&bench)!=0);
        before_wood=active_pickup_quantity(ODG_ITEM_WOOD,0u,0);
        odg_actor_apply_damage_internal(ODG_PLAYER_ID,UINT32_MAX,p->hp,ODG_DEATH_STARVATION);
        CHECK(p->hp==0u);CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_NONE)==0u);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_WORKBENCH,ODG_MATERIAL_NONE)==1u);
        CHECK(active_pickup_quantity(ODG_ITEM_WOOD,0u,0)==before_wood+7u);CHECK(odg_save_identity_validate_internal()!=0);
    }

    /* Death with an equipped backpack produces a long-lived recovery container and
     * recovery is transactional. This keeps one death from erasing accumulated effort. */
    CHECK(odg_init(UINT64_C(0x4445415448434143),320u,180u)==ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];odg_item_stack wood,stone,iron,ammo,bench;uint32_t cache;
        p->inventory.equipped_backpack_type=ODG_ITEM_BACKPACK;p->inventory.slot_count=ODG_INVENTORY_MAX_SLOTS;
        /* Occupy every base slot first so the protected workbench deliberately lives in
         * an expanded slot. Death must compact it into base capacity instead of hiding it. */
        odg_memset(&wood,0,sizeof(wood));wood.type_id=ODG_ITEM_WOOD;wood.quantity=7u;
        odg_memset(&stone,0,sizeof(stone));stone.type_id=ODG_ITEM_STONE;stone.quantity=3u;
        odg_memset(&iron,0,sizeof(iron));iron.type_id=ODG_ITEM_IRON;iron.quantity=2u;
        odg_memset(&ammo,0,sizeof(ammo));ammo.type_id=ODG_ITEM_AMMO;ammo.quantity=5u;
        odg_memset(&bench,0,sizeof(bench));bench.type_id=ODG_ITEM_WORKBENCH;bench.quantity=1u;
        CHECK(odg_inventory_add(&p->inventory,&wood)!=0);
        CHECK(odg_inventory_add(&p->inventory,&stone)!=0);
        CHECK(odg_inventory_add(&p->inventory,&iron)!=0);
        CHECK(odg_inventory_add(&p->inventory,&ammo)!=0);
        CHECK(odg_inventory_add(&p->inventory,&bench)!=0);
        CHECK(p->inventory.slots[ODG_INVENTORY_BASE_SLOTS].type_id==ODG_ITEM_WORKBENCH);
        odg_actor_apply_damage_internal(ODG_PLAYER_ID,UINT32_MAX,p->hp,ODG_DEATH_STARVATION);
        CHECK(p->hp==0u);
        CHECK(odg_inventory_capacity(&p->inventory)==ODG_INVENTORY_BASE_SLOTS);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_WORKBENCH,ODG_MATERIAL_NONE)==1u);
        CHECK(p->inventory.slots[0].type_id==ODG_ITEM_WORKBENCH);
        cache=find_death_cache(ODG_PLAYER_ID);CHECK(cache!=UINT32_MAX);
        if(cache!=UINT32_MAX){
            CHECK(g_odg_artifacts[cache].aux_tick>g_odg.tick);
            p->hp=p->max_hp;p->x=g_odg_artifacts[cache].x;p->z=g_odg_artifacts[cache].z;
            CHECK(odg_artifact_recover_death_cache(ODG_PLAYER_ID,cache)!=0);
            CHECK(p->inventory.equipped_backpack_type==ODG_ITEM_BACKPACK);
            CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_NONE)>=7u);
            CHECK(g_odg_artifacts[cache].active==0u);

            /* If the player equips a replacement before returning, the old recovery
             * container is an item they still own. Recovering its cargo must not
             * silently delete that second piece of equipment. */
            odg_actor_apply_damage_internal(ODG_PLAYER_ID,UINT32_MAX,p->hp,ODG_DEATH_STARVATION);
            cache=find_death_cache(ODG_PLAYER_ID);CHECK(cache!=UINT32_MAX);
            if(cache!=UINT32_MAX){
                odg_item_stack replacement;uint32_t before_backpacks;
                p->hp=p->max_hp;p->x=g_odg_artifacts[cache].x;p->z=g_odg_artifacts[cache].z;
                odg_memset(&replacement,0,sizeof(replacement));replacement.type_id=ODG_ITEM_BACKPACK;replacement.quantity=1u;
                CHECK(odg_inventory_add(&p->inventory,&replacement)!=0);
                CHECK(odg_inventory_equip_first_expander_internal(&p->inventory)!=0);
                before_backpacks=odg_inventory_total(&p->inventory,ODG_ITEM_BACKPACK,ODG_MATERIAL_NONE);
                CHECK(odg_artifact_recover_death_cache(ODG_PLAYER_ID,cache)!=0);
                CHECK(p->inventory.equipped_backpack_type==ODG_ITEM_BACKPACK);
                CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_BACKPACK,ODG_MATERIAL_NONE)==before_backpacks+1u);
                CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_NONE)>=7u);
                CHECK(g_odg_artifacts[cache].active==0u);
            }
        }
    }

    /* SAVE22 historically allowed a conquered natural turret to become portable while
     * retaining its deterministic high-bit procedural identity. SAVE23 reserves that
     * namespace exclusively for worldgen turrets. Build a transport-valid historical
     * blob and prove migration rewrites the dormant primary record and the carried
     * payload handle to the same sequential identity without losing turret state. */
    CHECK(odg_init(UINT64_C(0x50524f435f504943),320u,180u)==ODG_STATUS_OK);
    {
        const size_t header=40u;
        const size_t prefix=offsetof(odg_world,playable)-offsetof(odg_world,seed);
        const size_t player_stack0_offset=header+
            (offsetof(odg_world,actors)-offsetof(odg_world,seed))+
            (size_t)ODG_PLAYER_ID*sizeof(odg_actor)+offsetof(odg_actor,inventory)+
            offsetof(odg_inventory,slots);
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];uint32_t ti,target=UINT32_MAX,turret_count,pickup_count=0u,payload_size;
        odg_turret historical;odg_item_stack carried;uint64_t bytes,written=0u,old_id,new_id;uint8_t *blob;
        odg_inventory_init(&p->inventory);odg_turrets_stream_refresh();
        for(ti=0u;ti<g_odg.turret_count;++ti){
            const odg_turret *t=&g_odg_turrets[ti];
            if(t->active&&t->local_resident!=0u&&t->procedural!=0u&&
               (t->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u){target=ti;break;}
        }
        CHECK(target!=UINT32_MAX);
        if(target!=UINT32_MAX){
            historical=g_odg_turrets[target];old_id=historical.instance_id;
            historical.active=0u;historical.procedural=0u;historical.source_chunk_x=0;historical.source_chunk_z=0;
            historical.carried_by=ODG_TURRET_NONE;historical.target_kind=ODG_TURRET_TARGET_NONE;
            historical.aim_ticks=0u;historical.last_target_cell=UINT32_MAX;
            historical.target_global_cell_x=INT64_MIN;historical.target_global_cell_z=INT64_MIN;
            historical.target_actor_id=UINT32_MAX;historical.target_resource_stable_id=0u;
            odg_memset(&carried,0,sizeof(carried));carried.type_id=ODG_ITEM_TURRET;carried.quantity=1u;
            carried.material_tier=historical.material_tier;carried.flags=ODG_ITEM_FLAG_ARTIFACT;
            carried.instance_id=old_id;carried.payload_id=(uint64_t)target+UINT64_C(1);
            CHECK(odg_item_stack_normalize_internal(&carried)!=0);
            CHECK(carried.instance_id==old_id&&carried.payload_id==(uint64_t)target+UINT64_C(1));

            /* A historical pickup permanently retired the natural worldgen slot. Keep
             * that chunk override in the blob while the dynamic turret record below is
             * patched into the pre-SAVE23 portable representation. */
            odg_chunk_mark_procedural_turret_removed(g_odg_turrets[target].source_chunk_x,
                                                     g_odg_turrets[target].source_chunk_z);
            bytes=odg_save_blob_size();blob=(uint8_t *)malloc((size_t)bytes);CHECK(blob!=NULL);
            if(blob!=NULL){
                uint8_t *turret_base,*pickup_header,*pickup_base=NULL;CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                payload_size=test_get_u32_le(blob+20u);CHECK(bytes==header+(uint64_t)payload_size);
                CHECK(prefix+header+4u<=bytes);turret_count=test_get_u32_le(blob+header+prefix);
                CHECK(target<turret_count);turret_base=blob+header+prefix+4u;
                if(target<turret_count&&player_stack0_offset+sizeof(carried)<=bytes&&
                   header+prefix+4u+(size_t)turret_count*sizeof(odg_turret)<=bytes){
                    pickup_header=turret_base+(size_t)turret_count*sizeof(odg_turret);
                    CHECK((size_t)(blob+bytes-pickup_header)>=4u);pickup_count=test_get_u32_le(pickup_header);
                    pickup_base=pickup_header+4u;
                    memcpy(turret_base+(size_t)target*sizeof(odg_turret),&historical,sizeof(historical));
                    memcpy(blob+player_stack0_offset,&carried,sizeof(carried));
                    if(pickup_count!=0u&&pickup_base+sizeof(odg_world_pickup)<=blob+bytes){
                        odg_world_pickup stale;memcpy(&stale,pickup_base,sizeof(stale));
                        stale.active=0u;stale.age_ticks=UINT32_C(12345); /* historical dead-slot residue */
                        memcpy(pickup_base,&stale,sizeof(stale));
                    }
                    test_put_u32_le(blob+8u,22u);
                    test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));

                    CHECK(odg_reset(UINT64_C(0x5341564532334d47))==ODG_STATUS_OK);
                    CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);
                    CHECK(target<g_odg.turret_count);
                    if(target<g_odg.turret_count){
                        const odg_turret *migrated=&g_odg_turrets[target];
                        const odg_item_stack *handle=&g_odg.actors[ODG_PLAYER_ID].inventory.slots[0];
                        new_id=migrated->instance_id;
                        CHECK(migrated->active==0u&&migrated->procedural==0u);
                        CHECK(new_id!=0u&&new_id!=old_id&&(new_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)==0u);
                        CHECK(migrated->local_resident==0u&&migrated->x==0&&migrated->z==0&&
                              migrated->global_fx_x==0&&migrated->global_fx_z==0);
                        CHECK(migrated->material_tier==historical.material_tier);
                        CHECK(migrated->ammo==historical.ammo&&migrated->max_ammo==historical.max_ammo);
                        CHECK(migrated->mode==historical.mode&&migrated->owner==historical.owner);
                        CHECK(handle->type_id==ODG_ITEM_TURRET&&handle->quantity==1u);
                        CHECK(handle->payload_id==(uint64_t)target+UINT64_C(1));
                        CHECK(handle->instance_id==new_id&&handle->material_tier==historical.material_tier);
                        CHECK(g_odg.next_instance_id>new_id&&g_odg.next_instance_id<ODG_INSTANCE_ID_PROCEDURAL_BIT);
                        if(pickup_count!=0u){
                            const odg_world_pickup *dead=&g_odg_pickups[0];
                            CHECK(dead->active==0u&&dead->id==0u&&dead->x==0&&dead->z==0&&
                                  dead->global_fx_x==0&&dead->global_fx_z==0&&dead->local_resident==0u&&
                                  dead->pickup_cd==0u&&dead->age_ticks==0u&&dead->lifetime_ticks==0u&&
                                  dead->stack.type_id==ODG_ITEM_NONE&&dead->stack.quantity==0u);
                        }
                        CHECK(odg_save_identity_validate_internal()!=0);
                        CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_BUFFER_TOO_SMALL);
                    }
                }
                free(blob);
            }
        }
    }

    /* SAVE23 is canonical, not merely checksummed. The writer refuses impossible live
     * stacks, and the loader independently rejects a malicious blob whose checksum AND
     * state hash have been recomputed to match an overstacked inventory. Rejection must
     * remain transactional: the valid live world is byte/logically unchanged. */
    CHECK(odg_init(UINT64_C(0x5341564553454d41),320u,180u)==ODG_STATUS_OK);
    {
        const size_t header=40u;
        const size_t player_stack0_offset=header+
            (offsetof(odg_world,actors)-offsetof(odg_world,seed))+
            (size_t)ODG_PLAYER_ID*sizeof(odg_actor)+offsetof(odg_actor,inventory)+
            offsetof(odg_inventory,slots);
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];odg_item_stack wood,original,bad;
        uint64_t bytes,written=0u,valid_hash,malicious_hash,live_before_reject;uint8_t *blob;
        uint32_t payload_size;
        odg_inventory_init(&p->inventory);
        odg_memset(&wood,0,sizeof(wood));wood.type_id=ODG_ITEM_WOOD;wood.quantity=99u;
        CHECK(odg_item_stack_normalize_internal(&wood)!=0);p->inventory.slots[0]=wood;original=wood;
        bytes=odg_save_blob_size();blob=(uint8_t *)malloc((size_t)bytes);CHECK(blob!=NULL);
        if(blob!=NULL){
            CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
            valid_hash=odg_state_hash();payload_size=test_get_u32_le(blob+20u);
            CHECK(player_stack0_offset+sizeof(odg_item_stack)<=header+(size_t)payload_size);

            bad=original;bad.quantity=100u;p->inventory.slots[0]=bad;
            CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
            malicious_hash=odg_state_hash();p->inventory.slots[0]=original;

            /* This blob is self-consistent at the transport/hash layers. Only the new
             * semantic validator can distinguish it from a valid current save. */
            memcpy(blob+player_stack0_offset,&bad,sizeof(bad));
            test_put_u64_le(blob+24u,malicious_hash);
            test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
            live_before_reject=odg_state_hash();CHECK(live_before_reject==valid_hash);
            CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
            CHECK(odg_state_hash()==live_before_reject);

            /* Material, fluid payload, hidden capacity and unique identity are separate
             * invariants; exercise them on the write side without needing corrupt blobs. */
            p->inventory.slots[0]=original;p->inventory.slots[0].material_tier=ODG_MATERIAL_IRON;
            CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
            p->inventory.slots[0]=original;odg_memset(&p->inventory.slots[ODG_INVENTORY_BASE_SLOTS],0,sizeof(odg_item_stack));
            p->inventory.slots[ODG_INVENTORY_BASE_SLOTS]=original;
            CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
            odg_memset(&p->inventory.slots[ODG_INVENTORY_BASE_SLOTS],0,sizeof(odg_item_stack));
            {
                odg_item_stack flask;odg_memset(&flask,0,sizeof(flask));flask.type_id=ODG_ITEM_WATER_FLASK;flask.quantity=1u;
                flask.material_tier=ODG_MATERIAL_IRON;flask.instance_id=odg_next_instance_id();
                flask.payload_id=odg_fluid_payload_make_internal(ODG_FLUID_WATER,101u);
                CHECK(odg_item_stack_normalize_internal(&flask)!=0);p->inventory.slots[0]=flask;
                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
            }
            {
                odg_item_stack axe;odg_memset(&axe,0,sizeof(axe));axe.type_id=ODG_ITEM_AXE;axe.quantity=1u;axe.material_tier=ODG_MATERIAL_WOOD;
                CHECK(odg_item_stack_normalize_internal(&axe)!=0);CHECK(axe.instance_id==0u);p->inventory.slots[0]=axe;
                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
            }

            /* Unique object identity is global, not merely per stack. A malicious SAVE22
             * with two distinct canonical tool stacks sharing one instance_id must be
             * rejected even when its state hash/checksum are recomputed to match. */
            {
                const size_t player_stack1_offset=player_stack0_offset+sizeof(odg_item_stack);
                odg_item_stack axe_a,axe_b,duplicate;uint64_t axes_hash,duplicate_hash,saved_next;
                odg_memset(&p->inventory,0,sizeof(p->inventory));odg_inventory_init(&p->inventory);
                odg_memset(&axe_a,0,sizeof(axe_a));axe_a.type_id=ODG_ITEM_AXE;axe_a.quantity=1u;axe_a.material_tier=ODG_MATERIAL_WOOD;
                axe_a.instance_id=odg_next_instance_id();CHECK(odg_item_stack_normalize_internal(&axe_a)!=0);
                axe_b=axe_a;axe_b.instance_id=odg_next_instance_id();CHECK(axe_b.instance_id!=axe_a.instance_id);
                p->inventory.slots[0]=axe_a;p->inventory.slots[1]=axe_b;
                CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);axes_hash=odg_state_hash();

                duplicate=axe_b;duplicate.instance_id=axe_a.instance_id;p->inventory.slots[1]=duplicate;
                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
                duplicate_hash=odg_state_hash();p->inventory.slots[1]=axe_b;
                memcpy(blob+player_stack1_offset,&duplicate,sizeof(duplicate));
                test_put_u64_le(blob+24u,duplicate_hash);
                test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                CHECK(odg_state_hash()==axes_hash);CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
                CHECK(odg_state_hash()==axes_hash);CHECK(p->inventory.slots[0].instance_id==axe_a.instance_id);
                CHECK(p->inventory.slots[1].instance_id==axe_b.instance_id);

                /* The sequential allocator cursor is part of SAVE identity safety. It
                 * must never lag behind an already-live sequential instance_id. */
                saved_next=g_odg.next_instance_id;g_odg.next_instance_id=1u;
                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
                g_odg.next_instance_id=ODG_INSTANCE_ID_PROCEDURAL_BIT;
                {
                    odg_item_stack no_id=axe_a;uint32_t pickups_before=g_odg.pickup_count;
                    no_id.instance_id=0u;
                    CHECK(odg_next_instance_id()==0u);
                    CHECK(odg_spawn_world_pickup(&no_id,p->x,p->z,0u)==0);
                    CHECK(g_odg.pickup_count==pickups_before);
                }
                g_odg.next_instance_id=saved_next;
            }
            odg_inventory_init(&p->inventory);p->inventory.slots[0]=original;
            if(g_odg.pickup_count!=0u){
                uint32_t saved_id=g_odg_pickups[0].id;
                odg_world_pickup_deactivate_internal(&g_odg_pickups[0]);
                CHECK(g_odg_pickups[0].id==saved_id);CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_BUFFER_TOO_SMALL);
                g_odg_pickups[0].age_ticks=1u;CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
                g_odg_pickups[0].age_ticks=0u;
            }
            if(g_odg.resource_count!=0u){
                odg_resource_node *r=&g_odg_resources[0];uint32_t saved_id=r->id,saved_kind=r->kind;
                uint64_t saved_stable=r->stable_id;
                r->id=saved_id+1u;CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);r->id=saved_id;
                r->kind=UINT32_C(0xffffffff);CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);r->kind=saved_kind;
                if(r->procedural!=0u&&odg_worldgen_version()>=ODG_WORLDGEN_VERSION_CANONICAL_RESOURCES){
                    r->stable_id^=UINT64_C(1);CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);r->stable_id=saved_stable;
                }
                CHECK(odg_resource_state_validate_internal(r,0u)!=0);
            }
            CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK);
            free(blob);
        }
    }

    /* Current SAVE24 world entities and derived Open-Domain caches are semantic contracts.
     * cannot invent a turret cadence outside its authoritative tier/procedural profile,
     * nor may a chunk's cached territory counters disagree with its packed ownership.
     * Both attacks carry recomputed state hashes + checksums, so rejection proves the
     * semantic layer and transactional rollback rather than a shallow transport guard. */
    CHECK(odg_init(UINT64_C(0x50524f435f504943),320u,180u)==ODG_STATUS_OK);
    odg_turrets_stream_refresh();
    {
        const size_t header=40u;uint64_t bytes=odg_save_blob_size(),written=0u,live_hash,bad_hash;
        uint8_t *blob=(uint8_t *)malloc((size_t)bytes);uint8_t *turret_base,*chunk_base;
        uint32_t turret_count=0u,chunk_count=0u,target=UINT32_MAX,scan_i,payload_size;
        CHECK(blob!=NULL);
        if(blob!=NULL){
            CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
            payload_size=test_get_u32_le(blob+20u);
            turret_base=test_current_save_section(blob,bytes,TEST_SAVE_SECTION_TURRETS,&turret_count);
            for(scan_i=0u;scan_i<g_odg.turret_count;++scan_i)
                if(g_odg_turrets[scan_i].active&&g_odg_turrets[scan_i].procedural!=0u&&g_odg_turrets[scan_i].local_resident!=0u){target=scan_i;break;}
            CHECK(turret_base!=NULL&&target!=UINT32_MAX&&target<turret_count);
            if(turret_base!=NULL&&target!=UINT32_MAX&&target<turret_count){
                odg_turret original=g_odg_turrets[target],bad=original;
                bad.fire_period+=1u;g_odg_turrets[target]=bad;
                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
                bad_hash=odg_state_hash();g_odg_turrets[target]=original;
                memcpy(turret_base+(size_t)target*sizeof(odg_turret),&bad,sizeof(bad));
                test_put_u64_le(blob+24u,bad_hash);
                test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
                CHECK(odg_state_hash()==live_hash);
            }

            /* Restore a pristine blob before attacking a different dynamic section. */
            CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
            chunk_base=test_current_save_section(blob,bytes,TEST_SAVE_SECTION_CHUNKS,&chunk_count);
            CHECK(chunk_base!=NULL&&chunk_count==g_odg.chunk_cache_used&&chunk_count!=0u);
            if(chunk_base!=NULL&&chunk_count!=0u){
                odg_chunk_runtime original=g_odg_chunk_cache[0],bad=original;
                ++bad.territory_cells[0];g_odg_chunk_cache[0]=bad;
                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
                bad_hash=odg_state_hash();g_odg_chunk_cache[0]=original;
                memcpy(chunk_base,&bad,sizeof(bad));
                test_put_u64_le(blob+24u,bad_hash);
                test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
                CHECK(odg_state_hash()==live_hash);
            }

            /* Open Domain has one ownership authority: packed chunk state. The 128x128
             * territory/trail window, aggregate counters, score/level and retired finite-
             * arena bytes are derived compatibility caches. Even with a matching digest and
             * transport checksum, a SAVE24 that carries a second truth must fail closed. */
            {
                uint8_t *suffix;uint8_t old_owner;uint32_t cell=0u;
                CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                suffix=test_current_save_suffix(blob,bytes);CHECK(suffix!=NULL);
                old_owner=g_odg.territory[cell];g_odg.territory[cell]=(uint8_t)(old_owner==ODG_OWNER_NONE?ODG_OWNER_FROM_ID(ODG_PLAYER_ID):ODG_OWNER_NONE);
                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
                if(suffix!=NULL)suffix[offsetof(odg_world,territory)-offsetof(odg_world,playable)+cell]=g_odg.territory[cell];
                g_odg.territory[cell]=old_owner;test_put_u64_le(blob+24u,bad_hash);
                test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
            }
            {
                uint8_t *suffix;uint32_t old_count=g_odg.territory_count[ODG_PLAYER_ID];
                uint32_t old_score=g_odg.actors[ODG_PLAYER_ID].score,old_level=g_odg.actors[ODG_PLAYER_ID].level;
                uint32_t bad_count=old_count+1u,bad_level=1u+bad_count/64u;
                uint8_t *actor_bytes;
                if(bad_level>20u)bad_level=20u;
                CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                suffix=test_current_save_suffix(blob,bytes);CHECK(suffix!=NULL);
                g_odg.territory_count[ODG_PLAYER_ID]=bad_count;g_odg.actors[ODG_PLAYER_ID].score=bad_count;g_odg.actors[ODG_PLAYER_ID].level=bad_level;
                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
                actor_bytes=blob+header+(offsetof(odg_world,actors)-offsetof(odg_world,seed))+(size_t)ODG_PLAYER_ID*sizeof(odg_actor);
                memcpy(actor_bytes+offsetof(odg_actor,score),&bad_count,sizeof(bad_count));
                memcpy(actor_bytes+offsetof(odg_actor,level),&bad_level,sizeof(bad_level));
                if(suffix!=NULL)memcpy(suffix+(offsetof(odg_world,territory_count)-offsetof(odg_world,playable)),&bad_count,sizeof(bad_count));
                g_odg.territory_count[ODG_PLAYER_ID]=old_count;g_odg.actors[ODG_PLAYER_ID].score=old_score;g_odg.actors[ODG_PLAYER_ID].level=old_level;
                test_put_u64_le(blob+24u,bad_hash);test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
            }
            {
                uint8_t *suffix;uint8_t old_playable=g_odg.playable[0];
                CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                suffix=test_current_save_suffix(blob,bytes);CHECK(suffix!=NULL);
                g_odg.playable[0]=0u;CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
                if(suffix!=NULL)suffix[0]=0u;
                g_odg.playable[0]=old_playable;
                test_put_u64_le(blob+24u,bad_hash);test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
            }
            {
                uint8_t *suffix;uint8_t old_trail=g_odg.trail_owner[0];uint8_t bad_trail=(uint8_t)(old_trail==ODG_OWNER_NONE?ODG_OWNER_FROM_ID(1u):ODG_OWNER_NONE);
                CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                suffix=test_current_save_suffix(blob,bytes);CHECK(suffix!=NULL);
                g_odg.trail_owner[0]=bad_trail;CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
                if(suffix!=NULL)suffix[offsetof(odg_world,trail_owner)-offsetof(odg_world,playable)]=bad_trail;
                g_odg.trail_owner[0]=old_trail;test_put_u64_le(blob+24u,bad_hash);test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
            }
            {
                uint8_t *suffix;
                CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                suffix=test_current_save_suffix(blob,bytes);CHECK(suffix!=NULL);
                g_odg.save_reserved_flood_seen[0]=1u;CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);bad_hash=odg_state_hash();
                if(suffix!=NULL)suffix[offsetof(odg_world,save_reserved_flood_seen)-offsetof(odg_world,playable)]=1u;
                g_odg.save_reserved_flood_seen[0]=0u;test_put_u64_le(blob+24u,bad_hash);test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);CHECK(odg_state_hash()==live_hash);
            }

            /* Fauna definitions are also authority, not decoration. Corrupt only one
             * active entity's family while keeping every transport/hash layer valid. */
            CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
            for(scan_i=0u;scan_i<ODG_FAUNA_MAX_ENTRIES;++scan_i)if(g_odg.fauna[scan_i].active)break;
            CHECK(scan_i<ODG_FAUNA_MAX_ENTRIES);
            if(scan_i<ODG_FAUNA_MAX_ENTRIES){
                const size_t fauna_offset=header+(offsetof(odg_world,fauna)-offsetof(odg_world,seed))+
                                          (size_t)scan_i*sizeof(odg_fauna_entity);
                odg_fauna_entity original=g_odg.fauna[scan_i],bad=original;
                bad.family=bad.family==ODG_FAUNA_FAMILY_BIRD?ODG_FAUNA_FAMILY_MAMMAL:ODG_FAUNA_FAMILY_BIRD;
                g_odg.fauna[scan_i]=bad;CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
                bad_hash=odg_state_hash();g_odg.fauna[scan_i]=original;
                CHECK(fauna_offset+sizeof(bad)<=header+(size_t)payload_size);
                memcpy(blob+fauna_offset,&bad,sizeof(bad));test_put_u64_le(blob+24u,bad_hash);
                test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
                CHECK(odg_state_hash()==live_hash);
            }
            ++g_odg.fauna_count;CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);--g_odg.fauna_count;

            /* The persistent runtime used to be validated only while loading, so the
             * writer could create a file that its own loader rejected. Attack the exact
             * phase boundary: oxygen_loss_accum==4 can never survive a simulation tick. */
            CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
            {
                uint32_t runtime_count=0u;uint8_t *runtime_base=test_current_save_section(blob,bytes,TEST_SAVE_SECTION_RUNTIME,&runtime_count);
                odg_persistent_runtime_state original=g_odg_persistent_runtime,bad=original;
                CHECK(runtime_base!=NULL&&runtime_count==1u);
                bad.actors[ODG_PLAYER_ID].oxygen_loss_accum=4u;g_odg_persistent_runtime=bad;
                CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
                bad_hash=odg_state_hash();g_odg_persistent_runtime=original;
                if(runtime_base!=NULL&&runtime_count==1u){
                    memcpy(runtime_base,&bad,sizeof(bad));test_put_u64_le(blob+24u,bad_hash);
                    test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                    live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
                    CHECK(odg_state_hash()==live_hash);
                }
            }

            /* SAVE validity must depend only on the file being loaded, never on the
             * species that happened to occupy the same fixed fauna slot in the live
             * world. Give a saved defensive deer a valid non-zero combat runtime, then
             * deliberately make the pre-load live slot look passive. The loader must
             * parse runtime structurally first and validate it against the SAVED deer
             * only after the fauna prefix has been installed. */
            {
                uint32_t deer=UINT32_MAX;
                for(scan_i=0u;scan_i<ODG_FAUNA_MAX_ENTRIES;++scan_i)
                    if(g_odg.fauna[scan_i].active&&g_odg.fauna[scan_i].species_id==ODG_FAUNA_SPECIES_FOREST_DEER){deer=scan_i;break;}
                CHECK(deer!=UINT32_MAX);
                if(deer!=UINT32_MAX){
                    uint32_t saved_species;
                    g_odg_persistent_runtime.fauna_attack_cooldown[deer]=1u;
                    g_odg_persistent_runtime.fauna_target_actor[deer]=ODG_PLAYER_ID;
                    g_odg_persistent_runtime.fauna_aggro_ticks[deer]=1u;
                    CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                    saved_species=g_odg.fauna[deer].species_id;
                    g_odg.fauna[deer].species_id=ODG_FAUNA_SPECIES_ORCHARD_BIRD;
                    CHECK(odg_save_load(blob,bytes)==ODG_STATUS_OK);
                    CHECK(g_odg.fauna[deer].species_id==saved_species);
                    CHECK(g_odg_persistent_runtime.fauna_attack_cooldown[deer]==1u&&
                          g_odg_persistent_runtime.fauna_target_actor[deer]==ODG_PLAYER_ID&&
                          g_odg_persistent_runtime.fauna_aggro_ticks[deer]==1u);
                }
            }

            /* Inactive fauna slots are canonical tombstones in SAVE24. A stale byte is
             * rejected even when the state hash and transport checksum are recomputed. */
            {
                uint32_t inactive=UINT32_MAX;uint8_t *fauna_bytes;
                CHECK(odg_save_write(blob,bytes,&written)==ODG_STATUS_OK&&written==bytes);
                for(scan_i=0u;scan_i<ODG_FAUNA_MAX_ENTRIES;++scan_i)if(!g_odg.fauna[scan_i].active){inactive=scan_i;break;}
                CHECK(inactive!=UINT32_MAX);
                if(inactive!=UINT32_MAX){
                    const size_t fauna_offset=header+(offsetof(odg_world,fauna)-offsetof(odg_world,seed))+
                                              (size_t)inactive*sizeof(odg_fauna_entity);
                    uint64_t clean_hash=odg_state_hash();
                    g_odg.fauna[inactive].family=ODG_FAUNA_FAMILY_BIRD;
                    CHECK(odg_save_write(NULL,0u,&written)==ODG_STATUS_INVALID_STATE);
                    bad_hash=odg_state_hash();
                    fauna_bytes=blob+fauna_offset;
                    memcpy(fauna_bytes,&g_odg.fauna[inactive],sizeof(g_odg.fauna[inactive]));
                    memset(&g_odg.fauna[inactive],0,sizeof(g_odg.fauna[inactive]));
                    CHECK(odg_state_hash()==clean_hash);
                    test_put_u64_le(blob+24u,bad_hash);
                    test_put_u64_le(blob+32u,test_payload_checksum(blob+header,payload_size));
                    live_hash=odg_state_hash();CHECK(odg_save_load(blob,bytes)==ODG_STATUS_INVALID_ARGUMENT);
                    CHECK(odg_state_hash()==live_hash);
                }
            }
            free(blob);
        }
    }

    if(failures!=0){fprintf(stderr,"ecosystem: %d failure(s)\n",failures);return 1;}
    printf("ECOSYSTEM OK api=%u abi=%u fauna=%u habitats=%u food=%u flora=%u motion=body-safe+landing-safe flora-growth=collision-safe planting=transactional seed-recovery=lossless save=%u semantic-save=canonical+rollback+load-independent+fauna-tombstones\n",
           ODG_API_VERSION,ODG_FFI_ABI_VERSION,odg_fauna_species_count(),odg_fauna_habitat_count(),
           odg_food_definition_count(),odg_flora_species_count(),ODG_SAVE_SCHEMA_VERSION);
    return 0;
}
