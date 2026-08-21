#include "game_internal.h"

#include <stdint.h>

typedef struct {
    uint32_t species_id;
    uint32_t growth_form;
    uint32_t biome_mask;
    int32_t min_altitude_milli;
    int32_t max_altitude_milli;
    uint32_t min_moisture_permille;
    uint32_t max_moisture_permille;
    uint32_t weight;
} odg_flora_spawn_rule;

static const odg_flora_species_definition g_flora[] = {{
    .struct_size=sizeof(odg_flora_species_definition),
    .species_id=ODG_FLORA_SPECIES_APPLE_TREE,.display_code=2001u,
    .fruit_item_type=ODG_ITEM_APPLE,.seed_item_type=ODG_ITEM_APPLE_SEED,
    .growth_form=ODG_FLORA_GROWTH_TREE,.harvest_item_type=ODG_ITEM_WOOD,.harvest_tool_item_type=ODG_ITEM_AXE,
    .harvest_flags=ODG_FLORA_HARVEST_DESTROYS_PLANT|ODG_FLORA_HARVEST_TURRET_ELIGIBLE|ODG_FLORA_HARVEST_BLOCKING|ODG_FLORA_HARVEST_ALLOW_HAND,
    .harvest_base_ticks=480u,.seedling_ticks=2u*60u*ODG_TICK_RATE,.sapling_ticks=4u*60u*ODG_TICK_RATE,
    .young_ticks=8u*60u*ODG_TICK_RATE,.old_ticks=45u*60u*ODG_TICK_RATE,.fruit_cycle_ticks=4u*60u*ODG_TICK_RATE,
    .fruit_capacity_min=3u,.fruit_capacity_max=7u,.max_dynamic_per_chunk=20u,.min_spacing_milli=2800u,
    .planting_clearance_milli=1900u,.min_growth_moisture_permille=260u,.preferred_growth_moisture_permille=600u,
    .seed_germination_permille=180u,.fruit_seed_recovery_permille=350u,.fallen_fruit_seed_permille=250u,
    .natural_drop_max_per_cycle=1u,.collision_radius_milli=500u,.variant_count=4u,
    .stage_yield_permille={150u,300u,600u,1000u,1150u},
    .stage_harvest_time_permille={300u,480u,720u,1000u,1200u},
    .stage_collision_permille={250u,400u,650u,1000u,1200u},.reserved_u32={0u,0u}
}};

static const odg_flora_spawn_rule g_flora_spawn_rules[] = {
    {ODG_FLORA_SPECIES_APPLE_TREE,ODG_FLORA_GROWTH_TREE,
     ODG_BIOME_MASK(ODG_BIOME_PLAIN)|ODG_BIOME_MASK(ODG_BIOME_FOREST)|ODG_BIOME_MASK(ODG_BIOME_WETLAND),
     -300,4200,180u,1000u,100u}
};

const odg_flora_species_definition *odg_flora_species_internal(uint32_t species_id){
    uint32_t i;for(i=0u;i<(uint32_t)(sizeof(g_flora)/sizeof(g_flora[0]));++i)if(g_flora[i].species_id==species_id)return &g_flora[i];return NULL;
}

int32_t odg_flora_collision_radius_fx_internal(const odg_flora_species_definition *definition,uint32_t stage){
    uint32_t index,milli;uint64_t fx;
    if(definition==NULL)return 0;
    index=stage>=ODG_FLORA_STAGE_SEEDLING&&stage<=ODG_FLORA_STAGE_OLD?stage-ODG_FLORA_STAGE_SEEDLING:
          ODG_FLORA_STAGE_MATURE-ODG_FLORA_STAGE_SEEDLING;
    milli=(uint32_t)(((uint64_t)definition->collision_radius_milli*definition->stage_collision_permille[index])/1000u);
    fx=((uint64_t)milli*(uint64_t)ODG_FX_ONE)/UINT64_C(1000);
    if(fx<(uint64_t)(ODG_FX_ONE/8))fx=(uint64_t)(ODG_FX_ONE/8);
    return fx>(uint64_t)INT32_MAX?INT32_MAX:(int32_t)fx;
}

const odg_flora_species_definition *odg_flora_species_for_seed_internal(uint32_t seed_item_type,uint64_t payload_id){
    uint32_t i;
    if(payload_id!=0u && payload_id<=UINT32_MAX){
        const odg_flora_species_definition *d=odg_flora_species_internal((uint32_t)payload_id);
        if(d!=NULL && d->seed_item_type==seed_item_type)return d;
    }
    for(i=0u;i<(uint32_t)(sizeof(g_flora)/sizeof(g_flora[0]));++i)if(g_flora[i].seed_item_type==seed_item_type)return &g_flora[i];
    return NULL;
}

const odg_flora_species_definition *odg_flora_species_for_fruit_internal(uint32_t fruit_item_type,uint64_t payload_id){
    uint32_t i;
    if(payload_id!=0u && payload_id<=UINT32_MAX){
        const odg_flora_species_definition *d=odg_flora_species_internal((uint32_t)payload_id);
        if(d!=NULL && d->fruit_item_type==fruit_item_type)return d;
    }
    for(i=0u;i<(uint32_t)(sizeof(g_flora)/sizeof(g_flora[0]));++i)if(g_flora[i].fruit_item_type==fruit_item_type)return &g_flora[i];
    return NULL;
}

const odg_flora_species_definition *odg_flora_worldgen_species_internal(uint32_t growth_form,int32_t x,int32_t z,uint64_t entropy){
    odg_surface_sample surface;
    uint32_t i,total=0u,pick;
    if(!odg_environment_surface_local(x,z,&surface))return NULL;
    for(i=0u;i<(uint32_t)(sizeof(g_flora_spawn_rules)/sizeof(g_flora_spawn_rules[0]));++i){
        const odg_flora_spawn_rule *r=&g_flora_spawn_rules[i];
        if(r->growth_form!=growth_form || (r->biome_mask&ODG_BIOME_MASK(surface.biome))==0u ||
           surface.height_milli<r->min_altitude_milli || surface.height_milli>r->max_altitude_milli ||
           surface.moisture_permille<r->min_moisture_permille || surface.moisture_permille>r->max_moisture_permille)continue;
        total+=r->weight;
    }
    if(total==0u)return NULL;
    pick=(uint32_t)(entropy%(uint64_t)total);
    for(i=0u;i<(uint32_t)(sizeof(g_flora_spawn_rules)/sizeof(g_flora_spawn_rules[0]));++i){
        const odg_flora_spawn_rule *r=&g_flora_spawn_rules[i];
        if(r->growth_form!=growth_form || (r->biome_mask&ODG_BIOME_MASK(surface.biome))==0u ||
           surface.height_milli<r->min_altitude_milli || surface.height_milli>r->max_altitude_milli ||
           surface.moisture_permille<r->min_moisture_permille || surface.moisture_permille>r->max_moisture_permille)continue;
        if(pick<r->weight)return odg_flora_species_internal(r->species_id);
        pick-=r->weight;
    }
    return NULL;
}
uint32_t odg_flora_species_count(void){return (uint32_t)(sizeof(g_flora)/sizeof(g_flora[0]));}
int32_t odg_flora_species_get(uint32_t index,odg_flora_species_definition *out_definition,uint64_t capacity,uint64_t *out_required){
    if(out_required!=NULL)*out_required=(uint64_t)sizeof(odg_flora_species_definition);
    if(index>=odg_flora_species_count())return ODG_STATUS_INVALID_ARGUMENT;
    if(out_definition==NULL||capacity<(uint64_t)sizeof(*out_definition))return ODG_STATUS_BUFFER_TOO_SMALL;
    *out_definition=g_flora[index];return ODG_STATUS_OK;
}

void odg_ecology_init_resource(odg_resource_node *r,uint32_t species_id,uint32_t stage,uint32_t variant){
    const odg_flora_species_definition *d=odg_flora_species_internal(species_id);
    odg_surface_sample s;
    if(r==NULL||d==NULL)return;
    r->species_id=species_id;r->flora_stage=stage<ODG_FLORA_STAGE_SEEDLING?ODG_FLORA_STAGE_SEEDLING:(stage>ODG_FLORA_STAGE_OLD?ODG_FLORA_STAGE_OLD:stage);
    r->variant=d->variant_count==0u?0u:(variant%d->variant_count);
    r->fruit_capacity=d->fruit_capacity_min;
    if(d->fruit_capacity_max>d->fruit_capacity_min)
        r->fruit_capacity+=((uint32_t)(r->stable_id>>17u)%(d->fruit_capacity_max-d->fruit_capacity_min+1u));
    r->fruit_count=(r->flora_stage>=ODG_FLORA_STAGE_MATURE)?r->fruit_capacity/2u:0u;
    r->fruit_cycle_ticks=0u;r->windfall_count=0u;r->age_ticks=0u;
    if(odg_environment_surface_local(r->x,r->z,&s))r->soil_moisture_permille=s.moisture_permille;
    if(r->flora_stage==ODG_FLORA_STAGE_SAPLING)r->age_ticks=d->seedling_ticks;
    else if(r->flora_stage==ODG_FLORA_STAGE_YOUNG)r->age_ticks=(uint64_t)d->seedling_ticks+d->sapling_ticks;
    else if(r->flora_stage==ODG_FLORA_STAGE_MATURE)r->age_ticks=(uint64_t)d->seedling_ticks+d->sapling_ticks+d->young_ticks;
    else if(r->flora_stage==ODG_FLORA_STAGE_OLD)r->age_ticks=(uint64_t)d->seedling_ticks+d->sapling_ticks+d->young_ticks+d->old_ticks;
}

static uint32_t stage_for_age(const odg_flora_species_definition *d,uint64_t age_ticks){
    uint64_t a1=d->seedling_ticks,a2=a1+d->sapling_ticks,a3=a2+d->young_ticks,a4=a3+d->old_ticks;
    if(age_ticks<a1)return ODG_FLORA_STAGE_SEEDLING;
    if(age_ticks<a2)return ODG_FLORA_STAGE_SAPLING;
    if(age_ticks<a3)return ODG_FLORA_STAGE_YOUNG;
    if(age_ticks<a4)return ODG_FLORA_STAGE_MATURE;
    return ODG_FLORA_STAGE_OLD;
}

static void refresh_stage(odg_resource_node *r,const odg_flora_species_definition *d){
    uint32_t desired,stage;int32_t expansion_radius=0;
    if(r==NULL||d==NULL)return;
    desired=stage_for_age(d,r->age_ticks);
    if(desired<=r->flora_stage){r->flora_stage=desired;return;}
    /* Age and physical growth are separate authorities. A plant may become old enough to
     * advance while its crown/root footprint is temporarily blocked. Never grow a collider
     * through a wall, artifact, another resource, a reserved natural turret, actor or ground
     * fauna; retain age and catch up deterministically as soon as the space becomes free. */
    for(stage=r->flora_stage+1u;stage<=desired;++stage){
        int32_t radius=odg_flora_collision_radius_fx_internal(d,stage);
        if(radius>expansion_radius)expansion_radius=radius;
    }
    if(expansion_radius<=0)return;
    if(odg_chunk_procedural_turret_reserves_local_circle_internal(r->x,r->z,expansion_radius))return;
    if(!odg_position_clear_ignoring_resource_internal(r->x,r->z,expansion_radius,r->id))return;
    if(!odg_dynamic_position_clear_internal(r->x,r->z,expansion_radius,UINT32_MAX,UINT32_MAX))return;
    r->flora_stage=desired;
}

static int drop_flora_item(const odg_resource_node *r,uint32_t item,uint32_t quantity,uint64_t payload){
    odg_item_stack s;const odg_item_definition *def;
    if(r==NULL||item==ODG_ITEM_NONE||quantity==0u)return 0;
    def=odg_item_definition_internal(item);if(def==NULL)return 0;
    odg_memset(&s,0,sizeof(s));s.type_id=item;s.quantity=quantity;s.payload_id=payload;s.flags=def->flags;s.material_tier=def->default_material_tier;
    return odg_spawn_world_pickup(&s,r->x,r->z,20u);
}

void odg_ecology_tick(void){
    uint32_t i;if((g_odg.tick%ODG_TICK_RATE)!=0u)return;
    for(i=0u;i<g_odg.resource_count;++i){
        odg_resource_node *r=&g_odg_resources[i];const odg_flora_species_definition *d;odg_surface_sample s;
        if(!r->active||r->state!=ODG_RESOURCE_STATE_AVAILABLE||r->species_id==0u)continue;
        d=odg_flora_species_internal(r->species_id);if(d==NULL)continue;
        if(odg_environment_surface_local(r->x,r->z,&s)){
            uint32_t target=s.moisture_permille;
            if(target>r->soil_moisture_permille)r->soil_moisture_permille+=odg_min_u32(12u,target-r->soil_moisture_permille);
            else if(r->soil_moisture_permille>target)r->soil_moisture_permille-=odg_min_u32(3u,r->soil_moisture_permille-target);
        }
        if(r->soil_moisture_permille>=d->min_growth_moisture_permille){r->age_ticks+=ODG_TICK_RATE;refresh_stage(r,d);}
        if(r->flora_stage>=ODG_FLORA_STAGE_MATURE){
            r->fruit_cycle_ticks+=ODG_TICK_RATE;
            if(r->fruit_cycle_ticks>=d->fruit_cycle_ticks){
                r->fruit_cycle_ticks=0u;r->windfall_count=0u;
                if(r->fruit_count<r->fruit_capacity)++r->fruit_count;
            }
            /* One bounded windfall per cycle in this species definition. */
            if(r->fruit_count>0u && r->windfall_count<d->natural_drop_max_per_cycle &&
               r->fruit_cycle_ticks>d->fruit_cycle_ticks/2u){
                if(drop_flora_item(r,d->fruit_item_type,1u,(uint64_t)r->species_id)){
                    --r->fruit_count;++r->windfall_count;
                }
            }
        }
    }
}

int odg_ecology_gather_fruit(uint32_t actor_id,uint32_t resource_id){
    odg_resource_node *resource;
    const odg_flora_species_definition *flora;
    const odg_item_definition *fruit;
    odg_item_stack stack;
    if(actor_id>=ODG_MAX_ACTORS||resource_id>=g_odg.resource_count)return 0;
    resource=&g_odg_resources[resource_id];
    if(!resource->active||resource->state!=ODG_RESOURCE_STATE_AVAILABLE||resource->fruit_count==0u||
       !odg_territory_allows_environment_action(actor_id,resource->x,resource->z))return 0;
    flora=odg_flora_species_internal(resource->species_id);
    if(flora==NULL)return 0;
    fruit=odg_item_definition_internal(flora->fruit_item_type);
    if(fruit==NULL)return 0;
    odg_memset(&stack,0,sizeof(stack));
    stack.type_id=fruit->type_id;
    stack.quantity=1u;
    stack.flags=fruit->flags;
    stack.material_tier=fruit->default_material_tier;
    stack.payload_id=(uint64_t)resource->species_id;
    if(!odg_inventory_add(&g_odg.actors[actor_id].inventory,&stack))return 0;
    --resource->fruit_count;
    return 1;
}

int odg_resource_spawn_flora(uint32_t species_id,uint32_t stage,uint32_t variant,int32_t x,int32_t z,uint32_t bootstrap_actor_id){
    odg_resource_node *r;const odg_flora_species_definition *d=odg_flora_species_internal(species_id);uint32_t id;uint64_t stable_id;
    if(d==NULL)return 0;
    if(!odg_entities_reserve_resources(g_odg.resource_count+1u))return 0;
    stable_id=odg_next_instance_id();if(stable_id==0u)return 0;
    id=g_odg.resource_count++;r=&g_odg_resources[id];odg_memset(r,0,sizeof(*r));
    r->active=1u;r->id=id;r->kind=ODG_RESOURCE_FLORA;r->state=ODG_RESOURCE_STATE_AVAILABLE;r->x=x;r->z=z;r->local_resident=1u;r->bootstrap_actor_id=bootstrap_actor_id;
    odg_local_fx_to_global_fx_internal(x,z,&r->global_fx_x,&r->global_fx_z);r->stable_id=stable_id;r->harvest_actor=UINT32_MAX;
    r->yield_min=4u;r->yield_max=7u;odg_ecology_init_resource(r,species_id,stage,variant);
    odg_entities_spatial_mark_dirty();return 1;
}

int odg_ecology_plant_selected(uint32_t actor_id){
    odg_actor *a;odg_item_stack *seed;const odg_flora_species_definition *d;odg_surface_sample s;int32_t x,z;uint32_t i;uint32_t species_id;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    a=&g_odg.actors[actor_id];seed=odg_inventory_selected(&a->inventory);
    if(seed==NULL||seed->quantity==0u)return 0;
    {const odg_item_definition *seed_def=odg_item_definition_internal(seed->type_id);if(seed_def==NULL||(seed_def->capability_bits&ODG_ITEM_CAP_PLANT)==0u)return 0;}
    d=odg_flora_species_for_seed_internal(seed->type_id,seed->payload_id);if(d==NULL)return 0;
    species_id=d->species_id;
    x=a->x+(int32_t)(((int64_t)a->face_x_q15*(2*ODG_FX_ONE))/ODG_Q15_ONE);z=a->z+(int32_t)(((int64_t)a->face_z_q15*(2*ODG_FX_ONE))/ODG_Q15_ONE);
    if(!odg_territory_allows_environment_action(actor_id,x,z))return 0;
    if(!odg_environment_surface_local(x,z,&s)||s.moisture_permille<d->min_growth_moisture_permille||
       (s.flags&(ODG_SURFACE_FLAG_STEEP|ODG_SURFACE_FLAG_WATER))!=0u)return 0;
    {
        int32_t radius_fx=odg_flora_collision_radius_fx_internal(d,ODG_FLORA_STAGE_SEEDLING);
        if(odg_chunk_procedural_turret_reserves_local_circle_internal(x,z,radius_fx))return 0;
        if(!odg_position_clear_internal(x,z,radius_fx))return 0;
        if(!odg_dynamic_position_clear_internal(x,z,radius_fx,actor_id,UINT32_MAX))return 0;
    }
    for(i=0u;i<g_odg.resource_count;++i){const odg_resource_node *r=&g_odg_resources[i];int64_t min=(int64_t)d->min_spacing_milli*ODG_FX_ONE/1000;if(r->active&&odg_resource_is_flora_internal(r)&&odg_dist2(x,z,r->x,r->z)<min*min)return 0;}
    {
        odg_inventory staged=a->inventory;
        /* Planting is one transaction across inventory and world state. Consume only in a
         * staged copy; if allocation/spawn fails, the live inventory is untouched. */
        if(!odg_inventory_remove_from_slot(&staged,a->inventory.selected_slot,1u,NULL))return 0;
        if(!odg_resource_spawn_flora(species_id,ODG_FLORA_STAGE_SEEDLING,0u,x,z,actor_id))return 0;
        a->inventory=staged;
    }
    return 1;
}

int odg_ecology_irrigate_nearest(uint32_t actor_id,uint32_t requested_units,uint32_t *out_used){
    odg_actor *actor;uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)(3*ODG_FX_ONE)*(3*ODG_FX_ONE);
    uint32_t used=0u;
    if(out_used!=NULL)*out_used=0u;
    if(actor_id>=ODG_MAX_ACTORS||requested_units==0u)return 0;
    actor=&g_odg.actors[actor_id];
    for(i=0u;i<g_odg.resource_count;++i){
        odg_resource_node *r=&g_odg_resources[i];int64_t d2;
        if(!r->active||r->local_resident==0u||r->state!=ODG_RESOURCE_STATE_AVAILABLE||
           !odg_resource_is_flora_internal(r))continue;
        if(!odg_territory_allows_environment_action(actor_id,r->x,r->z))continue;
        d2=odg_dist2(actor->x,actor->z,r->x,r->z);if(d2<best_d2){best_d2=d2;best=i;}
    }
    if(best==UINT32_MAX)return 0;
    {
        odg_resource_node *r=&g_odg_resources[best];
        uint32_t need=r->soil_moisture_permille<1000u?1000u-r->soil_moisture_permille:0u;
        if(need==0u)return 0;
        /* One water unit contributes 10 permille. Bounded arithmetic prevents irrigation
         * from becoming an infinite growth accelerator or overflowing persistent state. */
        used=(need+9u)/10u;if(used>requested_units)used=requested_units;
        r->soil_moisture_permille+=used*10u;if(r->soil_moisture_permille>1000u)r->soil_moisture_permille=1000u;
    }
    if(out_used!=NULL)*out_used=used;
    return used!=0u;
}
