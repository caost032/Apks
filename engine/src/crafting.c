#include "game_internal.h"

#include <stddef.h>
#include <stdint.h>

#define ING(type,tier,qty) {type,tier,qty,0u}
#define NIL_ING {0u,0u,0u,0u}

static const odg_recipe_definition g_recipes[ODG_RECIPE_COUNT] = {
    {sizeof(odg_recipe_definition),ODG_RECIPE_AXE_WOOD,ODG_STATION_WORKBENCH,2001u,ODG_ITEM_AXE,1u,ODG_MATERIAL_WOOD,1u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,6u),NIL_ING,NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_AXE_STONE,ODG_STATION_WORKBENCH,2002u,ODG_ITEM_AXE,1u,ODG_MATERIAL_STONE,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,4u),ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,6u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_PICKAXE_WOOD,ODG_STATION_WORKBENCH,2003u,ODG_ITEM_PICKAXE,1u,ODG_MATERIAL_WOOD,1u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,6u),NIL_ING,NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_PICKAXE_STONE,ODG_STATION_WORKBENCH,2004u,ODG_ITEM_PICKAXE,1u,ODG_MATERIAL_STONE,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,4u),ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,8u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_BACKPACK,ODG_STATION_WORKBENCH,2005u,ODG_ITEM_BACKPACK,1u,ODG_MATERIAL_WOOD,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,12u),ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,4u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_CHEST,ODG_STATION_WORKBENCH,2006u,ODG_ITEM_CHEST,1u,ODG_MATERIAL_WOOD,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,16u),ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,4u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_SMITHY,ODG_STATION_WORKBENCH,2007u,ODG_ITEM_SMITHY,1u,ODG_MATERIAL_STONE,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,12u),ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,20u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_AXE_IRON,ODG_STATION_SMITHY,2101u,ODG_ITEM_AXE,1u,ODG_MATERIAL_IRON,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,4u),ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,8u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_PICKAXE_IRON,ODG_STATION_SMITHY,2102u,ODG_ITEM_PICKAXE,1u,ODG_MATERIAL_IRON,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,4u),ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,10u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_TURRET_WOOD,ODG_STATION_SMITHY,2103u,ODG_ITEM_TURRET,1u,ODG_MATERIAL_WOOD,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,12u),ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,8u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_TURRET_STONE,ODG_STATION_SMITHY,2104u,ODG_ITEM_TURRET,1u,ODG_MATERIAL_STONE,2u,{ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,12u),ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,10u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_TURRET_IRON,ODG_STATION_SMITHY,2105u,ODG_ITEM_TURRET,1u,ODG_MATERIAL_IRON,2u,{ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,18u),ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,6u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_REPROGRAM_WOOD,ODG_STATION_SMITHY,2106u,ODG_ITEM_REPROGRAM_CHIP,1u,ODG_MATERIAL_WOOD,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,4u),ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,2u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_REPROGRAM_STONE,ODG_STATION_SMITHY,2107u,ODG_ITEM_REPROGRAM_CHIP,1u,ODG_MATERIAL_STONE,2u,{ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,5u),ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,3u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_REPROGRAM_IRON,ODG_STATION_SMITHY,2108u,ODG_ITEM_REPROGRAM_CHIP,1u,ODG_MATERIAL_IRON,1u,{ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,6u),NIL_ING,NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_AMMO_X12,ODG_STATION_SMITHY,2109u,ODG_ITEM_AMMO,12u,ODG_MATERIAL_IRON,1u,{ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,2u),NIL_ING,NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_ASCEND_STONE,ODG_STATION_SMITHY,2110u,ODG_ITEM_ASCENSION_CHIP,1u,ODG_MATERIAL_STONE,2u,{ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,5u),ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,3u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_ASCEND_IRON,ODG_STATION_SMITHY,2111u,ODG_ITEM_ASCENSION_CHIP,1u,ODG_MATERIAL_IRON,2u,{ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,8u),ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,4u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_BIRD_TRAP,ODG_STATION_WORKBENCH,2201u,ODG_ITEM_BIRD_TRAP,1u,ODG_MATERIAL_WOOD,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,10u),ING(ODG_ITEM_APPLE,ODG_MATERIAL_NONE,1u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_HUNTING_KNIFE,ODG_STATION_WORKBENCH,2202u,ODG_ITEM_HUNTING_KNIFE,1u,ODG_MATERIAL_STONE,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,3u),ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,5u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_SWORD_WOOD,ODG_STATION_WORKBENCH,2203u,ODG_ITEM_SWORD,1u,ODG_MATERIAL_WOOD,1u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,8u),NIL_ING,NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_SWORD_STONE,ODG_STATION_WORKBENCH,2204u,ODG_ITEM_SWORD,1u,ODG_MATERIAL_STONE,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,3u),ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,8u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_SWORD_IRON,ODG_STATION_SMITHY,2205u,ODG_ITEM_SWORD,1u,ODG_MATERIAL_IRON,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,3u),ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,10u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_WATER_FLASK,ODG_STATION_SMITHY,2206u,ODG_ITEM_WATER_FLASK,1u,ODG_MATERIAL_IRON,2u,{ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,5u),ING(ODG_ITEM_LEATHER,ODG_MATERIAL_NONE,2u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_RAIN_BARREL,ODG_STATION_WORKBENCH,2207u,ODG_ITEM_RAIN_BARREL,1u,ODG_MATERIAL_WOOD,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,14u),ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,2u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_TORCH_X4,ODG_STATION_WORKBENCH,2208u,ODG_ITEM_TORCH,4u,ODG_MATERIAL_WOOD,2u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,1u),ING(ODG_ITEM_COAL,ODG_MATERIAL_NONE,1u),NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    /* Construction is intentionally loss-bearing: raw material is processed into a
     * reusable structural module, then dismantling returns that module, not raw ore/logs. */
    {sizeof(odg_recipe_definition),ODG_RECIPE_BUILD_BLOCK_WOOD,ODG_STATION_WORKBENCH,2301u,ODG_ITEM_BUILDING_BLOCK,2u,ODG_MATERIAL_WOOD,1u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,5u),NIL_ING,NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_BUILD_BLOCK_STONE,ODG_STATION_WORKBENCH,2302u,ODG_ITEM_BUILDING_BLOCK,2u,ODG_MATERIAL_STONE,1u,{ING(ODG_ITEM_STONE,ODG_MATERIAL_STONE,6u),NIL_ING,NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    {sizeof(odg_recipe_definition),ODG_RECIPE_BUILD_BLOCK_IRON,ODG_STATION_SMITHY,2303u,ODG_ITEM_BUILDING_BLOCK,2u,ODG_MATERIAL_IRON,1u,{ING(ODG_ITEM_IRON,ODG_MATERIAL_IRON,7u),NIL_ING,NIL_ING,NIL_ING},{0u,0u,0u,0u}},
    /* First water vehicle: intentionally primitive and repairable from the same wood
     * economy. It is a physical artifact, not a swim-speed inventory buff. */
    {sizeof(odg_recipe_definition),ODG_RECIPE_RAFT,ODG_STATION_WORKBENCH,2304u,ODG_ITEM_RAFT,1u,ODG_MATERIAL_WOOD,1u,{ING(ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,16u),NIL_ING,NIL_ING,NIL_ING},{0u,0u,0u,0u}}
};

typedef struct {
    uint32_t material_tier;
    uint32_t resource_item_type;
    uint32_t full_repair_cost;
    uint32_t station_item_type;
} odg_tool_repair_profile;

/* Tool-repair progression is content data. Generic repair code must not infer resource,
 * cost or station from if/else material-tier branches. The profile keeps the current
 * same-material economy explicit while leaving one authoritative place for progression. */
static const odg_tool_repair_profile g_tool_repair_profiles[] = {
    {ODG_MATERIAL_WOOD,ODG_ITEM_WOOD,3u,ODG_STATION_WORKBENCH},
    {ODG_MATERIAL_STONE,ODG_ITEM_STONE,5u,ODG_STATION_WORKBENCH},
    {ODG_MATERIAL_IRON,ODG_ITEM_IRON,6u,ODG_STATION_SMITHY}
};

static const odg_tool_repair_profile *tool_repair_profile(uint32_t material_tier){
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_tool_repair_profiles)/sizeof(g_tool_repair_profiles[0]));++i)
        if(g_tool_repair_profiles[i].material_tier==material_tier)return &g_tool_repair_profiles[i];
    return NULL;
}

static int tool_repair_profiles_validate(void){
    uint32_t i,j;
    for(i=0u;i<(uint32_t)(sizeof(g_tool_repair_profiles)/sizeof(g_tool_repair_profiles[0]));++i){
        const odg_tool_repair_profile *p=&g_tool_repair_profiles[i];
        const odg_item_definition *resource=odg_item_definition_internal(p->resource_item_type);
        const odg_item_definition *station=odg_item_definition_internal(p->station_item_type);
        if(p->material_tier==ODG_MATERIAL_NONE||p->full_repair_cost==0u||resource==NULL||station==NULL||
           resource->category!=ODG_ITEM_CATEGORY_RESOURCE||resource->default_material_tier!=p->material_tier||
           (station->capability_bits&ODG_ITEM_CAP_PLACE)==0u||!odg_artifact_item_deployable_internal(p->station_item_type))return 0;
        for(j=i+1u;j<(uint32_t)(sizeof(g_tool_repair_profiles)/sizeof(g_tool_repair_profiles[0]));++j)
            if(g_tool_repair_profiles[j].material_tier==p->material_tier)return 0;
    }
    return tool_repair_profile(ODG_MATERIAL_WOOD)!=NULL&&
           tool_repair_profile(ODG_MATERIAL_STONE)!=NULL&&
           tool_repair_profile(ODG_MATERIAL_IRON)!=NULL;
}

static const odg_recipe_definition *recipe_by_id(uint32_t recipe_id) {
    if (recipe_id==0u || recipe_id>ODG_RECIPE_COUNT) return NULL;
    return &g_recipes[recipe_id-1u];
}

uint32_t odg_recipe_find_output_internal(uint32_t item_type,uint32_t material_tier) {
    uint32_t i,found=0u;
    for(i=0u;i<ODG_RECIPE_COUNT;++i){
        const odg_recipe_definition *recipe=&g_recipes[i];
        if(recipe->output_item_type!=item_type||recipe->output_material_tier!=material_tier)continue;
        /* Callers use this as an authority lookup, not a preference heuristic. Ambiguous
         * producers must fail closed rather than silently depending on table order. */
        if(found!=0u)return 0u;
        found=recipe->recipe_id;
    }
    return found;
}

int odg_recipe_profiles_validate_internal(void) {
    uint32_t i,j,k;
    for(i=0u;i<ODG_RECIPE_COUNT;++i){
        const odg_recipe_definition *r=&g_recipes[i];
        const odg_item_definition *out=odg_item_definition_internal(r->output_item_type);
        const odg_item_definition *station=odg_item_definition_internal(r->station_item_type);
        if(r->recipe_id!=i+1u||out==NULL||r->output_quantity==0u||
           r->ingredient_count==0u||r->ingredient_count>ODG_RECIPE_MAX_INGREDIENTS)return 0;
        if(station==NULL||(station->capability_bits&ODG_ITEM_CAP_PLACE)==0u||
           !odg_artifact_item_deployable_internal(r->station_item_type))return 0;
        if(out->max_stack==1u&&r->output_quantity!=1u)return 0;
        if(out->max_stack>1u&&r->output_quantity>out->max_stack)return 0;
        if(r->output_material_tier>ODG_MATERIAL_IRON)return 0;
        for(j=0u;j<r->ingredient_count;++j){
            const odg_recipe_ingredient *ing=&r->ingredients[j];
            if(ing->item_type==ODG_ITEM_NONE||ing->quantity==0u||
               odg_item_definition_internal(ing->item_type)==NULL||ing->material_tier>ODG_MATERIAL_IRON)return 0;
            for(k=0u;k<j;++k){
                const odg_recipe_ingredient *prev=&r->ingredients[k];
                if(prev->item_type==ing->item_type&&prev->material_tier==ing->material_tier)return 0;
            }
        }
        for(j=r->ingredient_count;j<ODG_RECIPE_MAX_INGREDIENTS;++j){
            const odg_recipe_ingredient *ing=&r->ingredients[j];
            if(ing->item_type!=ODG_ITEM_NONE||ing->quantity!=0u||ing->material_tier!=ODG_MATERIAL_NONE)return 0;
        }
        for(j=0u;j<i;++j)if(g_recipes[j].recipe_id==r->recipe_id)return 0;
    }
    return tool_repair_profiles_validate();
}

static void normalize_output_stack(odg_item_stack *stack,const odg_recipe_definition *recipe,uint64_t instance_id) {
    const odg_item_definition *def=odg_item_definition_internal(recipe->output_item_type);
    odg_memset(stack,0,sizeof(*stack));stack->type_id=recipe->output_item_type;stack->quantity=1u;stack->material_tier=recipe->output_material_tier;
    if (def!=NULL) stack->flags=def->flags;
    if (def!=NULL && (def->flags&ODG_ITEM_FLAG_DURABILITY)!=0u) {
        stack->max_durability=odg_item_max_durability_internal(stack->type_id,stack->material_tier);
        stack->durability=stack->max_durability;
    }
    if (def!=NULL && def->max_stack==1u) stack->instance_id=instance_id;
}

static int craft_inventory(odg_inventory *inventory,const odg_recipe_definition *recipe,uint32_t quantity,int assign_ids) {
    uint32_t i,u;
    const odg_item_definition *out_def;
    if (inventory==NULL || recipe==NULL || quantity==0u || quantity>99u) return 0;
    for (i=0u;i<recipe->ingredient_count;++i) {
        const odg_recipe_ingredient *ingredient=&recipe->ingredients[i];
        uint64_t needed=(uint64_t)ingredient->quantity*quantity;
        if (needed>UINT32_MAX || odg_inventory_total(inventory,ingredient->item_type,ingredient->material_tier)<(uint32_t)needed) return 0;
    }
    for (i=0u;i<recipe->ingredient_count;++i) {
        const odg_recipe_ingredient *ingredient=&recipe->ingredients[i];
        if (!odg_inventory_consume(inventory,ingredient->item_type,ingredient->material_tier,ingredient->quantity*quantity)) return 0;
    }
    out_def=odg_item_definition_internal(recipe->output_item_type);if (out_def==NULL) return 0;
    if (out_def->max_stack>1u) {
        odg_item_stack output;
        uint64_t total=(uint64_t)recipe->output_quantity*quantity;
        if (total>UINT32_MAX) return 0;
        normalize_output_stack(&output,recipe,0u);output.quantity=(uint32_t)total;
        return odg_inventory_add(inventory,&output);
    }
    for (u=0u;u<quantity*recipe->output_quantity;++u) {
        odg_item_stack output;uint64_t id=assign_ids?odg_next_instance_id():(UINT64_C(0x40000000)+u);
        if(id==0u)return 0;
        normalize_output_stack(&output,recipe,id);
        /* Backpack is equipment, not cargo. When the equipment slot is empty the
         * crafted result goes directly there and expands the inventory atomically.
         * This prevents a real four-slot progression deadlock (two tools + wood +
         * stone) while respecting the directive that the backpack must not consume
         * permanently one of the slots it exists to add. Additional backpacks, if
         * ever crafted, remain ordinary inventory items. */
        if ((out_def->capability_bits&ODG_ITEM_CAP_EXPAND_INVENTORY)!=0u &&
            inventory->equipped_backpack_type==ODG_ITEM_NONE &&
            odg_inventory_equip_expander_type_internal(inventory,output.type_id)) {
            continue;
        }
        if (!odg_inventory_add(inventory,&output)) return 0;
    }
    return 1;
}

uint32_t odg_recipe_count(void) { return ODG_RECIPE_COUNT; }

int32_t odg_recipe_get(uint32_t recipe_id,odg_recipe_definition *out_recipe,uint64_t capacity,uint64_t *out_required) {
    const odg_recipe_definition *recipe=recipe_by_id(recipe_id);
    if (out_required!=NULL) *out_required=(uint64_t)sizeof(odg_recipe_definition);
    if (recipe==NULL) return ODG_STATUS_INVALID_ARGUMENT;
    if (out_recipe==NULL || capacity<(uint64_t)sizeof(*out_recipe)) return ODG_STATUS_BUFFER_TOO_SMALL;
    *out_recipe=*recipe;return ODG_STATUS_OK;
}

uint32_t odg_recipe_max_craftable(uint32_t actor_id,uint32_t recipe_id) {
    const odg_recipe_definition *recipe=recipe_by_id(recipe_id);uint32_t quantity,best=0u;
    if (!g_odg.initialized || actor_id>=ODG_MAX_ACTORS || recipe==NULL) return 0u;
    if (!odg_crafting_station_near_actor(actor_id,recipe->station_item_type,NULL)) return 0u;
    for (quantity=1u;quantity<=99u;++quantity) {
        odg_inventory temp=g_odg.actors[actor_id].inventory;
        if (!craft_inventory(&temp,recipe,quantity,0)) break;
        best=quantity;
    }
    return best;
}

int32_t odg_craft(uint32_t actor_id,uint32_t recipe_id,uint32_t quantity) {
    const odg_recipe_definition *recipe=recipe_by_id(recipe_id);odg_inventory temp;
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (actor_id>=ODG_MAX_ACTORS || recipe==NULL || quantity==0u || quantity>99u) return ODG_STATUS_INVALID_ARGUMENT;
    if (!odg_crafting_station_near_actor(actor_id,recipe->station_item_type,NULL)) return ODG_STATUS_INVALID_STATE;
    temp=g_odg.actors[actor_id].inventory;
    if (!craft_inventory(&temp,recipe,quantity,0)) return ODG_STATUS_INVALID_STATE;
    temp=g_odg.actors[actor_id].inventory;
    if (!craft_inventory(&temp,recipe,quantity,1)) return ODG_STATUS_INVALID_STATE;
    g_odg.actors[actor_id].inventory=temp;
    odg_emit_particles(g_odg.actors[actor_id].x,g_odg.actors[actor_id].z,0x8ce8ffffu,8u);
    return ODG_STATUS_OK;
}

int32_t odg_repair_quote_selected(uint32_t actor_id,odg_repair_quote *out_quote,
                                  uint64_t capacity,uint64_t *out_required) {
    const odg_item_stack *tool;
    const odg_item_definition *definition;
    const odg_tool_repair_profile *repair;
    uint32_t damage,cost;
    if (out_required!=NULL) *out_required=(uint64_t)sizeof(odg_repair_quote);
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (actor_id>=ODG_MAX_ACTORS || out_quote==NULL || capacity<(uint64_t)sizeof(*out_quote))
        return actor_id>=ODG_MAX_ACTORS?ODG_STATUS_INVALID_ARGUMENT:ODG_STATUS_BUFFER_TOO_SMALL;
    tool=odg_inventory_selected_const(&g_odg.actors[actor_id].inventory);
    definition=tool!=NULL?odg_item_definition_internal(tool->type_id):NULL;
    if (tool==NULL || definition==NULL || (definition->flags&ODG_ITEM_FLAG_DURABILITY)==0u ||
        tool->max_durability==0u || tool->durability>=tool->max_durability) return ODG_STATUS_INVALID_STATE;
    repair=tool_repair_profile(tool->material_tier);
    if (repair==NULL) return ODG_STATUS_INVALID_ARGUMENT;
    damage=tool->max_durability-tool->durability;
    cost=(uint32_t)(((uint64_t)damage*repair->full_repair_cost+tool->max_durability-1u)/tool->max_durability);
    if (cost==0u) cost=1u;
    odg_memset(out_quote,0,sizeof(*out_quote));
    out_quote->struct_size=(uint32_t)sizeof(*out_quote);
    out_quote->item_type=tool->type_id;
    out_quote->material_tier=tool->material_tier;
    out_quote->durability_before=tool->durability;
    out_quote->durability_after=tool->max_durability;
    out_quote->cost_item_type=repair->resource_item_type;
    out_quote->cost_quantity=cost;
    out_quote->station_item_type=repair->station_item_type;
    return ODG_STATUS_OK;
}

int32_t odg_repair_selected(uint32_t actor_id) {
    odg_repair_quote quote;
    odg_actor *actor;
    odg_inventory staged;
    odg_item_stack *tool;
    uint64_t required=0u;
    int32_t status=odg_repair_quote_selected(actor_id,&quote,sizeof(quote),&required);
    if (status!=ODG_STATUS_OK) return status;
    actor=&g_odg.actors[actor_id];
    if (!odg_crafting_station_near_actor(actor_id,quote.station_item_type,NULL)) return ODG_STATUS_INVALID_STATE;
    staged=actor->inventory;
    if (odg_inventory_total(&staged,quote.cost_item_type,quote.material_tier)<quote.cost_quantity)
        return ODG_STATUS_INVALID_STATE;
    if (!odg_inventory_consume(&staged,quote.cost_item_type,quote.material_tier,quote.cost_quantity))
        return ODG_STATUS_INVALID_STATE;
    tool=odg_inventory_selected(&staged);
    if (tool==NULL || tool->type_id!=quote.item_type || tool->material_tier!=quote.material_tier ||
        tool->max_durability==0u || tool->durability>=tool->max_durability) return ODG_STATUS_INVALID_STATE;
    /* Resource consumption + tool mutation commit together. This remains correct even if a
     * future repair profile uses a resource that can share metadata with selected cargo. */
    tool->durability=tool->max_durability;
    actor->inventory=staged;
    odg_emit_particles(actor->x,actor->z,quote.material_tier==ODG_MATERIAL_IRON?0xc5d0d5ffu:0x8ce8ffffu,7u);
    return ODG_STATUS_OK;
}
