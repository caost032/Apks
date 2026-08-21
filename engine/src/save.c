#include "game_internal.h"

#include <stddef.h>
#include <stdint.h>

#define SAVE_HEADER_BYTES UINT64_C(40)
#define SAVE_MAGIC_0 UINT8_C(0x4f)
#define SAVE_MAGIC_1 UINT8_C(0x44)
#define SAVE_MAGIC_2 UINT8_C(0x47)
#define SAVE_MAGIC_3 UINT8_C(0x53)
#define SAVE_MAGIC_4 UINT8_C(0x41)
#define SAVE_MAGIC_5 UINT8_C(0x56)
#define SAVE_MAGIC_6 UINT8_C(0x45)
#define SAVE_MAGIC_7 UINT8_C(0x31)

/* World saves have their own DataVersion-style authority. API/FFI are provenance,
 * never the compatibility rule. Schemas 14 and 15 share the legacy raw layout; schema
 * 16 gives nest substrate explicit semantics. Schema 17 adds a versioned runtime section
 * for worldgen provenance + respiration. Schema 18 moves ordinary construction modules out
 * of heavyweight artifact records into a compact dedicated section. Schema 19 activates the
 * pre-existing construction `shape` field as a typed floor/wall/doorway/roof topology. Schema 20
 * activates construction health/integrity in formerly reserved bytes. Schema 21 migrates modern
 * worldgen-v2 worlds to v3, whose only generation change is physically valid natural-turret
 * resolution. Schema 22 migrates modern v2/v3 worlds to worldgen-v4, where procedural resource
 * position/species are canonical per (seed, chunk, ordinal, kind) and cannot drift with streaming,
 * depletion or actor motion. Schema 23 separates procedural high-bit turret identities from
 * portable/manual sequential identities and migrates historical carried natural turrets without
 * losing their state. Schema 24 removes presentation-only particles, rebuildable interaction hints
 * and the derived bot navigation graph from deterministic SAVE/hash authority while retaining their
 * frozen raw-layout bytes as canonical zeros. Schema 25 separates resource identity namespaces:
 * procedural nodes use a worldgen-derived high-bit identity, while bootstrap/player-created nodes
 * use the monotonic manual identity allocator; depleted manual remnants now have a bounded visual
 * lifetime and can compact without index reuse becoming identity reuse. Genuine v1 legacy worlds
 * retain their frozen procedural generation semantics. */
#define ODG_SAVE_SCHEMA_LEGACY14 UINT32_C(14)
#define ODG_SAVE_SCHEMA_LEGACY15 UINT32_C(15)
#define ODG_SAVE_SCHEMA_LEGACY16 UINT32_C(16)
#define ODG_SAVE_SCHEMA_LEGACY17 UINT32_C(17)
#define ODG_SAVE_SCHEMA_LEGACY18 UINT32_C(18)
#define ODG_SAVE_SCHEMA_LEGACY19 UINT32_C(19)
#define ODG_SAVE_SCHEMA_LEGACY20 UINT32_C(20)
#define ODG_SAVE_SCHEMA_LEGACY21 UINT32_C(21)
#define ODG_SAVE_SCHEMA_LEGACY22 UINT32_C(22)
#define ODG_SAVE_SCHEMA_LEGACY23 UINT32_C(23)
#define ODG_SAVE_SCHEMA_LEGACY24 UINT32_C(24)
#define ODG_SAVE_MIGRATION_IDENTITY UINT32_C(1)
#define ODG_SAVE_MIGRATION_NEST_SUBSTRATE UINT32_C(2)
#define ODG_SAVE_MIGRATION_RUNTIME_STATE UINT32_C(3)
#define ODG_SAVE_MIGRATION_CONSTRUCTION_STORE UINT32_C(4)
#define ODG_SAVE_MIGRATION_CONSTRUCTION_SHAPES UINT32_C(5)
#define ODG_SAVE_MIGRATION_CONSTRUCTION_DURABILITY UINT32_C(6)
#define ODG_SAVE_MIGRATION_WORLDGEN_SAFE_TURRETS UINT32_C(7)
#define ODG_SAVE_MIGRATION_WORLDGEN_CANONICAL_RESOURCES UINT32_C(8)
#define ODG_SAVE_MIGRATION_MANUAL_TURRET_ID_NAMESPACE UINT32_C(9)
#define ODG_SAVE_MIGRATION_CANONICAL_AUTHORITY UINT32_C(10)
#define ODG_SAVE_MIGRATION_RESOURCE_ID_NAMESPACE UINT32_C(11)
typedef struct { uint32_t from_schema,to_schema,kind; } odg_save_migration_step;
static const odg_save_migration_step g_save_migrations[] = {
    {ODG_SAVE_SCHEMA_LEGACY14,ODG_SAVE_SCHEMA_LEGACY15,ODG_SAVE_MIGRATION_IDENTITY},
    {ODG_SAVE_SCHEMA_LEGACY15,ODG_SAVE_SCHEMA_LEGACY16,ODG_SAVE_MIGRATION_NEST_SUBSTRATE},
    {ODG_SAVE_SCHEMA_LEGACY16,ODG_SAVE_SCHEMA_LEGACY17,ODG_SAVE_MIGRATION_RUNTIME_STATE},
    {ODG_SAVE_SCHEMA_LEGACY17,ODG_SAVE_SCHEMA_LEGACY18,ODG_SAVE_MIGRATION_CONSTRUCTION_STORE},
    {ODG_SAVE_SCHEMA_LEGACY18,ODG_SAVE_SCHEMA_LEGACY19,ODG_SAVE_MIGRATION_CONSTRUCTION_SHAPES},
    {ODG_SAVE_SCHEMA_LEGACY19,ODG_SAVE_SCHEMA_LEGACY20,ODG_SAVE_MIGRATION_CONSTRUCTION_DURABILITY},
    {ODG_SAVE_SCHEMA_LEGACY20,ODG_SAVE_SCHEMA_LEGACY21,ODG_SAVE_MIGRATION_WORLDGEN_SAFE_TURRETS},
    {ODG_SAVE_SCHEMA_LEGACY21,ODG_SAVE_SCHEMA_LEGACY22,ODG_SAVE_MIGRATION_WORLDGEN_CANONICAL_RESOURCES},
    {ODG_SAVE_SCHEMA_LEGACY22,ODG_SAVE_SCHEMA_LEGACY23,ODG_SAVE_MIGRATION_MANUAL_TURRET_ID_NAMESPACE},
    {ODG_SAVE_SCHEMA_LEGACY23,ODG_SAVE_SCHEMA_LEGACY24,ODG_SAVE_MIGRATION_CANONICAL_AUTHORITY},
    {ODG_SAVE_SCHEMA_LEGACY24,ODG_SAVE_SCHEMA_VERSION,ODG_SAVE_MIGRATION_RESOURCE_ID_NAMESPACE}
};

typedef struct {
    uint32_t active;
    uint32_t id;
    uint64_t stable_id;
    uint32_t species_id;
    uint32_t egg_count;
    uint32_t hatch_ticks;
    uint32_t parent_a;
    uint32_t parent_b;
    uint64_t host_resource_stable_id;
    int32_t x,z;
    int64_t global_fx_x,global_fx_z;
    uint32_t local_resident;
} odg_fauna_nest_save15;

_Static_assert(sizeof(odg_actor)==4976u, "save15 actor layout changed: add explicit migration");
_Static_assert(sizeof(odg_fauna_entity)==184u, "save15 fauna layout changed: add explicit migration");
_Static_assert(sizeof(odg_fauna_nest_save15)==80u, "save15 legacy nest layout must remain frozen");
_Static_assert(sizeof(odg_fauna_nest)==80u, "save16 nest layout changed: add explicit migration");
_Static_assert(sizeof(odg_persistent_runtime_state)==1280u, "save17 runtime-state layout changed: add explicit migration");
_Static_assert(sizeof(odg_turret)==184u, "save15 turret layout changed: add explicit migration");
_Static_assert(sizeof(odg_world_pickup)==88u, "save15 pickup layout changed: add explicit migration");
_Static_assert(sizeof(odg_resource_node)==152u, "save15 resource layout changed: add explicit migration");
_Static_assert(sizeof(odg_artifact)==1048u, "save15 artifact layout changed: add explicit migration");
_Static_assert(sizeof(odg_construction_block)==72u, "save18-25 construction layout changed: add explicit migration");
_Static_assert(sizeof(odg_chunk_runtime)==1152u, "save15 chunk layout changed: add explicit migration");
_Static_assert(offsetof(odg_world,playable)-offsetof(odg_world,seed)==70352u,
               "save15 prefix layout changed: add explicit migration");
_Static_assert(offsetof(odg_world,stats)-offsetof(odg_world,playable)==114776u,
               "save15 suffix layout changed: add explicit migration");
_Static_assert(offsetof(odg_world,actors) >= offsetof(odg_world,seed), "actors must persist in save prefix");
_Static_assert(offsetof(odg_world,fauna) < offsetof(odg_world,playable), "fauna must persist in save prefix");
_Static_assert(offsetof(odg_world,fauna_nests) < offsetof(odg_world,playable), "nests must persist in save prefix");
_Static_assert(offsetof(odg_world,weather_rain_permille) < offsetof(odg_world,playable), "weather must persist in save prefix");
_Static_assert(offsetof(odg_world,territory) >= offsetof(odg_world,playable), "territory must persist in save suffix");
_Static_assert(offsetof(odg_world,territory) < offsetof(odg_world,stats), "territory must precede presentation stats");

static size_t logical_offset(void) { return offsetof(odg_world,seed); }
static size_t prefix_size(void) { return offsetof(odg_world,playable)-offsetof(odg_world,seed); }
static size_t suffix_offset(void) { return offsetof(odg_world,playable); }
static size_t suffix_size(void) { return offsetof(odg_world,stats)-offsetof(odg_world,playable); }

static void save_canonicalize_non_authority_bytes(uint8_t *prefix_bytes,uint8_t *suffix_bytes) {
    const size_t p0=offsetof(odg_world,particles)-logical_offset();
    const size_t h0=offsetof(odg_world,interaction_hint)-logical_offset();
    const size_t n0=offsetof(odg_world,bot_nav_edges)-suffix_offset();
    if(prefix_bytes!=NULL){
        odg_memset(prefix_bytes+p0,0,sizeof(g_odg.particles));
        odg_memset(prefix_bytes+h0,0,sizeof(g_odg.interaction_hint));
    }
    if(suffix_bytes!=NULL)odg_memset(suffix_bytes+n0,0,sizeof(g_odg.bot_nav_edges));
}

static int save_non_authority_bytes_are_canonical(const uint8_t *prefix_bytes,const uint8_t *suffix_bytes) {
    const size_t p0=offsetof(odg_world,particles)-logical_offset();
    const size_t h0=offsetof(odg_world,interaction_hint)-logical_offset();
    const size_t n0=offsetof(odg_world,bot_nav_edges)-suffix_offset();
    size_t i;
    if(prefix_bytes==NULL||suffix_bytes==NULL)return 0;
    for(i=0u;i<sizeof(g_odg.particles);++i)if(prefix_bytes[p0+i]!=0u)return 0;
    for(i=0u;i<sizeof(g_odg.interaction_hint);++i)if(prefix_bytes[h0+i]!=0u)return 0;
    for(i=0u;i<sizeof(g_odg.bot_nav_edges);++i)if(suffix_bytes[n0+i]!=0u)return 0;
    return 1;
}

static uint64_t counted_bytes(uint32_t count,size_t elem_size) {
    return UINT64_C(4)+(uint64_t)count*(uint64_t)elem_size;
}
static uint64_t payload_size_for_schema(uint32_t schema,uint32_t turrets,uint32_t pickups,uint32_t resources,
                                        uint32_t artifacts,uint32_t construction,uint32_t chunks) {
    uint64_t size=(uint64_t)prefix_size()+
           counted_bytes(turrets,sizeof(odg_turret))+
           counted_bytes(pickups,sizeof(odg_world_pickup))+
           counted_bytes(resources,sizeof(odg_resource_node))+
           counted_bytes(artifacts,sizeof(odg_artifact))+
           counted_bytes(chunks,sizeof(odg_chunk_runtime))+
           (uint64_t)suffix_size();
    if(schema>=ODG_SAVE_SCHEMA_LEGACY17)size+=counted_bytes(1u,sizeof(odg_persistent_runtime_state));
    if(schema>=ODG_SAVE_SCHEMA_LEGACY18)size+=counted_bytes(construction,sizeof(odg_construction_block));
    return size;
}
static uint64_t payload_size_for(uint32_t turrets,uint32_t pickups,uint32_t resources,
                                 uint32_t artifacts,uint32_t construction,uint32_t chunks) {
    return payload_size_for_schema(ODG_SAVE_SCHEMA_VERSION,turrets,pickups,resources,artifacts,construction,chunks);
}

static void put_u32_le(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);p[2]=(uint8_t)(v>>16u);p[3]=(uint8_t)(v>>24u);
}
static void put_u64_le(uint8_t *p,uint64_t v) {
    uint32_t i;for(i=0u;i<8u;++i)p[i]=(uint8_t)(v>>(i*8u));
}
static uint32_t get_u32_le(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);
}
static uint64_t get_u64_le(const uint8_t *p) {
    uint32_t i;uint64_t v=0u;for(i=0u;i<8u;++i)v|=(uint64_t)p[i]<<(i*8u);return v;
}
static uint64_t payload_checksum(const uint8_t *p,size_t n) {
    uint64_t h=UINT64_C(1469598103934665603);size_t i;
    for(i=0u;i<n;++i){h^=p[i];h*=UINT64_C(1099511628211);}return h;
}


typedef struct {
    odg_world world;
    odg_persistent_runtime_state persistent_runtime;
    uint32_t construction_count;
    uint32_t construction_shape[ODG_MAX_ACTORS];
    odg_turret *turrets;
    odg_world_pickup *pickups;
    odg_resource_node *resources;
    odg_artifact *artifacts;
    odg_construction_block *construction;
    odg_chunk_runtime *chunks;
} odg_save_load_backup;

static void save_load_backup_dispose(odg_save_load_backup *backup){
    if(backup==NULL)return;
    odg_mem_free(backup->turrets);odg_mem_free(backup->pickups);odg_mem_free(backup->resources);
    odg_mem_free(backup->artifacts);odg_mem_free(backup->construction);odg_mem_free(backup->chunks);
    odg_mem_free(backup);
}

static int save_load_backup_copy(void **out,const void *src,uint32_t count,size_t elem_size){
    size_t bytes;void *copy;
    if(out==NULL)return 0;
    *out=NULL;
    if(count==0u)return 1;
    if(src==NULL||elem_size==0u||count>SIZE_MAX/elem_size)return 0;
    bytes=(size_t)count*elem_size;copy=odg_mem_realloc(NULL,bytes);if(copy==NULL)return 0;
    odg_memcpy(copy,src,bytes);*out=copy;return 1;
}

static odg_save_load_backup *save_load_backup_capture(void){
    odg_save_load_backup *backup;uint32_t i;
    backup=(odg_save_load_backup *)odg_mem_realloc(NULL,sizeof(*backup));if(backup==NULL)return NULL;
    odg_memset(backup,0,sizeof(*backup));backup->world=g_odg;backup->persistent_runtime=g_odg_persistent_runtime;
    backup->construction_count=g_odg_construction_count;
    for(i=0u;i<ODG_MAX_ACTORS;++i)backup->construction_shape[i]=odg_construction_selected_shape_internal(i);
    if(!save_load_backup_copy((void **)&backup->turrets,g_odg_turrets,g_odg.turret_count,sizeof(*g_odg_turrets))||
       !save_load_backup_copy((void **)&backup->pickups,g_odg_pickups,g_odg.pickup_count,sizeof(*g_odg_pickups))||
       !save_load_backup_copy((void **)&backup->resources,g_odg_resources,g_odg.resource_count,sizeof(*g_odg_resources))||
       !save_load_backup_copy((void **)&backup->artifacts,g_odg_artifacts,g_odg.artifact_count,sizeof(*g_odg_artifacts))||
       !save_load_backup_copy((void **)&backup->construction,g_odg_construction_blocks,g_odg_construction_count,sizeof(*g_odg_construction_blocks))||
       !save_load_backup_copy((void **)&backup->chunks,g_odg_chunk_cache,g_odg.chunk_cache_used,sizeof(*g_odg_chunk_cache))){
        save_load_backup_dispose(backup);return NULL;
    }
    return backup;
}

static void save_load_backup_restore(const odg_save_load_backup *backup){
    uint32_t i;
    if(backup==NULL)return;
    g_odg=backup->world;g_odg_persistent_runtime=backup->persistent_runtime;g_odg_construction_count=backup->construction_count;
    if(g_odg.turret_count!=0u)odg_memcpy(g_odg_turrets,backup->turrets,(size_t)g_odg.turret_count*sizeof(*g_odg_turrets));
    if(g_odg.pickup_count!=0u)odg_memcpy(g_odg_pickups,backup->pickups,(size_t)g_odg.pickup_count*sizeof(*g_odg_pickups));
    if(g_odg.resource_count!=0u)odg_memcpy(g_odg_resources,backup->resources,(size_t)g_odg.resource_count*sizeof(*g_odg_resources));
    if(g_odg.artifact_count!=0u)odg_memcpy(g_odg_artifacts,backup->artifacts,(size_t)g_odg.artifact_count*sizeof(*g_odg_artifacts));
    if(g_odg_construction_count!=0u)odg_memcpy(g_odg_construction_blocks,backup->construction,(size_t)g_odg_construction_count*sizeof(*g_odg_construction_blocks));
    if(g_odg.chunk_cache_used!=0u)odg_memcpy(g_odg_chunk_cache,backup->chunks,(size_t)g_odg.chunk_cache_used*sizeof(*g_odg_chunk_cache));
    for(i=0u;i<ODG_MAX_ACTORS;++i)(void)odg_construction_set_shape_internal(i,backup->construction_shape[i]);
    /* Spatial refs and summaries are derivable caches. A failed load may have rebuilt them
     * for the rejected world; discard/rebuild those views from the restored authority. */
    odg_entities_spatial_mark_dirty();odg_chunks_refresh_summaries();
}

static int save_payload_stack_validate(const odg_item_stack *stack,uint8_t *turret_refs,uint8_t *artifact_refs){
    uint64_t raw;
    if(stack==NULL||odg_item_stack_empty_internal(stack)||stack->payload_id==0u)return 1;
    if(stack->type_id==ODG_ITEM_TURRET){
        const odg_turret *turret;
        raw=stack->payload_id-UINT64_C(1);
        if(raw>=g_odg.turret_count||stack->quantity!=1u||stack->instance_id==0u)return 0;
        turret=&g_odg_turrets[(uint32_t)raw];
        if(turret->active||turret->procedural!=0u||turret->instance_id==0u||turret->instance_id!=stack->instance_id||
           turret->material_tier!=stack->material_tier)return 0;
        if(turret_refs!=NULL){if(turret_refs[(uint32_t)raw]!=0u)return 0;turret_refs[(uint32_t)raw]=1u;}
        return 1;
    }
    if(odg_artifact_item_deployable_internal(stack->type_id)){
        const odg_artifact *artifact;
        raw=stack->payload_id-UINT64_C(1);
        if(raw>=g_odg.artifact_count||stack->quantity!=1u||stack->instance_id==0u)return 0;
        artifact=&g_odg_artifacts[(uint32_t)raw];
        if(artifact->active||artifact->instance_id==0u||artifact->instance_id!=stack->instance_id||
           artifact->item_type!=stack->type_id||artifact->material_tier!=stack->material_tier||
           (artifact->state&ODG_ARTIFACT_STATE_DEATH_CACHE)!=0u)return 0;
        if(artifact_refs!=NULL){if(artifact_refs[(uint32_t)raw]!=0u)return 0;artifact_refs[(uint32_t)raw]=1u;}
    }
    return 1;
}

static int save_stack_material_valid(const odg_item_stack *stack,const odg_item_definition *definition){
    if(stack==NULL||definition==NULL)return 0;
    /* Item/recipe registry is the sole material-variant authority. Save validation must
     * not grow a second list of which tools/chips/building modules have tier variants. */
    return odg_item_material_variant_valid_internal(stack->type_id,stack->material_tier);
}

static int save_stack_payload_semantic_validate(const odg_item_stack *stack,const odg_item_definition *definition){
    const odg_fluid_container_definition *container;
    if(stack==NULL||definition==NULL)return 0;

    container=odg_fluid_container_definition_internal(stack->type_id);
    if(container!=NULL){
        const odg_fluid_definition *fluid;uint32_t fluid_id,units;
        if(stack->payload_id==0u)return 1; /* empty container */
        fluid_id=odg_fluid_payload_id_internal(stack->payload_id);
        units=odg_fluid_payload_units_internal(stack->payload_id);
        fluid=odg_fluid_definition_internal(fluid_id);
        return fluid!=NULL&&units!=0u&&units<=container->capacity_units&&
               (fluid->flags&container->accepted_fluid_flags)!=0u;
    }

    /* Biological stack payloads carry species/variety. Zero remains a valid legacy-
     * neutral/current-default value, but a non-zero ID must name a species that really
     * produces this exact seed or fruit item. */
    if((definition->flags&ODG_ITEM_FLAG_SEED)!=0u){
        const odg_flora_species_definition *flora;
        if(stack->payload_id==0u)return 1;
        if(stack->payload_id>UINT32_MAX)return 0;
        flora=odg_flora_species_internal((uint32_t)stack->payload_id);
        return flora!=NULL&&flora->seed_item_type==stack->type_id;
    }
    if((definition->flags&ODG_ITEM_FLAG_FOOD)!=0u){
        const odg_flora_species_definition *flora;
        if(stack->payload_id==0u)return 1;
        if(stack->payload_id>UINT32_MAX)return 0;
        flora=odg_flora_species_internal((uint32_t)stack->payload_id);
        return flora!=NULL&&flora->fruit_item_type==stack->type_id;
    }

    /* Turrets and artifact-shaped inventory items use payload_id as a stable world
     * handle. Their referential integrity is checked by save_payload_stack_validate(). */
    if(stack->type_id==ODG_ITEM_TURRET||odg_artifact_item_deployable_internal(stack->type_id))return 1;
    return stack->payload_id==0u;
}

static int save_stack_canonical_validate(const odg_item_stack *stack){
    const uint32_t known_static_flags=ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_RESOURCE|
        ODG_ITEM_FLAG_CHIP|ODG_ITEM_FLAG_ARTIFACT|ODG_ITEM_FLAG_DURABILITY|
        ODG_ITEM_FLAG_FOOD|ODG_ITEM_FLAG_SEED;
    const odg_item_definition *definition;odg_item_stack normalized;
    uint32_t expected_max;
    if(stack==NULL)return 0;
    if(stack->type_id==ODG_ITEM_NONE||stack->quantity==0u){
        /* Current saves use zeroed empty slots. A half-empty stack (type with qty 0,
         * qty with type 0, stale payload, etc.) is corruption, not an alternate empty
         * representation. Legacy schemas remain governed by their migration path. */
        return stack->type_id==ODG_ITEM_NONE&&stack->quantity==0u&&
               stack->material_tier==ODG_MATERIAL_NONE&&stack->durability==0u&&
               stack->max_durability==0u&&stack->flags==0u&&
               stack->instance_id==0u&&stack->payload_id==0u;
    }
    definition=odg_item_definition_internal(stack->type_id);
    if(definition==NULL||definition->max_stack==0u||stack->quantity>definition->max_stack)return 0;
    if(!save_stack_material_valid(stack,definition))return 0;

    normalized=*stack;
    if(!odg_item_stack_normalize_internal(&normalized))return 0;
    /* Unknown/dynamic future bits are deliberately not erased by normalization. Static
     * category flags, however, are registry authority and must already be canonical in
     * a current SAVE23 blob. */
    if((stack->flags&known_static_flags)!=(normalized.flags&known_static_flags) ||
       stack->material_tier!=normalized.material_tier)return 0;

    if(definition->max_stack>1u){
        if(stack->instance_id!=0u)return 0;
    }else{
        if(stack->quantity!=1u||stack->instance_id==0u)return 0;
    }
    if((definition->flags&ODG_ITEM_FLAG_DURABILITY)!=0u){
        expected_max=odg_item_max_durability_internal(stack->type_id,stack->material_tier);
        if(expected_max==0u||stack->max_durability!=expected_max||stack->durability==0u||
           stack->durability>expected_max)return 0;
    }else if(stack->durability!=0u||stack->max_durability!=0u)return 0;
    return save_stack_payload_semantic_validate(stack,definition);
}

static int save_inventory_canonical_validate(const odg_inventory *inventory){
    uint32_t expected_slots,i;
    if(inventory==NULL)return 0;
    if(inventory->equipped_backpack_type==ODG_ITEM_NONE)expected_slots=ODG_INVENTORY_BASE_SLOTS;
    else{
        expected_slots=odg_item_inventory_expander_slots_internal(inventory->equipped_backpack_type);
        if(expected_slots==0u)return 0;
    }
    if(inventory->slot_count!=expected_slots||inventory->slot_count>ODG_INVENTORY_MAX_SLOTS||
       inventory->selected_slot>=inventory->slot_count||inventory->reserved_u32!=0u)return 0;
    for(i=0u;i<inventory->slot_count;++i)if(!save_stack_canonical_validate(&inventory->slots[i]))return 0;
    for(;i<ODG_INVENTORY_MAX_SLOTS;++i){
        const odg_item_stack *stack=&inventory->slots[i];
        if(stack->type_id!=ODG_ITEM_NONE||stack->quantity!=0u||stack->material_tier!=0u||
           stack->durability!=0u||stack->max_durability!=0u||stack->flags!=0u||
           stack->instance_id!=0u||stack->payload_id!=0u)return 0;
    }
    return 1;
}

static int save_input_interaction_state_validate_internal(void){
    const odg_input *input=&g_odg.input;int32_t nx,nz;uint32_t mag,expected_strength;
    if(input->move_x_q15 < -ODG_Q15_ONE || input->move_x_q15 > ODG_Q15_ONE ||
       input->move_z_q15 < -ODG_Q15_ONE || input->move_z_q15 > ODG_Q15_ONE ||
       input->aim_x_q15 < -ODG_Q15_ONE || input->aim_x_q15 > ODG_Q15_ONE ||
       input->aim_z_q15 < -ODG_Q15_ONE || input->aim_z_q15 > ODG_Q15_ONE ||
       input->move_strength_q15<0||input->move_strength_q15>ODG_Q15_ONE||input->world_heading_mode>1u)return 0;
    if(input->world_heading_mode==0u){
        mag=odg_isqrt_u64((uint64_t)((int64_t)input->move_x_q15*input->move_x_q15+
                                     (int64_t)input->move_z_q15*input->move_z_q15));
        expected_strength=mag>(uint32_t)ODG_Q15_ONE?(uint32_t)ODG_Q15_ONE:mag;
        if((uint32_t)input->move_strength_q15!=expected_strength)return 0;
    }else if(input->move_strength_q15==0){
        if(input->move_x_q15!=0||input->move_z_q15!=0)return 0;
    }else{
        if(input->move_strength_q15<=ODG_PLAYER_INPUT_DEADZONE||
           (input->move_x_q15==0&&input->move_z_q15==0))return 0;
        nx=input->move_x_q15;nz=input->move_z_q15;odg_normalize_q15(nx,nz,&nx,&nz);
        if(nx!=input->move_x_q15||nz!=input->move_z_q15)return 0;
    }
    if(g_odg.interact_hold_fired>1u||g_odg.interact_pressed_prev>1u)return 0;
    if(g_odg.interact_pressed_prev==0u){
        if(g_odg.interact_ticks!=0u||g_odg.interact_hold_fired!=0u)return 0;
    }else if(g_odg.interact_ticks==0u)return 0;
    return 1;
}

static int save_runtime_count_bounds_validate_internal(void){
    if(g_odg.obstacle_count>ODG_MAX_OBSTACLES)return 0;
    if(g_odg.turret_count>g_odg_turret_capacity ||
       g_odg.pickup_count>g_odg_pickup_capacity ||
       g_odg.resource_count>g_odg_resource_capacity ||
       g_odg.artifact_count>g_odg_artifact_capacity ||
       g_odg_construction_count>g_odg_construction_capacity ||
       g_odg.chunk_cache_used>g_odg_chunk_cache_capacity)return 0;
    if((g_odg.turret_count!=0u&&g_odg_turrets==NULL) ||
       (g_odg.pickup_count!=0u&&g_odg_pickups==NULL) ||
       (g_odg.resource_count!=0u&&g_odg_resources==NULL) ||
       (g_odg.artifact_count!=0u&&g_odg_artifacts==NULL) ||
       (g_odg_construction_count!=0u&&g_odg_construction_blocks==NULL) ||
       (g_odg.chunk_cache_used!=0u&&g_odg_chunk_cache==NULL))return 0;
    return 1;
}

static int save_rng_state_validate_internal(const odm_rng *rng){
    odm_rng copy;uint32_t sample=0u;
    if(rng==NULL)return 0;
    copy=*rng;
    return odm_rng_next_u32(&copy,&sample)==ODM_STATUS_OK;
}

static int save_unit_q15_validate_internal(int32_t x,int32_t z){
    int32_t nx,nz;if(x==0&&z==0)return 0;
    odg_normalize_q15(x,z,&nx,&nz);return nx==x&&nz==z;
}

static int save_control_state_validate_internal(void){
    if(!save_unit_q15_validate_internal(g_odg.camera_dir_x_q15,g_odg.camera_dir_z_q15)||
       !save_unit_q15_validate_internal(g_odg.control_basis_x_q15,g_odg.control_basis_z_q15)||
       g_odg.control_active>1u||g_odg.control_strength_q15<0||g_odg.control_strength_q15>ODG_Q15_ONE)return 0;
    if(g_odg.control_active==0u)
        return g_odg.control_heading_x_q15==0&&g_odg.control_heading_z_q15==0&&g_odg.control_strength_q15==0;
    return g_odg.control_strength_q15>0&&
           save_unit_q15_validate_internal(g_odg.control_heading_x_q15,g_odg.control_heading_z_q15);
}

static int save_fixed_world_state_validate_internal(void){
    if(g_odg.tick_accum_scaled>=ODG_TICK_US_NUM ||
       !save_rng_state_validate_internal(&g_odg.rng)||!save_rng_state_validate_internal(&g_odg.ecology_rng)||
       !save_control_state_validate_internal())return 0;
    /* Current-schema worlds have only two legitimate generation provenances: frozen v1
     * legacy or the current generator. Transitional v2/v3 worlds are migrated before a
     * SAVE24 is produced; accepting them here would silently resurrect retired rules. */
    if(g_odg_persistent_runtime.worldgen_version!=ODG_WORLDGEN_VERSION_LEGACY &&
       g_odg_persistent_runtime.worldgen_version!=ODG_WORLDGEN_VERSION_CURRENT)return 0;
    if(g_odg.weather_rain_permille>1000u || g_odg.save_reserved_weather_u32!=0u)return 0;
    if((g_odg.world_origin_cell_x%(int64_t)ODG_CHUNK_SIZE_CELLS)!=0 ||
       (g_odg.world_origin_cell_z%(int64_t)ODG_CHUNK_SIZE_CELLS)!=0)return 0;
    if(g_odg.match_over!=0u || g_odg.winner_id!=UINT32_MAX)return 0;
    if(g_odg.opened_artifact_id!=UINT32_MAX){
        const odg_artifact *artifact;
        if(g_odg.opened_artifact_id>=g_odg.artifact_count || g_odg_artifacts==NULL)return 0;
        artifact=&g_odg_artifacts[g_odg.opened_artifact_id];
        if(!artifact->active || (artifact->capability_bits&ODG_ARTIFACT_CAP_OPEN_UI)==0u)return 0;
    }
    return 1;
}

static int odg_save_current_semantics_validate_internal(void){
    uint32_t i,j;
    if(!save_runtime_count_bounds_validate_internal() ||
       !odg_command_queue_state_validate_internal(&g_odg.commands)||
       !save_input_interaction_state_validate_internal())return 0;
    for(i=0u;i<ODG_MAX_ACTORS;++i)if(!save_inventory_canonical_validate(&g_odg.actors[i].inventory))return 0;
    for(i=0u;i<g_odg.pickup_count;++i){
        const odg_world_pickup *pickup=&g_odg_pickups[i];
        if(!pickup->active)continue;
        if(pickup->id!=i||!save_stack_canonical_validate(&pickup->stack))return 0;
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *artifact=&g_odg_artifacts[i];
        if(!artifact->active)continue;
        for(j=0u;j<ODG_CHEST_SLOTS;++j)
            if(!save_stack_canonical_validate(&artifact->storage.slots[j]))return 0;
    }
    return 1;
}

static int save_actor_state_validate_internal(const odg_actor *actor,uint32_t expected_id){
    uint32_t expected_type=expected_id==ODG_PLAYER_ID?ODG_ACTOR_PLAYER:ODG_ACTOR_BOT;
    int32_t local_x=0,local_z=0;
    if(actor==NULL||expected_id>=ODG_MAX_ACTORS||actor->active!=1u||actor->id!=expected_id||
       actor->type!=expected_type||actor->name_code!=expected_id||actor->local_resident>1u||
       !save_rng_state_validate_internal(&actor->rng)||
       !save_unit_q15_validate_internal(actor->face_x_q15,actor->face_z_q15)||
       (actor->turn_sign!=-1&&actor->turn_sign!=1)||actor->steer_q15 < -ODG_Q15_ONE||actor->steer_q15>ODG_Q15_ONE||
       actor->turn_rate_q15 < -ODG_Q15_ONE||actor->turn_rate_q15>ODG_Q15_ONE||actor->speed_fx<0||
       actor->grounded>1u||actor->trail_active>1u||actor->trail_broken>1u||
       actor->bot_mode>ODG_BOT_RETURN||actor->slide_axis>2u||actor->capture_ammo_credit!=0u||
       actor->max_hp!=100u||actor->hp>actor->max_hp||actor->satiety_permille>ODG_ACTOR_SATIETY_MAX||
       actor->hydration_permille>ODG_ACTOR_HYDRATION_MAX||actor->death_reason>ODG_DEATH_MONSTER||
       actor->trail_path_len>ODG_MAX_TRAIL_PATH_POINTS)return 0;
    if(actor->satiety_decay_accum>=ODG_ACTOR_SATIETY_DECAY_TICKS ||
       actor->hydration_decay_accum>=ODG_ACTOR_HYDRATION_DECAY_TICKS ||
       actor->starvation_accum>=ODG_STARVATION_DAMAGE_TICKS ||
       actor->dehydration_accum>=ODG_DEHYDRATION_DAMAGE_TICKS ||
       (actor->satiety_permille!=0u&&actor->starvation_accum!=0u) ||
       (actor->hydration_permille!=0u&&actor->dehydration_accum!=0u) ||
       actor->dash_cd>ODG_DASH_COOLDOWN_TICKS || actor->dash_ticks>ODG_DASH_DURATION_TICKS ||
       actor->flash_ticks>ODG_DAMAGE_FLASH_TICKS || actor->melee_cooldown_ticks>ODG_MELEE_COOLDOWN_TICKS ||
       actor->territory_recovery_ticks>=ODG_TERRITORY_RECOVERY_DELAY_TICKS ||
       actor->slide_lock_ticks>ODG_SLIDE_LOCK_TICKS || actor->ai_commit_ticks>2u*ODG_BOT_STEER_COMMIT_TICKS ||
       actor->progress_ticks>=ODG_BOT_PROGRESS_WINDOW_TICKS || actor->respawn_ticks>3u*ODG_TICK_RATE)return 0;
    if(actor->hp!=0u){
        if(actor->respawn_ticks!=0u||actor->death_reason!=ODG_DEATH_NONE)return 0;
    }else if(actor->death_reason==ODG_DEATH_NONE)return 0;
    if(actor->radius!=(expected_type==ODG_ACTOR_PLAYER?330:320))return 0;
    if((actor->last_cell!=UINT32_MAX&&actor->last_cell>=ODG_CELL_COUNT)||
       (actor->home_cell!=UINT32_MAX&&actor->home_cell>=ODG_CELL_COUNT)||
       (actor->trail_head_cell!=UINT32_MAX&&actor->trail_head_cell>=ODG_CELL_COUNT)||
       (actor->trail_render_anchor_cell!=UINT32_MAX&&actor->trail_render_anchor_cell>=ODG_CELL_COUNT)||
       (actor->ai_plan_cell!=UINT32_MAX&&actor->ai_plan_cell>=ODG_CELL_COUNT))return 0;
    if(actor->local_resident!=0u){
        if(!odg_global_fx_to_local_internal(actor->global_fx_x,actor->global_fx_z,&local_x,&local_z)||
           local_x!=actor->x||local_z!=actor->z)return 0;
    }else if(actor->x!=0||actor->z!=0)return 0;
    return 1;
}

static uint32_t save_fauna_expected_stage_internal(const odg_fauna_species_definition *definition,uint64_t age_ticks){
    uint64_t young_end,juvenile_end,adult_end;
    if(definition==NULL)return 0u;
    young_end=(uint64_t)definition->young_ticks;
    juvenile_end=young_end+(uint64_t)definition->juvenile_ticks;
    adult_end=juvenile_end+(uint64_t)definition->old_ticks;
    if(age_ticks<young_end)return ODG_FAUNA_STAGE_YOUNG;
    if(age_ticks<juvenile_end)return ODG_FAUNA_STAGE_JUVENILE;
    if(age_ticks<adult_end)return ODG_FAUNA_STAGE_ADULT;
    return ODG_FAUNA_STAGE_OLD;
}

static int save_fauna_state_validate_internal(const odg_fauna_entity *entity,uint32_t expected_id){
    const odg_fauna_species_definition *definition;int32_t local_x=0,local_z=0;
    if(entity==NULL||entity->active>1u)return 0;
    if(entity->active==0u){
        const uint8_t *bytes=(const uint8_t *)entity;size_t i;
        for(i=0u;i<sizeof(*entity);++i)if(bytes[i]!=0u)return 0;
        return odg_survival_loaded_fauna_state_validate_internal(expected_id);
    }
    definition=odg_fauna_species_internal(entity->species_id);
    if(definition==NULL||entity->id!=expected_id||entity->stable_id==0u||entity->family!=definition->family||
       !save_rng_state_validate_internal(&entity->rng)||
       (entity->face_x_q15<-ODG_Q15_ONE||entity->face_x_q15>ODG_Q15_ONE||
        entity->face_z_q15<-ODG_Q15_ONE||entity->face_z_q15>ODG_Q15_ONE)||
       (definition->variant_count==0u?entity->variant!=0u:entity->variant>=definition->variant_count)||
       entity->state<ODG_FAUNA_STATE_GROUND||entity->state>ODG_FAUNA_STATE_HUNT_PREY||
       entity->tame>1u||entity->legacy_persistent_u32!=0u||entity->legacy_target_pickup_id!=UINT32_MAX||entity->hp==0u||entity->max_hp!=definition->max_health||
       entity->hp>entity->max_hp||entity->satiety_permille>1000u||entity->hydration_permille>1000u||
       entity->life_stage<ODG_FAUNA_STAGE_YOUNG||entity->life_stage>ODG_FAUNA_STAGE_OLD||
       entity->sex>ODG_FAUNA_SEX_MALE||entity->local_resident>1u||
       entity->age_ticks>=(uint64_t)definition->lifespan_ticks||
       entity->life_stage!=save_fauna_expected_stage_internal(definition,entity->age_ticks)||
       entity->breeding_cooldown>definition->breeding_cooldown_ticks||
       entity->pregnancy_ticks>definition->gestation_or_incubation_ticks)return 0;
    if(definition->satiety_decay_ticks==0u || entity->satiety_decay_accum>=definition->satiety_decay_ticks ||
       entity->starvation_accum>=ODG_FAUNA_STARVATION_DAMAGE_TICKS ||
       (entity->satiety_permille!=0u&&entity->starvation_accum!=0u) ||
       entity->decision_ticks>ODG_FAUNA_DECISION_MAX_TICKS)return 0;
    if((definition->behavior_flags&ODG_FAUNA_BEHAVIOR_AQUATIC)!=0u){
        if(entity->hydration_permille!=1000u||entity->hydration_decay_accum!=0u||entity->dehydration_accum!=0u)return 0;
    }else{
        if((definition->hydration_decay_ticks==0u&&entity->hydration_decay_accum!=0u) ||
           (definition->hydration_decay_ticks!=0u&&entity->hydration_decay_accum>=definition->hydration_decay_ticks) ||
           entity->dehydration_accum>=ODG_FAUNA_DEHYDRATION_DAMAGE_TICKS ||
           (entity->hydration_permille!=0u&&entity->dehydration_accum!=0u))return 0;
    }
    if((entity->tame!=0u&&entity->owner_actor_id>=ODG_MAX_ACTORS)||
       (entity->tame==0u&&entity->owner_actor_id!=UINT32_MAX))return 0;
    if(entity->nest_id!=UINT32_MAX){
        if(entity->nest_id>=ODG_FAUNA_MAX_NESTS||!g_odg.fauna_nests[entity->nest_id].active||
           g_odg.fauna_nests[entity->nest_id].species_id!=entity->species_id)return 0;
    }
    if(entity->local_resident!=0u){
        if(!odg_global_fx_to_local_internal(entity->global_fx_x,entity->global_fx_z,&local_x,&local_z)||
           local_x!=entity->x||local_z!=entity->z)return 0;
    }
    return odg_survival_loaded_fauna_state_validate_internal(expected_id);
}

static void save_migrate_inactive_fauna_tombstones_internal(void){
    uint32_t i;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        if(g_odg.fauna[i].active!=0u)continue;
        /* Inactive fixed-array slots have no identity. SAVE24 makes the representation
         * canonical; legacy bytes are discarded because they cannot affect a live entity. */
        odg_memset(&g_odg.fauna[i],0,sizeof(g_odg.fauna[i]));
        odg_survival_reset_fauna(i);
    }
}

static int save_fauna_nest_state_validate_internal(const odg_fauna_nest *nest,uint32_t expected_id){
    const odg_fauna_species_definition *definition;const odg_fauna_nesting_definition *profile;
    const odg_fauna_entity *parent_a,*parent_b;
    int32_t local_x=0,local_z=0;
    if(nest==NULL||nest->active>1u)return 0;
    if(nest->active==0u){
        const uint8_t *bytes=(const uint8_t *)nest;size_t i;
        for(i=0u;i<sizeof(*nest);++i)if(bytes[i]!=0u)return 0;
        return 1;
    }
    definition=odg_fauna_species_internal(nest->species_id);profile=odg_fauna_nesting_internal(nest->species_id);
    if(definition==NULL||profile==NULL||definition->reproduction_mode!=ODG_FAUNA_REPRO_EGG||
       nest->id!=expected_id||nest->stable_id==0u||nest->substrate==0u||
       (nest->substrate&(nest->substrate-1u))!=0u||(profile->substrate_mask&nest->substrate)==0u||
       nest->egg_count==0u||nest->egg_count>definition->offspring_max||
       nest->hatch_ticks>definition->gestation_or_incubation_ticks||nest->parent_a>=ODG_FAUNA_MAX_ENTRIES||
       nest->parent_b>=ODG_FAUNA_MAX_ENTRIES||nest->parent_a==nest->parent_b||nest->local_resident>1u)return 0;
    parent_a=&g_odg.fauna[nest->parent_a];parent_b=&g_odg.fauna[nest->parent_b];
    if(!parent_a->active||!parent_b->active||parent_a->species_id!=nest->species_id||parent_b->species_id!=nest->species_id||
       parent_a->nest_id!=expected_id||parent_b->nest_id!=expected_id||
       parent_a->sex!=ODG_FAUNA_SEX_FEMALE||parent_b->sex!=ODG_FAUNA_SEX_MALE)return 0;
    if(nest->substrate==ODG_NEST_SUBSTRATE_TREE){if(nest->host_resource_stable_id==0u)return 0;}
    else if(nest->host_resource_stable_id!=0u)return 0;
    if(nest->local_resident!=0u){
        if(!odg_global_fx_to_local_internal(nest->global_fx_x,nest->global_fx_z,&local_x,&local_z)||
           local_x!=nest->x||local_z!=nest->z)return 0;
    }
    return 1;
}

static int save_current_world_semantics_validate_internal(void){
    uint32_t i,fauna_active=0u,nests_active=0u;
    if(!save_runtime_count_bounds_validate_internal()||!save_fixed_world_state_validate_internal())return 0;
    for(i=0u;i<ODG_MAX_ACTORS;++i)if(!save_actor_state_validate_internal(&g_odg.actors[i],i))return 0;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        if(!save_fauna_state_validate_internal(&g_odg.fauna[i],i))return 0;
        if(g_odg.fauna[i].active!=0u)++fauna_active;
    }
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i){
        if(!save_fauna_nest_state_validate_internal(&g_odg.fauna_nests[i],i))return 0;
        if(g_odg.fauna_nests[i].active!=0u)++nests_active;
    }
    if(fauna_active!=g_odg.fauna_count||nests_active!=g_odg.fauna_nest_count)return 0;
    if(!odg_construction_loaded_state_validate_internal(ODG_SAVE_SCHEMA_VERSION))return 0;
    for(i=0u;i<g_odg.resource_count;++i)
        if(!odg_resource_state_validate_internal(&g_odg_resources[i],i))return 0;
    for(i=0u;i<g_odg.turret_count;++i)
        if(!odg_turret_state_validate_internal(&g_odg_turrets[i],i))return 0;
    for(i=0u;i<g_odg.chunk_cache_used;++i)
        if(!odg_chunk_runtime_state_validate_internal(&g_odg_chunk_cache[i],i))return 0;
    if(!odg_chunks_derived_cache_validate_internal())return 0;
    for(i=0u;i<g_odg.pickup_count;++i){
        const odg_world_pickup *p=&g_odg_pickups[i];
        if(p->active)continue;
        if(p->id!=i||p->x!=0||p->z!=0||p->global_fx_x!=0||p->global_fx_z!=0||
           p->local_resident!=0u||p->pickup_cd!=0u||p->age_ticks!=0u||p->lifetime_ticks!=0u||
           !save_stack_canonical_validate(&p->stack))return 0;
    }
    for(i=0u;i<g_odg.turret_count;++i){
        const odg_turret *t=&g_odg_turrets[i];
        /* A procedural record exists only while materialized. Sleeping natural turrets
         * are represented solely by chunk state; accepting an inactive procedural copy
         * would reintroduce the stale floating-origin cache SAVE23 removed. */
        if(!t->active&&t->procedural!=0u)return 0;
        if(t->active||t->instance_id==0u)continue;
        if(t->x!=0||t->z!=0||t->global_fx_x!=0||t->global_fx_z!=0||t->local_resident!=0u)return 0;
    }
    for(i=0u;i<g_odg.artifact_count;++i)
        if(!odg_artifact_state_validate_internal(&g_odg_artifacts[i],i))return 0;
    if(!odg_artifact_cross_reference_validate_internal())return 0;
    return 1;
}

static uint64_t save_instance_hash(uint64_t id){
    id^=id>>33u;id*=UINT64_C(0xff51afd7ed558ccd);id^=id>>33u;
    id*=UINT64_C(0xc4ceb9fe1a85ec53);id^=id>>33u;return id;
}

static int save_instance_register(uint64_t *table,size_t capacity,uint64_t id){
    size_t mask,index,probe;
    if(table==NULL||capacity==0u||(capacity&(capacity-1u))!=0u||id==0u)return 0;
    mask=capacity-1u;index=(size_t)(save_instance_hash(id)&(uint64_t)mask);
    for(probe=0u;probe<capacity;++probe){
        uint64_t *slot=&table[(index+probe)&mask];
        if(*slot==0u){*slot=id;return 1;}
        if(*slot==id)return 0;
    }
    return 0;
}

static int save_stack_is_world_handle(const odg_item_stack *stack){
    if(stack==NULL||stack->payload_id==0u)return 0;
    return stack->type_id==ODG_ITEM_TURRET||odg_artifact_item_deployable_internal(stack->type_id);
}

static int save_register_unique_stack_instance(const odg_item_stack *stack,uint64_t *table,size_t capacity,
                                               uint64_t *max_sequential){
    const odg_item_definition *definition;
    if(stack==NULL||odg_item_stack_empty_internal(stack))return 1;
    definition=odg_item_definition_internal(stack->type_id);if(definition==NULL)return 0;
    if(definition->max_stack>1u||save_stack_is_world_handle(stack))return 1;
    if(stack->instance_id==0u||(stack->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u)return 0;
    if(!save_instance_register(table,capacity,stack->instance_id))return 0;
    if(stack->instance_id>*max_sequential)*max_sequential=stack->instance_id;
    return 1;
}

static int odg_save_instance_semantics_validate_internal(void){
    uint64_t max_entries64,max_sequential=0u,needed64;uint64_t *table=NULL;size_t capacity=16u;uint32_t i,j;int ok=0;
    max_entries64=(uint64_t)g_odg.turret_count+(uint64_t)g_odg.artifact_count+(uint64_t)g_odg.resource_count+
                  (uint64_t)g_odg_construction_count+(uint64_t)ODG_MAX_ACTORS*(uint64_t)ODG_INVENTORY_MAX_SLOTS+
                  (uint64_t)g_odg.pickup_count+(uint64_t)g_odg.artifact_count*(uint64_t)ODG_CHEST_SLOTS;
    if(max_entries64>UINT64_MAX/2u)return 0;
    needed64=max_entries64*2u+1u;
    while((uint64_t)capacity<needed64){if(capacity>SIZE_MAX/2u)return 0;capacity*=2u;}
    if(capacity>SIZE_MAX/sizeof(uint64_t))return 0;
    table=(uint64_t *)odg_mem_realloc(NULL,capacity*sizeof(uint64_t));if(table==NULL)return 0;
    odg_memset(table,0,capacity*sizeof(uint64_t));

    /* Turret/artifact records are the primary identity while carried/stored payload
     * items are handles to the same inactive record and are therefore not registered a
     * second time. Construction has no persistent item handle after dismantling. */
    for(i=0u;i<g_odg.turret_count;++i){
        const odg_turret *t=&g_odg_turrets[i];
        if(t->instance_id==0u)continue;
        if((t->procedural!=0u)!=((t->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u))goto done;
        if(!save_instance_register(table,capacity,t->instance_id))goto done;
        if(t->procedural==0u&&t->instance_id>max_sequential)max_sequential=t->instance_id;
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *a=&g_odg_artifacts[i];
        if(a->instance_id==0u)continue;
        if((a->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u||!save_instance_register(table,capacity,a->instance_id))goto done;
        if(a->instance_id>max_sequential)max_sequential=a->instance_id;
    }
    for(i=0u;i<g_odg_construction_count;++i){
        const odg_construction_block *b=&g_odg_construction_blocks[i];
        if(!b->active||b->instance_id==0u||(b->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u||
           !save_instance_register(table,capacity,b->instance_id))goto done;
        if(b->instance_id>max_sequential)max_sequential=b->instance_id;
    }
    /* SAVE25 makes every non-procedural resource a true manual identity. Resource array
     * indices are paging/interaction handles only and may change when depleted remnants
     * compact; stable_id is the monotonic primary identity and therefore participates in
     * the same uniqueness/cursor proof as tools, artifacts and construction. */
    for(i=0u;i<g_odg.resource_count;++i){
        const odg_resource_node *r=&g_odg_resources[i];
        if(r->procedural!=0u)continue;
        if(r->stable_id==0u||(r->stable_id&ODG_RESOURCE_STABLE_PROCEDURAL_BIT)!=0u||
           !save_instance_register(table,capacity,r->stable_id))goto done;
        if(r->stable_id>max_sequential)max_sequential=r->stable_id;
    }
    for(i=0u;i<ODG_MAX_ACTORS;++i)for(j=0u;j<ODG_INVENTORY_MAX_SLOTS;++j)
        if(!save_register_unique_stack_instance(&g_odg.actors[i].inventory.slots[j],table,capacity,&max_sequential))goto done;
    for(i=0u;i<g_odg.pickup_count;++i)if(g_odg_pickups[i].active&&
       !save_register_unique_stack_instance(&g_odg_pickups[i].stack,table,capacity,&max_sequential))goto done;
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *a=&g_odg_artifacts[i];if(!a->active)continue;
        for(j=0u;j<ODG_CHEST_SLOTS;++j)
            if(!save_register_unique_stack_instance(&a->storage.slots[j],table,capacity,&max_sequential))goto done;
    }
    /* Sequential IDs are monotonic in every current world. High-bit procedural turret
     * IDs live in a separate deterministic namespace and do not constrain this cursor. */
    if(g_odg.next_instance_id==0u||g_odg.next_instance_id>ODG_INSTANCE_ID_PROCEDURAL_BIT||
       g_odg.next_instance_id<=max_sequential)goto done;
    ok=1;
done:
    odg_mem_free(table);return ok;
}

int odg_save_identity_validate_internal(void){
    uint8_t *turret_refs=NULL,*artifact_refs=NULL;uint32_t i,j;int ok=0;
    if(g_odg.turret_count!=0u){turret_refs=(uint8_t *)odg_mem_realloc(NULL,(size_t)g_odg.turret_count);if(turret_refs==NULL)goto done;odg_memset(turret_refs,0,(size_t)g_odg.turret_count);}
    if(g_odg.artifact_count!=0u){artifact_refs=(uint8_t *)odg_mem_realloc(NULL,(size_t)g_odg.artifact_count);if(artifact_refs==NULL)goto done;odg_memset(artifact_refs,0,(size_t)g_odg.artifact_count);}

    for(i=0u;i<g_odg.turret_count;++i){
        const odg_turret *t=&g_odg_turrets[i];
        if((t->active||t->procedural!=0u||t->instance_id!=0u)&&(t->id!=i||t->instance_id==0u))goto done;
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *a=&g_odg_artifacts[i];
        if((a->active||a->instance_id!=0u)&&(a->id!=i||a->instance_id==0u))goto done;
    }
    for(i=0u;i<ODG_MAX_ACTORS;++i){
        for(j=0u;j<ODG_INVENTORY_MAX_SLOTS;++j)
            if(!save_payload_stack_validate(&g_odg.actors[i].inventory.slots[j],turret_refs,artifact_refs))goto done;
    }
    for(i=0u;i<g_odg.pickup_count;++i){
        if(g_odg_pickups[i].active&&!save_payload_stack_validate(&g_odg_pickups[i].stack,turret_refs,artifact_refs))goto done;
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *a=&g_odg_artifacts[i];
        if(!a->active)continue;
        for(j=0u;j<ODG_CHEST_SLOTS;++j)
            if(!save_payload_stack_validate(&a->storage.slots[j],turret_refs,artifact_refs))goto done;
    }

    /* A dormant stateful object is not a second kind of free slot: it is the backing
     * record for exactly one carried/stored world-handle item. Requiring the reverse
     * edge as well as duplicate protection prevents unreachable orphan records from
     * leaking identity/capacity forever in an otherwise checksum-valid save. Active
     * objects and true tombstones must not have any inventory handle at all. */
    for(i=0u;i<g_odg.turret_count;++i){
        const odg_turret *t=&g_odg_turrets[i];
        uint8_t expected=(!t->active&&t->procedural==0u&&t->instance_id!=0u)?1u:0u;
        if(turret_refs!=NULL&&turret_refs[i]!=expected)goto done;
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *a=&g_odg_artifacts[i];
        uint8_t expected=(!a->active&&a->instance_id!=0u)?1u:0u;
        if(artifact_refs!=NULL&&artifact_refs[i]!=expected)goto done;
    }
    ok=1;
done:
    odg_mem_free(turret_refs);odg_mem_free(artifact_refs);return ok;
}

static void save_migration_max_sequential_stack(const odg_item_stack *stack,uint64_t *max_id){
    if(stack==NULL||max_id==NULL||stack->instance_id==0u||
       (stack->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u)return;
    if(stack->instance_id>*max_id)*max_id=stack->instance_id;
}

static void save_migration_rewrite_turret_handle(odg_item_stack *stack,uint32_t turret_id,
                                                 uint64_t old_id,uint64_t new_id){
    if(stack==NULL)return;
    if(stack->type_id==ODG_ITEM_TURRET&&stack->payload_id==(uint64_t)turret_id+UINT64_C(1)&&
       stack->instance_id==old_id)stack->instance_id=new_id;
}

static int save_migrate_manual_turret_instance_namespace_internal(void){
    uint64_t max_id=0u;uint32_t i,j;int needs_migration=0;
    /* SAVE23 canonicalizes non-authoritative spatial caches of portable dormant objects
     * and dead pickup slots. SAVE22 and older could legally retain their last local
     * coordinates even though placement always reconstructs them. Clearing them here
     * prevents an upgraded world from carrying floating-origin history forever. */
    for(i=0u;i<g_odg.pickup_count;++i){
        odg_world_pickup *p=&g_odg_pickups[i];
        if(!p->active){uint32_t id=p->id;odg_memset(p,0,sizeof(*p));p->id=id;}
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        odg_artifact *a=&g_odg_artifacts[i];
        if(!a->active&&a->instance_id==0u){uint32_t id=a->id;odg_memset(a,0,sizeof(*a));a->id=id;continue;}
        if(!a->active){a->x=0;a->z=0;a->global_fx_x=0;a->global_fx_z=0;a->local_resident=0u;}
    }
    for(i=0u;i<g_odg.turret_count;++i){
        odg_turret *t=&g_odg_turrets[i];
        if(!t->active&&t->procedural!=0u){
            /* Historical SAVE22 could serialize a sleeping natural turret even though
             * its authoritative state had already been stored in the chunk override.
             * SAVE23 canonicalizes that duplicate materialization to a free tombstone. */
            uint32_t id=t->id;odg_memset(t,0,sizeof(*t));t->id=id;continue;
        }
        if(!t->active&&t->instance_id!=0u){
            t->x=0;t->z=0;t->global_fx_x=0;t->global_fx_z=0;t->local_resident=0u;
        }
    }
    for(i=0u;i<g_odg.turret_count;++i){
        const odg_turret *t=&g_odg_turrets[i];
        if(t->instance_id!=0u&&t->procedural==0u&&(t->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u)
            needs_migration=1;
        if(t->procedural==0u&&t->instance_id>max_id&&(t->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)==0u)
            max_id=t->instance_id;
    }
    if(!needs_migration)return 1;
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *a=&g_odg_artifacts[i];
        if(a->instance_id!=0u&&(a->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)==0u&&a->instance_id>max_id)max_id=a->instance_id;
        for(j=0u;j<ODG_CHEST_SLOTS;++j)save_migration_max_sequential_stack(&a->storage.slots[j],&max_id);
    }
    for(i=0u;i<g_odg_construction_count;++i){
        const odg_construction_block *b=&g_odg_construction_blocks[i];
        if(b->instance_id!=0u&&(b->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)==0u&&b->instance_id>max_id)max_id=b->instance_id;
    }
    for(i=0u;i<ODG_MAX_ACTORS;++i)for(j=0u;j<ODG_INVENTORY_MAX_SLOTS;++j)
        save_migration_max_sequential_stack(&g_odg.actors[i].inventory.slots[j],&max_id);
    for(i=0u;i<g_odg.pickup_count;++i)save_migration_max_sequential_stack(&g_odg_pickups[i].stack,&max_id);
    if(max_id>=ODG_INSTANCE_ID_SEQUENTIAL_MAX)return 0;
    if(g_odg.next_instance_id==0u||g_odg.next_instance_id<=max_id||
       g_odg.next_instance_id>=ODG_INSTANCE_ID_PROCEDURAL_BIT)g_odg.next_instance_id=max_id+UINT64_C(1);

    for(i=0u;i<g_odg.turret_count;++i){
        odg_turret *t=&g_odg_turrets[i];uint64_t old_id,new_id;
        if(t->instance_id==0u||t->procedural!=0u||(t->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)==0u)continue;
        old_id=t->instance_id;new_id=odg_next_instance_id();if(new_id==0u)return 0;
        for(j=0u;j<ODG_MAX_ACTORS;++j){uint32_t k;for(k=0u;k<ODG_INVENTORY_MAX_SLOTS;++k)
            save_migration_rewrite_turret_handle(&g_odg.actors[j].inventory.slots[k],i,old_id,new_id);}
        for(j=0u;j<g_odg.pickup_count;++j)
            save_migration_rewrite_turret_handle(&g_odg_pickups[j].stack,i,old_id,new_id);
        for(j=0u;j<g_odg.artifact_count;++j){uint32_t k;for(k=0u;k<ODG_CHEST_SLOTS;++k)
            save_migration_rewrite_turret_handle(&g_odg_artifacts[j].storage.slots[k],i,old_id,new_id);}
        t->instance_id=new_id;
        /* Historical carried natural turrets could retain their derived procedural
         * cadence/lock offsets after procedural=0. SAVE23's namespace migration also
         * crosses that semantic boundary: portable/manual objects use the canonical
         * tier profile, while ammo remains persistent payload state. */
        odg_apply_turret_tier(t,t->material_tier,1);
    }
    return 1;
}

static uint8_t *write_section(uint8_t *cursor,uint32_t count,const void *data,size_t elem_size) {
    put_u32_le(cursor,count);cursor+=4u;
    if(count!=0u){size_t bytes=(size_t)count*elem_size;odg_memcpy(cursor,data,bytes);cursor+=bytes;}
    return cursor;
}

static int read_section_header(const uint8_t **cursor,uint64_t *remaining,uint32_t *out_count,
                               size_t elem_size,const uint8_t **out_data) {
    uint32_t count;uint64_t bytes;
    if(cursor==NULL||*cursor==NULL||remaining==NULL||out_count==NULL||out_data==NULL||*remaining<UINT64_C(4))return 0;
    count=get_u32_le(*cursor);*cursor+=4u;*remaining-=UINT64_C(4);
    bytes=(uint64_t)count*(uint64_t)elem_size;
    if(bytes>*remaining)return 0;
    *out_count=count;*out_data=*cursor;*cursor+=(size_t)bytes;*remaining-=bytes;return 1;
}

static uint32_t save_schema_next(uint32_t schema_version){
    uint32_t i;
    if(schema_version==ODG_SAVE_SCHEMA_VERSION)return schema_version;
    for(i=0u;i<(uint32_t)(sizeof(g_save_migrations)/sizeof(g_save_migrations[0]));++i)
        if(g_save_migrations[i].from_schema==schema_version)return g_save_migrations[i].to_schema;
    return 0u;
}
uint32_t odg_save_schema_version(void) { return ODG_SAVE_SCHEMA_VERSION; }
uint32_t odg_save_schema_supported(uint32_t schema_version) {
    uint32_t current=schema_version,guard=0u;
    /* Central DataVersion-style migration authority. Hosts never infer compatibility from
     * API/FFI. Cycles/unknown gaps fail closed. */
    while(current!=ODG_SAVE_SCHEMA_VERSION && guard++<16u){current=save_schema_next(current);if(current==0u)return 0u;}
    return current==ODG_SAVE_SCHEMA_VERSION?1u:0u;
}
uint64_t odg_save_blob_size(void) {
    return SAVE_HEADER_BYTES+payload_size_for(g_odg.turret_count,g_odg.pickup_count,g_odg.resource_count,
                                              g_odg.artifact_count,g_odg_construction_count,g_odg.chunk_cache_used);
}

int32_t odg_save_write(uint8_t *out_blob,uint64_t capacity,uint64_t *out_required) {
    uint64_t payload_size,required,checksum;uint8_t *cursor;
    const uint8_t *prefix=(const uint8_t *)&g_odg+logical_offset();
    const uint8_t *suffix=(const uint8_t *)&g_odg+suffix_offset();
    if(!g_odg.initialized||!odg_save_current_semantics_validate_internal()||
       !save_current_world_semantics_validate_internal()||
       !odg_survival_state_validate(&g_odg_persistent_runtime)||
       !odg_save_instance_semantics_validate_internal()||
       !odg_save_identity_validate_internal())return ODG_STATUS_INVALID_STATE;
    payload_size=payload_size_for(g_odg.turret_count,g_odg.pickup_count,g_odg.resource_count,
                                  g_odg.artifact_count,g_odg_construction_count,g_odg.chunk_cache_used);
    required=SAVE_HEADER_BYTES+payload_size;if(out_required!=NULL)*out_required=required;
    if(payload_size>UINT32_MAX)return ODG_STATUS_INVALID_STATE;
    if(out_blob==NULL||capacity<required)return ODG_STATUS_BUFFER_TOO_SMALL;
    out_blob[0]=SAVE_MAGIC_0;out_blob[1]=SAVE_MAGIC_1;out_blob[2]=SAVE_MAGIC_2;out_blob[3]=SAVE_MAGIC_3;
    out_blob[4]=SAVE_MAGIC_4;out_blob[5]=SAVE_MAGIC_5;out_blob[6]=SAVE_MAGIC_6;out_blob[7]=SAVE_MAGIC_7;
    put_u32_le(out_blob+8,ODG_SAVE_SCHEMA_VERSION);put_u32_le(out_blob+12,ODG_API_VERSION);put_u32_le(out_blob+16,ODG_FFI_ABI_VERSION);
    put_u32_le(out_blob+20,(uint32_t)payload_size);put_u64_le(out_blob+24,odg_state_hash());
    cursor=out_blob+SAVE_HEADER_BYTES;odg_memcpy(cursor,prefix,prefix_size());
    save_canonicalize_non_authority_bytes(cursor,NULL);cursor+=prefix_size();
    cursor=write_section(cursor,g_odg.turret_count,g_odg_turrets,sizeof(*g_odg_turrets));
    cursor=write_section(cursor,g_odg.pickup_count,g_odg_pickups,sizeof(*g_odg_pickups));
    cursor=write_section(cursor,g_odg.resource_count,g_odg_resources,sizeof(*g_odg_resources));
    cursor=write_section(cursor,g_odg.artifact_count,g_odg_artifacts,sizeof(*g_odg_artifacts));
    cursor=write_section(cursor,g_odg_construction_count,g_odg_construction_blocks,sizeof(*g_odg_construction_blocks));
    cursor=write_section(cursor,g_odg.chunk_cache_used,g_odg_chunk_cache,sizeof(*g_odg_chunk_cache));
    cursor=write_section(cursor,1u,&g_odg_persistent_runtime,sizeof(g_odg_persistent_runtime));
    odg_memcpy(cursor,suffix,suffix_size());save_canonicalize_non_authority_bytes(NULL,cursor);
    checksum=payload_checksum(out_blob+SAVE_HEADER_BYTES,(size_t)payload_size);put_u64_le(out_blob+32,checksum);return ODG_STATUS_OK;
}

static void migrate_legacy_nests_from_prefix(const uint8_t *prefix_src){
    const size_t nest_offset=offsetof(odg_world,fauna_nests)-offsetof(odg_world,seed);
    const odg_fauna_nest_save15 *old_nests=(const odg_fauna_nest_save15 *)(const void *)(prefix_src+nest_offset);
    uint32_t i;
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i){
        const odg_fauna_nest_save15 *old=&old_nests[i];odg_fauna_nest *n=&g_odg.fauna_nests[i];
        odg_memset(n,0,sizeof(*n));
        n->active=old->active;n->id=old->id;n->stable_id=old->stable_id;n->species_id=old->species_id;
        n->substrate=old->active!=0u?(old->host_resource_stable_id!=0u?ODG_NEST_SUBSTRATE_TREE:ODG_NEST_SUBSTRATE_GROUND):0u;
        n->egg_count=old->egg_count;n->hatch_ticks=old->hatch_ticks;n->parent_a=old->parent_a;n->parent_b=old->parent_b;
        n->host_resource_stable_id=old->host_resource_stable_id;n->x=old->x;n->z=old->z;
        n->global_fx_x=old->global_fx_x;n->global_fx_z=old->global_fx_z;n->local_resident=old->local_resident;
    }
}

int32_t odg_save_load(const uint8_t *blob,uint64_t size) {
    uint32_t schema,api,ffi,stored_size,tc,pc,rc,ac,bc=0u,cc,runtime_count=0u;
    int migrate_canonical_resources=0,migrate_resource_id_namespaces=0;
    int32_t failure_status=ODG_STATUS_INVALID_ARGUMENT;
    odg_save_load_backup *backup=NULL;
    uint64_t checksum,expected_hash,remaining;
    const uint8_t *cursor,*td,*pd,*rd,*ad,*bd=NULL,*cd,*runtime_data=NULL,*prefix_src,*suffix_src;
    uint8_t *prefix=(uint8_t *)&g_odg+logical_offset();uint8_t *suffix=(uint8_t *)&g_odg+suffix_offset();
    if(!g_odg.initialized)return ODG_STATUS_INVALID_STATE;
    if(blob==NULL||size<SAVE_HEADER_BYTES)return ODG_STATUS_INVALID_ARGUMENT;
    if(blob[0]!=SAVE_MAGIC_0||blob[1]!=SAVE_MAGIC_1||blob[2]!=SAVE_MAGIC_2||blob[3]!=SAVE_MAGIC_3||
       blob[4]!=SAVE_MAGIC_4||blob[5]!=SAVE_MAGIC_5||blob[6]!=SAVE_MAGIC_6||blob[7]!=SAVE_MAGIC_7)return ODG_STATUS_VERSION_MISMATCH;
    schema=get_u32_le(blob+8);api=get_u32_le(blob+12);ffi=get_u32_le(blob+16);stored_size=get_u32_le(blob+20);
    if(odg_save_schema_supported(schema)==0u)return ODG_STATUS_VERSION_MISMATCH;
    if(schema!=ODG_SAVE_SCHEMA_VERSION&&schema!=ODG_SAVE_SCHEMA_LEGACY24&&schema!=ODG_SAVE_SCHEMA_LEGACY23&&schema!=ODG_SAVE_SCHEMA_LEGACY22&&schema!=ODG_SAVE_SCHEMA_LEGACY21&&schema!=ODG_SAVE_SCHEMA_LEGACY20&&schema!=ODG_SAVE_SCHEMA_LEGACY19&&
       schema!=ODG_SAVE_SCHEMA_LEGACY18&&schema!=ODG_SAVE_SCHEMA_LEGACY17&&schema!=ODG_SAVE_SCHEMA_LEGACY16&&schema!=ODG_SAVE_SCHEMA_LEGACY15&&
       schema!=ODG_SAVE_SCHEMA_LEGACY14)return ODG_STATUS_VERSION_MISMATCH;
    /* API/FFI in the header are provenance only. Save compatibility is governed by the
     * data schema/migration authority above, so a UI ABI change cannot strand a world. */
    (void)api;(void)ffi;
    if(size!=SAVE_HEADER_BYTES+(uint64_t)stored_size)return ODG_STATUS_INVALID_ARGUMENT;
    if((uint64_t)stored_size<(uint64_t)prefix_size()+UINT64_C(20)+(uint64_t)suffix_size())return ODG_STATUS_INVALID_ARGUMENT;
    checksum=get_u64_le(blob+32);if(payload_checksum(blob+SAVE_HEADER_BYTES,stored_size)!=checksum)return ODG_STATUS_INVALID_ARGUMENT;
    expected_hash=get_u64_le(blob+24);cursor=blob+SAVE_HEADER_BYTES;remaining=stored_size;
    if(remaining<(uint64_t)prefix_size())return ODG_STATUS_INVALID_ARGUMENT;
    prefix_src=cursor;cursor+=prefix_size();remaining-=(uint64_t)prefix_size();
    if(!read_section_header(&cursor,&remaining,&tc,sizeof(odg_turret),&td) ||
       !read_section_header(&cursor,&remaining,&pc,sizeof(odg_world_pickup),&pd) ||
       !read_section_header(&cursor,&remaining,&rc,sizeof(odg_resource_node),&rd) ||
       !read_section_header(&cursor,&remaining,&ac,sizeof(odg_artifact),&ad))return ODG_STATUS_INVALID_ARGUMENT;
    if(schema>=ODG_SAVE_SCHEMA_LEGACY18){
        if(!read_section_header(&cursor,&remaining,&bc,sizeof(odg_construction_block),&bd))return ODG_STATUS_INVALID_ARGUMENT;
    }
    if(!read_section_header(&cursor,&remaining,&cc,sizeof(odg_chunk_runtime),&cd))return ODG_STATUS_INVALID_ARGUMENT;
    if(schema>=ODG_SAVE_SCHEMA_LEGACY17){
        if(!read_section_header(&cursor,&remaining,&runtime_count,sizeof(odg_persistent_runtime_state),&runtime_data) ||
           runtime_count!=1u)return ODG_STATUS_INVALID_ARGUMENT;
    }
    if(remaining!=(uint64_t)suffix_size())return ODG_STATUS_INVALID_ARGUMENT;
    suffix_src=cursor;
    if(schema==ODG_SAVE_SCHEMA_VERSION&&!save_non_authority_bytes_are_canonical(prefix_src,suffix_src))
        return ODG_STATUS_INVALID_ARGUMENT;
    /* Everything above is read-only parsing. Capture the live authority before the first
     * destructive reset so every later validation/migration failure can restore exactly
     * the world that was running when load() was called. */
    backup=save_load_backup_capture();if(backup==NULL)return ODG_STATUS_INVALID_STATE;
    if((tc!=0u&&!odg_entities_reserve_turrets(tc)) || (pc!=0u&&!odg_entities_reserve_pickups(pc)) ||
       (rc!=0u&&!odg_entities_reserve_resources(rc)) || (ac!=0u&&!odg_entities_reserve_artifacts(ac)) ||
       (bc!=0u&&!odg_entities_reserve_construction(bc)) || (cc!=0u&&!odg_chunks_reserve_runtime(cc))){
        save_load_backup_dispose(backup);return ODG_STATUS_INVALID_STATE;
    }
    odg_entities_reset_runtime();odg_chunks_reset_runtime();
    if(schema>=ODG_SAVE_SCHEMA_LEGACY17){
        odg_memcpy(&g_odg_persistent_runtime,runtime_data,sizeof(g_odg_persistent_runtime));
        if(!odg_survival_state_validate(&g_odg_persistent_runtime)){failure_status=ODG_STATUS_INVALID_ARGUMENT;goto rollback;}
        /* SAVE22 is the explicit provenance boundary for canonical procedural resources.
         * Modern v2/v3 worlds advance to v4 while genuine v1 worlds remain frozen. The
         * version-specific terrain/turret thresholds preserve every earlier generation
         * rule; v4 only canonicalizes procedural resource identity/placement. SAVE25
         * then advances v2+ worlds from v4 to v5's disjoint resource-id namespaces. */
        if(schema<=ODG_SAVE_SCHEMA_LEGACY21 &&
           g_odg_persistent_runtime.worldgen_version>=ODG_WORLDGEN_VERSION_BATHYMETRY &&
           g_odg_persistent_runtime.worldgen_version<ODG_WORLDGEN_VERSION_CANONICAL_RESOURCES){
            migrate_canonical_resources=1;
            g_odg_persistent_runtime.worldgen_version=ODG_WORLDGEN_VERSION_CANONICAL_RESOURCES;
        }
    }else odg_survival_reset_legacy_world();
    if(schema<=ODG_SAVE_SCHEMA_LEGACY24&&
       g_odg_persistent_runtime.worldgen_version!=ODG_WORLDGEN_VERSION_RESOURCE_ID_NAMESPACES)
        migrate_resource_id_namespaces=1;
    odg_memcpy(prefix,prefix_src,prefix_size());
    if(schema==ODG_SAVE_SCHEMA_LEGACY14||schema==ODG_SAVE_SCHEMA_LEGACY15)migrate_legacy_nests_from_prefix(prefix_src);
    if(tc!=0u)odg_memcpy(g_odg_turrets,td,(size_t)tc*sizeof(*g_odg_turrets));
    if(pc!=0u)odg_memcpy(g_odg_pickups,pd,(size_t)pc*sizeof(*g_odg_pickups));
    if(rc!=0u)odg_memcpy(g_odg_resources,rd,(size_t)rc*sizeof(*g_odg_resources));
    if(ac!=0u)odg_memcpy(g_odg_artifacts,ad,(size_t)ac*sizeof(*g_odg_artifacts));
    if(schema>=ODG_SAVE_SCHEMA_LEGACY18){
        uint32_t bi;
        if(bc!=0u)odg_memcpy(g_odg_construction_blocks,bd,(size_t)bc*sizeof(*g_odg_construction_blocks));
        g_odg_construction_count=bc;
        if(schema<=ODG_SAVE_SCHEMA_LEGACY19){
            /* SAVE18/19 stored these exact bytes as reserved zeros. SAVE20 gives them
             * explicit integrity semantics; migration reconstructs full health from
             * material + shape without inventing damage history. */
            for(bi=0u;bi<g_odg_construction_count;++bi){
                odg_construction_block *b=&g_odg_construction_blocks[bi];
                b->max_health=odg_construction_max_health_internal(b->material_tier,b->shape);
                b->health=b->max_health;b->reserved_u32=0u;
            }
        }
        if(!odg_construction_loaded_state_validate_internal(schema)){failure_status=ODG_STATUS_INVALID_ARGUMENT;goto rollback;}
    }else{
        uint32_t read_i,write_i=0u;
        g_odg_construction_count=0u;
        for(read_i=0u;read_i<ac;++read_i){
            odg_artifact *a=&g_odg_artifacts[read_i];
            if(a->active&&a->item_type==ODG_ITEM_BUILDING_BLOCK){
                if(!odg_construction_import_legacy_artifact_internal(a)){failure_status=ODG_STATUS_INVALID_STATE;goto rollback;}
                continue;
            }
            if(write_i!=read_i)g_odg_artifacts[write_i]=*a;
            if(write_i<ac)g_odg_artifacts[write_i].id=write_i;
            ++write_i;
        }
        if(write_i<ac)odg_memset(&g_odg_artifacts[write_i],0,(size_t)(ac-write_i)*sizeof(*g_odg_artifacts));
        ac=write_i;
    }
    if(cc!=0u)odg_memcpy(g_odg_chunk_cache,cd,(size_t)cc*sizeof(*g_odg_chunk_cache));
    g_odg.turret_count=tc;g_odg.pickup_count=pc;g_odg.resource_count=rc;g_odg.artifact_count=ac;g_odg.chunk_cache_used=cc;
    odg_memcpy(suffix,suffix_src,suffix_size());
    /* SAVE24 removes presentation/derived bytes from logical persistence. Legacy saves
     * may contain them; discard them before migration continues. Current saves are
     * required to carry canonical zeros, so this is idempotent for SAVE24. */
    save_canonicalize_non_authority_bytes(prefix,suffix);
    /* `playable[]` is a frozen raw-layout tombstone from the retired finite arena.
     * Legacy schemas may contain historical mask bytes; Open Domain must never revive
     * them as gameplay authority. Current-schema bytes remain untouched until exact-hash
     * validation so round-trip integrity is still strict. */
    if(schema==ODG_SAVE_SCHEMA_VERSION){
        if(odg_state_hash()!=expected_hash){failure_status=ODG_STATUS_INVALID_ARGUMENT;goto rollback;}
    }else{
        g_odg.playable_count=ODG_CELL_COUNT;
        odg_memset(g_odg.playable,1,sizeof(g_odg.playable));
        /* Finite-arena saves could persist a terminal winner. Open Domain has no terminal
         * match state; carrying that flag forward would freeze the simulation after a
         * successful migration. Normalize the retired rule explicitly at the schema
         * boundary rather than accepting it as current SAVE24 authority. */
        g_odg.match_over=0u;g_odg.winner_id=UINT32_MAX;
        save_migrate_inactive_fauna_tombstones_internal();
    }
    /* SAVE22 was already stack-canonical, but it predates the strict separation between
     * high-bit procedural turret ids and portable/manual sequential ids. Validate its
     * stack surface first, then migrate any historically carried natural turret by
     * rewriting both the primary turret record and its exact payload handle. */
    if((schema==ODG_SAVE_SCHEMA_VERSION||schema==ODG_SAVE_SCHEMA_LEGACY24||schema==ODG_SAVE_SCHEMA_LEGACY23||schema==ODG_SAVE_SCHEMA_LEGACY22)&&
       !odg_save_current_semantics_validate_internal()){
        failure_status=ODG_STATUS_INVALID_ARGUMENT;goto rollback;
    }
    if(schema<=ODG_SAVE_SCHEMA_LEGACY22&&!save_migrate_manual_turret_instance_namespace_internal()){
        failure_status=ODG_STATUS_INVALID_ARGUMENT;goto rollback;
    }
    if(migrate_canonical_resources)odg_resources_migrate_canonical_worldgen_internal();
    if(migrate_resource_id_namespaces&&!odg_resources_migrate_identity_worldgen_internal()){
        failure_status=ODG_STATUS_INVALID_ARGUMENT;goto rollback;
    }
    /* Resource/worldgen migrations may compact or rematerialize resident nodes. Chunk
     * summaries are derived cache, so rebuild them before validating current cross-cache
     * invariants instead of validating stale pre-migration acceleration data. */
    if(migrate_canonical_resources||migrate_resource_id_namespaces)odg_chunks_refresh_summaries();
    /* SAVE20+ already speaks the modern turret/construction/runtime model, so after its
     * explicit migrations it must satisfy every current semantic invariant. Earlier v1
     * worlds intentionally retain frozen legacy turret behavior; applying the modern
     * turret profile validator to those would destroy backward compatibility rather than
     * prove safety. Their dedicated legacy migrations remain the authority. */
    if(schema>=ODG_SAVE_SCHEMA_LEGACY20){
        if(!odg_save_current_semantics_validate_internal()||!save_current_world_semantics_validate_internal()||
           !odg_save_instance_semantics_validate_internal()){
            failure_status=ODG_STATUS_INVALID_ARGUMENT;goto rollback;
        }
    }
    /* SAVE18+ already has the dedicated construction section, so artifact/turret payload
     * indices are stable across migration. Reject duplicate, active-target or mismatched
     * capability handles before they can re-enter runtime allocation logic. Older schemas
     * predate that stable artifact index boundary and remain governed by their migrations. */
    if(schema>=ODG_SAVE_SCHEMA_LEGACY18&&!odg_save_identity_validate_internal()){failure_status=ODG_STATUS_INVALID_ARGUMENT;goto rollback;}
    odg_chunks_refresh_summaries();odg_bot_navigation_rebuild_internal();odg_rebuild_interaction_hint();
    odg_reset_presentation_rng_internal();odg_rebuild_stats();save_load_backup_dispose(backup);return ODG_STATUS_OK;
rollback:
    save_load_backup_restore(backup);save_load_backup_dispose(backup);return failure_status;
}
