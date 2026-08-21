#include "game_internal.h"

#include <stddef.h>
#include <stdint.h>

#define ODG_CONSTRUCTION_RADIUS_FX (18 * ODG_FX_ONE / 25) /* 0.72 m structural cell footprint */
#define ODG_CONSTRUCTION_PLACE_DISTANCE_FX (9 * ODG_FX_ONE / 5)
#define ODG_CONSTRUCTION_WALL_HEIGHT_MILLI UINT32_C(1840)
#define ODG_CONSTRUCTION_DOORWAY_HEIGHT_MILLI UINT32_C(1840)
#define ODG_CONSTRUCTION_ROOF_HEIGHT_MILLI UINT32_C(2040)
#define ODG_CONSTRUCTION_FLOOR_HEIGHT_MILLI UINT32_C(120)

typedef struct {
    uint32_t material_tier;
    uint32_t dismantle_ticks;
    uint32_t base_health;
} odg_construction_material_profile;

static const odg_construction_material_profile g_construction_materials[] = {
    {ODG_MATERIAL_WOOD, ODG_INTERACT_HOLD_TICKS, UINT32_C(160)},
    {ODG_MATERIAL_STONE, (3u * ODG_INTERACT_HOLD_TICKS) / 2u, UINT32_C(320)},
    {ODG_MATERIAL_IRON, 2u * ODG_INTERACT_HOLD_TICKS, UINT32_C(520)}
};

/* Build shape is command/runtime state, not world state. Existing SAVE18 worlds never had
 * a selectable shape mode, so loading one deliberately defaults every actor to WALL. Shape
 * of already placed modules becomes SAVE19 data; structural health becomes SAVE20 data. */
static uint32_t g_construction_selected_shape[ODG_MAX_ACTORS];

static const odg_construction_material_profile *construction_material_profile(uint32_t material_tier) {
    uint32_t i;
    for (i=0u;i<(uint32_t)(sizeof(g_construction_materials)/sizeof(g_construction_materials[0]));++i)
        if (g_construction_materials[i].material_tier==material_tier) return &g_construction_materials[i];
    return NULL;
}

int odg_construction_shape_valid_internal(uint32_t shape) {
    return shape==ODG_CONSTRUCTION_SHAPE_WALL || shape==ODG_CONSTRUCTION_SHAPE_FLOOR ||
           shape==ODG_CONSTRUCTION_SHAPE_DOORWAY || shape==ODG_CONSTRUCTION_SHAPE_ROOF;
}

static uint32_t construction_shape_layer(uint32_t shape) {
    if(shape==ODG_CONSTRUCTION_SHAPE_FLOOR)return 0u;
    if(shape==ODG_CONSTRUCTION_SHAPE_WALL||shape==ODG_CONSTRUCTION_SHAPE_DOORWAY)return 1u;
    if(shape==ODG_CONSTRUCTION_SHAPE_ROOF)return 2u;
    return UINT32_MAX;
}

static uint32_t construction_shape_priority(uint32_t shape) {
    if(shape==ODG_CONSTRUCTION_SHAPE_ROOF)return 3u;
    if(shape==ODG_CONSTRUCTION_SHAPE_WALL||shape==ODG_CONSTRUCTION_SHAPE_DOORWAY)return 2u;
    return shape==ODG_CONSTRUCTION_SHAPE_FLOOR?1u:0u;
}

static int construction_shape_supports_roof(uint32_t shape) {
    return shape==ODG_CONSTRUCTION_SHAPE_WALL||shape==ODG_CONSTRUCTION_SHAPE_DOORWAY;
}

uint32_t odg_construction_max_health_internal(uint32_t material_tier,uint32_t shape){
    const odg_construction_material_profile *profile=construction_material_profile(material_tier);
    uint64_t scaled;uint32_t permille;
    if(profile==NULL||!odg_construction_shape_valid_internal(shape))return 0u;
    if(shape==ODG_CONSTRUCTION_SHAPE_WALL)permille=1200u;
    else if(shape==ODG_CONSTRUCTION_SHAPE_FLOOR)permille=900u;
    else if(shape==ODG_CONSTRUCTION_SHAPE_DOORWAY)permille=950u;
    else permille=1000u;
    scaled=((uint64_t)profile->base_health*(uint64_t)permille+999u)/1000u;
    return scaled>UINT32_MAX?UINT32_MAX:(uint32_t)scaled;
}

uint32_t odg_construction_selected_shape_internal(uint32_t actor_id) {
    uint32_t shape;
    if(actor_id>=ODG_MAX_ACTORS)return ODG_CONSTRUCTION_SHAPE_WALL;
    shape=g_construction_selected_shape[actor_id];
    return odg_construction_shape_valid_internal(shape)?shape:ODG_CONSTRUCTION_SHAPE_WALL;
}

int odg_construction_set_shape_internal(uint32_t actor_id,uint32_t shape) {
    if(actor_id>=ODG_MAX_ACTORS||!odg_construction_shape_valid_internal(shape))return 0;
    g_construction_selected_shape[actor_id]=shape;
    return 1;
}

int odg_construction_profiles_validate_internal(void) {
    const odg_item_definition *item=odg_item_definition_internal(ODG_ITEM_BUILDING_BLOCK);
    uint32_t i,j;
    if(item==NULL||item->category!=ODG_ITEM_CATEGORY_DEPLOYABLE||item->max_stack<2u||
       (item->capability_bits&(ODG_ITEM_CAP_PLACE|ODG_ITEM_CAP_CONSTRUCT))!=(ODG_ITEM_CAP_PLACE|ODG_ITEM_CAP_CONSTRUCT)||
       (item->flags&ODG_ITEM_FLAG_ARTIFACT)!=0u)return 0;
    for(i=0u;i<(uint32_t)(sizeof(g_construction_materials)/sizeof(g_construction_materials[0]));++i){
        const odg_construction_material_profile *p=&g_construction_materials[i];
        if(p->material_tier==ODG_MATERIAL_NONE||p->dismantle_ticks<ODG_INTERACT_HOLD_TICKS||p->base_health==0u)return 0;
        for(j=i+1u;j<(uint32_t)(sizeof(g_construction_materials)/sizeof(g_construction_materials[0]));++j)
            if(g_construction_materials[j].material_tier==p->material_tier)return 0;
    }
    for(i=0u;i<(uint32_t)(sizeof(g_construction_materials)/sizeof(g_construction_materials[0]));++i){
        odg_recipe_definition recipe;uint64_t required=0u;
        uint32_t recipe_id=odg_recipe_find_output_internal(ODG_ITEM_BUILDING_BLOCK,g_construction_materials[i].material_tier);
        if(recipe_id==0u||odg_recipe_get(recipe_id,&recipe,sizeof(recipe),&required)!=ODG_STATUS_OK||
           recipe.output_quantity==0u||recipe.ingredient_count!=1u)return 0;
    }
    return construction_material_profile(ODG_MATERIAL_WOOD)!=NULL&&
           construction_material_profile(ODG_MATERIAL_STONE)!=NULL&&
           construction_material_profile(ODG_MATERIAL_IRON)!=NULL&&
           construction_shape_layer(ODG_CONSTRUCTION_SHAPE_FLOOR)==0u&&
           construction_shape_layer(ODG_CONSTRUCTION_SHAPE_WALL)==1u&&
           construction_shape_layer(ODG_CONSTRUCTION_SHAPE_DOORWAY)==1u&&
           construction_shape_layer(ODG_CONSTRUCTION_SHAPE_ROOF)==2u&&
           odg_construction_max_health_internal(ODG_MATERIAL_WOOD,ODG_CONSTRUCTION_SHAPE_WALL)>
           odg_construction_max_health_internal(ODG_MATERIAL_WOOD,ODG_CONSTRUCTION_SHAPE_FLOOR);
}

uint32_t odg_construction_shape_height_milli_internal(uint32_t shape){
    if(shape==ODG_CONSTRUCTION_SHAPE_WALL)return ODG_CONSTRUCTION_WALL_HEIGHT_MILLI;
    if(shape==ODG_CONSTRUCTION_SHAPE_FLOOR)return ODG_CONSTRUCTION_FLOOR_HEIGHT_MILLI;
    if(shape==ODG_CONSTRUCTION_SHAPE_DOORWAY)return ODG_CONSTRUCTION_DOORWAY_HEIGHT_MILLI;
    if(shape==ODG_CONSTRUCTION_SHAPE_ROOF)return ODG_CONSTRUCTION_ROOF_HEIGHT_MILLI;
    return 0u;
}

uint32_t odg_construction_physical_height_milli_internal(const odg_construction_block *block){
    return block!=NULL&&block->active?odg_construction_shape_height_milli_internal(block->shape):0u;
}
int32_t odg_construction_collision_radius_fx_internal(const odg_construction_block *block){
    /* Floors and roofs are traversable layers. A doorway is an intentionally open center;
     * its narrow posts are visual detail rather than a fake circular wall collider. */
    return block!=NULL&&block->active&&block->shape==ODG_CONSTRUCTION_SHAPE_WALL?ODG_CONSTRUCTION_RADIUS_FX:0;
}

int32_t odg_construction_airspace_radius_fx_internal(const odg_construction_block *block){
    if(block==NULL||!block->active)return 0;
    return block->shape==ODG_CONSTRUCTION_SHAPE_WALL||block->shape==ODG_CONSTRUCTION_SHAPE_ROOF?
           ODG_CONSTRUCTION_RADIUS_FX:0;
}

void odg_construction_reset_runtime_internal(void) {
    uint32_t i;
    if(g_odg_construction_blocks!=NULL&&g_odg_construction_capacity!=0u)
        odg_memset(g_odg_construction_blocks,0,(size_t)g_odg_construction_capacity*sizeof(*g_odg_construction_blocks));
    g_odg_construction_count=0u;
    for(i=0u;i<ODG_MAX_ACTORS;++i)g_construction_selected_shape[i]=ODG_CONSTRUCTION_SHAPE_WALL;
}

void odg_construction_release_runtime_internal(void) {
    uint32_t i;
    odg_mem_free(g_odg_construction_blocks);
    g_odg_construction_blocks=NULL;
    g_odg_construction_capacity=0u;
    g_odg_construction_count=0u;
    for(i=0u;i<ODG_MAX_ACTORS;++i)g_construction_selected_shape[i]=ODG_CONSTRUCTION_SHAPE_WALL;
}

void odg_construction_sync_globals_from_local_internal(void) {
    uint32_t i;
    for(i=0u;i<g_odg_construction_count;++i){
        odg_construction_block *b=&g_odg_construction_blocks[i];
        if(!b->active||b->local_resident==0u)continue;
        odg_local_fx_to_global_fx_internal(b->x,b->z,&b->global_fx_x,&b->global_fx_z);
    }
}

void odg_construction_refresh_local_cache_internal(void) {
    uint32_t i;
    for(i=0u;i<g_odg_construction_count;++i){
        odg_construction_block *b=&g_odg_construction_blocks[i];
        if(!b->active)continue;
        b->local_resident=odg_global_fx_to_local_internal(b->global_fx_x,b->global_fx_z,&b->x,&b->z)?1u:0u;
        if(b->local_resident==0u){b->x=0;b->z=0;}
    }
}

static uint32_t alloc_construction_slot(void) {
    uint32_t i=g_odg_construction_count;
    if(!odg_entities_reserve_construction(i+1u))return UINT32_MAX;
    ++g_odg_construction_count;
    odg_memset(&g_odg_construction_blocks[i],0,sizeof(g_odg_construction_blocks[i]));
    g_odg_construction_blocks[i].id=i;
    return i;
}

static void remove_construction_slot(uint32_t index) {
    uint32_t last;
    if(index>=g_odg_construction_count)return;
    last=g_odg_construction_count-1u;
    if(index!=last){
        g_odg_construction_blocks[index]=g_odg_construction_blocks[last];
        g_odg_construction_blocks[index].id=index;
    }
    odg_memset(&g_odg_construction_blocks[last],0,sizeof(g_odg_construction_blocks[last]));
    --g_odg_construction_count;
}

typedef struct {
    int64_t gx,gz;
    uint32_t ignore_id;
    uint32_t layer;
    uint32_t shape;
    uint32_t query_kind; /* 0=layer, 1=exact shape, 2=roof support */
} odg_construction_cell_query;

static int construction_cell_visit(uint32_t id,void *context) {
    odg_construction_cell_query *q=(odg_construction_cell_query *)context;
    const odg_construction_block *b;int64_t bx,bz;
    if(q==NULL||id>=g_odg_construction_count||id==q->ignore_id)return 0;
    b=&g_odg_construction_blocks[id];if(!b->active)return 0;
    odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&bx,&bz);
    if(bx!=q->gx||bz!=q->gz)return 0;
    if(q->query_kind==1u)return b->shape==q->shape;
    if(q->query_kind==2u)return construction_shape_supports_roof(b->shape);
    return construction_shape_layer(b->shape)==q->layer;
}

static int construction_cell_query(int64_t gx,int64_t gz,uint32_t ignore_id,uint32_t query_kind,
                                   uint32_t layer,uint32_t shape) {
    odg_construction_cell_query q;
    q.gx=gx;q.gz=gz;q.ignore_id=ignore_id;q.query_kind=query_kind;q.layer=layer;q.shape=shape;
    return odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_CONSTRUCTION,
        gx*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2,
        gz*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2,ODG_FX_ONE,construction_cell_visit,&q);
}

static int construction_cell_layer_occupied(int64_t gx,int64_t gz,uint32_t layer,uint32_t ignore_id) {
    return construction_cell_query(gx,gz,ignore_id,0u,layer,0u);
}
static int construction_cell_has_shape(int64_t gx,int64_t gz,uint32_t shape,uint32_t ignore_id) {
    return construction_cell_query(gx,gz,ignore_id,1u,0u,shape);
}
static int construction_cell_has_roof_support(int64_t gx,int64_t gz,uint32_t ignore_id) {
    return construction_cell_query(gx,gz,ignore_id,2u,0u,0u);
}

typedef struct {
    int64_t global_x,global_z;
    int32_t radius;
    uint32_t ignore_id;
} odg_construction_collision_query;

static int construction_collision_visit(uint32_t id,void *context) {
    odg_construction_collision_query *q=(odg_construction_collision_query *)context;
    const odg_construction_block *b;int32_t block_radius,sum;int64_t dx,dz;
    if(q==NULL||id>=g_odg_construction_count||id==q->ignore_id)return 0;
    b=&g_odg_construction_blocks[id];if(!b->active)return 0;
    block_radius=odg_construction_collision_radius_fx_internal(b);if(block_radius<=0)return 0;
    sum=q->radius+block_radius;dx=b->global_fx_x-q->global_x;dz=b->global_fx_z-q->global_z;
    return dx*dx+dz*dz<(int64_t)sum*sum;
}

int odg_construction_position_blocked_internal(int32_t x,int32_t z,int32_t radius,uint32_t ignore_id) {
    odg_construction_collision_query q;
    odg_local_fx_to_global_fx_internal(x,z,&q.global_x,&q.global_z);
    q.radius=radius;q.ignore_id=ignore_id;
    return odg_entities_spatial_visit_near_global(ODG_SPATIAL_KIND_CONSTRUCTION,q.global_x,q.global_z,
                                                   radius+ODG_CONSTRUCTION_RADIUS_FX,
                                                   construction_collision_visit,&q);
}

static int construction_cell_candidate(const odg_actor *actor,int64_t gx,int64_t gz,uint32_t shape,
                                       int32_t *out_x,int32_t *out_z) {
    int32_t x,z;odg_surface_sample surface;int64_t dx,dz,max_d2;uint32_t layer;
    if(actor==NULL||out_x==NULL||out_z==NULL||!odg_construction_shape_valid_internal(shape))return 0;
    layer=construction_shape_layer(shape);if(layer==UINT32_MAX)return 0;
    if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_FROM_ID(actor->id))return 0;
    if(construction_cell_layer_occupied(gx,gz,layer,UINT32_MAX))return 0;
    if(shape==ODG_CONSTRUCTION_SHAPE_ROOF&&!construction_cell_has_roof_support(gx,gz,UINT32_MAX))return 0;
    if(!odg_global_cell_center_to_local_fx_internal(gx,gz,&x,&z))return 0;
    dx=(int64_t)x-actor->x;dz=(int64_t)z-actor->z;
    max_d2=(int64_t)(ODG_CONSTRUCTION_PLACE_DISTANCE_FX+ODG_FX_ONE)*(ODG_CONSTRUCTION_PLACE_DISTANCE_FX+ODG_FX_ONE);
    if(dx*dx+dz*dz>max_d2)return 0;
    if(!odg_environment_surface_local(x,z,&surface)||(surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)return 0;
    if(odg_chunk_procedural_turret_reserves_local_circle_internal(x,z,ODG_CONSTRUCTION_RADIUS_FX))return 0;
    /* A roof deliberately shares the support cell with its wall/doorway. Other layers must
     * still pass the same world occupancy authority as actors and artifacts. */
    if(shape!=ODG_CONSTRUCTION_SHAPE_ROOF&&!odg_position_clear_internal(x,z,ODG_CONSTRUCTION_RADIUS_FX))return 0;
    if(shape==ODG_CONSTRUCTION_SHAPE_WALL&&
       !odg_dynamic_position_clear_internal(x,z,ODG_CONSTRUCTION_RADIUS_FX,actor->id,UINT32_MAX))return 0;
    *out_x=x;*out_z=z;return 1;
}

int odg_construction_placement_candidate_internal(const odg_actor *actor,int32_t *out_x,int32_t *out_z,int64_t *out_gx,int64_t *out_gz) {
    int32_t raw_x,raw_z,x,z;int64_t gx,gz;uint32_t shape;
    if(actor==NULL||out_x==NULL||out_z==NULL)return 0;
    shape=odg_construction_selected_shape_internal(actor->id);
    raw_x=actor->x+(int32_t)(((int64_t)actor->face_x_q15*ODG_CONSTRUCTION_PLACE_DISTANCE_FX)/ODG_Q15_ONE);
    raw_z=actor->z+(int32_t)(((int64_t)actor->face_z_q15*ODG_CONSTRUCTION_PLACE_DISTANCE_FX)/ODG_Q15_ONE);
    odg_local_fx_to_global_cell_internal(raw_x,raw_z,&gx,&gz);
    if(!construction_cell_candidate(actor,gx,gz,shape,&x,&z))return 0;
    *out_x=x;*out_z=z;if(out_gx!=NULL)*out_gx=gx;if(out_gz!=NULL)*out_gz=gz;return 1;
}

int odg_construction_place_selected_at_global_cell_internal(uint32_t actor_id,int64_t gx,int64_t gz) {
    odg_actor *actor;odg_item_stack *stack;odg_inventory staged;odg_construction_block *block;uint64_t instance_id;uint32_t index,material_tier,shape,selected_slot;int32_t x,z;
    const odg_item_definition *item;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];stack=odg_inventory_selected(&actor->inventory);shape=odg_construction_selected_shape_internal(actor_id);
    if(stack==NULL||stack->quantity==0u)return 0;
    item=odg_item_definition_internal(stack->type_id);material_tier=stack->material_tier;selected_slot=actor->inventory.selected_slot;
    if(item==NULL||(item->capability_bits&ODG_ITEM_CAP_CONSTRUCT)==0u||construction_material_profile(material_tier)==NULL)return 0;
    if(!construction_cell_candidate(actor,gx,gz,shape,&x,&z))return 0;
    if(!odg_entities_reserve_construction(g_odg_construction_count+1u))return 0;
    staged=actor->inventory;
    if(!odg_inventory_remove_from_slot(&staged,selected_slot,1u,NULL))return 0;
    instance_id=odg_next_instance_id();if(instance_id==0u)return 0;
    index=g_odg_construction_count++;block=&g_odg_construction_blocks[index];odg_memset(block,0,sizeof(*block));
    block->active=1u;block->id=index;block->instance_id=instance_id;
    block->owner_actor_id=actor_id;block->material_tier=material_tier;block->shape=shape;
    block->max_health=odg_construction_max_health_internal(material_tier,shape);block->health=block->max_health;
    block->x=x;block->z=z;block->local_resident=1u;
    odg_local_fx_to_global_fx_internal(x,z,&block->global_fx_x,&block->global_fx_z);
    actor->inventory=staged;odg_entities_spatial_mark_dirty();odg_emit_particles(x,z,0x8ce8ffffu,8u);return 1;
}

int odg_construction_place_selected_internal(uint32_t actor_id) {
    odg_actor *actor;int32_t x,z;int64_t gx,gz;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];
    if(!odg_construction_placement_candidate_internal(actor,&x,&z,&gx,&gz))return 0;
    return odg_construction_place_selected_at_global_cell_internal(actor_id,gx,gz);
}

static int actor_can_damage_construction(uint32_t actor_id,const odg_construction_block *block);
static int actor_controls_construction(uint32_t actor_id,const odg_construction_block *block);

static uint32_t nearest_construction(const odg_actor *actor) {
    uint32_t i,best=UINT32_MAX,best_priority=0u;int64_t best_d2=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX;
    if(actor==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg_construction_count;++i){
        const odg_construction_block *b=&g_odg_construction_blocks[i];int64_t d2;uint32_t priority;
        if(!b->active||!b->local_resident)continue;
        d2=odg_dist2(actor->x,actor->z,b->x,b->z);priority=construction_shape_priority(b->shape);
        if(d2<best_d2||(d2==best_d2&&priority>best_priority)){best_d2=d2;best=i;best_priority=priority;}
    }
    return best;
}

static uint32_t nearest_damageable_construction(const odg_actor *actor){
    uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)ODG_MELEE_RANGE_FX*ODG_MELEE_RANGE_FX;
    if(actor==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg_construction_count;++i){
        const odg_construction_block *b=&g_odg_construction_blocks[i];int64_t d2;
        if(!b->active||!b->local_resident||!actor_can_damage_construction(actor->id,b))continue;
        d2=odg_dist2(actor->x,actor->z,b->x,b->z);if(d2<best_d2){best_d2=d2;best=i;}
    }
    return best;
}

static uint32_t nearest_repairable_construction(const odg_actor *actor){
    uint32_t i,best=UINT32_MAX,best_priority=0u;
    int64_t best_d2=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX;
    if(actor==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg_construction_count;++i){
        const odg_construction_block *b=&g_odg_construction_blocks[i];int64_t d2;uint32_t priority;
        if(!b->active||!b->local_resident||b->health==0u||b->health>=b->max_health||
           !actor_controls_construction(actor->id,b))continue;
        d2=odg_dist2(actor->x,actor->z,b->x,b->z);priority=construction_shape_priority(b->shape);
        if(d2<best_d2||(d2==best_d2&&priority>best_priority)){
            best_d2=d2;best=i;best_priority=priority;
        }
    }
    return best;
}

static int construction_has_dependent_roof(const odg_construction_block *block) {
    int64_t gx,gz;
    if(block==NULL||!construction_shape_supports_roof(block->shape))return 0;
    odg_global_fx_to_global_cell_internal(block->global_fx_x,block->global_fx_z,&gx,&gz);
    return construction_cell_has_shape(gx,gz,ODG_CONSTRUCTION_SHAPE_ROOF,block->id);
}

static int actor_can_dismantle(uint32_t actor_id,const odg_construction_block *block) {
    int64_t gx,gz;uint8_t land_owner;
    if(actor_id>=ODG_MAX_ACTORS||block==NULL||!block->active||construction_has_dependent_roof(block))return 0;
    odg_global_fx_to_global_cell_internal(block->global_fx_x,block->global_fx_z,&gx,&gz);
    land_owner=odg_chunk_owner_at_global_cell(gx,gz);
    /* Territorial control is the protection authority. The historical builder may reclaim
     * while still controlling the cell; neutral ground is salvageable; conquered ground
     * belongs operationally to the new controller. */
    if(land_owner==ODG_OWNER_NONE)return 1;
    return land_owner==ODG_OWNER_FROM_ID(actor_id);
}

static int actor_controls_construction(uint32_t actor_id,const odg_construction_block *block){
    int64_t gx,gz;uint8_t land_owner;
    if(actor_id>=ODG_MAX_ACTORS||block==NULL||!block->active)return 0;
    odg_global_fx_to_global_cell_internal(block->global_fx_x,block->global_fx_z,&gx,&gz);
    land_owner=odg_chunk_owner_at_global_cell(gx,gz);
    return land_owner==ODG_OWNER_FROM_ID(actor_id);
}

static int actor_can_damage_construction(uint32_t actor_id,const odg_construction_block *block){
    int64_t gx,gz;uint8_t land_owner;
    if(actor_id>=ODG_MAX_ACTORS||block==NULL||!block->active||block->health==0u)return 0;
    odg_global_fx_to_global_cell_internal(block->global_fx_x,block->global_fx_z,&gx,&gz);
    land_owner=odg_chunk_owner_at_global_cell(gx,gz);
    if(land_owner==ODG_OWNER_FROM_ID(actor_id))return 0;
    /* On neutral land the historical builder can salvage its module instead of attacking it.
     * Once another nation controls the cell, territorial authority supersedes builder history. */
    if(land_owner==ODG_OWNER_NONE&&block->owner_actor_id==actor_id)return 0;
    return 1;
}

static uint32_t construction_find_instance(uint64_t instance_id){
    uint32_t i;for(i=0u;i<g_odg_construction_count;++i)
        if(g_odg_construction_blocks[i].active&&g_odg_construction_blocks[i].instance_id==instance_id)return i;
    return UINT32_MAX;
}

static uint32_t construction_find_roof_at_cell(int64_t gx,int64_t gz){
    uint32_t i;for(i=0u;i<g_odg_construction_count;++i){
        const odg_construction_block *b=&g_odg_construction_blocks[i];int64_t bx,bz;
        if(!b->active||b->shape!=ODG_CONSTRUCTION_SHAPE_ROOF)continue;
        odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&bx,&bz);
        if(bx==gx&&bz==gz)return i;
    }
    return UINT32_MAX;
}

static void construction_destroy_index(uint32_t construction_id){
    uint64_t instance;int64_t gx=0,gz=0;int support;uint32_t roof;int32_t px,pz;
    if(construction_id>=g_odg_construction_count)return;
    instance=g_odg_construction_blocks[construction_id].instance_id;
    support=construction_shape_supports_roof(g_odg_construction_blocks[construction_id].shape);
    odg_global_fx_to_global_cell_internal(g_odg_construction_blocks[construction_id].global_fx_x,
                                           g_odg_construction_blocks[construction_id].global_fx_z,&gx,&gz);
    if(support){
        roof=construction_find_roof_at_cell(gx,gz);
        if(roof<g_odg_construction_count){
            px=g_odg_construction_blocks[roof].x;pz=g_odg_construction_blocks[roof].z;
            remove_construction_slot(roof);odg_emit_particles(px,pz,0xd8c6a0ffu,14u);
        }
        construction_id=construction_find_instance(instance);
        if(construction_id>=g_odg_construction_count){odg_entities_spatial_mark_dirty();return;}
    }
    px=g_odg_construction_blocks[construction_id].x;pz=g_odg_construction_blocks[construction_id].z;
    remove_construction_slot(construction_id);odg_entities_spatial_mark_dirty();odg_emit_particles(px,pz,0xe18b70ffu,16u);
}

int odg_construction_apply_damage_internal(uint32_t attacker_id,uint32_t construction_id,uint32_t damage){
    odg_actor *attacker;odg_construction_block *block;
    if(attacker_id>=ODG_MAX_ACTORS||construction_id>=g_odg_construction_count||damage==0u)return 0;
    attacker=&g_odg.actors[attacker_id];block=&g_odg_construction_blocks[construction_id];
    if(!attacker->active||attacker->hp==0u||!block->local_resident||!actor_can_damage_construction(attacker_id,block)||
       odg_dist2(attacker->x,attacker->z,block->x,block->z)>(int64_t)ODG_MELEE_RANGE_FX*ODG_MELEE_RANGE_FX)return 0;
    if(damage>=block->health){construction_destroy_index(construction_id);return 1;}
    block->health-=damage;odg_emit_particles(block->x,block->z,0xc99773ffu,5u);return 1;
}

int odg_construction_repair_internal(uint32_t actor_id,uint32_t construction_id){
    odg_actor *actor;odg_construction_block *block;uint32_t slot,amount;
    if(actor_id>=ODG_MAX_ACTORS||construction_id>=g_odg_construction_count)return 0;
    actor=&g_odg.actors[actor_id];block=&g_odg_construction_blocks[construction_id];
    if(!actor->active||actor->hp==0u||!block->local_resident||block->health==0u||block->health>=block->max_health||
       !actor_controls_construction(actor_id,block)||
       odg_dist2(actor->x,actor->z,block->x,block->z)>(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX)return 0;
    if(!odg_inventory_find_type(&actor->inventory,ODG_ITEM_BUILDING_BLOCK,block->material_tier,&slot))return 0;
    if(!odg_inventory_remove_from_slot(&actor->inventory,slot,1u,NULL))return 0;
    amount=(block->max_health+3u)/4u;
    if(amount>block->max_health-block->health)block->health=block->max_health;else block->health+=amount;
    odg_emit_particles(block->x,block->z,0x86d6b0ffu,8u);return 1;
}

int odg_construction_actor_may_dismantle_internal(uint32_t actor_id,uint32_t construction_id) {
    const odg_construction_block *block;
    if(actor_id>=ODG_MAX_ACTORS||construction_id>=g_odg_construction_count)return 0;
    block=&g_odg_construction_blocks[construction_id];
    return block->local_resident&&actor_can_dismantle(actor_id,block);
}

int odg_construction_build_hint_internal(const odg_actor *actor,const odg_item_stack *selected,odg_interaction_hint *hint) {
    uint32_t id;const odg_construction_material_profile *profile;
    if(actor==NULL||hint==NULL)return 0;
    if(selected!=NULL&&selected->quantity!=0u){
        const odg_item_definition *item=odg_item_definition_internal(selected->type_id);
        if(item!=NULL&&(item->capability_bits&ODG_ITEM_CAP_ATTACK)!=0u){
            id=nearest_damageable_construction(actor);
            if(id<g_odg_construction_count){
                hint->action=ODG_INTERACTION_ATTACK_CONSTRUCTION;hint->target_kind=ODG_INTERACTION_TARGET_CONSTRUCTION;
                hint->target_id=id;hint->valid=actor->melee_cooldown_ticks==0u?1u:0u;hint->requires_hold=0u;
                hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;return 1;
            }
        }
    }
    /* Repair is checked before placement. A matching building block is the repair
     * transaction itself; allowing its CONSTRUCT capability to win first made repair
     * unreachable exactly when the player selected the required material. */
    id=nearest_repairable_construction(actor);
    if(id<g_odg_construction_count){
        uint32_t slot=UINT32_MAX;int has_material=odg_inventory_find_type(&actor->inventory,ODG_ITEM_BUILDING_BLOCK,
                                                   g_odg_construction_blocks[id].material_tier,&slot);
        hint->action=ODG_INTERACTION_REPAIR_CONSTRUCTION;hint->target_kind=ODG_INTERACTION_TARGET_CONSTRUCTION;hint->target_id=id;
        hint->valid=has_material?1u:0u;hint->requires_hold=1u;hint->threshold_ticks=ODG_INTERACT_HOLD_TICKS/2u;
        hint->message_code=has_material?ODG_MESSAGE_NONE:ODG_MESSAGE_MISSING_RESOURCES;return 1;
    }
    if(selected!=NULL&&selected->quantity!=0u){
        const odg_item_definition *item=odg_item_definition_internal(selected->type_id);
        if(item!=NULL&&(item->capability_bits&ODG_ITEM_CAP_CONSTRUCT)!=0u){
            int32_t x,z;
            hint->action=ODG_INTERACTION_PLACE_CONSTRUCTION;hint->target_kind=ODG_INTERACTION_TARGET_CONSTRUCTION;
            hint->target_id=UINT32_MAX;hint->valid=odg_construction_placement_candidate_internal(actor,&x,&z,NULL,NULL)?1u:0u;
            hint->requires_hold=0u;hint->threshold_ticks=ODG_INTERACT_TAP_MAX_TICKS;
            hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_INVALID_PLACEMENT;
            return 1;
        }
    }
    id=nearest_construction(actor);if(id>=g_odg_construction_count)return 0;
    profile=construction_material_profile(g_odg_construction_blocks[id].material_tier);
    hint->action=ODG_INTERACTION_DISMANTLE_CONSTRUCTION;hint->target_kind=ODG_INTERACTION_TARGET_CONSTRUCTION;hint->target_id=id;
    hint->valid=actor_can_dismantle(actor->id,&g_odg_construction_blocks[id])?1u:0u;
    hint->requires_hold=1u;hint->threshold_ticks=profile!=NULL?profile->dismantle_ticks:ODG_INTERACT_HOLD_TICKS;
    hint->message_code=hint->valid?ODG_MESSAGE_NONE:ODG_MESSAGE_TERRITORY_REQUIRED;
    return 1;
}

int odg_construction_execute_tap_internal(uint32_t actor_id,const odg_interaction_hint *hint) {
    if(hint==NULL||!hint->valid||hint->action!=ODG_INTERACTION_PLACE_CONSTRUCTION)return 0;
    return odg_construction_place_selected_internal(actor_id);
}

int odg_construction_dismantle_internal(uint32_t actor_id,uint32_t construction_id) {
    odg_actor *actor;odg_construction_block *block;odg_item_stack recovered;int damaged;
    if(actor_id>=ODG_MAX_ACTORS||construction_id>=g_odg_construction_count)return 0;
    actor=&g_odg.actors[actor_id];block=&g_odg_construction_blocks[construction_id];
    if(!odg_construction_actor_may_dismantle_internal(actor_id,construction_id)||
       odg_dist2(actor->x,actor->z,block->x,block->z)>(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX)return 0;
    damaged=block->health<block->max_health;
    odg_memset(&recovered,0,sizeof(recovered));
    if(!damaged){
        recovered.type_id=ODG_ITEM_BUILDING_BLOCK;recovered.quantity=1u;recovered.material_tier=block->material_tier;
        if(!odg_item_stack_normalize_internal(&recovered))return 0;
        /* Intact modules remain reusable stackable commodities. */
        if(!odg_inventory_add(&actor->inventory,&recovered)&&
           !odg_spawn_world_pickup(&recovered,block->x,block->z,ODG_MANUAL_DROP_REPICKUP_TICKS))return 0;
    }else{
        odg_recipe_definition recipe;uint64_t required=0u;uint32_t recipe_id,quantity;
        /* A damaged stackable module cannot truthfully become a pristine stack item: that
         * would turn dismantle -> place into free healing and make repair irrational. Recover
         * deterministic raw salvage from the authoritative build recipe instead. Construction
         * profiles guarantee one input ingredient for these compact modules. */
        recipe_id=odg_recipe_find_output_internal(ODG_ITEM_BUILDING_BLOCK,block->material_tier);
        if(recipe_id==0u||odg_recipe_get(recipe_id,&recipe,sizeof(recipe),&required)!=ODG_STATUS_OK||
           recipe.output_quantity==0u||recipe.ingredient_count!=1u)return 0;
        quantity=(uint32_t)(((uint64_t)recipe.ingredients[0].quantity*(uint64_t)block->health)/
                            ((uint64_t)recipe.output_quantity*(uint64_t)block->max_health));
        if(quantity!=0u){
            recovered.type_id=recipe.ingredients[0].item_type;recovered.quantity=quantity;
            recovered.material_tier=recipe.ingredients[0].material_tier;
            if(!odg_item_stack_normalize_internal(&recovered)||
               !odg_spawn_world_pickup(&recovered,block->x,block->z,ODG_MANUAL_DROP_REPICKUP_TICKS))return 0;
        }
    }
    {int32_t px=block->x,pz=block->z;
    remove_construction_slot(construction_id);odg_entities_spatial_mark_dirty();
    odg_emit_particles(px,pz,damaged?0xc99773ffu:0x8ce8ffffu,10u);}
    return 1;
}

int odg_construction_execute_hold_internal(uint32_t actor_id,const odg_interaction_hint *hint) {
    if(hint==NULL||!hint->valid)return 0;
    if(hint->action==ODG_INTERACTION_DISMANTLE_CONSTRUCTION)return odg_construction_dismantle_internal(actor_id,hint->target_id);
    if(hint->action==ODG_INTERACTION_REPAIR_CONSTRUCTION)return odg_construction_repair_internal(actor_id,hint->target_id);
    return 0;
}

int odg_construction_import_legacy_artifact_internal(const odg_artifact *artifact) {
    odg_construction_block *block;uint64_t instance_id;uint32_t index;
    if(artifact==NULL||!artifact->active||artifact->item_type!=ODG_ITEM_BUILDING_BLOCK||
       construction_material_profile(artifact->material_tier)==NULL)return 0;
    instance_id=artifact->instance_id!=0u?artifact->instance_id:odg_next_instance_id();if(instance_id==0u)return 0;
    index=alloc_construction_slot();if(index==UINT32_MAX)return 0;
    block=&g_odg_construction_blocks[index];odg_memset(block,0,sizeof(*block));
    block->active=1u;block->id=index;block->instance_id=instance_id;
    block->owner_actor_id=artifact->owner_actor_id;block->material_tier=artifact->material_tier;block->shape=ODG_CONSTRUCTION_SHAPE_WALL;
    block->max_health=odg_construction_max_health_internal(block->material_tier,block->shape);block->health=block->max_health;
    block->global_fx_x=artifact->global_fx_x;block->global_fx_z=artifact->global_fx_z;block->x=artifact->x;block->z=artifact->z;
    block->local_resident=artifact->local_resident;
    return 1;
}

int odg_construction_loaded_state_validate_internal(uint32_t source_schema) {
    uint32_t i,j;
    for(i=0u;i<g_odg_construction_count;++i){
        const odg_construction_block *a=&g_odg_construction_blocks[i];int64_t agx,agz;
        uint32_t expected_health=odg_construction_max_health_internal(a->material_tier,a->shape);
        if(!a->active||a->id!=i||construction_material_profile(a->material_tier)==NULL||
           !odg_construction_shape_valid_internal(a->shape)||expected_health==0u||
           a->max_health!=expected_health||a->health==0u||a->health>a->max_health||a->reserved_u32!=0u)return 0;
        /* SAVE18 only ever emitted WALL. Accepting a future shape in an old header would
         * turn a corrupt/backported blob into an invented semantic migration. */
        if(source_schema<=18u&&a->shape!=ODG_CONSTRUCTION_SHAPE_WALL)return 0;
        odg_global_fx_to_global_cell_internal(a->global_fx_x,a->global_fx_z,&agx,&agz);
        for(j=0u;j<i;++j){
            const odg_construction_block *b=&g_odg_construction_blocks[j];int64_t bgx,bgz;
            if(!b->active)continue;
            odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&bgx,&bgz);
            if(agx==bgx&&agz==bgz&&construction_shape_layer(a->shape)==construction_shape_layer(b->shape))return 0;
        }
        if(a->shape==ODG_CONSTRUCTION_SHAPE_ROOF){
            int supported=0;
            for(j=0u;j<g_odg_construction_count;++j){
                const odg_construction_block *b=&g_odg_construction_blocks[j];int64_t bgx,bgz;
                if(j==i||!b->active||!construction_shape_supports_roof(b->shape))continue;
                odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&bgx,&bgz);
                if(agx==bgx&&agz==bgz){supported=1;break;}
            }
            if(!supported)return 0;
        }
    }
    return 1;
}

uint32_t odg_construction_count(void) {
    return g_odg_construction_count;
}

int32_t odg_copy_construction_page(uint32_t offset,odg_construction_snapshot *out_construction,
                                   uint64_t capacity,uint64_t *out_required) {
    uint32_t i,count=0u,total;
    if(out_required!=NULL)*out_required=(uint64_t)sizeof(odg_construction_snapshot);
    if(!g_odg.initialized)return ODG_STATUS_INVALID_STATE;
    if(out_construction==NULL||capacity<(uint64_t)sizeof(*out_construction))return ODG_STATUS_BUFFER_TOO_SMALL;
    odg_memset(out_construction,0,sizeof(*out_construction));out_construction->struct_size=(uint32_t)sizeof(*out_construction);
    total=odg_construction_count();out_construction->total_count=total;
    out_construction->selected_shape=odg_construction_selected_shape_internal(ODG_PLAYER_ID);
    if(offset>=total)return ODG_STATUS_OK;
    for(i=0u;i<g_odg_construction_count&&count<ODG_CONSTRUCTION_MAX_ENTRIES;++i){
        const odg_construction_block *b=&g_odg_construction_blocks[i];odg_construction_entry *e;int64_t xm,zm,gx,gz;uint8_t owner;
        if(i<offset)continue;
        e=&out_construction->entries[count++];e->instance_id=b->instance_id;e->construction_id=i;e->owner_actor_id=b->owner_actor_id;
        e->material_tier=b->material_tier;e->shape=b->shape;e->state=b->state;e->health=b->health;e->max_health=b->max_health;
        xm=(b->global_fx_x*INT64_C(1000))/(int64_t)ODG_FX_ONE;zm=(b->global_fx_z*INT64_C(1000))/(int64_t)ODG_FX_ONE;
        e->x_milli=xm<INT32_MIN?INT32_MIN:(xm>INT32_MAX?INT32_MAX:(int32_t)xm);
        e->z_milli=zm<INT32_MIN?INT32_MIN:(zm>INT32_MAX?INT32_MAX:(int32_t)zm);
        odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&gx,&gz);owner=odg_chunk_owner_at_global_cell(gx,gz);
        e->controller_actor_id=owner==ODG_OWNER_NONE?UINT32_MAX:ODG_ID_FROM_OWNER(owner);
    }
    out_construction->count=count;return ODG_STATUS_OK;
}
