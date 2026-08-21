#include "game_internal.h"

#include <limits.h>
#include <stdint.h>

static int within_bounds(int32_t x_milli,int32_t z_milli,const odg_map_query_desc *q) {
    return x_milli>=q->min_x_milli&&x_milli<=q->max_x_milli&&z_milli>=q->min_z_milli&&z_milli<=q->max_z_milli;
}

static int32_t sat_i64_i32(int64_t value) {
    if(value>INT32_MAX)return INT32_MAX;
    if(value<INT32_MIN)return INT32_MIN;
    return (int32_t)value;
}

static int32_t global_fx_to_milli_sat(int64_t value_fx) {
    const int64_t max_fx=((int64_t)INT32_MAX*(int64_t)ODG_FX_ONE)/INT64_C(1000);
    const int64_t min_fx=((int64_t)INT32_MIN*(int64_t)ODG_FX_ONE)/INT64_C(1000);
    int64_t cells,rem,milli;
    if(value_fx>=max_fx)return INT32_MAX;
    if(value_fx<=min_fx)return INT32_MIN;
    cells=value_fx/(int64_t)ODG_FX_ONE;rem=value_fx%(int64_t)ODG_FX_ONE;
    milli=cells*INT64_C(1000)+(rem*INT64_C(1000))/(int64_t)ODG_FX_ONE;
    return sat_i64_i32(milli);
}

static int64_t global_fx_to_cell(int64_t value_fx) {
    return odg_floor_div_i64_internal(value_fx,(int64_t)ODG_FX_ONE);
}

static int64_t map_chunk_coord(int64_t cell) {
    return odg_floor_div_i64_internal(cell,(int64_t)ODG_CHUNK_SIZE_CELLS);
}

static uint32_t coarse_chunk_flags(int64_t gx,int64_t gz) {
    int64_t cx=map_chunk_coord(gx),cz=map_chunk_coord(gz);
    const odg_chunk_summary_runtime *summary=odg_chunk_summary_at(cx,cz);
    odg_chunk_descriptor descriptor;uint64_t required=0u;uint32_t flags=ODG_MAP_FLAG_PLAYABLE;
    if(summary!=NULL){
        if(summary->resource_count!=0u)flags|=ODG_MAP_FLAG_RESOURCE;
        if(summary->artifact_count!=0u||summary->turret_count!=0u)flags|=ODG_MAP_FLAG_ARTIFACT;
        if(summary->construction_count!=0u)flags|=ODG_MAP_FLAG_CONSTRUCTION;
    } else if(odg_chunk_descriptor_get(cx,cz,&descriptor,sizeof(descriptor),&required)==ODG_STATUS_OK &&
              descriptor.tree_count+descriptor.stone_count+descriptor.iron_count!=0u) {
        /* Unvisited chunks have no mutations yet, so pure deterministic worldgen is a
         * complete resource summary. Once visited, depletion lives in the summary. */
        flags|=ODG_MAP_FLAG_RESOURCE;
    }
    if(odg_chunk_trail_at_global_cell(gx,gz)!=ODG_OWNER_NONE)flags|=ODG_MAP_FLAG_TRAIL;
    return flags;
}

static uint32_t exact_flags_for_global_cell(int64_t gx,int64_t gz) {
    uint32_t flags=ODG_MAP_FLAG_PLAYABLE;
    int64_t cx=map_chunk_coord(gx),cz=map_chunk_coord(gz);
    uint32_t i=odg_entities_spatial_lower_bound(cx,cz),count=0u;
    const odg_spatial_ref *refs=odg_entities_spatial_refs(&count);
    if(odg_chunk_trail_at_global_cell(gx,gz)!=ODG_OWNER_NONE)flags|=ODG_MAP_FLAG_TRAIL;
    while(i<count&&refs[i].chunk_x==cx&&refs[i].chunk_z==cz){
        const odg_spatial_ref *ref=&refs[i];
        if(ref->kind==ODG_SPATIAL_KIND_RESOURCE&&ref->id<g_odg.resource_count){
            const odg_resource_node *r=&g_odg_resources[ref->id];
            if(r->active&&r->state==ODG_RESOURCE_STATE_AVAILABLE&&
               global_fx_to_cell(r->global_fx_x)==gx&&global_fx_to_cell(r->global_fx_z)==gz)flags|=ODG_MAP_FLAG_RESOURCE;
        } else if(ref->kind==ODG_SPATIAL_KIND_ARTIFACT&&ref->id<g_odg.artifact_count){
            const odg_artifact *a=&g_odg_artifacts[ref->id];
            if(a->active&&global_fx_to_cell(a->global_fx_x)==gx&&global_fx_to_cell(a->global_fx_z)==gz)flags|=ODG_MAP_FLAG_ARTIFACT;
        } else if(ref->kind==ODG_SPATIAL_KIND_CONSTRUCTION&&ref->id<g_odg_construction_count){
            const odg_construction_block *b=&g_odg_construction_blocks[ref->id];
            if(b->active&&global_fx_to_cell(b->global_fx_x)==gx&&global_fx_to_cell(b->global_fx_z)==gz)
                flags|=ODG_MAP_FLAG_CONSTRUCTION;
        } else if(ref->kind==ODG_SPATIAL_KIND_TURRET&&ref->id<g_odg.turret_count){
            const odg_turret *t=&g_odg_turrets[ref->id];
            if(t->active&&t->carried_by==ODG_TURRET_NONE&&
               global_fx_to_cell(t->global_fx_x)==gx&&global_fx_to_cell(t->global_fx_z)==gz)flags|=ODG_MAP_FLAG_ARTIFACT;
        }
        ++i;
    }
    return flags;
}

static void add_marker_global_fx(odg_map_marker *out,uint32_t cap,uint32_t *count,uint32_t kind,uint32_t id,uint32_t owner,uint32_t tier,int64_t x_fx,int64_t z_fx,uint32_t state) {
    odg_map_marker *m;
    if(*count>=cap||out==NULL)return;
    m=&out[*count];m->kind=kind;m->id=id;m->owner_actor_id=owner;m->material_tier=tier;
    m->x_milli=global_fx_to_milli_sat(x_fx);m->z_milli=global_fx_to_milli_sat(z_fx);m->state=state;m->reserved_u32=0u;++*count;
}

int32_t odg_map_query(const odg_map_query_desc *query,
                      odg_map_sample *out_samples,uint64_t sample_capacity,
                      uint64_t *out_required_samples,
                      odg_map_marker *out_markers,uint32_t marker_capacity,
                      uint32_t *out_marker_count) {
    uint64_t required;uint32_t x,z,markers=0u;int64_t span_x,span_z;int coarse;
    if(out_required_samples==NULL||out_marker_count==NULL)return ODG_STATUS_INVALID_ARGUMENT;
    *out_marker_count=0u;
    if(!g_odg.initialized)return ODG_STATUS_INVALID_STATE;
    if(query==NULL||query->struct_size!=sizeof(*query)||query->width==0u||query->height==0u||
       query->width>ODG_MAP_MAX_RESOLUTION||query->height>ODG_MAP_MAX_RESOLUTION||
       query->max_x_milli<=query->min_x_milli||query->max_z_milli<=query->min_z_milli)return ODG_STATUS_INVALID_ARGUMENT;
    required=(uint64_t)query->width*query->height;*out_required_samples=required;
    if(out_samples==NULL||sample_capacity<required)return ODG_STATUS_BUFFER_TOO_SMALL;
    span_x=(int64_t)query->max_x_milli-query->min_x_milli;span_z=(int64_t)query->max_z_milli-query->min_z_milli;
    /* Above ~4 metres/sample the map is a strategic view. Use chunk summaries instead
     * of O(samples * all historical entities); close zoom retains exact cell flags. */
    coarse=(span_x/(int64_t)query->width>=INT64_C(4000) || span_z/(int64_t)query->height>=INT64_C(4000));
    for(z=0u;z<query->height;++z)for(x=0u;x<query->width;++x){
        uint64_t idx=(uint64_t)z*query->width+x;odg_map_sample *sample=&out_samples[idx];
        int64_t xm=(int64_t)query->min_x_milli+(span_x*(2*(int64_t)x+1))/(2*(int64_t)query->width);
        int64_t zm=(int64_t)query->min_z_milli+(span_z*(2*(int64_t)z+1))/(2*(int64_t)query->height);
        int64_t gx=odg_floor_div_i64_internal(xm,INT64_C(1000)),gz=odg_floor_div_i64_internal(zm,INT64_C(1000));int32_t hm=0;
        sample->owner_actor_plus_one=odg_chunk_owner_at_global_cell(gx,gz);
        sample->flags=coarse?coarse_chunk_flags(gx,gz):exact_flags_for_global_cell(gx,gz);
        if(odg_world_height_milli64(gx,gz,&hm)!=ODG_STATUS_OK)hm=0;
        sample->height_milli=hm;sample->reserved_u32=0u;
    }
    for(x=0u;x<ODG_MAX_ACTORS;++x){const odg_actor *a=&g_odg.actors[x];int32_t xm,zm;if(!a->active)continue;xm=global_fx_to_milli_sat(a->global_fx_x);zm=global_fx_to_milli_sat(a->global_fx_z);if(within_bounds(xm,zm,query))add_marker_global_fx(out_markers,marker_capacity,&markers,ODG_MAP_MARKER_ACTOR,a->id,a->id,0u,a->global_fx_x,a->global_fx_z,a->hp!=0u?1u:0u);}
    {
        int64_t min_gx=odg_floor_div_i64_internal(query->min_x_milli,INT64_C(1000)),max_gx=odg_floor_div_i64_internal(query->max_x_milli,INT64_C(1000));
        int64_t min_gz=odg_floor_div_i64_internal(query->min_z_milli,INT64_C(1000)),max_gz=odg_floor_div_i64_internal(query->max_z_milli,INT64_C(1000));
        int64_t min_cx=map_chunk_coord(min_gx),max_cx=map_chunk_coord(max_gx);
        int64_t min_cz=map_chunk_coord(min_gz),max_cz=map_chunk_coord(max_gz);
        uint32_t ref_count=0u,ri=odg_entities_spatial_lower_bound(min_cx,min_cz);
        const odg_spatial_ref *refs=odg_entities_spatial_refs(&ref_count);
        for(;ri<ref_count;++ri){
            const odg_spatial_ref *ref=&refs[ri];
            int32_t xm,zm;uint32_t owner;
            if(ref->chunk_x>max_cx)break;
            if(ref->chunk_x<min_cx||ref->chunk_z<min_cz||ref->chunk_z>max_cz)continue;
            if(ref->kind==ODG_SPATIAL_KIND_TURRET&&ref->id<g_odg.turret_count){
                const odg_turret *t=&g_odg_turrets[ref->id];if(!t->active||t->carried_by!=ODG_TURRET_NONE)continue;
                xm=global_fx_to_milli_sat(t->global_fx_x);zm=global_fx_to_milli_sat(t->global_fx_z);if(!within_bounds(xm,zm,query))continue;
                owner=t->owner==ODG_TURRET_NEUTRAL?UINT32_MAX:ODG_ID_FROM_OWNER(t->owner);
                add_marker_global_fx(out_markers,marker_capacity,&markers,ODG_MAP_MARKER_TURRET,t->id,owner,t->material_tier,t->global_fx_x,t->global_fx_z,t->ammo);
            } else if(ref->kind==ODG_SPATIAL_KIND_ARTIFACT&&ref->id<g_odg.artifact_count){
                const odg_artifact *a=&g_odg_artifacts[ref->id];if(!a->active)continue;
                xm=global_fx_to_milli_sat(a->global_fx_x);zm=global_fx_to_milli_sat(a->global_fx_z);if(!within_bounds(xm,zm,query))continue;
                add_marker_global_fx(out_markers,marker_capacity,&markers,ODG_MAP_MARKER_ARTIFACT,a->id,a->owner_actor_id,a->material_tier,a->global_fx_x,a->global_fx_z,a->item_type);
            } else if(ref->kind==ODG_SPATIAL_KIND_CONSTRUCTION&&ref->id<g_odg_construction_count){
                const odg_construction_block *b=&g_odg_construction_blocks[ref->id];uint32_t controller=UINT32_MAX;int64_t cgx,cgz;uint8_t land_owner;
                if(!b->active)continue;
                xm=global_fx_to_milli_sat(b->global_fx_x);zm=global_fx_to_milli_sat(b->global_fx_z);if(!within_bounds(xm,zm,query))continue;
                odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&cgx,&cgz);land_owner=odg_chunk_owner_at_global_cell(cgx,cgz);
                if(land_owner!=ODG_OWNER_NONE)controller=ODG_ID_FROM_OWNER(land_owner);
                add_marker_global_fx(out_markers,marker_capacity,&markers,ODG_MAP_MARKER_CONSTRUCTION,b->id,controller,b->material_tier,b->global_fx_x,b->global_fx_z,b->shape);
            }
        }
    }
    *out_marker_count=markers;return ODG_STATUS_OK;
}
