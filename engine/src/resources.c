#include "game_internal.h"

#include <stdint.h>

static uint32_t mix32(uint64_t v) {
    v ^= v >> 33u;
    v *= UINT64_C(0xff51afd7ed558ccd);
    v ^= v >> 33u;
    v *= UINT64_C(0xc4ceb9fe1a85ec53);
    v ^= v >> 33u;
    return (uint32_t)(v ^ (v >> 32u));
}

typedef struct {
    uint32_t resource_kind;
    uint32_t harvest_item_type;
    uint32_t required_tool_capability;
    uint32_t preferred_tool_item_type;
    uint32_t minimum_tool_tier;
    uint32_t yield_min;
    uint32_t yield_max;
    uint32_t harvest_ticks_by_tier[4];
    uint32_t harvest_particle_rgba;
} odg_resource_deposit_profile;

/* Mineral/deposit mechanics are content data. Generic harvesting must not grow a new
 * if/else branch for every future coal/copper/clay/etc. deposit. A zero duration means
 * that material tier cannot harvest the deposit. */
static const odg_resource_deposit_profile g_resource_deposit_profiles[] = {
    {ODG_RESOURCE_STONE,ODG_ITEM_STONE,ODG_ITEM_CAP_MINE,ODG_ITEM_PICKAXE,ODG_MATERIAL_WOOD,5u,9u,{0u,480u,300u,192u},0xa9a9a3ffu},
    {ODG_RESOURCE_IRON,ODG_ITEM_IRON,ODG_ITEM_CAP_MINE,ODG_ITEM_PICKAXE,ODG_MATERIAL_STONE,3u,6u,{0u,0u,576u,324u},0xc5d0d5ffu},
    {ODG_RESOURCE_COAL,ODG_ITEM_COAL,ODG_ITEM_CAP_MINE,ODG_ITEM_PICKAXE,ODG_MATERIAL_WOOD,3u,7u,{0u,420u,264u,180u},0x444b4cffu}
};

static const odg_resource_deposit_profile *resource_deposit_profile(uint32_t kind) {
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_resource_deposit_profiles)/sizeof(g_resource_deposit_profiles[0]));++i)
        if(g_resource_deposit_profiles[i].resource_kind==kind)return &g_resource_deposit_profiles[i];
    return NULL;
}

int odg_resource_profiles_validate_internal(void) {
    uint32_t i,j;
    for(i=0u;i<(uint32_t)(sizeof(g_resource_deposit_profiles)/sizeof(g_resource_deposit_profiles[0]));++i){
        const odg_resource_deposit_profile *p=&g_resource_deposit_profiles[i];
        const odg_item_definition *output=odg_item_definition_internal(p->harvest_item_type);
        const odg_item_definition *tool=odg_item_definition_internal(p->preferred_tool_item_type);
        if(p->resource_kind==0u||p->resource_kind==ODG_RESOURCE_TREE||p->resource_kind==ODG_RESOURCE_FLORA||
           output==NULL||(output->flags&ODG_ITEM_FLAG_RESOURCE)==0u||p->required_tool_capability==0u||
           tool==NULL||(tool->capability_bits&p->required_tool_capability)==0u||
           p->minimum_tool_tier<ODG_MATERIAL_WOOD||p->minimum_tool_tier>ODG_MATERIAL_IRON||
           p->yield_min==0u||p->yield_max<p->yield_min)return 0;
        if(p->harvest_ticks_by_tier[p->minimum_tool_tier]==0u)return 0;
        for(j=0u;j<i;++j)if(g_resource_deposit_profiles[j].resource_kind==p->resource_kind)return 0;
    }
    return resource_deposit_profile(ODG_RESOURCE_STONE)!=NULL&&
           resource_deposit_profile(ODG_RESOURCE_IRON)!=NULL&&
           resource_deposit_profile(ODG_RESOURCE_COAL)!=NULL;
}

const odg_flora_species_definition *odg_resource_flora_definition_internal(const odg_resource_node *resource) {
    if(resource==NULL)return NULL;
    if(resource->species_id!=0u)return odg_flora_species_internal(resource->species_id);
    if(resource->kind==ODG_RESOURCE_TREE || resource->kind==ODG_RESOURCE_FLORA)
        return odg_flora_worldgen_species_internal(ODG_FLORA_GROWTH_TREE,resource->x,resource->z,resource->stable_id);
    return NULL;
}

int odg_resource_is_flora_internal(const odg_resource_node *resource) {
    if(resource==NULL)return 0;
    if(resource->species_id==0u && resource->kind!=ODG_RESOURCE_TREE && resource->kind!=ODG_RESOURCE_FLORA)return 0;
    return odg_resource_flora_definition_internal(resource)!=NULL;
}

uint32_t odg_resource_harvest_item_type_internal(const odg_resource_node *resource) {
    if(resource==NULL)return ODG_ITEM_NONE;
    if(odg_resource_is_flora_internal(resource)){
        const odg_flora_species_definition *flora=odg_resource_flora_definition_internal(resource);
        return flora!=NULL?flora->harvest_item_type:ODG_ITEM_NONE;
    }
    {
        const odg_resource_deposit_profile *profile=resource_deposit_profile(resource->kind);
        return profile!=NULL?profile->harvest_item_type:ODG_ITEM_NONE;
    }
}

uint32_t odg_resource_harvest_material_internal(const odg_resource_node *resource) {
    const odg_item_definition *item=odg_item_definition_internal(odg_resource_harvest_item_type_internal(resource));
    return item!=NULL?item->default_material_tier:ODG_MATERIAL_NONE;
}

uint32_t odg_resource_harvest_tool_capability_internal(const odg_resource_node *resource) {
    const odg_resource_deposit_profile *profile;
    if(resource==NULL||odg_resource_is_flora_internal(resource))return 0u;
    profile=resource_deposit_profile(resource->kind);
    return profile!=NULL?profile->required_tool_capability:0u;
}

uint32_t odg_resource_harvest_tool_type_internal(const odg_resource_node *resource) {
    if(resource==NULL)return ODG_ITEM_NONE;
    if(odg_resource_is_flora_internal(resource)){
        const odg_flora_species_definition *flora=odg_resource_flora_definition_internal(resource);
        return flora!=NULL?flora->harvest_tool_item_type:ODG_ITEM_NONE;
    }
    {
        const odg_resource_deposit_profile *profile=resource_deposit_profile(resource->kind);
        return profile!=NULL?profile->preferred_tool_item_type:ODG_ITEM_NONE;
    }
}

uint32_t odg_resource_harvest_min_tool_tier_internal(const odg_resource_node *resource) {
    if(resource==NULL)return ODG_MATERIAL_NONE;
    if(odg_resource_is_flora_internal(resource)){
        const odg_item_definition *tool=odg_item_definition_internal(odg_resource_harvest_tool_type_internal(resource));
        return tool!=NULL?tool->default_material_tier:ODG_MATERIAL_NONE;
    }
    {
        const odg_resource_deposit_profile *profile=resource_deposit_profile(resource->kind);
        return profile!=NULL?profile->minimum_tool_tier:ODG_MATERIAL_NONE;
    }
}

int odg_resource_harvest_allows_hand_internal(const odg_resource_node *resource) {
    const odg_flora_species_definition *flora;
    if(resource==NULL)return 0;
    if(!odg_resource_is_flora_internal(resource))return 0;
    flora=odg_resource_flora_definition_internal(resource);
    return flora!=NULL && (flora->harvest_flags&ODG_FLORA_HARVEST_ALLOW_HAND)!=0u;
}

static int resource_kind_requires_geological_exposure(uint32_t kind);

static int obstacle_overlap(int32_t x,int32_t z,int32_t radius,const odg_obstacle *o) {
    int32_t cx=odg_clamp_i32(x,o->x-o->hx,o->x+o->hx);
    int32_t cz=odg_clamp_i32(z,o->z-o->hz,o->z+o->hz);
    int64_t dx=(int64_t)x-cx,dz=(int64_t)z-cz;
    return dx*dx+dz*dz < (int64_t)radius*radius;
}

static int resource_surface_allows_kind(int32_t x,int32_t z,uint32_t kind){
    odg_surface_sample surface;
    const int geological=resource_kind_requires_geological_exposure(kind);
    if(!odg_environment_surface_local(x,z,&surface))return 0;
    /* Every current resource is physically surface-anchored. Water would bury the node
     * below the water plane. Trees/flora/loose stone additionally require walkable ground;
     * exposed ore seams may legitimately live on a steep cave wall. */
    if((surface.flags&ODG_SURFACE_FLAG_WATER)!=0u)return 0;
    if(!geological&&(surface.flags&ODG_SURFACE_FLAG_STEEP)!=0u)return 0;
    return 1;
}

static int resource_spawn_clear(int32_t x,int32_t z,uint32_t upto,uint32_t kind) {
    uint32_t i;
    const int32_t radius=ODG_FX_ONE;
    const int geological=resource_kind_requires_geological_exposure(kind);
    if(!resource_surface_allows_kind(x,z,kind))return 0;
    for (i=0u;i<g_odg.obstacle_count;++i) if (obstacle_overlap(x,z,radius,&g_odg.obstacles[i])) return 0;
    for (i=0u;i<ODG_MAX_ACTORS;++i) {
        if (g_odg.actors[i].active && odg_dist2(x,z,g_odg.actors[i].x,g_odg.actors[i].z) <
            (int64_t)(3*ODG_FX_ONE)*(3*ODG_FX_ONE)) return 0;
    }
    for (i=0u;i<upto;++i) {
        const odg_resource_node *r=&g_odg_resources[i];
        int32_t spacing=3*ODG_FX_ONE;
        if(!r->active||r->local_resident==0u||r->state!=ODG_RESOURCE_STATE_AVAILABLE)continue;
        if(geological&&resource_kind_requires_geological_exposure(r->kind))spacing=ODG_FX_ONE;
        if(odg_dist2(x,z,r->x,r->z)<(int64_t)spacing*spacing)return 0;
    }
    return 1;
}

static void set_resource_with_identity(uint32_t id,uint32_t kind,int32_t x,int32_t z,uint32_t bootstrap_actor_id,
                                       uint64_t explicit_stable_id,int explicit_identity) {
    odg_resource_node *r=&g_odg_resources[id];
    odg_memset(r,0,sizeof(*r));
    if(!explicit_identity){explicit_stable_id=odg_next_instance_id();if(explicit_stable_id==0u){r->id=id;return;}}
    r->active=1u;r->id=id;r->kind=kind;r->state=ODG_RESOURCE_STATE_AVAILABLE;
    r->x=x;r->z=z;r->local_resident=1u;odg_local_fx_to_global_fx_internal(x,z,&r->global_fx_x,&r->global_fx_z);
    r->bootstrap_actor_id=bootstrap_actor_id;
    r->stable_id=explicit_stable_id;
    r->harvest_actor=UINT32_MAX;
    if (kind==ODG_RESOURCE_TREE || kind==ODG_RESOURCE_FLORA) {
        const odg_flora_species_definition *flora=odg_flora_worldgen_species_internal(ODG_FLORA_GROWTH_TREE,x,z,r->stable_id);
        r->yield_min=4u;r->yield_max=7u;
        if(flora!=NULL)odg_ecology_init_resource(r,flora->species_id,ODG_FLORA_STAGE_MATURE,(uint32_t)(r->stable_id&UINT64_C(0xffffffff)));
        else {r->active=0u;r->state=ODG_RESOURCE_STATE_DEPLETED;}
    } else {
        const odg_resource_deposit_profile *profile=resource_deposit_profile(kind);
        if(profile!=NULL){r->yield_min=profile->yield_min;r->yield_max=profile->yield_max;}
        else {r->active=0u;r->state=ODG_RESOURCE_STATE_DEPLETED;}
    }
}

static void set_resource(uint32_t id,uint32_t kind,int32_t x,int32_t z,uint32_t bootstrap_actor_id) {
    set_resource_with_identity(id,kind,x,z,bootstrap_actor_id,0u,0);
}

static int resource_kind_requires_geological_exposure(uint32_t kind){
    return kind==ODG_RESOURCE_IRON||kind==ODG_RESOURCE_COAL;
}

static int resource_geology_exposure_local(int32_t x,int32_t z,uint32_t kind){
    int64_t gx,gz;
    if(!resource_kind_requires_geological_exposure(kind))return 1;
    odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
    return odg_geology_surface_exposure_internal(gx,gz,kind);
}

static int add_bootstrap_resource_at(uint32_t actor_id,uint32_t kind,int32_t x,int32_t z){
    if(odg_chunk_procedural_turret_reserves_local_circle_internal(x,z,ODG_FX_ONE))return 0;
    if(!resource_spawn_clear(x,z,g_odg.resource_count,kind))return 0;
    if(!resource_geology_exposure_local(x,z,kind))return 0;
    if(!odg_entities_reserve_resources(g_odg.resource_count+1u))return 0;
    set_resource(g_odg.resource_count,kind,x,z,actor_id);
    if(!g_odg_resources[g_odg.resource_count].active)return 0;
    ++g_odg.resource_count;return 1;
}

static int add_near_actor(uint32_t actor_id,uint32_t kind,uint32_t ordinal) {
    const odg_actor *a=&g_odg.actors[actor_id];
    uint32_t attempt;
    for (attempt=0u;attempt<96u;++attempt) {
        uint32_t h=mix32(g_odg.seed ^ ((uint64_t)actor_id<<48u) ^ ((uint64_t)kind<<32u) ^ ((uint64_t)ordinal<<16u) ^ attempt);
        int32_t sx=(int32_t)(h&31u)-15;
        int32_t sz=(int32_t)((h>>5u)&31u)-15;
        int32_t x,z;
        if (sx>-4 && sx<4) sx += sx>=0?4:-4;
        if (sz>-4 && sz<4) sz += sz>=0?4:-4;
        x=a->x+sx*ODG_FX_ONE;
        z=a->z+sz*ODG_FX_ONE;
        if(add_bootstrap_resource_at(actor_id,kind,x,z))return 1;
    }
    /* Wood and loose stone are the guaranteed bootstrap floor. Preserve random-looking
     * placement on the common path, then deterministically scan expanding square rings
     * rather than accepting water/steep terrain or silently returning fewer resources. */
    if(kind==ODG_RESOURCE_TREE||kind==ODG_RESOURCE_STONE){
        uint32_t radius;
        uint32_t phase=mix32(g_odg.seed^((uint64_t)actor_id<<32u)^((uint64_t)kind<<16u)^ordinal)&3u;
        for(radius=4u;radius<=64u;++radius){
            int32_t r=(int32_t)radius;int32_t side;
            for(side=-r;side<=r;++side){
                int32_t offsets[4][2]={{side,-r},{r,side},{-side,r},{-r,-side}};
                uint32_t j;
                for(j=0u;j<4u;++j){
                    uint32_t q=(j+phase)&3u;
                    int32_t x=a->x+offsets[q][0]*ODG_FX_ONE;
                    int32_t z=a->z+offsets[q][1]*ODG_FX_ONE;
                    if(add_bootstrap_resource_at(actor_id,kind,x,z))return 1;
                }
            }
        }
    }
    return 0;
}

void odg_resources_build(void) {
    uint32_t actor,ordinal;
    g_odg.resource_count=0u;
    /* Bootstrap is guaranteed per nation before ambient filling. Three trees provide at
     * least 12 wood, enough to enter the wooden-tool loop on every supported seed. */
    for (actor=0u;actor<ODG_MAX_ACTORS;++actor) {
        for (ordinal=0u;ordinal<3u;++ordinal) (void)add_near_actor(actor,ODG_RESOURCE_TREE,ordinal);
        for (ordinal=0u;ordinal<2u;++ordinal) (void)add_near_actor(actor,ODG_RESOURCE_STONE,ordinal);
        (void)add_near_actor(actor,ODG_RESOURCE_COAL,0u);
        (void)add_near_actor(actor,ODG_RESOURCE_IRON,0u);
    }
    /* Ambient resources are streamed from deterministic chunk descriptors. Keeping the
     * fixed array for bootstrap guarantees prevents a visited-world history from becoming
     * a giant global resource array. */

}


static uint64_t resource_chunk_hash(int64_t cx,int64_t cz,uint32_t ordinal,uint32_t kind,uint32_t attempt) {
    uint64_t v=g_odg.seed^UINT64_C(0x5245535f43484e4b);
    v^=(uint64_t)cx*UINT64_C(0x9e3779b97f4a7c15);
    v^=(uint64_t)cz*UINT64_C(0xd1b54a32d192ed03);
    v^=(uint64_t)ordinal*UINT64_C(0x94d049bb133111eb);
    v^=(uint64_t)kind*UINT64_C(0xbf58476d1ce4e5b9);
    v^=(uint64_t)attempt*UINT64_C(0x632be59bd9b4e019);
    v^=v>>30u;v*=UINT64_C(0xbf58476d1ce4e5b9);v^=v>>27u;v*=UINT64_C(0x94d049bb133111eb);v^=v>>31u;
    return v;
}

static uint64_t procedural_resource_stable_id(int64_t cx,int64_t cz,uint32_t ordinal,uint32_t kind){
    uint64_t raw=resource_chunk_hash(cx,cz,ordinal,kind,UINT32_C(0x53544142));
    if(odg_worldgen_version()>=ODG_WORLDGEN_VERSION_RESOURCE_ID_NAMESPACES)
        return ODG_RESOURCE_STABLE_PROCEDURAL_BIT|(raw&~ODG_RESOURCE_STABLE_PROCEDURAL_BIT);
    return raw;
}

typedef struct {
    uint32_t kind;
    uint32_t ordinal;
    int64_t gx;
    int64_t gz;
    uint8_t valid;
} odg_canonical_resource_slot;

static int canonical_resource_surface_allows(int64_t gx,int64_t gz,uint32_t kind){
    odg_surface_sample surface;uint64_t required=0u;
    const int geological=resource_kind_requires_geological_exposure(kind);
    if(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)!=ODG_STATUS_OK)return 0;
    if((surface.flags&ODG_SURFACE_FLAG_WATER)!=0u)return 0;
    if(!geological&&(surface.flags&ODG_SURFACE_FLAG_STEEP)!=0u)return 0;
    return 1;
}

static int canonical_resource_spacing_clear(const odg_canonical_resource_slot *slots,uint32_t count,
                                            int64_t gx,int64_t gz,uint32_t kind){
    uint32_t i;const int geological=resource_kind_requires_geological_exposure(kind);
    for(i=0u;i<count;++i){
        const odg_canonical_resource_slot *other=&slots[i];int64_t dx,dz,spacing=3;
        if(other->valid==0u)continue;
        /* v4 also removes the old 1 m ore-node overlap allowance. Runtime collision
         * radii are ~0.9 m, so two geological nodes need 2 whole cells to coexist. */
        if(geological&&resource_kind_requires_geological_exposure(other->kind))spacing=2;
        dx=gx-other->gx;dz=gz-other->gz;
        if(dx*dx+dz*dz<spacing*spacing)return 0;
    }
    return 1;
}

static int canonical_resource_choose_cell(int64_t cx,int64_t cz,uint32_t kind,uint32_t ordinal,
                                          const odg_canonical_resource_slot *slots,uint32_t prior_count,
                                          int64_t *out_gx,int64_t *out_gz){
    static const uint32_t geological_steps[8]={7u,11u,13u,17u,19u,23u,29u,31u};
    const int geological=resource_kind_requires_geological_exposure(kind);
    const uint32_t attempt_limit=geological?900u:12u;
    const uint64_t search_seed=resource_chunk_hash(cx,cz,ordinal,kind,UINT32_C(0x47454f53));
    const uint32_t search_start=(uint32_t)(search_seed%900u);
    const uint32_t search_step=geological_steps[(search_seed>>12u)&7u];
    uint32_t attempt;
    for(attempt=0u;attempt<attempt_limit;++attempt){
        uint64_t h=resource_chunk_hash(cx,cz,ordinal,kind,attempt);
        uint32_t cell=geological?(search_start+attempt*search_step)%900u:0u;
        int64_t gx=cx*(int64_t)ODG_CHUNK_SIZE_CELLS+1+
                   (geological?(int64_t)(cell%30u):(int64_t)(h%30u));
        int64_t gz=cz*(int64_t)ODG_CHUNK_SIZE_CELLS+1+
                   (geological?(int64_t)(cell/30u):(int64_t)((h>>8u)%30u));
        int64_t tgx,tgz;
        if(odg_chunk_procedural_turret_cell(cx,cz,&tgx,&tgz) &&
           gx>=tgx-2&&gx<=tgx+2&&gz>=tgz-2&&gz<=tgz+2)continue;
        if(!geological&&odg_world_cave_entrance64(gx,gz)!=0u)continue;
        if(!canonical_resource_surface_allows(gx,gz,kind))continue;
        if(geological&&!odg_geology_surface_exposure_internal(gx,gz,kind))continue;
        if(!canonical_resource_spacing_clear(slots,prior_count,gx,gz,kind))continue;
        *out_gx=gx;*out_gz=gz;return 1;
    }
    return 0;
}

static uint32_t canonical_resource_layout(int64_t cx,int64_t cz,odg_canonical_resource_slot slots[64]){
    odg_chunk_descriptor descriptor;uint64_t required=0u;uint32_t count=0u,k,coal_count;
    if(odg_chunk_descriptor_get(cx,cz,&descriptor,sizeof(descriptor),&required)!=ODG_STATUS_OK)return 0u;
    coal_count=odg_chunk_coal_candidate_count_internal(cx,cz);
#define ODG_APPEND_CANONICAL_RESOURCE(resource_kind,resource_count) do { \
        for(k=0u;k<(resource_count)&&count<64u;++k){ \
            odg_canonical_resource_slot *slot=&slots[count]; \
            odg_memset(slot,0,sizeof(*slot));slot->kind=(resource_kind);slot->ordinal=count; \
            slot->valid=(uint8_t)(canonical_resource_choose_cell(cx,cz,slot->kind,slot->ordinal,slots,count,&slot->gx,&slot->gz)?1u:0u); \
            ++count; \
        } \
    } while(0)
    ODG_APPEND_CANONICAL_RESOURCE(ODG_RESOURCE_TREE,descriptor.tree_count);
    ODG_APPEND_CANONICAL_RESOURCE(ODG_RESOURCE_STONE,descriptor.stone_count);
    ODG_APPEND_CANONICAL_RESOURCE(ODG_RESOURCE_COAL,coal_count);
    ODG_APPEND_CANONICAL_RESOURCE(ODG_RESOURCE_IRON,descriptor.iron_count);
#undef ODG_APPEND_CANONICAL_RESOURCE
    return count;
}


int odg_resource_state_validate_internal(const odg_resource_node *r,uint32_t expected_id){
    const odg_flora_species_definition *flora=NULL;
    if(r==NULL||r->active==0u||r->id!=expected_id)return 0;
    if(r->kind!=ODG_RESOURCE_TREE&&r->kind!=ODG_RESOURCE_STONE&&r->kind!=ODG_RESOURCE_IRON&&
       r->kind!=ODG_RESOURCE_FLORA&&r->kind!=ODG_RESOURCE_COAL)return 0;
    if(r->state!=ODG_RESOURCE_STATE_AVAILABLE&&r->state!=ODG_RESOURCE_STATE_DEPLETED)return 0;
    if(r->harvest_actor!=UINT32_MAX&&r->harvest_actor>=ODG_MAX_ACTORS)return 0;
    if(r->state==ODG_RESOURCE_STATE_AVAILABLE){
        if(r->harvest_grace>ODG_HARVEST_GRACE_TICKS)return 0;
        if(r->harvest_actor==UINT32_MAX&&r->harvest_grace!=0u)return 0;
    }else{
        /* Depleted non-procedural nodes keep a short visible stump/vein remnant, using
         * the otherwise inactive harvest-grace field as a state-tagged decay timer.
         * Procedural depletion lives in the chunk bitset and therefore needs no runtime
         * remnant once the next stream refresh compacts it. */
        if(r->harvest_actor!=UINT32_MAX||r->harvest_progress!=0u||r->harvest_required!=0u||
           r->turret_hits!=0u||r->harvest_grace>ODG_RESOURCE_DEPLETED_VISUAL_TICKS||
           (r->procedural!=0u&&r->harvest_grace!=0u))return 0;
    }
    if((r->harvest_required==0u&&r->harvest_progress!=0u)||
       (r->harvest_required!=0u&&r->harvest_progress>r->harvest_required))return 0;
    if(r->yield_min==0u||r->yield_max<r->yield_min)return 0;
    if(r->bootstrap_actor_id!=UINT32_MAX&&r->bootstrap_actor_id>=ODG_MAX_ACTORS)return 0;
    if(r->procedural>1u)return 0;

    if(r->procedural!=0u){
        if(r->chunk_ordinal>=64u)return 0;
        if(odg_worldgen_version()>=ODG_WORLDGEN_VERSION_CANONICAL_RESOURCES){
            uint64_t expected_stable=procedural_resource_stable_id(r->chunk_x,r->chunk_z,r->chunk_ordinal,r->kind);
            /* SAVE validation stays O(N): canonical position itself is exhaustively
             * certified by WORLDGEN4 gates. Here the stable identity proves the record
             * belongs to this exact chunk/ordinal/kind without regenerating up to 64
             * placement searches for every serialized node. */
            if(r->stable_id!=expected_stable)return 0;
        }
    }else if(r->stable_id==0u||(r->stable_id&ODG_RESOURCE_STABLE_PROCEDURAL_BIT)!=0u)return 0;
    if(odg_worldgen_version()>=ODG_WORLDGEN_VERSION_RESOURCE_ID_NAMESPACES&&r->procedural!=0u&&
       (r->stable_id&ODG_RESOURCE_STABLE_PROCEDURAL_BIT)==0u)return 0;
    if(r->local_resident!=0u){
        int32_t x=0,z=0;
        if(!odg_global_fx_to_local_internal(r->global_fx_x,r->global_fx_z,&x,&z)||x!=r->x||z!=r->z)return 0;
    }

    if(r->kind==ODG_RESOURCE_TREE||r->kind==ODG_RESOURCE_FLORA){
        flora=odg_flora_species_internal(r->species_id);if(flora==NULL)return 0;
        if(r->flora_stage<ODG_FLORA_STAGE_SEEDLING||r->flora_stage>ODG_FLORA_STAGE_OLD)return 0;
        if((flora->variant_count==0u&&r->variant!=0u)||(flora->variant_count!=0u&&r->variant>=flora->variant_count))return 0;
        if(r->fruit_capacity<flora->fruit_capacity_min||r->fruit_capacity>flora->fruit_capacity_max||
           r->fruit_count>r->fruit_capacity||r->soil_moisture_permille>1000u||
           r->windfall_count>flora->natural_drop_max_per_cycle)return 0;
        if(flora->fruit_cycle_ticks!=0u&&r->fruit_cycle_ticks>=flora->fruit_cycle_ticks)return 0;
    }else{
        if(resource_deposit_profile(r->kind)==NULL||r->species_id!=0u||r->flora_stage!=0u||r->variant!=0u||
           r->fruit_count!=0u||r->fruit_capacity!=0u||r->soil_moisture_permille!=0u||
           r->fruit_cycle_ticks!=0u||r->windfall_count!=0u||r->age_ticks!=0u)return 0;
    }
    return 1;
}

static int materialize_canonical_resource(int64_t cx,int64_t cz,const odg_canonical_resource_slot *slot){
    uint32_t i;int32_t x,z;odg_resource_node *r;uint64_t stable_id;
    if(slot==NULL||slot->valid==0u||slot->ordinal>=64u||odg_chunk_resource_depleted(cx,cz,slot->ordinal))return 0;
    for(i=0u;i<g_odg.resource_count;++i){
        odg_resource_node *existing=&g_odg_resources[i];
        if(existing->procedural!=0u&&existing->chunk_x==cx&&existing->chunk_z==cz&&
           existing->chunk_ordinal==slot->ordinal){
            if(existing->local_resident!=0u)return 1;
            if(!odg_global_cell_center_to_local_fx_internal(slot->gx,slot->gz,&x,&z))return 0;
            if(!odg_position_clear_internal(x,z,ODG_FX_ONE)||
               !odg_dynamic_position_clear_internal(x,z,ODG_FX_ONE,UINT32_MAX,UINT32_MAX))return 0;
            existing->x=x;existing->z=z;existing->global_fx_x=slot->gx*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
            existing->global_fx_z=slot->gz*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;existing->local_resident=1u;
            return 1;
        }
    }
    if(!odg_global_cell_center_to_local_fx_internal(slot->gx,slot->gz,&x,&z))return 0;
    /* Canonical position is immutable. Runtime occupancy can only defer materialization;
     * it must never select a different worldgen candidate based on who happens to be here. */
    if(!odg_position_clear_internal(x,z,ODG_FX_ONE) ||
       !odg_dynamic_position_clear_internal(x,z,ODG_FX_ONE,UINT32_MAX,UINT32_MAX))return 0;
    if(!odg_entities_reserve_resources(g_odg.resource_count+1u))return 0;
    stable_id=procedural_resource_stable_id(cx,cz,slot->ordinal,slot->kind);
    r=&g_odg_resources[g_odg.resource_count];
    set_resource_with_identity(g_odg.resource_count,slot->kind,x,z,UINT32_MAX,stable_id,1);
    if(!r->active)return 0;
    if((slot->kind==ODG_RESOURCE_TREE||slot->kind==ODG_RESOURCE_FLORA)&&r->species_id!=0u){
        uint32_t roll=(uint32_t)(r->stable_id%100u);
        uint32_t stage=roll<10u?ODG_FLORA_STAGE_SEEDLING:(roll<25u?ODG_FLORA_STAGE_SAPLING:(roll<45u?ODG_FLORA_STAGE_YOUNG:(roll<90u?ODG_FLORA_STAGE_MATURE:ODG_FLORA_STAGE_OLD)));
        odg_ecology_init_resource(r,r->species_id,stage,(uint32_t)(r->stable_id>>8u));
    }
    r->chunk_x=cx;r->chunk_z=cz;r->chunk_ordinal=slot->ordinal;r->procedural=1u;
    ++g_odg.resource_count;return 1;
}

void odg_resources_migrate_canonical_worldgen_internal(void){
    odg_canonical_resource_slot slots[64];int64_t cached_cx=INT64_MIN,cached_cz=INT64_MIN;
    uint32_t slot_count=0u,read_i,write_i=0u;
    /* SAVE21 can contain v3 procedural nodes whose coordinates were selected while other
     * mutable nodes happened to be resident. Move their identity to the v4 canonical cell
     * immediately, but preserve harvest/ecology state. Setting local_resident=0 makes the
     * normal streamer perform collision-safe wake-up instead of overlapping a saved body. */
    for(read_i=0u;read_i<g_odg.resource_count;++read_i){
        odg_resource_node resource=g_odg_resources[read_i];
        if(resource.procedural!=0u){
            const odg_canonical_resource_slot *slot;int32_t local_x=0,local_z=0;
            if(resource.chunk_x!=cached_cx||resource.chunk_z!=cached_cz){
                cached_cx=resource.chunk_x;cached_cz=resource.chunk_z;
                slot_count=canonical_resource_layout(cached_cx,cached_cz,slots);
            }
            if(resource.chunk_ordinal>=slot_count)continue;
            slot=&slots[resource.chunk_ordinal];
            if(slot->valid==0u||slot->kind!=resource.kind||
               odg_chunk_resource_depleted(resource.chunk_x,resource.chunk_z,resource.chunk_ordinal))continue;
            resource.stable_id=procedural_resource_stable_id(resource.chunk_x,resource.chunk_z,resource.chunk_ordinal,resource.kind);
            resource.global_fx_x=slot->gx*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
            resource.global_fx_z=slot->gz*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
            if(odg_global_fx_to_local_internal(resource.global_fx_x,resource.global_fx_z,&local_x,&local_z)){
                if(resource.kind==ODG_RESOURCE_TREE||resource.kind==ODG_RESOURCE_FLORA){
                    const odg_flora_species_definition *canonical=
                        odg_flora_worldgen_species_internal(ODG_FLORA_GROWTH_TREE,local_x,local_z,resource.stable_id);
                    if(canonical!=NULL&&canonical->species_id!=resource.species_id){
                        uint32_t roll=(uint32_t)(resource.stable_id%100u);
                        uint32_t stage=roll<10u?ODG_FLORA_STAGE_SEEDLING:(roll<25u?ODG_FLORA_STAGE_SAPLING:(roll<45u?ODG_FLORA_STAGE_YOUNG:(roll<90u?ODG_FLORA_STAGE_MATURE:ODG_FLORA_STAGE_OLD)));
                        resource.species_id=canonical->species_id;resource.x=local_x;resource.z=local_z;
                        odg_ecology_init_resource(&resource,canonical->species_id,stage,(uint32_t)(resource.stable_id>>8u));
                    }
                }
                resource.x=local_x;resource.z=local_z;
            }
            resource.local_resident=0u;
        }
        g_odg_resources[write_i]=resource;
        g_odg_resources[write_i].id=write_i;++write_i;
    }
    for(read_i=write_i;read_i<g_odg.resource_count;++read_i)odg_memset(&g_odg_resources[read_i],0,sizeof(g_odg_resources[read_i]));
    g_odg.resource_count=write_i;odg_entities_spatial_mark_dirty();odg_resources_stream_refresh();
}

static void migrate_resource_stable_references(uint64_t old_id,uint64_t new_id){
    uint32_t i;
    if(old_id==0u||old_id==new_id)return;
    for(i=0u;i<g_odg.turret_count;++i)
        if(g_odg_turrets[i].target_resource_stable_id==old_id)g_odg_turrets[i].target_resource_stable_id=new_id;
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i)
        if(g_odg.fauna_nests[i].active&&g_odg.fauna_nests[i].host_resource_stable_id==old_id)
            g_odg.fauna_nests[i].host_resource_stable_id=new_id;
}

int odg_resources_migrate_identity_worldgen_internal(void){
    uint32_t i;
    const int upgrade_procedural=odg_worldgen_version()>=ODG_WORLDGEN_VERSION_BATHYMETRY&&
                                 odg_worldgen_version()<ODG_WORLDGEN_VERSION_RESOURCE_ID_NAMESPACES;
    for(i=0u;i<g_odg.resource_count;++i){
        odg_resource_node *r=&g_odg_resources[i];uint64_t old_id,new_id;
        if(!r->active)return 0;
        old_id=r->stable_id;
        if(r->procedural!=0u){
            if(!upgrade_procedural)continue; /* genuine v1 worldgen keeps its frozen identity semantics */
            new_id=ODG_RESOURCE_STABLE_PROCEDURAL_BIT|
                   (resource_chunk_hash(r->chunk_x,r->chunk_z,r->chunk_ordinal,r->kind,UINT32_C(0x53544142))&
                    ~ODG_RESOURCE_STABLE_PROCEDURAL_BIT);
        }else{
            new_id=odg_next_instance_id();if(new_id==0u)return 0;
        }
        migrate_resource_stable_references(old_id,new_id);r->stable_id=new_id;
        if(r->state==ODG_RESOURCE_STATE_DEPLETED){
            /* SAVE24 kept completed harvest progress forever. SAVE25 makes depletion a
             * short visual lifecycle phase, so migrate the dead work reservation into a
             * canonical remnant that will compact without losing any live resource. */
            r->harvest_actor=UINT32_MAX;r->harvest_progress=0u;r->harvest_required=0u;r->turret_hits=0u;
            r->harvest_grace=r->procedural!=0u?0u:ODG_RESOURCE_DEPLETED_VISUAL_TICKS;
        }
    }
    if(upgrade_procedural)g_odg_persistent_runtime.worldgen_version=ODG_WORLDGEN_VERSION_RESOURCE_ID_NAMESPACES;
    odg_entities_spatial_mark_dirty();return 1;
}

static int spawn_chunk_resource_legacy(int64_t cx,int64_t cz,uint32_t kind,uint32_t ordinal) {
    static const uint32_t geological_steps[8]={7u,11u,13u,17u,19u,23u,29u,31u};
    uint32_t attempt,i;
    const int geological=resource_kind_requires_geological_exposure(kind);
    const uint32_t attempt_limit=geological?900u:12u;
    const uint64_t search_seed=resource_chunk_hash(cx,cz,ordinal,kind,UINT32_C(0x47454f53));
    const uint32_t search_start=(uint32_t)(search_seed%900u);
    const uint32_t search_step=geological_steps[(search_seed>>12u)&7u];
    if(ordinal>=64u || odg_chunk_resource_depleted(cx,cz,ordinal)) return 0;
    for(i=0u;i<g_odg.resource_count;++i){
        const odg_resource_node *existing=&g_odg_resources[i];
        if(existing->procedural!=0u&&existing->chunk_x==cx&&existing->chunk_z==cz&&existing->chunk_ordinal==ordinal)return 1;
    }
    for(attempt=0u;attempt<attempt_limit;++attempt){
        uint64_t h=resource_chunk_hash(cx,cz,ordinal,kind,attempt);
        uint32_t cell=geological?(search_start+attempt*search_step)%900u:0u;
        int64_t gx=cx*(int64_t)ODG_CHUNK_SIZE_CELLS+1+
                   (geological?(int64_t)(cell%30u):(int64_t)(h%30u));
        int64_t gz=cz*(int64_t)ODG_CHUNK_SIZE_CELLS+1+
                   (geological?(int64_t)(cell/30u):(int64_t)((h>>8u)%30u));
        int64_t tgx,tgz;
        if(odg_chunk_procedural_turret_cell(cx,cz,&tgx,&tgz) &&
           gx>=tgx-2 && gx<=tgx+2 && gz>=tgz-2 && gz<=tgz+2)continue;
        int64_t lx=gx-odg_global_center_cell_x_internal();
        int64_t lz=gz-odg_global_center_cell_z_internal();
        int64_t fx=lx*(int64_t)ODG_FX_ONE+ODG_FX_ONE/2;
        int64_t fz=lz*(int64_t)ODG_FX_ONE+ODG_FX_ONE/2;
        odg_resource_node *r;
        if(fx<INT32_MIN||fx>INT32_MAX||fz<INT32_MIN||fz>INT32_MAX) continue;
        if(!geological&&odg_world_cave_entrance64(gx,gz)!=0u)continue;
        if(!resource_spawn_clear((int32_t)fx,(int32_t)fz,g_odg.resource_count,kind)) continue;
        if(geological&&!odg_geology_surface_exposure_internal(gx,gz,kind))continue;
        if(!odg_entities_reserve_resources(g_odg.resource_count+1u))return 0;
        r=&g_odg_resources[g_odg.resource_count];
        set_resource(g_odg.resource_count,kind,(int32_t)fx,(int32_t)fz,UINT32_MAX);
        if(!r->active)continue;
        r->stable_id=procedural_resource_stable_id(cx,cz,ordinal,kind);
        if((kind==ODG_RESOURCE_TREE || kind==ODG_RESOURCE_FLORA) && r->species_id!=0u){
            uint32_t roll=(uint32_t)(r->stable_id%100u);
            uint32_t stage=roll<10u?ODG_FLORA_STAGE_SEEDLING:(roll<25u?ODG_FLORA_STAGE_SAPLING:(roll<45u?ODG_FLORA_STAGE_YOUNG:(roll<90u?ODG_FLORA_STAGE_MATURE:ODG_FLORA_STAGE_OLD)));
            odg_ecology_init_resource(r,r->species_id,stage,(uint32_t)(r->stable_id>>8u));
        }
        r->chunk_x=cx;r->chunk_z=cz;r->chunk_ordinal=ordinal;r->procedural=1u;
        ++g_odg.resource_count;return 1;
    }
    return 0;
}

void odg_resources_stream_refresh(void) {
    static const int32_t player_offsets[9][2]={{0,0},{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}};
    int64_t chunk_x[96],chunk_z[96];
    uint32_t chunk_count=0u,i,write=0u,oi;
    int64_t pgx,pgz,pcx,pcz;
    odg_global_fx_to_global_cell_internal(g_odg.actors[ODG_PLAYER_ID].global_fx_x,g_odg.actors[ODG_PLAYER_ID].global_fx_z,&pgx,&pgz);
    pcx=odg_floor_div_i64_internal(pgx,(int64_t)ODG_CHUNK_SIZE_CELLS);pcz=odg_floor_div_i64_internal(pgz,(int64_t)ODG_CHUNK_SIZE_CELLS);
    for(oi=0u;oi<9u;++oi){chunk_x[chunk_count]=pcx+player_offsets[oi][0];chunk_z[chunk_count]=pcz+player_offsets[oi][1];++chunk_count;}
    for(i=1u;i<ODG_MAX_ACTORS&&chunk_count<96u;++i){
        const odg_actor *a=&g_odg.actors[i];int64_t gx,gz,cx,cz;uint32_t j;int duplicate=0;
        if(!a->active||a->hp==0u)continue;
        odg_global_fx_to_global_cell_internal(a->global_fx_x,a->global_fx_z,&gx,&gz);
        cx=odg_floor_div_i64_internal(gx,(int64_t)ODG_CHUNK_SIZE_CELLS);cz=odg_floor_div_i64_internal(gz,(int64_t)ODG_CHUNK_SIZE_CELLS);
        for(j=0u;j<chunk_count;++j)if(chunk_x[j]==cx&&chunk_z[j]==cz){duplicate=1;break;}
        if(!duplicate){chunk_x[chunk_count]=cx;chunk_z[chunk_count]=cz;++chunk_count;}
    }
    /* A remote TALA turret is an economic actor too. Keep its resource chunk materialized
     * even when neither the player nor a bot is nearby, so each shot still hits a physical tree. */
    for(i=0u;i<g_odg.turret_count&&chunk_count<96u;++i){
        const odg_turret *t=&g_odg_turrets[i];int64_t gx,gz,cx,cz;uint32_t j;int duplicate=0;
        if(!t->active||t->carried_by!=ODG_TURRET_NONE||t->mode!=ODG_TURRET_MODE_HARVEST)continue;
        gx=odg_floor_div_i64_internal(t->global_fx_x,(int64_t)ODG_FX_ONE);
        gz=odg_floor_div_i64_internal(t->global_fx_z,(int64_t)ODG_FX_ONE);
        cx=odg_floor_div_i64_internal(gx,(int64_t)ODG_CHUNK_SIZE_CELLS);cz=odg_floor_div_i64_internal(gz,(int64_t)ODG_CHUNK_SIZE_CELLS);
        for(j=0u;j<chunk_count;++j)if(chunk_x[j]==cx&&chunk_z[j]==cz){duplicate=1;break;}
        if(!duplicate){chunk_x[chunk_count]=cx;chunk_z[chunk_count]=cz;++chunk_count;}
    }
    /* Keep procedural entities whose chunk is still resident. This preserves partial
     * HOLD progress, reservations and deterministic target IDs across a stream refresh. */
    for(i=0u;i<g_odg.resource_count;++i){
        odg_resource_node *r=&g_odg_resources[i];int keep;uint32_t j;
        if(!r->active)continue;
        /* Procedural depletion is permanently represented by the chunk bitset, so its
         * runtime record can disappear immediately. Player/bootstrap remnants remain
         * only for their short visual decay window, then compact away as well. */
        if(r->state==ODG_RESOURCE_STATE_DEPLETED){
            if(r->procedural!=0u||r->harvest_grace==0u)continue;
            keep=1;
        }else keep=r->procedural==0u;
        if(!keep)for(j=0u;j<chunk_count;++j)if(r->chunk_x==chunk_x[j]&&r->chunk_z==chunk_z[j]){keep=1;break;}
        if(!keep)continue;
        if(write!=i)g_odg_resources[write]=g_odg_resources[i];
        g_odg_resources[write].id=write;++write;
    }
    for(i=write;i<g_odg.resource_count;++i)odg_memset(&g_odg_resources[i],0,sizeof(g_odg_resources[i]));
    g_odg.resource_count=write;
    for(oi=0u;oi<chunk_count;++oi){
        odg_chunk_descriptor descriptor;uint64_t required=0u;uint32_t ordinal=0u,k;
        int64_t cx=chunk_x[oi],cz=chunk_z[oi];
        if(odg_chunk_descriptor_get(cx,cz,&descriptor,sizeof(descriptor),&required)!=ODG_STATUS_OK)continue;
        if(odg_worldgen_version()>=ODG_WORLDGEN_VERSION_CANONICAL_RESOURCES){
            odg_canonical_resource_slot slots[64];uint32_t slot_count=canonical_resource_layout(cx,cz,slots);
            for(k=0u;k<slot_count;++k)(void)materialize_canonical_resource(cx,cz,&slots[k]);
        }else{
            for(k=0u;k<descriptor.tree_count;++k,++ordinal)(void)spawn_chunk_resource_legacy(cx,cz,ODG_RESOURCE_TREE,ordinal);
            for(k=0u;k<descriptor.stone_count;++k,++ordinal)(void)spawn_chunk_resource_legacy(cx,cz,ODG_RESOURCE_STONE,ordinal);
            for(k=0u;k<odg_chunk_coal_candidate_count_internal(cx,cz);++k,++ordinal)(void)spawn_chunk_resource_legacy(cx,cz,ODG_RESOURCE_COAL,ordinal);
            for(k=0u;k<descriptor.iron_count;++k,++ordinal)(void)spawn_chunk_resource_legacy(cx,cz,ODG_RESOURCE_IRON,ordinal);
        }
    }
    odg_entities_spatial_mark_dirty();
}


static uint32_t harvest_required(const odg_actor *actor,const odg_resource_node *resource,uint32_t *message) {
    const odg_item_stack *tool=odg_inventory_selected_const(&actor->inventory);
    uint32_t tier=ODG_MATERIAL_NONE,type=ODG_ITEM_NONE;
    if (message!=NULL) *message=ODG_MESSAGE_NONE;
    if (tool!=NULL && tool->quantity!=0u) {type=tool->type_id;tier=tool->material_tier;}
    if (odg_resource_is_flora_internal(resource)) {
        const odg_flora_species_definition *flora=odg_resource_flora_definition_internal(resource);
        uint32_t base=flora!=NULL?flora->harvest_base_ticks:480u;
        uint32_t stage_index=resource->flora_stage>=ODG_FLORA_STAGE_SEEDLING&&resource->flora_stage<=ODG_FLORA_STAGE_OLD?resource->flora_stage-1u:3u;
        if(flora!=NULL)base=(uint32_t)(((uint64_t)base*flora->stage_harvest_time_permille[stage_index])/1000u);
        if(flora!=NULL && flora->harvest_tool_item_type!=ODG_ITEM_NONE && type!=flora->harvest_tool_item_type){
            if(type==ODG_ITEM_NONE && (flora->harvest_flags&ODG_FLORA_HARVEST_ALLOW_HAND)!=0u)return base*2u;
            if(message!=NULL)*message=ODG_MESSAGE_TOOL_REQUIRED;
            return 0u;
        }
        if (tier==ODG_MATERIAL_IRON) return odg_min_u32(base,192u);
        if (tier==ODG_MATERIAL_STONE) return odg_min_u32(base,300u);
        return base;
    }
    {
        const odg_resource_deposit_profile *profile=resource_deposit_profile(resource->kind);
        const odg_item_definition *tool_definition=odg_item_definition_internal(type);
        if(profile==NULL)return 0u;
        if(tool_definition==NULL||(tool_definition->capability_bits&profile->required_tool_capability)==0u){
            if(message!=NULL)*message=ODG_MESSAGE_TOOL_REQUIRED;
            return 0u;
        }
        if(tier<profile->minimum_tool_tier||tier>ODG_MATERIAL_IRON||profile->harvest_ticks_by_tier[tier]==0u){
            if(message!=NULL)*message=ODG_MESSAGE_PICKAXE_TIER_REQUIRED;
            return 0u;
        }
        return profile->harvest_ticks_by_tier[tier];
    }
}

static uint32_t resource_harvest_particle_rgba(const odg_resource_node *resource) {
    const odg_resource_deposit_profile *profile;
    if(resource==NULL)return 0xa9a9a3ffu;
    if(odg_resource_is_flora_internal(resource))return 0xa8c57affu;
    profile=resource_deposit_profile(resource->kind);
    return profile!=NULL?profile->harvest_particle_rgba:0xa9a9a3ffu;
}

static uint32_t resource_harvest_quantity(const odg_resource_node *resource) {
    uint32_t span,quantity;
    if(resource==NULL||resource->yield_max<resource->yield_min)return 0u;
    span=resource->yield_max-resource->yield_min+1u;
    quantity=resource->yield_min+(mix32(resource->stable_id^g_odg.seed)%span);
    if(resource->species_id!=0u){
        const odg_flora_species_definition *flora=odg_flora_species_internal(resource->species_id);
        uint32_t stage_index=resource->flora_stage>=ODG_FLORA_STAGE_SEEDLING&&resource->flora_stage<=ODG_FLORA_STAGE_OLD?resource->flora_stage-1u:3u;
        if(flora!=NULL)quantity=(uint32_t)(((uint64_t)quantity*flora->stage_yield_permille[stage_index]+999u)/1000u);
        if(quantity==0u)quantity=1u;
    }
    return quantity;
}

static int spill_flora_fruit(odg_resource_node *resource) {
    const odg_flora_species_definition *flora;const odg_item_definition *definition;odg_item_stack fruit;
    if(resource==NULL||resource->species_id==0u||resource->fruit_count==0u)return 1;
    flora=odg_flora_species_internal(resource->species_id);
    if(flora==NULL||flora->fruit_item_type==ODG_ITEM_NONE){resource->fruit_count=0u;return 1;}
    definition=odg_item_definition_internal(flora->fruit_item_type);
    if(definition==NULL)return 0;
    odg_memset(&fruit,0,sizeof(fruit));fruit.type_id=flora->fruit_item_type;fruit.quantity=resource->fruit_count;
    fruit.flags=definition->flags;fruit.material_tier=definition->default_material_tier;fruit.payload_id=resource->species_id;
    if(!odg_spawn_world_pickup(&fruit,resource->x,resource->z,20u))return 0;
    resource->fruit_count=0u;return 1;
}

static int complete_resource_harvest(odg_resource_node *resource) {
    const odg_item_definition *definition;odg_item_stack output;uint32_t quantity,drop_count=1u;
    if(resource==NULL)return 0;
    quantity=resource_harvest_quantity(resource);if(quantity==0u)return 0;
    odg_memset(&output,0,sizeof(output));output.type_id=odg_resource_harvest_item_type_internal(resource);output.quantity=quantity;
    output.material_tier=odg_resource_harvest_material_internal(resource);definition=odg_item_definition_internal(output.type_id);
    if(definition==NULL||!odg_item_stack_normalize_internal(&output))return 0;
    if(resource->species_id!=0u&&resource->fruit_count!=0u)++drop_count;
    /* Every operation that can fail externally is preflighted before the harvest commits:
     * pickup capacity first, then the persistent depletion record for procedural nodes.
     * Once these succeed, the two pickup inserts and depletion bit cannot require memory. */
    if(!odg_world_pickups_prepare_internal(drop_count))return 0;
    if(resource->procedural!=0u&&
       !odg_chunk_prepare_resource_depletion_internal(resource->chunk_x,resource->chunk_z,resource->chunk_ordinal))return 0;
    if(!spill_flora_fruit(resource))return 0;
    if(!odg_spawn_world_pickup(&output,resource->x,resource->z,0u))return 0;
    if(resource->procedural!=0u&&
       !odg_chunk_mark_resource_depleted(resource->chunk_x,resource->chunk_z,resource->chunk_ordinal))return 0;
    resource->state=ODG_RESOURCE_STATE_DEPLETED;resource->harvest_actor=UINT32_MAX;
    resource->harvest_progress=0u;resource->harvest_required=0u;resource->turret_hits=0u;
    resource->harvest_grace=resource->procedural!=0u?0u:ODG_RESOURCE_DEPLETED_VISUAL_TICKS;
    return 1;
}

static uint32_t nearest_resource(const odg_actor *actor) {
    uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)ODG_RESOURCE_INTERACT_RANGE_FX*ODG_RESOURCE_INTERACT_RANGE_FX;
    for (i=0u;i<g_odg.resource_count;++i) {
        const odg_resource_node *r=&g_odg_resources[i];int64_t d2;
        if (!r->active || r->local_resident==0u || r->state!=ODG_RESOURCE_STATE_AVAILABLE) continue;
        d2=odg_dist2(actor->x,actor->z,r->x,r->z);
        if (d2<=best_d2){best_d2=d2;best=i;}
    }
    return best;
}

int odg_resource_build_hint(const odg_actor *actor,odg_interaction_hint *hint) {
    uint32_t id,required,message=0u;const odg_item_stack *selected;const odg_item_definition *selected_def;odg_resource_node *resource;
    if (actor==NULL || hint==NULL) return 0;
    id=nearest_resource(actor);if(id>=g_odg.resource_count)return 0;resource=&g_odg_resources[id];
    selected=odg_inventory_selected_const(&actor->inventory);selected_def=selected!=NULL?odg_item_definition_internal(selected->type_id):NULL;
    if(resource->species_id!=0u && resource->fruit_count>0u && (selected_def==NULL||(selected_def->capability_bits&ODG_ITEM_CAP_HARVEST)==0u)){
        hint->action=ODG_INTERACTION_GATHER_FRUIT;hint->target_kind=ODG_INTERACTION_TARGET_RESOURCE;hint->target_id=id;
        hint->requires_hold=0u;hint->valid=odg_territory_allows_environment_action(actor->id,resource->x,resource->z)?1u:0u;
        hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_TERRITORY_REQUIRED;return 1;
    }
    required=harvest_required(actor,resource,&message);
    hint->action=ODG_INTERACTION_HARVEST;hint->target_kind=ODG_INTERACTION_TARGET_RESOURCE;hint->target_id=id;
    hint->requires_hold=1u;hint->valid=(required!=0u&&odg_territory_allows_environment_action(actor->id,resource->x,resource->z))?1u:0u;hint->threshold_ticks=required;
    hint->message_code=hint->valid?message:(!odg_territory_allows_environment_action(actor->id,resource->x,resource->z)?ODG_MESSAGE_TERRITORY_REQUIRED:message);
    hint->progress_ticks=(resource->harvest_actor==actor->id)?resource->harvest_progress:0u;
    return 1;
}


int odg_resource_hold_tick(uint32_t actor_id,uint32_t resource_id) {
    odg_actor *actor;odg_resource_node *r;uint32_t required,message=0u;
    if(actor_id>=ODG_MAX_ACTORS||resource_id>=g_odg.resource_count)return 0;
    actor=&g_odg.actors[actor_id];r=&g_odg_resources[resource_id];
    if(!actor->active||actor->hp==0u||!r->active||r->local_resident==0u||r->state!=ODG_RESOURCE_STATE_AVAILABLE)return 0;
    if(odg_dist2(actor->x,actor->z,r->x,r->z)>(int64_t)ODG_RESOURCE_INTERACT_RANGE_FX*ODG_RESOURCE_INTERACT_RANGE_FX)return 0;
    if(!odg_territory_allows_environment_action(actor_id,r->x,r->z))return 0;
    /* One resource has one active work reservation. Without this, two legitimate
     * actors alternating ticks reset each other's progress forever. The reservation
     * naturally expires through harvest_grace when its owner walks away. */
    if(r->harvest_actor!=UINT32_MAX && r->harvest_actor!=actor_id && r->harvest_grace>0u)return 0;
    required=harvest_required(actor,r,&message);if(required==0u)return 0;
    if(r->harvest_actor!=actor_id){r->harvest_actor=actor_id;r->harvest_progress=0u;}
    r->harvest_required=required;r->harvest_grace=ODG_HARVEST_GRACE_TICKS;
    if(r->harvest_progress<required){
        uint32_t next_progress=r->harvest_progress+1u;
        if(next_progress>=required){
            /* The final work unit is transactional with loot + procedural depletion.
             * Preflight/commit the harvest before charging the final tool wear. If world
             * allocation cannot commit, progress and durability remain exactly where they
             * were, so retries never grind a tool against an already-complete progress bar. */
            if(!complete_resource_harvest(r))return 0;
            if((required%12u)==0u)odg_item_wear_internal(odg_inventory_selected(&actor->inventory),1u);
            odg_emit_particles(r->x,r->z,resource_harvest_particle_rgba(r),12u);
            return 2;
        }
        r->harvest_progress=next_progress;
        /* Durability is proportional only to work actually performed this tick. */
        if((next_progress%12u)==0u)odg_item_wear_internal(odg_inventory_selected(&actor->inventory),1u);
    }
    return 1;
}

int odg_resource_turret_hit(uint64_t stable_id,uint32_t turret_tier) {
    uint32_t i;
    odg_resource_node *r=NULL;
    uint32_t required;
    if(stable_id==0u)return 0;
    for(i=0u;i<g_odg.resource_count;++i){
        if(g_odg_resources[i].active && g_odg_resources[i].stable_id==stable_id){r=&g_odg_resources[i];break;}
    }
    if(r==NULL || r->local_resident==0u || r->state!=ODG_RESOURCE_STATE_AVAILABLE || !odg_resource_is_flora_internal(r))return 0;
    {const odg_flora_species_definition *flora=odg_resource_flora_definition_internal(r);if(flora==NULL||(flora->harvest_flags&ODG_FLORA_HARVEST_TURRET_ELIGIBLE)==0u)return 0;}
    /* TALA preserves the directive's tier profile without introducing a second damage
     * system: each real turret shot contributes one deterministic harvest impact. */
    if(turret_tier==ODG_MATERIAL_IRON) required=2u;
    else if(turret_tier==ODG_MATERIAL_STONE) required=2u+(mix32(r->stable_id^UINT64_C(0x53544f4e45))&1u);
    else required=3u+(mix32(r->stable_id^UINT64_C(0x574f4f44))&1u);
    if(r->turret_hits<required)++r->turret_hits;
    odg_emit_particles(r->x,r->z,0xb68b5affu,5u);
    if(r->turret_hits<required)return 1;
    if(!complete_resource_harvest(r)){r->turret_hits=required-1u;return 1;}
    odg_emit_particles(r->x,r->z,0xa8c57affu,16u);
    return 2;
}

void odg_resources_tick(void) {
    uint32_t i;
    for(i=0u;i<g_odg.resource_count;++i){
        odg_resource_node *r=&g_odg_resources[i];if(!r->active)continue;
        if(r->state==ODG_RESOURCE_STATE_DEPLETED){
            if(r->procedural==0u&&r->harvest_grace>0u)--r->harvest_grace;
            continue;
        }
        if(r->local_resident==0u)continue;
        if(r->harvest_grace>0u)--r->harvest_grace;else if(r->harvest_progress>0u){uint32_t decay=r->harvest_progress>4u?4u:r->harvest_progress;r->harvest_progress-=decay;if(r->harvest_progress==0u)r->harvest_actor=UINT32_MAX;}
    }
}

int32_t odg_resource_collision_radius_fx_internal(const odg_resource_node *r) {
    if(r==NULL)return 0;
    if(r->species_id!=0u){
        const odg_flora_species_definition *flora=odg_resource_flora_definition_internal(r);
        if(flora!=NULL)return odg_flora_collision_radius_fx_internal(flora,r->flora_stage);
        return ODG_FX_ONE/2;
    }
    return ODG_FX_ONE*9/10;
}

uint32_t odg_resource_physical_height_milli_internal(const odg_resource_node *r){
    if(r==NULL||!r->active||r->state!=ODG_RESOURCE_STATE_AVAILABLE)return 0u;
    if(r->species_id!=0u){
        const odg_flora_species_definition *flora=odg_resource_flora_definition_internal(r);
        static const uint32_t stage_height_permille[ODG_FLORA_MAX_STAGES]={260u,430u,680u,1000u,1120u};
        uint32_t base,stage,variation;
        if(flora==NULL)return 0u;
        if(flora->growth_form==ODG_FLORA_GROWTH_TREE)base=2800u;
        else if(flora->growth_form==ODG_FLORA_GROWTH_SHRUB)base=1100u;
        else if(flora->growth_form==ODG_FLORA_GROWTH_CROP)base=900u;
        else base=450u;
        stage=r->flora_stage>=ODG_FLORA_STAGE_SEEDLING&&r->flora_stage<=ODG_FLORA_STAGE_OLD?
            r->flora_stage-ODG_FLORA_STAGE_SEEDLING:ODG_FLORA_STAGE_MATURE-ODG_FLORA_STAGE_SEEDLING;
        /* Stable per-plant morphology changes physical height by ±10%; render and
         * airspace query the same result so a visible crown never becomes intangible. */
        variation=900u+(uint32_t)((r->stable_id^(r->stable_id>>32u))&UINT64_C(255))*200u/255u;
        return (uint32_t)(((uint64_t)base*stage_height_permille[stage]*variation)/UINT64_C(1000000));
    }
    if(r->kind==ODG_RESOURCE_STONE)return 950u;
    if(r->kind==ODG_RESOURCE_COAL||r->kind==ODG_RESOURCE_IRON)return 520u;
    return 700u;
}

static int32_t resource_max_collision_radius_fx(void) {
    uint32_t i,j,max_milli=900u;
    for(i=0u;i<odg_flora_species_count();++i){
        odg_flora_species_definition flora;uint64_t required=0u;
        if(odg_flora_species_get(i,&flora,sizeof(flora),&required)!=ODG_STATUS_OK)continue;
        for(j=0u;j<ODG_FLORA_MAX_STAGES;++j){
            int32_t radius_fx=odg_flora_collision_radius_fx_internal(&flora,ODG_FLORA_STAGE_SEEDLING+j);
            uint32_t milli=(uint32_t)(((uint64_t)(radius_fx>0?radius_fx:0)*1000u)/(uint32_t)ODG_FX_ONE);
            if(milli>max_milli)max_milli=milli;
        }
    }
    return (int32_t)(((int64_t)max_milli*ODG_FX_ONE)/1000);
}

typedef struct {
    int64_t global_x,global_z;
    int32_t actor_radius;
    uint32_t ignore_resource_id;
} odg_resource_collision_query;

static int resource_collision_visit(uint32_t id,void *context) {
    odg_resource_collision_query *q=(odg_resource_collision_query *)context;
    const odg_resource_node *r;int32_t rr,sum;int64_t dx,dz;
    if(q==NULL||id>=g_odg.resource_count||id==q->ignore_resource_id)return 0;
    r=&g_odg_resources[id];
    if(!r->active||r->local_resident==0u||r->state!=ODG_RESOURCE_STATE_AVAILABLE)return 0;
    rr=odg_resource_collision_radius_fx_internal(r);sum=q->actor_radius+rr;
    dx=r->global_fx_x-q->global_x;dz=r->global_fx_z-q->global_z;
    return dx*dx+dz*dz<(int64_t)sum*sum;
}

int odg_resource_position_blocked_ignoring_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_resource_id) {
    odg_resource_collision_query q;int32_t query_radius=radius+resource_max_collision_radius_fx();
    odg_local_fx_to_global_fx_internal(x,z,&q.global_x,&q.global_z);q.actor_radius=radius;q.ignore_resource_id=ignore_resource_id;
    return odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_RESOURCE,q.global_x,q.global_z,
                                                   query_radius,resource_collision_visit,&q);
}

int odg_resource_position_blocked(int32_t x,int32_t z,int32_t radius) {
    return odg_resource_position_blocked_ignoring_internal(x,z,radius,UINT32_MAX);
}

int32_t odg_copy_resources(odg_resource_snapshot *out_resources,uint64_t capacity,uint64_t *out_required) {
    uint32_t picked[ODG_RESOURCE_MAX_ENTRIES],picked_count=0u,i;
    if(out_required!=NULL)*out_required=(uint64_t)sizeof(odg_resource_snapshot);
    if(!g_odg.initialized)return ODG_STATUS_INVALID_STATE;
    if(out_resources==NULL||capacity<(uint64_t)sizeof(*out_resources))return ODG_STATUS_BUFFER_TOO_SMALL;
    odg_memset(out_resources,0,sizeof(*out_resources));out_resources->struct_size=(uint32_t)sizeof(*out_resources);
    /* Snapshot is intentionally bounded. Select nearest resources so UI/map never lose
     * the player's local context merely because far-away bots are also fully simulated. */
    while(picked_count<ODG_RESOURCE_MAX_ENTRIES){
        uint32_t best=UINT32_MAX;int64_t best_d2=INT64_MAX;
        for(i=0u;i<g_odg.resource_count;++i){
            const odg_resource_node *r=&g_odg_resources[i];uint32_t j;int used=0;int64_t d2;
            if(!r->active||r->local_resident==0u)continue;
            for(j=0u;j<picked_count;++j)if(picked[j]==i){used=1;break;}
            if(used)continue;
            d2=odg_dist2(g_odg.actors[ODG_PLAYER_ID].x,g_odg.actors[ODG_PLAYER_ID].z,r->x,r->z);
            if(d2<best_d2){best_d2=d2;best=i;}
        }
        if(best==UINT32_MAX)break;
        picked[picked_count++]=best;
    }
    for(i=0u;i<picked_count;++i){
        const odg_resource_node *r=&g_odg_resources[picked[i]];odg_resource_entry *e=&out_resources->entries[i];
        int64_t gx_milli=(r->global_fx_x/(int64_t)ODG_FX_ONE)*INT64_C(1000)+((r->global_fx_x%(int64_t)ODG_FX_ONE)*INT64_C(1000))/(int64_t)ODG_FX_ONE;
        int64_t gz_milli=(r->global_fx_z/(int64_t)ODG_FX_ONE)*INT64_C(1000)+((r->global_fx_z%(int64_t)ODG_FX_ONE)*INT64_C(1000))/(int64_t)ODG_FX_ONE;
        e->stable_id=r->stable_id;e->resource_id=r->id;e->kind=r->kind;e->state=r->state;
        e->x_milli=gx_milli<INT32_MIN?INT32_MIN:(gx_milli>INT32_MAX?INT32_MAX:(int32_t)gx_milli);
        e->z_milli=gz_milli<INT32_MIN?INT32_MIN:(gz_milli>INT32_MAX?INT32_MAX:(int32_t)gz_milli);
        e->progress_ticks=r->harvest_progress;e->required_ticks=r->harvest_required;
        e->yield_preview_min=r->yield_min;e->yield_preview_max=r->yield_max;
        e->species_id=r->species_id;e->flora_stage=r->flora_stage;e->fruit_count=r->fruit_count;e->soil_moisture_permille=r->soil_moisture_permille;
    }
    out_resources->count=picked_count;return ODG_STATUS_OK;
}
