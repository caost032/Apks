#include "game_internal.h"

#include <stdint.h>

odg_chunk_runtime *g_odg_chunk_cache = NULL;
uint32_t g_odg_chunk_cache_capacity = 0u;
odg_chunk_summary_runtime *g_odg_chunk_summaries = NULL;
uint32_t g_odg_chunk_summary_capacity = 0u;

static uint32_t chunk_ordinal_from_global(int64_t gx,int64_t gz,int64_t *out_cx,int64_t *out_cz);
static odg_chunk_runtime *chunk_find(int64_t cx,int64_t cz,int create);

/* This module is intentionally pure: descriptor generation is a function only of the
 * world seed, signed chunk coordinate and independent stream constants. Visiting C->A->B
 * therefore cannot perturb A->B->C. It is the worldgen authority used by the future
 * active/sleeping chunk cache rather than an order-sensitive RNG cursor. */

static uint64_t mix64(uint64_t x) {
    x^=x>>30u;x*=UINT64_C(0xbf58476d1ce4e5b9);
    x^=x>>27u;x*=UINT64_C(0x94d049bb133111eb);
    x^=x>>31u;return x;
}

static uint64_t zigzag64(int64_t v) {
    uint64_t u=(uint64_t)v;
    return (u<<1u)^(uint64_t)(-(int64_t)(u>>63u));
}

static uint64_t chunk_stream(int64_t cx,int64_t cz,uint64_t stream) {
    uint64_t x=g_odg.seed^stream;
    x^=mix64(zigzag64(cx)+UINT64_C(0x9e3779b97f4a7c15));
    x^=mix64(zigzag64(cz)+UINT64_C(0xd1b54a32d192ed03));
    return mix64(x);
}

static uint32_t lattice_u16(int64_t gx,int64_t gz,uint64_t stream) {
    uint64_t h=chunk_stream(gx,gz,stream);
    return (uint32_t)(h&UINT64_C(0xffff));
}

static uint32_t smooth_q16(uint32_t t) {
    /* smoothstep t*t*(3-2t), Q16 */
    uint64_t tt=((uint64_t)t*t)>>16u;
    uint64_t k=UINT64_C(196608)-2u*(uint64_t)t;
    uint64_t v=(tt*k)>>16u;
    return v>65535u?65535u:(uint32_t)v;
}

static int32_t value_noise_height(int64_t x,int64_t z,int64_t scale,uint64_t stream,int32_t amplitude) {
    int64_t gx=odg_floor_div_i64_internal(x,scale),gz=odg_floor_div_i64_internal(z,scale);
    int64_t ox=x-gx*scale,oz=z-gz*scale;
    uint32_t tx=smooth_q16((uint32_t)((ox*65535)/scale));
    uint32_t tz=smooth_q16((uint32_t)((oz*65535)/scale));
    int64_t a=(int64_t)lattice_u16(gx,gz,stream)-32768;
    int64_t b=(int64_t)lattice_u16(gx+1,gz,stream)-32768;
    int64_t c=(int64_t)lattice_u16(gx,gz+1,stream)-32768;
    int64_t d=(int64_t)lattice_u16(gx+1,gz+1,stream)-32768;
    int64_t top=a+(((b-a)*(int64_t)tx)>>16u);
    int64_t bot=c+(((d-c)*(int64_t)tx)>>16u);
    int64_t v=top+(((bot-top)*(int64_t)tz)>>16u);
    return (int32_t)((v*(int64_t)amplitude)/32768);
}

int32_t odg_world_height_milli64(int64_t world_cell_x,int64_t world_cell_z,int32_t *out_height_milli) {
    int64_t h;
    int32_t mountain_macro;
    int32_t mountain_detail;
    if(!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if(out_height_milli==NULL) return ODG_STATUS_INVALID_ARGUMENT;

    /* One deterministic height authority drives collision, rendering, water and biomes.
     * Broad macro noise creates traversable mountain masses instead of per-cell spikes;
     * progressively smaller streams add readable relief without producing void seams. */
    h=420;
    h+=value_noise_height(world_cell_x,world_cell_z,48,UINT64_C(0x4845494748543031),1250);
    h+=value_noise_height(world_cell_x,world_cell_z,17,UINT64_C(0x4845494748543032),430);
    h+=value_noise_height(world_cell_x,world_cell_z,7, UINT64_C(0x4845494748543033),120);
    mountain_macro=value_noise_height(world_cell_x,world_cell_z,112,UINT64_C(0x4d4f554e5441494e),2800);
    if(mountain_macro>360){
        h+=(int64_t)(mountain_macro-360);
        mountain_detail=value_noise_height(world_cell_x,world_cell_z,43,UINT64_C(0x52494447455f3031),760);
        if(mountain_detail>0) h+=(int64_t)mountain_detail/2;
    }
    /* Worldgen v1 froze low basins at -420 mm. v2 introduced bathymetry; later
     * worldgen revisions must not accidentally make stored v2 worlds fall back to v1. */
    if(odg_worldgen_version()>=ODG_WORLDGEN_VERSION_BATHYMETRY){
        if(h<-1550)h=-1550;
    }else if(h<-420)h=-420;
    if(h>6200) h=6200;
    *out_height_milli=(int32_t)h;
    return ODG_STATUS_OK;
}

static uint32_t range_from(uint64_t h,uint32_t lo,uint32_t hi) {
    uint32_t span=hi-lo+1u;
    return lo+(uint32_t)(h%(uint64_t)span);
}

int32_t odg_chunk_descriptor_get(int64_t chunk_x,int64_t chunk_z,
                                 odg_chunk_descriptor *out_descriptor,
                                 uint64_t capacity,uint64_t *out_required) {
    uint64_t bio,res,tur,stable;
    uint32_t biome,roll;
    int64_t x0,z0,x1,z1,xc,zc;
    if(out_required!=NULL)*out_required=(uint64_t)sizeof(odg_chunk_descriptor);
    if(!g_odg.initialized)return ODG_STATUS_INVALID_STATE;
    if(out_descriptor==NULL||capacity<(uint64_t)sizeof(*out_descriptor))return ODG_STATUS_BUFFER_TOO_SMALL;
    bio=chunk_stream(chunk_x,chunk_z,UINT64_C(0x42494f4d455f3031));
    res=chunk_stream(chunk_x,chunk_z,UINT64_C(0x5245534f55524345));
    tur=chunk_stream(chunk_x,chunk_z,UINT64_C(0x5455525245543031));
    stable=chunk_stream(chunk_x,chunk_z,UINT64_C(0x4348554e4b5f4944));
    x0=chunk_x*(int64_t)ODG_CHUNK_SIZE_CELLS;z0=chunk_z*(int64_t)ODG_CHUNK_SIZE_CELLS;
    x1=x0+ODG_CHUNK_SIZE_CELLS;z1=z0+ODG_CHUNK_SIZE_CELLS;xc=x0+ODG_CHUNK_SIZE_CELLS/2;zc=z0+ODG_CHUNK_SIZE_CELLS/2;
    odg_memset(out_descriptor,0,sizeof(*out_descriptor));
    out_descriptor->struct_size=(uint32_t)sizeof(*out_descriptor);
    out_descriptor->chunk_x=chunk_x;out_descriptor->chunk_z=chunk_z;out_descriptor->stable_id=stable;
    (void)odg_world_height_milli64(x0,z0,&out_descriptor->corner_height_milli[0]);
    (void)odg_world_height_milli64(x1,z0,&out_descriptor->corner_height_milli[1]);
    (void)odg_world_height_milli64(x0,z1,&out_descriptor->corner_height_milli[2]);
    (void)odg_world_height_milli64(x1,z1,&out_descriptor->corner_height_milli[3]);
    (void)odg_world_height_milli64(xc,zc,&out_descriptor->center_height_milli);

    /* Biome choice is derived from the same large-scale terrain signal used by gameplay.
     * A chunk therefore cannot render as low plain while its physical center is mountain. */
    roll=(uint32_t)(bio%100u);
    if(out_descriptor->center_height_milli>3000) biome=ODG_BIOME_HIGHLANDS;
    else if(out_descriptor->center_height_milli<500 && (uint32_t)((bio>>16u)%1000u)>650u) biome=ODG_BIOME_WETLAND;
    else biome=roll<46u?ODG_BIOME_PLAIN:(roll<76u?ODG_BIOME_FOREST:ODG_BIOME_ROCKY);
    out_descriptor->biome=biome;
    if(biome==ODG_BIOME_FOREST){
        out_descriptor->tree_count=range_from(res,10u,18u);out_descriptor->stone_count=range_from(res>>8u,2u,4u);out_descriptor->iron_count=range_from(res>>16u,0u,1u);
    }else if(biome==ODG_BIOME_ROCKY){
        out_descriptor->tree_count=range_from(res,2u,5u);out_descriptor->stone_count=range_from(res>>8u,7u,12u);out_descriptor->iron_count=range_from(res>>16u,3u,5u);
    }else if(biome==ODG_BIOME_HIGHLANDS){
        out_descriptor->tree_count=range_from(res,1u,4u);out_descriptor->stone_count=range_from(res>>8u,8u,14u);out_descriptor->iron_count=range_from(res>>16u,3u,6u);
    }else if(biome==ODG_BIOME_WETLAND){
        out_descriptor->tree_count=range_from(res,7u,13u);out_descriptor->stone_count=range_from(res>>8u,1u,3u);out_descriptor->iron_count=range_from(res>>16u,0u,1u);
    }else{
        out_descriptor->tree_count=range_from(res,4u,8u);out_descriptor->stone_count=range_from(res>>8u,3u,5u);out_descriptor->iron_count=range_from(res>>16u,1u,2u);
    }
    out_descriptor->has_procedural_turret=(uint32_t)(tur%100u)<18u?1u:0u;
    roll=(uint32_t)((tur>>12u)%100u);out_descriptor->turret_material_tier=roll<60u?ODG_MATERIAL_WOOD:(roll<90u?ODG_MATERIAL_STONE:ODG_MATERIAL_IRON);
    return ODG_STATUS_OK;
}

uint32_t odg_chunk_coal_candidate_count_internal(int64_t chunk_x,int64_t chunk_z) {
    odg_chunk_descriptor descriptor;
    uint64_t required=0u;
    uint64_t res;
    if(odg_chunk_descriptor_get(chunk_x,chunk_z,&descriptor,sizeof(descriptor),&required)!=ODG_STATUS_OK)return 0u;
    res=chunk_stream(chunk_x,chunk_z,UINT64_C(0x5245534f55524345));
    if(descriptor.biome==ODG_BIOME_ROCKY||descriptor.biome==ODG_BIOME_HIGHLANDS)return range_from(res>>24u,4u,7u);
    return range_from(res>>24u,1u,3u);
}

int odg_chunk_procedural_turret_cell(int64_t chunk_x,int64_t chunk_z,int64_t *out_gx,int64_t *out_gz) {
    odg_chunk_descriptor descriptor;uint64_t required=0u,h;int64_t base_x,base_z,gx,gz;uint32_t attempt;
    if(out_gx==NULL||out_gz==NULL)return 0;
    if(odg_chunk_descriptor_get(chunk_x,chunk_z,&descriptor,sizeof(descriptor),&required)!=ODG_STATUS_OK ||
       descriptor.has_procedural_turret==0u)return 0;
    h=chunk_stream(chunk_x,chunk_z,UINT64_C(0x545552525f504f53));
    base_x=chunk_x*(int64_t)ODG_CHUNK_SIZE_CELLS+4;
    base_z=chunk_z*(int64_t)ODG_CHUNK_SIZE_CELLS+4;
    /* v1/v2 provenance is frozen at the historical hashed cell. SAVE21 explicitly
     * migrates persisted v2 worlds to v3; genuine v1 worlds keep their old placement. */
    gx=base_x+(int64_t)(h%24u);gz=base_z+(int64_t)((h>>11u)%24u);
    if(odg_worldgen_version()<ODG_WORLDGEN_VERSION_SAFE_TURRETS){
        *out_gx=gx;*out_gz=gz;return 1;
    }
    /* v3 preserves that exact hashed cell whenever it is physically valid. Only an
     * impossible water/steep result triggers a deterministic full-interior fallback. */
    if(odg_world_cell_safe_ground_internal(gx,gz)){
        *out_gx=gx;*out_gz=gz;return 1;
    }
    /* A natural turret is real collision-bearing infrastructure, not a decorative
     * marker. Exhaust the whole 24x24 interior in a deterministic rotated order instead
     * of materializing an impossible turret or silently perturbing the chunk RNG stream. */
    {
        uint32_t start=(uint32_t)((h^(h>>23u))%UINT64_C(576));
        for(attempt=0u;attempt<UINT32_C(576);++attempt){
            uint32_t ordinal=(start+attempt)%UINT32_C(576);
            gx=base_x+(int64_t)(ordinal%24u);gz=base_z+(int64_t)(ordinal/24u);
            if(!odg_world_cell_safe_ground_internal(gx,gz))continue;
            *out_gx=gx;*out_gz=gz;return 1;
        }
    }
    return 0;
}

int odg_chunk_procedural_turret_reserves_local_circle_internal(int32_t x,int32_t z,int32_t radius){
    odg_chunk_turret_state saved;
    int64_t global_fx_x,global_fx_z,gx,gz,cx,cz,tgx,tgz,tfx_x,tfx_z,dx,dz,combined;
    if(radius<0)return 0;
    odg_local_fx_to_global_fx_internal(x,z,&global_fx_x,&global_fx_z);
    gx=odg_floor_div_i64_internal(global_fx_x,(int64_t)ODG_FX_ONE);
    gz=odg_floor_div_i64_internal(global_fx_z,(int64_t)ODG_FX_ONE);
    cx=odg_floor_div_i64_internal(gx,(int64_t)ODG_CHUNK_SIZE_CELLS);
    cz=odg_floor_div_i64_internal(gz,(int64_t)ODG_CHUNK_SIZE_CELLS);
    /* Once a natural turret has been physically removed, its old worldgen cell becomes
     * ordinary space. A sleeping/never-seen turret remains persistent infrastructure and
     * reserves its collider even before streaming materializes it. */
    if(odg_chunk_procedural_turret_state(cx,cz,&saved)&&saved.state==2u)return 0;
    if(!odg_chunk_procedural_turret_cell(cx,cz,&tgx,&tgz))return 0;
    tfx_x=tgx*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
    tfx_z=tgz*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
    dx=global_fx_x-tfx_x;dz=global_fx_z-tfx_z;
    combined=(int64_t)radius+(int64_t)ODG_TURRET_COLLISION_RADIUS_FX;
    /* Natural turret cells live at least four cells inside their chunk. Every bootstrap
     * caller uses a circle <3 m, so a turret in a neighbouring chunk cannot overlap this
     * query; checking the containing chunk is both complete and cheap. */
    return dx*dx+dz*dz<combined*combined;
}

/* -------------------------------------------------------------------------
 * Active-window runtime
 * -------------------------------------------------------------------------
 * The 128x128 arrays are a precision window, not the world.  The global world is
 * addressed in signed 64-bit cells and split into 32x32 chunks.  Modified/visited
 * chunk records preserve ownership, committed trails and depleted procedural
 * resources when the floating origin moves.
 */
static uint32_t packed_get(const uint8_t *packed,uint32_t ordinal) {
    uint8_t b;
    if(packed==NULL || ordinal>=ODG_CHUNK_CELL_COUNT) return 0u;
    b=packed[ordinal>>1u];
    return (ordinal&1u)!=0u ? (uint32_t)(b>>4u) : (uint32_t)(b&UINT8_C(0x0f));
}

uint8_t odg_chunk_runtime_owner_at_ordinal_internal(const odg_chunk_runtime *record,uint32_t ordinal){
    if(record==NULL||record->used==0u||ordinal>=ODG_CHUNK_CELL_COUNT)return ODG_OWNER_NONE;
    return (uint8_t)packed_get(record->territory_packed,ordinal);
}

uint8_t odg_chunk_runtime_trail_at_ordinal_internal(const odg_chunk_runtime *record,uint32_t ordinal){
    if(record==NULL||record->used==0u||ordinal>=ODG_CHUNK_CELL_COUNT)return ODG_OWNER_NONE;
    return (uint8_t)packed_get(record->trail_packed,ordinal);
}

int odg_chunk_runtime_state_validate_internal(const odg_chunk_runtime *record,uint32_t expected_index){
    uint32_t ordinal,counts[ODG_MAX_ACTORS]={0u};
    odg_chunk_descriptor descriptor;uint64_t required=0u;
    (void)expected_index; /* section order is not identity; signed coordinates/stable_id are. */
    if(record==NULL||record->used!=1u||
       (record->state!=ODG_CHUNK_STATE_ACTIVE&&record->state!=ODG_CHUNK_STATE_DIRTY&&record->state!=ODG_CHUNK_STATE_SLEEPING)||
       record->stable_id!=chunk_stream(record->chunk_x,record->chunk_z,UINT64_C(0x4348554e4b5f4944))||
       record->last_touch_tick>g_odg.tick||record->procedural_turret_reserved!=0u||
       record->procedural_turret_state>2u)return 0;
    for(ordinal=0u;ordinal<ODG_CHUNK_CELL_COUNT;++ordinal){
        uint32_t owner=packed_get(record->territory_packed,ordinal);
        uint32_t trail=packed_get(record->trail_packed,ordinal);
        if(owner>ODG_MAX_ACTORS||trail>ODG_MAX_ACTORS)return 0;
        if(owner!=ODG_OWNER_NONE)++counts[ODG_ID_FROM_OWNER(owner)];
    }
    for(ordinal=0u;ordinal<ODG_MAX_ACTORS;++ordinal)if(record->territory_cells[ordinal]!=counts[ordinal])return 0;
    if(record->procedural_turret_state==0u){
        return record->procedural_turret_owner==0u&&record->procedural_turret_material_tier==0u&&
               record->procedural_turret_mode==0u&&record->procedural_turret_ammo==0u&&
               record->procedural_turret_shots_fired==0u&&record->procedural_turret_cells_conquered==0u&&
               record->procedural_turret_instance_id==0u;
    }
    if(odg_chunk_descriptor_get(record->chunk_x,record->chunk_z,&descriptor,sizeof(descriptor),&required)!=ODG_STATUS_OK||
       descriptor.has_procedural_turret==0u)return 0;
    if(record->procedural_turret_state==2u){
        return record->procedural_turret_owner==0u&&record->procedural_turret_material_tier==0u&&
               record->procedural_turret_mode==0u&&record->procedural_turret_ammo==0u&&
               record->procedural_turret_shots_fired==0u&&record->procedural_turret_cells_conquered==0u&&
               record->procedural_turret_instance_id==0u;
    }
    if(record->procedural_turret_owner>ODG_MAX_ACTORS||
       !odg_turret_persisted_profile_validate_internal(record->procedural_turret_material_tier,
            record->procedural_turret_mode,record->procedural_turret_ammo))return 0;
    return record->procedural_turret_instance_id==
           (ODG_INSTANCE_ID_PROCEDURAL_BIT|(descriptor.stable_id&ODG_INSTANCE_ID_SEQUENTIAL_MAX));
}


int odg_chunks_derived_cache_validate_internal(void){
    uint64_t totals[ODG_MAX_ACTORS]={0u};
    uint32_t i,x,z;
    for(i=0u;i<g_odg.chunk_cache_used;++i){
        const odg_chunk_runtime *record=&g_odg_chunk_cache[i];uint32_t actor;
        if(record->used==0u)continue;
        for(actor=0u;actor<ODG_MAX_ACTORS;++actor){
            totals[actor]+=(uint64_t)record->territory_cells[actor];
            if(totals[actor]>UINT32_MAX)return 0;
        }
    }
    for(i=0u;i<ODG_MAX_ACTORS;++i){
        uint32_t score=(uint32_t)totals[i];
        uint32_t level=1u+score/64u;if(level>20u)level=20u;
        if(g_odg.territory_count[i]!=score||g_odg.actors[i].score!=score||g_odg.actors[i].level!=level)return 0;
    }
    if(g_odg.playable_count!=ODG_CELL_COUNT)return 0;
    for(i=0u;i<ODG_CELL_COUNT;++i){
        if(g_odg.playable[i]!=1u||g_odg.save_reserved_flood_seen[i]!=0u||g_odg.save_reserved_flood_queue[i]!=0u)return 0;
    }
    for(z=0u;z<ODG_GRID_SIZE;++z)for(x=0u;x<ODG_GRID_SIZE;++x){
        int64_t gx=g_odg.world_origin_cell_x+(int64_t)x;
        int64_t gz=g_odg.world_origin_cell_z+(int64_t)z;
        int64_t cx,cz;uint32_t ordinal=chunk_ordinal_from_global(gx,gz,&cx,&cz);
        const odg_chunk_runtime *record=chunk_find(cx,cz,0);
        uint32_t cell=z*ODG_GRID_SIZE+x;
        uint8_t owner=record!=NULL?odg_chunk_runtime_owner_at_ordinal_internal(record,ordinal):ODG_OWNER_NONE;
        uint8_t trail=record!=NULL?odg_chunk_runtime_trail_at_ordinal_internal(record,ordinal):ODG_OWNER_NONE;
        if(g_odg.territory[cell]!=owner||g_odg.trail_owner[cell]!=trail)return 0;
    }
    return 1;
}

static void packed_set(uint8_t *packed,uint32_t ordinal,uint32_t value) {
    uint32_t byte_index;
    uint8_t b,v;
    if(packed==NULL || ordinal>=ODG_CHUNK_CELL_COUNT) return;
    byte_index=ordinal>>1u;
    b=packed[byte_index];v=(uint8_t)(value&UINT32_C(0x0f));
    if((ordinal&1u)!=0u) b=(uint8_t)((b&UINT8_C(0x0f))|(uint8_t)(v<<4u));
    else b=(uint8_t)((b&UINT8_C(0xf0))|v);
    packed[byte_index]=b;
}

static void chunk_record_set_owner(odg_chunk_runtime *record,uint32_t ordinal,uint8_t owner) {
    uint8_t old;
    if(record==NULL||ordinal>=ODG_CHUNK_CELL_COUNT)return;
    old=(uint8_t)packed_get(record->territory_packed,ordinal);
    if(old==owner)return;
    if(old!=ODG_OWNER_NONE){uint32_t id=ODG_ID_FROM_OWNER(old);if(id<ODG_MAX_ACTORS&&record->territory_cells[id]>0u)--record->territory_cells[id];}
    packed_set(record->territory_packed,ordinal,owner);
    if(owner!=ODG_OWNER_NONE){uint32_t id=ODG_ID_FROM_OWNER(owner);if(id<ODG_MAX_ACTORS)++record->territory_cells[id];}
}

static uint32_t chunk_ordinal_from_global(int64_t gx,int64_t gz,int64_t *out_cx,int64_t *out_cz) {
    int64_t cx=odg_floor_div_i64_internal(gx,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t cz=odg_floor_div_i64_internal(gz,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t lx=gx-cx*(int64_t)ODG_CHUNK_SIZE_CELLS;
    int64_t lz=gz-cz*(int64_t)ODG_CHUNK_SIZE_CELLS;
    if(out_cx!=NULL) *out_cx=cx;
    if(out_cz!=NULL) *out_cz=cz;
    return (uint32_t)lz*(uint32_t)ODG_CHUNK_SIZE_CELLS+(uint32_t)lx;
}

int odg_chunks_reserve_runtime(uint32_t needed) {
    odg_chunk_runtime *next;odg_chunk_summary_runtime *summary_next;
    uint32_t capacity=g_odg_chunk_cache_capacity;
    size_t old_bytes,new_bytes,old_summary_bytes,new_summary_bytes;
    if(needed<=capacity && needed<=g_odg_chunk_summary_capacity)return 1;
    if(capacity<g_odg_chunk_summary_capacity)capacity=g_odg_chunk_summary_capacity;
    if(capacity==0u)capacity=ODG_CHUNK_CACHE_INITIAL_CAPACITY;
    while(capacity<needed){
        if(capacity>UINT32_MAX/2u){capacity=needed;break;}
        capacity*=2u;
    }
#if SIZE_MAX <= UINT32_MAX
    if(capacity>(uint32_t)(SIZE_MAX/sizeof(odg_chunk_runtime)) ||
       capacity>(uint32_t)(SIZE_MAX/sizeof(odg_chunk_summary_runtime)))return 0;
#endif
    old_bytes=(size_t)g_odg_chunk_cache_capacity*sizeof(odg_chunk_runtime);
    new_bytes=(size_t)capacity*sizeof(odg_chunk_runtime);
    next=(odg_chunk_runtime *)odg_mem_realloc(g_odg_chunk_cache,new_bytes);
    if(next==NULL)return 0;
    g_odg_chunk_cache=next;
    if(new_bytes>old_bytes)odg_memset((uint8_t *)g_odg_chunk_cache+old_bytes,0,new_bytes-old_bytes);
    g_odg_chunk_cache_capacity=capacity;

    old_summary_bytes=(size_t)g_odg_chunk_summary_capacity*sizeof(odg_chunk_summary_runtime);
    new_summary_bytes=(size_t)capacity*sizeof(odg_chunk_summary_runtime);
    summary_next=(odg_chunk_summary_runtime *)odg_mem_realloc(g_odg_chunk_summaries,new_summary_bytes);
    if(summary_next==NULL)return 0;
    g_odg_chunk_summaries=summary_next;
    if(new_summary_bytes>old_summary_bytes)odg_memset((uint8_t *)g_odg_chunk_summaries+old_summary_bytes,0,new_summary_bytes-old_summary_bytes);
    g_odg_chunk_summary_capacity=capacity;return 1;
}

static odg_chunk_runtime *chunk_find(int64_t cx,int64_t cz,int create) {
    uint32_t i;
    for(i=0u;i<g_odg.chunk_cache_used;++i) {
        odg_chunk_runtime *record=&g_odg_chunk_cache[i];
        if(record->used!=0u && record->chunk_x==cx && record->chunk_z==cz) return record;
    }
    if(!create)return NULL;
    if(!odg_chunks_reserve_runtime(g_odg.chunk_cache_used+1u))return NULL;
    {
        odg_chunk_runtime *record=&g_odg_chunk_cache[g_odg.chunk_cache_used++];
        odg_memset(record,0,sizeof(*record));record->used=1u;record->state=ODG_CHUNK_STATE_ACTIVE;
        record->chunk_x=cx;record->chunk_z=cz;record->stable_id=chunk_stream(cx,cz,UINT64_C(0x4348554e4b5f4944));
        record->last_touch_tick=g_odg.tick;return record;
    }
}

int64_t odg_global_center_cell_x_internal(void) {
    return g_odg.world_origin_cell_x+(int64_t)ODG_WORLD_HALF_CELLS;
}
int64_t odg_global_center_cell_z_internal(void) {
    return g_odg.world_origin_cell_z+(int64_t)ODG_WORLD_HALF_CELLS;
}

void odg_local_fx_to_global_fx_internal(int32_t x,int32_t z,int64_t *out_x_fx,int64_t *out_z_fx) {
    if(out_x_fx!=NULL)*out_x_fx=odg_global_center_cell_x_internal()*(int64_t)ODG_FX_ONE+(int64_t)x;
    if(out_z_fx!=NULL)*out_z_fx=odg_global_center_cell_z_internal()*(int64_t)ODG_FX_ONE+(int64_t)z;
}

int odg_global_fx_to_local_internal(int64_t x_fx,int64_t z_fx,int32_t *out_x,int32_t *out_z) {
    int64_t lx=x_fx-odg_global_center_cell_x_internal()*(int64_t)ODG_FX_ONE;
    int64_t lz=z_fx-odg_global_center_cell_z_internal()*(int64_t)ODG_FX_ONE;
    if(out_x==NULL||out_z==NULL||lx<INT32_MIN||lx>INT32_MAX||lz<INT32_MIN||lz>INT32_MAX)return 0;
    *out_x=(int32_t)lx;*out_z=(int32_t)lz;return 1;
}

int odg_global_cell_to_local_internal(int64_t gx,int64_t gz,uint32_t *out_cell) {
    int64_t lx=gx-g_odg.world_origin_cell_x,lz=gz-g_odg.world_origin_cell_z;
    if(out_cell==NULL || lx<0 || lz<0 || lx>=(int64_t)ODG_GRID_SIZE || lz>=(int64_t)ODG_GRID_SIZE) return 0;
    *out_cell=(uint32_t)lz*ODG_GRID_SIZE+(uint32_t)lx;return 1;
}

int odg_global_cell_center_to_local_fx_internal(int64_t gx,int64_t gz,int32_t *out_x,int32_t *out_z) {
    int64_t dx=gx-odg_global_center_cell_x_internal();
    int64_t dz=gz-odg_global_center_cell_z_internal();
    int64_t fx=dx*(int64_t)ODG_CELL_FX+(int64_t)ODG_CELL_FX/2;
    int64_t fz=dz*(int64_t)ODG_CELL_FX+(int64_t)ODG_CELL_FX/2;
    if(out_x==NULL || out_z==NULL || fx<INT32_MIN || fx>INT32_MAX || fz<INT32_MIN || fz>INT32_MAX) return 0;
    *out_x=(int32_t)fx;*out_z=(int32_t)fz;return 1;
}

static int global_cell_in_active_window(int64_t gx,int64_t gz,uint32_t *out_cell) {
    return odg_global_cell_to_local_internal(gx,gz,out_cell);
}

uint8_t odg_chunk_owner_at_global_cell(int64_t gx,int64_t gz) {
    uint32_t local_cell,ordinal;int64_t cx,cz;odg_chunk_runtime *record;
    if(global_cell_in_active_window(gx,gz,&local_cell)) return g_odg.territory[local_cell];
    ordinal=chunk_ordinal_from_global(gx,gz,&cx,&cz);record=chunk_find(cx,cz,0);
    return record!=NULL?(uint8_t)packed_get(record->territory_packed,ordinal):ODG_OWNER_NONE;
}

uint8_t odg_chunk_trail_at_global_cell(int64_t gx,int64_t gz) {
    uint32_t local_cell,ordinal;int64_t cx,cz;odg_chunk_runtime *record;
    if(global_cell_in_active_window(gx,gz,&local_cell)) return g_odg.trail_owner[local_cell];
    ordinal=chunk_ordinal_from_global(gx,gz,&cx,&cz);record=chunk_find(cx,cz,0);
    return record!=NULL?(uint8_t)packed_get(record->trail_packed,ordinal):ODG_OWNER_NONE;
}

void odg_chunks_release_runtime(void) {
    odg_mem_free(g_odg_chunk_cache);odg_mem_free(g_odg_chunk_summaries);
    g_odg_chunk_cache=NULL;g_odg_chunk_summaries=NULL;g_odg_chunk_cache_capacity=0u;g_odg_chunk_summary_capacity=0u;g_odg.chunk_cache_used=0u;
}

void odg_chunks_reset_runtime(void) {
    if(g_odg_chunk_cache!=NULL && g_odg.chunk_cache_used!=0u)
        odg_memset(g_odg_chunk_cache,0,(size_t)g_odg.chunk_cache_used*sizeof(odg_chunk_runtime));
    if(g_odg_chunk_summaries!=NULL && g_odg.chunk_cache_used!=0u)
        odg_memset(g_odg_chunk_summaries,0,(size_t)g_odg.chunk_cache_used*sizeof(odg_chunk_summary_runtime));
    g_odg.chunk_cache_used=0u;g_odg.chunk_recenter_count=0u;
    /* Local cell [64,64] is global [0,0] at boot. */
    g_odg.world_origin_cell_x=-(int64_t)ODG_WORLD_HALF_CELLS;
    g_odg.world_origin_cell_z=-(int64_t)ODG_WORLD_HALF_CELLS;
}

void odg_chunks_capture_active_window(void) {
    uint32_t z,x;
    for(z=0u;z<ODG_GRID_SIZE;++z) for(x=0u;x<ODG_GRID_SIZE;++x) {
        int64_t gx=g_odg.world_origin_cell_x+(int64_t)x;
        int64_t gz=g_odg.world_origin_cell_z+(int64_t)z;
        int64_t cx,cz;uint32_t ordinal=chunk_ordinal_from_global(gx,gz,&cx,&cz);
        odg_chunk_runtime *record=chunk_find(cx,cz,1);
        uint32_t cell=z*ODG_GRID_SIZE+x;
        if(record==NULL) continue;
        chunk_record_set_owner(record,ordinal,g_odg.territory[cell]);
        packed_set(record->trail_packed,ordinal,g_odg.trail_owner[cell]);
        record->last_touch_tick=g_odg.tick;
    }
    {
        uint32_t i;
        for(i=0u;i<g_odg.chunk_cache_used;++i){
            odg_chunk_runtime *record=&g_odg_chunk_cache[i];
            if(record->used==0u)continue;
            if(record->state==ODG_CHUNK_STATE_ACTIVE) record->state=ODG_CHUNK_STATE_SLEEPING;
        }
    }
}

void odg_chunks_load_active_window(void) {
    uint32_t z,x,i;
    for(i=0u;i<g_odg.chunk_cache_used;++i) if(g_odg_chunk_cache[i].state==ODG_CHUNK_STATE_ACTIVE)
        g_odg_chunk_cache[i].state=ODG_CHUNK_STATE_SLEEPING;
    g_odg.playable_count=ODG_CELL_COUNT;
    odg_memset(g_odg.playable,1,sizeof(g_odg.playable));
    odg_memset(g_odg.territory,0,sizeof(g_odg.territory));
    odg_memset(g_odg.trail_owner,0,sizeof(g_odg.trail_owner));
    for(z=0u;z<ODG_GRID_SIZE;++z) for(x=0u;x<ODG_GRID_SIZE;++x) {
        int64_t gx=g_odg.world_origin_cell_x+(int64_t)x;
        int64_t gz=g_odg.world_origin_cell_z+(int64_t)z;
        int64_t cx,cz;uint32_t ordinal=chunk_ordinal_from_global(gx,gz,&cx,&cz);
        odg_chunk_runtime *record=chunk_find(cx,cz,1);uint32_t cell=z*ODG_GRID_SIZE+x;
        if(record==NULL) continue;
        g_odg.territory[cell]=(uint8_t)packed_get(record->territory_packed,ordinal);
        g_odg.trail_owner[cell]=(uint8_t)packed_get(record->trail_packed,ordinal);
        record->state=ODG_CHUNK_STATE_ACTIVE;record->last_touch_tick=g_odg.tick;
    }
}

static uint32_t remap_cell_after_origin(uint32_t old_cell,int64_t old_origin_x,int64_t old_origin_z) {
    int64_t gx,gz;uint32_t mapped;
    if(old_cell>=ODG_CELL_COUNT) return UINT32_MAX;
    gx=old_origin_x+(int64_t)(old_cell&(ODG_GRID_SIZE-1u));
    gz=old_origin_z+(int64_t)(old_cell>>ODG_GRID_SHIFT);
    return odg_global_cell_to_local_internal(gx,gz,&mapped)?mapped:UINT32_MAX;
}

static void shift_world_entities(int32_t shift_x,int32_t shift_z,int64_t old_origin_x,int64_t old_origin_z) {
    uint32_t i,j;
    for(i=0u;i<ODG_MAX_ACTORS;++i) {
        odg_actor *actor=&g_odg.actors[i];
        int32_t new_x=0,new_z=0;
        actor->local_resident=odg_global_fx_to_local_internal(actor->global_fx_x,actor->global_fx_z,&new_x,&new_z)?1u:0u;
        if(actor->local_resident){actor->x=new_x;actor->z=new_z;actor->progress_x-=shift_x;actor->progress_z-=shift_z;}
        else{actor->x=0;actor->z=0;actor->progress_x=0;actor->progress_z=0;}
        actor->home_cell=odg_global_cell_to_local_internal(actor->home_global_cell_x,actor->home_global_cell_z,&j)?j:UINT32_MAX;
        actor->last_cell=remap_cell_after_origin(actor->last_cell,old_origin_x,old_origin_z);
        actor->trail_head_cell=remap_cell_after_origin(actor->trail_head_cell,old_origin_x,old_origin_z);
        actor->trail_render_anchor_cell=remap_cell_after_origin(actor->trail_render_anchor_cell,old_origin_x,old_origin_z);
        actor->ai_plan_cell=remap_cell_after_origin(actor->ai_plan_cell,old_origin_x,old_origin_z);
        for(j=0u;j<actor->trail_path_len;++j){actor->trail_path_x[j]-=shift_x;actor->trail_path_z[j]-=shift_z;}
    }
    for(i=0u;i<g_odg.obstacle_count;++i){g_odg.obstacles[i].x-=shift_x;g_odg.obstacles[i].z-=shift_z;}
    for(i=0u;i<ODG_MAX_PARTICLES;++i)if(g_odg.particles[i].active){g_odg.particles[i].x-=shift_x;g_odg.particles[i].z-=shift_z;}
    g_odg.camera_anchor_x-=shift_x;g_odg.camera_anchor_z-=shift_z;
    g_odg.remote_view_x-=shift_x;g_odg.remote_view_z-=shift_z;
}

void odg_chunks_maybe_recenter(void) {
    const odg_actor *player=&g_odg.actors[ODG_PLAYER_ID];
    int32_t delta_cells_x=0,delta_cells_z=0,shift_x,shift_z;
    int64_t old_origin_x,old_origin_z;
    if(!g_odg.initialized || !player->active || player->hp==0u) return;
    if(player->x>ODG_FLOATING_ORIGIN_TRIGGER_FX) delta_cells_x=ODG_CHUNK_SIZE_CELLS;
    else if(player->x<-ODG_FLOATING_ORIGIN_TRIGGER_FX) delta_cells_x=-ODG_CHUNK_SIZE_CELLS;
    if(player->z>ODG_FLOATING_ORIGIN_TRIGGER_FX) delta_cells_z=ODG_CHUNK_SIZE_CELLS;
    else if(player->z<-ODG_FLOATING_ORIGIN_TRIGGER_FX) delta_cells_z=-ODG_CHUNK_SIZE_CELLS;
    if(delta_cells_x==0 && delta_cells_z==0) return;
    old_origin_x=g_odg.world_origin_cell_x;old_origin_z=g_odg.world_origin_cell_z;
    odg_chunks_capture_active_window();
    odg_entities_sync_globals_from_local();
    g_odg.world_origin_cell_x+=(int64_t)delta_cells_x;g_odg.world_origin_cell_z+=(int64_t)delta_cells_z;
    shift_x=delta_cells_x*ODG_FX_ONE;shift_z=delta_cells_z*ODG_FX_ONE;
    shift_world_entities(shift_x,shift_z,old_origin_x,old_origin_z);
    odg_fauna_shift_local(shift_x,shift_z);
    odg_entities_refresh_local_cache();
    odg_fauna_refresh_local_cache();
    odg_chunks_load_active_window();
    odg_resources_stream_refresh();
    odg_turrets_stream_refresh();
    odg_chunks_refresh_summaries();
    /* Local nav cells now refer to a different global terrain window. Rebuild the
     * derived terrain graph immediately; retaining the old graph would route bots
     * using slopes/water from the region that just scrolled out of the window. */
    odg_bot_navigation_rebuild_internal();
    ++g_odg.chunk_recenter_count;
}


static int64_t summary_chunk_from_global_cell(int64_t g) {
    return odg_floor_div_i64_internal(g,(int64_t)ODG_CHUNK_SIZE_CELLS);
}

static uint32_t popcount64_local(uint64_t v) {
    uint32_t n=0u;while(v!=UINT64_C(0)){v&=v-UINT64_C(1);++n;}return n;
}

static odg_chunk_summary_runtime *summary_for_record(odg_chunk_runtime *record) {
    ptrdiff_t index;
    if(record==NULL||g_odg_chunk_cache==NULL||g_odg_chunk_summaries==NULL)return NULL;
    index=record-g_odg_chunk_cache;
    if(index<0||(uint64_t)index>=g_odg.chunk_cache_used)return NULL;
    return &g_odg_chunk_summaries[(uint32_t)index];
}

const odg_chunk_summary_runtime *odg_chunk_summary_at(int64_t chunk_x,int64_t chunk_z) {
    odg_chunk_runtime *record=chunk_find(chunk_x,chunk_z,0);
    return summary_for_record(record);
}

static void summary_increment_for_chunk(int64_t cx,int64_t cz,uint32_t kind) {
    odg_chunk_runtime *record=chunk_find(cx,cz,0);
    odg_chunk_summary_runtime *summary=summary_for_record(record);
    if(summary==NULL)return;
    if(kind==1u)++summary->actor_count;
    else if(kind==2u)++summary->artifact_count;
    else if(kind==3u)++summary->turret_count;
    else if(kind==4u)++summary->construction_count;
    else if(kind==5u)++summary->resource_count;
}

static void summary_increment_for_actor(const odg_actor *actor) {
    int64_t gx,gz;
    if(actor==NULL)return;
    odg_global_fx_to_global_cell_internal(actor->global_fx_x,actor->global_fx_z,&gx,&gz);
    summary_increment_for_chunk(summary_chunk_from_global_cell(gx),summary_chunk_from_global_cell(gz),1u);
}

void odg_chunks_refresh_summaries(void) {
    uint32_t i;
    if(g_odg_chunk_summaries==NULL||g_odg_chunk_summary_capacity<g_odg.chunk_cache_used)return;
    for(i=0u;i<g_odg.chunk_cache_used;++i){
        odg_chunk_runtime *record=&g_odg_chunk_cache[i];odg_chunk_summary_runtime next;
        odg_chunk_descriptor descriptor;uint64_t required=0u;uint32_t total=0u,depleted=0u;
        odg_memset(&next,0,sizeof(next));
        if(record->used==0u){g_odg_chunk_summaries[i]=next;continue;}
        if(odg_chunk_descriptor_get(record->chunk_x,record->chunk_z,&descriptor,sizeof(descriptor),&required)==ODG_STATUS_OK){
            total=descriptor.tree_count+descriptor.stone_count+descriptor.iron_count;
            if(total>64u)total=64u;
            depleted=popcount64_local(record->depleted_resource_mask & (total==64u?UINT64_MAX:((UINT64_C(1)<<total)-UINT64_C(1))));
            next.resource_count=total>=depleted?total-depleted:0u;
        }
        /* Summary refresh is derived-only. Chunk lifecycle/dirty state is persistent and
         * participates in the deterministic state hash, so a cache rebuild must never
         * rewrite it (especially after save-load validation). Residency transitions are
         * performed explicitly by capture/load-active-window instead. */
        g_odg_chunk_summaries[i]=next;
    }
    for(i=0u;i<ODG_MAX_ACTORS;++i)if(g_odg.actors[i].active)summary_increment_for_actor(&g_odg.actors[i]);
    {
        uint32_t ref_count=0u;
        const odg_spatial_ref *refs=odg_entities_spatial_refs(&ref_count);
        for(i=0u;i<ref_count;++i){
            const odg_spatial_ref *ref=&refs[i];
            if(ref->kind==ODG_SPATIAL_KIND_ARTIFACT)summary_increment_for_chunk(ref->chunk_x,ref->chunk_z,2u);
            else if(ref->kind==ODG_SPATIAL_KIND_TURRET)summary_increment_for_chunk(ref->chunk_x,ref->chunk_z,3u);
            else if(ref->kind==ODG_SPATIAL_KIND_CONSTRUCTION)summary_increment_for_chunk(ref->chunk_x,ref->chunk_z,4u);
            else if(ref->kind==ODG_SPATIAL_KIND_RESOURCE && ref->id<g_odg.resource_count &&
                    g_odg_resources[ref->id].procedural==0u)summary_increment_for_chunk(ref->chunk_x,ref->chunk_z,5u);
        }
    }
    /* g_odg_chunk_summaries is a rebuildable acceleration cache. Do not derive persistent
     * chunk `state` from it: `state` is serialized/hashed and may be DIRTY because logical
     * world data changed. Turning DIRTY into ACTIVE here made a successful load mutate its
     * own state hash. */
}

int odg_chunk_prepare_resource_depletion_internal(int64_t chunk_x,int64_t chunk_z,uint32_t ordinal) {
    /* Depletion persistence is part of the harvest transaction. Allocate/locate its
     * chunk record before any loot or node state is committed so OOM cannot turn a
     * harvested procedural resource into a respawning duplication source. */
    if(ordinal>=64u)return 0;
    return chunk_find(chunk_x,chunk_z,1)!=NULL;
}

int odg_chunk_mark_resource_depleted(int64_t chunk_x,int64_t chunk_z,uint32_t ordinal) {
    odg_chunk_runtime *record;
    if(ordinal>=64u) return 0;
    record=chunk_find(chunk_x,chunk_z,1);if(record==NULL)return 0;
    record->depleted_resource_mask|=UINT64_C(1)<<ordinal;record->state=ODG_CHUNK_STATE_DIRTY;record->last_touch_tick=g_odg.tick;return 1;
}

int odg_chunk_resource_depleted(int64_t chunk_x,int64_t chunk_z,uint32_t ordinal) {
    odg_chunk_runtime *record;
    if(ordinal>=64u) return 0;
    record=chunk_find(chunk_x,chunk_z,0);return record!=NULL && (record->depleted_resource_mask&(UINT64_C(1)<<ordinal))!=0u;
}

int odg_chunk_procedural_turret_state(int64_t chunk_x,int64_t chunk_z,odg_chunk_turret_state *out_state) {
    const odg_chunk_runtime *record;
    if(out_state==NULL)return 0;
    odg_memset(out_state,0,sizeof(*out_state));
    record=chunk_find(chunk_x,chunk_z,0);
    if(record==NULL || record->procedural_turret_state==0u)return 0;
    out_state->state=record->procedural_turret_state;out_state->owner=record->procedural_turret_owner;
    out_state->material_tier=record->procedural_turret_material_tier;out_state->mode=record->procedural_turret_mode;
    out_state->ammo=record->procedural_turret_ammo;out_state->shots_fired=record->procedural_turret_shots_fired;
    out_state->cells_conquered=record->procedural_turret_cells_conquered;out_state->instance_id=record->procedural_turret_instance_id;
    return 1;
}

int odg_chunk_prepare_procedural_turret_state_internal(int64_t chunk_x,int64_t chunk_z) {
    /* Mutating a natural turret is a persistent-world transaction. Allocate its chunk
     * authority before inventory/ammo/ownership changes so OOM can never leave the
     * materialized turret ahead of the state that will reconstruct it after streaming. */
    return chunk_find(chunk_x,chunk_z,1)!=NULL;
}

int odg_chunk_store_procedural_turret_state(int64_t chunk_x,int64_t chunk_z,const odg_chunk_turret_state *state) {
    odg_chunk_runtime *record;
    if(state==NULL || state->state==0u)return 0;
    record=chunk_find(chunk_x,chunk_z,0);if(record==NULL)return 0;
    record->procedural_turret_state=state->state;record->procedural_turret_owner=state->owner;
    record->procedural_turret_material_tier=state->material_tier;record->procedural_turret_mode=state->mode;
    record->procedural_turret_ammo=state->ammo;record->procedural_turret_shots_fired=state->shots_fired;
    record->procedural_turret_cells_conquered=state->cells_conquered;record->procedural_turret_instance_id=state->instance_id;
    record->state=ODG_CHUNK_STATE_DIRTY;record->last_touch_tick=g_odg.tick;return 1;
}

int odg_chunk_mark_procedural_turret_removed(int64_t chunk_x,int64_t chunk_z) {
    odg_chunk_turret_state state;odg_memset(&state,0,sizeof(state));state.state=2u;
    return odg_chunk_store_procedural_turret_state(chunk_x,chunk_z,&state);
}

void odg_local_fx_to_global_cell_internal(int32_t x,int32_t z,int64_t *out_gx,int64_t *out_gz) {
    int64_t global_fx_x=odg_global_center_cell_x_internal()*(int64_t)ODG_FX_ONE+(int64_t)x;
    int64_t global_fx_z=odg_global_center_cell_z_internal()*(int64_t)ODG_FX_ONE+(int64_t)z;
    if(out_gx!=NULL)*out_gx=odg_floor_div_i64_internal(global_fx_x,(int64_t)ODG_FX_ONE);
    if(out_gz!=NULL)*out_gz=odg_floor_div_i64_internal(global_fx_z,(int64_t)ODG_FX_ONE);
}

void odg_global_fx_to_global_cell_internal(int64_t x_fx,int64_t z_fx,int64_t *out_gx,int64_t *out_gz) {
    if(out_gx!=NULL)*out_gx=odg_floor_div_i64_internal(x_fx,(int64_t)ODG_FX_ONE);
    if(out_gz!=NULL)*out_gz=odg_floor_div_i64_internal(z_fx,(int64_t)ODG_FX_ONE);
}

void odg_chunk_set_owner_at_global_cell(int64_t gx,int64_t gz,uint8_t owner) {
    int64_t cx,cz;uint32_t ordinal=chunk_ordinal_from_global(gx,gz,&cx,&cz),local_cell;
    odg_chunk_runtime *record;uint8_t old;
    if(owner>ODG_MAX_ACTORS)return;
    record=chunk_find(cx,cz,1);if(record==NULL)return;
    old=odg_chunk_runtime_owner_at_ordinal_internal(record,ordinal);if(old==owner)return;
    chunk_record_set_owner(record,ordinal,owner);record->state=ODG_CHUNK_STATE_DIRTY;record->last_touch_tick=g_odg.tick;
    if(old!=ODG_OWNER_NONE){
        uint32_t id=ODG_ID_FROM_OWNER(old);
        if(id<ODG_MAX_ACTORS&&g_odg.territory_count[id]>0u){
            uint32_t level;--g_odg.territory_count[id];g_odg.actors[id].score=g_odg.territory_count[id];
            level=1u+g_odg.actors[id].score/64u;g_odg.actors[id].level=level>20u?20u:level;
        }
    }
    if(owner!=ODG_OWNER_NONE){
        uint32_t id=ODG_ID_FROM_OWNER(owner);
        if(id<ODG_MAX_ACTORS){
            uint32_t level;++g_odg.territory_count[id];g_odg.actors[id].score=g_odg.territory_count[id];
            level=1u+g_odg.actors[id].score/64u;g_odg.actors[id].level=level>20u?20u:level;
        }
    }
    if(global_cell_in_active_window(gx,gz,&local_cell))g_odg.territory[local_cell]=owner;
}

void odg_chunk_set_trail_at_global_cell(int64_t gx,int64_t gz,uint8_t owner) {
    int64_t cx,cz;uint32_t ordinal=chunk_ordinal_from_global(gx,gz,&cx,&cz),local_cell;
    odg_chunk_runtime *record=chunk_find(cx,cz,1);
    if(record!=NULL){packed_set(record->trail_packed,ordinal,owner);record->state=ODG_CHUNK_STATE_DIRTY;record->last_touch_tick=g_odg.tick;}
    if(global_cell_in_active_window(gx,gz,&local_cell))g_odg.trail_owner[local_cell]=owner;
}

void odg_chunk_clear_trail_owner(uint8_t owner) {
    uint32_t i,ordinal;
    if(owner==ODG_OWNER_NONE)return;
    for(i=0u;i<g_odg.chunk_cache_used;++i){
        odg_chunk_runtime *record=&g_odg_chunk_cache[i];int changed=0;
        if(record->used==0u)continue;
        for(ordinal=0u;ordinal<ODG_CHUNK_CELL_COUNT;++ordinal)if(packed_get(record->trail_packed,ordinal)==owner){packed_set(record->trail_packed,ordinal,ODG_OWNER_NONE);changed=1;}
        if(changed){record->state=ODG_CHUNK_STATE_DIRTY;record->last_touch_tick=g_odg.tick;}
    }
    for(i=0u;i<ODG_CELL_COUNT;++i)if(g_odg.trail_owner[i]==owner)g_odg.trail_owner[i]=ODG_OWNER_NONE;
}
