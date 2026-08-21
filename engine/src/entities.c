#include "game_internal.h"

#include <stdint.h>


#define ODG_TURRET_INITIAL_CAPACITY UINT32_C(64)
#define ODG_PICKUP_INITIAL_CAPACITY UINT32_C(128)
#define ODG_RESOURCE_INITIAL_CAPACITY UINT32_C(512)
#define ODG_ARTIFACT_INITIAL_CAPACITY UINT32_C(64)
#define ODG_CONSTRUCTION_INITIAL_CAPACITY UINT32_C(256)

odg_turret *g_odg_turrets = NULL;
uint32_t g_odg_turret_capacity = 0u;
odg_world_pickup *g_odg_pickups = NULL;
uint32_t g_odg_pickup_capacity = 0u;
odg_resource_node *g_odg_resources = NULL;
uint32_t g_odg_resource_capacity = 0u;
odg_artifact *g_odg_artifacts = NULL;
uint32_t g_odg_artifact_capacity = 0u;
odg_construction_block *g_odg_construction_blocks = NULL;
uint32_t g_odg_construction_capacity = 0u;
uint32_t g_odg_construction_count = 0u;
odg_spatial_ref *g_odg_spatial_refs = NULL;
uint32_t g_odg_spatial_ref_count = 0u;
static uint32_t g_odg_spatial_ref_capacity = 0u;
static uint32_t g_odg_spatial_dirty = 1u;

static int reserve_bytes(void **storage,uint32_t *capacity,uint32_t needed,uint32_t initial,size_t elem_size) {
    uint32_t next_cap=*capacity;size_t old_bytes,new_bytes;void *next;
    if(needed<=next_cap)return 1;
    if(next_cap==0u)next_cap=initial;
    while(next_cap<needed){
        if(next_cap>UINT32_MAX/2u){next_cap=needed;break;}
        next_cap*=2u;
    }
#if SIZE_MAX <= UINT32_MAX
    if(next_cap>(uint32_t)(SIZE_MAX/elem_size))return 0;
#endif
    old_bytes=(size_t)(*capacity)*elem_size;new_bytes=(size_t)next_cap*elem_size;
    next=odg_mem_realloc(*storage,new_bytes);if(next==NULL)return 0;
    *storage=next;
    if(new_bytes>old_bytes)odg_memset((uint8_t *)(*storage)+old_bytes,0,new_bytes-old_bytes);
    *capacity=next_cap;return 1;
}

int odg_entities_reserve_turrets(uint32_t needed) {
    return reserve_bytes((void **)&g_odg_turrets,&g_odg_turret_capacity,needed,ODG_TURRET_INITIAL_CAPACITY,sizeof(*g_odg_turrets));
}
int odg_entities_reserve_pickups(uint32_t needed) {
    return reserve_bytes((void **)&g_odg_pickups,&g_odg_pickup_capacity,needed,ODG_PICKUP_INITIAL_CAPACITY,sizeof(*g_odg_pickups));
}
int odg_entities_reserve_resources(uint32_t needed) {
    return reserve_bytes((void **)&g_odg_resources,&g_odg_resource_capacity,needed,ODG_RESOURCE_INITIAL_CAPACITY,sizeof(*g_odg_resources));
}
int odg_entities_reserve_artifacts(uint32_t needed) {
    return reserve_bytes((void **)&g_odg_artifacts,&g_odg_artifact_capacity,needed,ODG_ARTIFACT_INITIAL_CAPACITY,sizeof(*g_odg_artifacts));
}
int odg_entities_reserve_construction(uint32_t needed) {
    return reserve_bytes((void **)&g_odg_construction_blocks,&g_odg_construction_capacity,needed,ODG_CONSTRUCTION_INITIAL_CAPACITY,sizeof(*g_odg_construction_blocks));
}


static void global_fx_to_chunk(int64_t x_fx,int64_t z_fx,int64_t *out_chunk_x,int64_t *out_chunk_z) {
    int64_t gx=odg_floor_div_i64_internal(x_fx,(int64_t)ODG_FX_ONE);
    int64_t gz=odg_floor_div_i64_internal(z_fx,(int64_t)ODG_FX_ONE);
    if(out_chunk_x!=NULL) *out_chunk_x=odg_floor_div_i64_internal(gx,(int64_t)ODG_CHUNK_SIZE_CELLS);
    if(out_chunk_z!=NULL) *out_chunk_z=odg_floor_div_i64_internal(gz,(int64_t)ODG_CHUNK_SIZE_CELLS);
}

void odg_entities_spatial_mark_dirty(void) { g_odg_spatial_dirty=1u; }

static int spatial_ref_cmp(const void *a,const void *b) {
    const odg_spatial_ref *ra=(const odg_spatial_ref *)a;
    const odg_spatial_ref *rb=(const odg_spatial_ref *)b;
    if(ra->chunk_x<rb->chunk_x) return -1;
    if(ra->chunk_x>rb->chunk_x) return 1;
    if(ra->chunk_z<rb->chunk_z) return -1;
    if(ra->chunk_z>rb->chunk_z) return 1;
    if(ra->kind<rb->kind) return -1;
    if(ra->kind>rb->kind) return 1;
    if(ra->id<rb->id) return -1;
    if(ra->id>rb->id) return 1;
    return 0;
}

static void spatial_ref_swap(odg_spatial_ref *a,odg_spatial_ref *b) {
    odg_spatial_ref t=*a;*a=*b;*b=t;
}

static void spatial_ref_heap_sift(uint32_t root,uint32_t count) {
    for (;;) {
        uint32_t child=root*2u+1u;
        uint32_t best=root;
        if(child<count && spatial_ref_cmp(&g_odg_spatial_refs[best],&g_odg_spatial_refs[child])<0) best=child;
        if(child+1u<count && spatial_ref_cmp(&g_odg_spatial_refs[best],&g_odg_spatial_refs[child+1u])<0) best=child+1u;
        if(best==root) break;
        spatial_ref_swap(&g_odg_spatial_refs[root],&g_odg_spatial_refs[best]);
        root=best;
    }
}

static void spatial_ref_sort(void) {
    uint32_t count=g_odg_spatial_ref_count;
    uint32_t i;
    if(count<2u) return;
    i=count/2u;
    while(i!=0u){--i;spatial_ref_heap_sift(i,count);}
    i=count;
    while(i>1u){--i;spatial_ref_swap(&g_odg_spatial_refs[0],&g_odg_spatial_refs[i]);spatial_ref_heap_sift(0u,i);}
}

static int spatial_reserve(uint32_t needed) {
    uint32_t cap=g_odg_spatial_ref_capacity;
    odg_spatial_ref *next;
    size_t old_bytes,new_bytes;
    if(needed<=cap) return 1;
    if(cap==0u) cap=256u;
    while(cap<needed) {
        if(cap>UINT32_MAX/2u) { cap=needed; break; }
        cap*=2u;
    }
#if SIZE_MAX <= UINT32_MAX
    if(cap>(uint32_t)(SIZE_MAX/sizeof(*g_odg_spatial_refs))) return 0;
#endif
    old_bytes=(size_t)g_odg_spatial_ref_capacity*sizeof(*g_odg_spatial_refs);
    new_bytes=(size_t)cap*sizeof(*g_odg_spatial_refs);
    next=(odg_spatial_ref *)odg_mem_realloc(g_odg_spatial_refs,new_bytes);
    if(next==NULL) return 0;
    g_odg_spatial_refs=next;
    if(new_bytes>old_bytes) odg_memset((uint8_t *)g_odg_spatial_refs+old_bytes,0,new_bytes-old_bytes);
    g_odg_spatial_ref_capacity=cap;
    return 1;
}

static void spatial_add_global(uint32_t kind,uint32_t id,int64_t global_fx_x,int64_t global_fx_z) {
    odg_spatial_ref *ref;
    int64_t cx=0,cz=0;
    if(!spatial_reserve(g_odg_spatial_ref_count+1u)) return;
    global_fx_to_chunk(global_fx_x,global_fx_z,&cx,&cz);
    ref=&g_odg_spatial_refs[g_odg_spatial_ref_count++];
    ref->chunk_x=cx;ref->chunk_z=cz;ref->kind=kind;ref->id=id;
}

void odg_entities_spatial_rebuild(void) {
    uint32_t i;
    if(!g_odg_spatial_dirty) return;
    g_odg_spatial_ref_count=0u;
    for(i=0u;i<g_odg.turret_count;++i) {
        const odg_turret *e=&g_odg_turrets[i];
        if(e->active&&e->carried_by==ODG_TURRET_NONE) spatial_add_global(ODG_SPATIAL_KIND_TURRET,i,e->global_fx_x,e->global_fx_z);
    }
    for(i=0u;i<g_odg.pickup_count;++i) {
        const odg_world_pickup *e=&g_odg_pickups[i];
        if(e->active) spatial_add_global(ODG_SPATIAL_KIND_PICKUP,i,e->global_fx_x,e->global_fx_z);
    }
    for(i=0u;i<g_odg.resource_count;++i) {
        const odg_resource_node *e=&g_odg_resources[i];
        if(e->active) spatial_add_global(ODG_SPATIAL_KIND_RESOURCE,i,e->global_fx_x,e->global_fx_z);
    }
    for(i=0u;i<g_odg.artifact_count;++i) {
        const odg_artifact *e=&g_odg_artifacts[i];
        if(e->active) spatial_add_global(ODG_SPATIAL_KIND_ARTIFACT,i,e->global_fx_x,e->global_fx_z);
    }
    for(i=0u;i<g_odg_construction_count;++i) {
        const odg_construction_block *e=&g_odg_construction_blocks[i];
        if(e->active) spatial_add_global(ODG_SPATIAL_KIND_CONSTRUCTION,i,e->global_fx_x,e->global_fx_z);
    }
    if(g_odg_spatial_ref_count>1u)
        spatial_ref_sort();
    g_odg_spatial_dirty=0u;
}

const odg_spatial_ref *odg_entities_spatial_refs(uint32_t *out_count) {
    odg_entities_spatial_rebuild();
    if(out_count!=NULL) *out_count=g_odg_spatial_ref_count;
    return g_odg_spatial_refs;
}

uint32_t odg_entities_spatial_lower_bound(int64_t chunk_x,int64_t chunk_z) {
    uint32_t lo=0u,hi;
    odg_entities_spatial_rebuild();
    hi=g_odg_spatial_ref_count;
    while(lo<hi) {
        uint32_t mid=lo+(hi-lo)/2u;
        const odg_spatial_ref *r=&g_odg_spatial_refs[mid];
        if(r->chunk_x<chunk_x||(r->chunk_x==chunk_x&&r->chunk_z<chunk_z)) lo=mid+1u;
        else hi=mid;
    }
    return lo;
}
int odg_entities_spatial_visit_near_global(uint32_t kind,int64_t global_fx_x,int64_t global_fx_z,
                                           int32_t radius_fx,odg_spatial_visit_fn visit,void *context) {
    int64_t min_cell_x,max_cell_x,min_cell_z,max_cell_z,min_cx,max_cx,min_cz,max_cz,cx,cz;
    uint32_t count=0u;
    const odg_spatial_ref *refs;
    if(kind==0u||radius_fx<0||visit==NULL)return 0;
    min_cell_x=odg_floor_div_i64_internal(global_fx_x-(int64_t)radius_fx,(int64_t)ODG_FX_ONE);
    max_cell_x=odg_floor_div_i64_internal(global_fx_x+(int64_t)radius_fx,(int64_t)ODG_FX_ONE);
    min_cell_z=odg_floor_div_i64_internal(global_fx_z-(int64_t)radius_fx,(int64_t)ODG_FX_ONE);
    max_cell_z=odg_floor_div_i64_internal(global_fx_z+(int64_t)radius_fx,(int64_t)ODG_FX_ONE);
    min_cx=odg_floor_div_i64_internal(min_cell_x,(int64_t)ODG_CHUNK_SIZE_CELLS);
    max_cx=odg_floor_div_i64_internal(max_cell_x,(int64_t)ODG_CHUNK_SIZE_CELLS);
    min_cz=odg_floor_div_i64_internal(min_cell_z,(int64_t)ODG_CHUNK_SIZE_CELLS);
    max_cz=odg_floor_div_i64_internal(max_cell_z,(int64_t)ODG_CHUNK_SIZE_CELLS);
    refs=odg_entities_spatial_refs(&count);
    for(cz=min_cz;cz<=max_cz;++cz){
        for(cx=min_cx;cx<=max_cx;++cx){
            uint32_t pos=odg_entities_spatial_lower_bound(cx,cz);
            while(pos<count&&refs[pos].chunk_x==cx&&refs[pos].chunk_z==cz){
                if(refs[pos].kind==kind&&visit(refs[pos].id,context))return 1;
                ++pos;
            }
        }
    }
    return 0;
}


void odg_entities_reset_runtime(void) {
    if(g_odg_turrets!=NULL && g_odg_turret_capacity!=0u)odg_memset(g_odg_turrets,0,(size_t)g_odg_turret_capacity*sizeof(*g_odg_turrets));
    if(g_odg_pickups!=NULL && g_odg_pickup_capacity!=0u)odg_memset(g_odg_pickups,0,(size_t)g_odg_pickup_capacity*sizeof(*g_odg_pickups));
    if(g_odg_resources!=NULL && g_odg_resource_capacity!=0u)odg_memset(g_odg_resources,0,(size_t)g_odg_resource_capacity*sizeof(*g_odg_resources));
    if(g_odg_artifacts!=NULL && g_odg_artifact_capacity!=0u)odg_memset(g_odg_artifacts,0,(size_t)g_odg_artifact_capacity*sizeof(*g_odg_artifacts));
    odg_construction_reset_runtime_internal();
    g_odg.turret_count=0u;g_odg.pickup_count=0u;g_odg.resource_count=0u;g_odg.artifact_count=0u;
    g_odg_spatial_ref_count=0u;g_odg_spatial_dirty=1u;
}

void odg_entities_release_runtime(void) {
    odg_mem_free(g_odg_turrets);odg_mem_free(g_odg_pickups);odg_mem_free(g_odg_resources);odg_mem_free(g_odg_artifacts);odg_mem_free(g_odg_spatial_refs);
    odg_construction_release_runtime_internal();
    g_odg_turrets=NULL;g_odg_pickups=NULL;g_odg_resources=NULL;g_odg_artifacts=NULL;g_odg_spatial_refs=NULL;
    g_odg_turret_capacity=0u;g_odg_pickup_capacity=0u;g_odg_resource_capacity=0u;g_odg_artifact_capacity=0u;g_odg_spatial_ref_capacity=0u;g_odg_spatial_ref_count=0u;g_odg_spatial_dirty=1u;
    g_odg.turret_count=0u;g_odg.pickup_count=0u;g_odg.resource_count=0u;g_odg.artifact_count=0u;
}

void odg_entities_sync_globals_from_local(void) {
    uint32_t i;
    for(i=0u;i<g_odg.turret_count;++i){
        odg_turret *e=&g_odg_turrets[i];
        if(!e->active||e->local_resident==0u)continue;
        odg_local_fx_to_global_fx_internal(e->x,e->z,&e->global_fx_x,&e->global_fx_z);
    }
    for(i=0u;i<g_odg.pickup_count;++i){
        odg_world_pickup *e=&g_odg_pickups[i];
        if(!e->active||e->local_resident==0u)continue;
        odg_local_fx_to_global_fx_internal(e->x,e->z,&e->global_fx_x,&e->global_fx_z);
    }
    for(i=0u;i<g_odg.resource_count;++i){
        odg_resource_node *e=&g_odg_resources[i];
        if(!e->active||e->local_resident==0u)continue;
        odg_local_fx_to_global_fx_internal(e->x,e->z,&e->global_fx_x,&e->global_fx_z);
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        odg_artifact *e=&g_odg_artifacts[i];
        if(!e->active||e->local_resident==0u)continue;
        odg_local_fx_to_global_fx_internal(e->x,e->z,&e->global_fx_x,&e->global_fx_z);
    }
    odg_construction_sync_globals_from_local_internal();
    odg_entities_spatial_mark_dirty();
}

void odg_entities_refresh_local_cache(void) {
    uint32_t i;
    for(i=0u;i<g_odg.turret_count;++i){
        odg_turret *e=&g_odg_turrets[i];
        if(!e->active)continue;
        e->local_resident=odg_global_fx_to_local_internal(e->global_fx_x,e->global_fx_z,&e->x,&e->z)?1u:0u;
        if(e->local_resident==0u){e->x=0;e->z=0;}
    }
    for(i=0u;i<g_odg.pickup_count;++i){
        odg_world_pickup *e=&g_odg_pickups[i];
        if(!e->active)continue;
        e->local_resident=odg_global_fx_to_local_internal(e->global_fx_x,e->global_fx_z,&e->x,&e->z)?1u:0u;
        if(e->local_resident==0u){e->x=0;e->z=0;}
    }
    for(i=0u;i<g_odg.resource_count;++i){
        odg_resource_node *e=&g_odg_resources[i];
        if(!e->active)continue;
        e->local_resident=odg_global_fx_to_local_internal(e->global_fx_x,e->global_fx_z,&e->x,&e->z)?1u:0u;
        if(e->local_resident==0u){e->x=0;e->z=0;}
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        odg_artifact *e=&g_odg_artifacts[i];
        if(!e->active)continue;
        e->local_resident=odg_global_fx_to_local_internal(e->global_fx_x,e->global_fx_z,&e->x,&e->z)?1u:0u;
        if(e->local_resident==0u){e->x=0;e->z=0;}
    }
    odg_construction_refresh_local_cache_internal();
    odg_entities_spatial_mark_dirty();
}
