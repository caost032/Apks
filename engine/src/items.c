#include "game_internal.h"

#include <stddef.h>
#include <stdint.h>

/* Display codes are stable presentation keys, not gameplay rules. A localized host
 * may map them to text without duplicating any mechanical values. */
typedef struct {
    uint32_t item_type;
    uint32_t base_damage;
    uint32_t stone_bonus;
    uint32_t iron_bonus;
} odg_attack_profile;

/* Attack tuning is content data. Generic melee code must not decide damage by asking
 * whether a weapon happens to be a knife, sword, or a future concrete item ID. */
static const odg_attack_profile g_attack_profiles[] = {
    {ODG_ITEM_HUNTING_KNIFE,10u,8u,20u},
    {ODG_ITEM_SWORD,14u,8u,20u}
};

typedef struct {
    uint32_t item_type;
    uint32_t slot_count;
    uint32_t recovery_cache;
} odg_inventory_expander_profile;

/* Inventory expansion is equipment data, not a BACKPACK special case. Future belts,
 * packs or higher-tier containers can join this table without changing inventory, AI,
 * crafting or death-recovery transactions. */
static const odg_inventory_expander_profile g_inventory_expanders[] = {
    {ODG_ITEM_BACKPACK,ODG_INVENTORY_MAX_SLOTS,1u}
};

static const odg_inventory_expander_profile *inventory_expander_profile(uint32_t item_type){
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_inventory_expanders)/sizeof(g_inventory_expanders[0]));++i)
        if(g_inventory_expanders[i].item_type==item_type)return &g_inventory_expanders[i];
    return NULL;
}

static const odg_item_definition g_item_definitions[ODG_ITEM_TYPE_COUNT] = {
    {sizeof(odg_item_definition),ODG_ITEM_NONE,ODG_ITEM_CATEGORY_NONE,0u,0u,ODG_MATERIAL_NONE,0u,0u,0u,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_WOOD,ODG_ITEM_CATEGORY_RESOURCE,1001u,99u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_RESOURCE,0u,0u,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_STONE,ODG_ITEM_CATEGORY_RESOURCE,1002u,99u,ODG_MATERIAL_STONE,ODG_ITEM_FLAG_RESOURCE,0u,0u,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_IRON,ODG_ITEM_CATEGORY_RESOURCE,1003u,99u,ODG_MATERIAL_IRON,ODG_ITEM_FLAG_RESOURCE,0u,0u,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_AMMO,ODG_ITEM_CATEGORY_TECH,1101u,96u,ODG_MATERIAL_IRON,0u,0u,ODG_ITEM_CAP_REFILL_TURRET,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_REPROGRAM_CHIP,ODG_ITEM_CATEGORY_TECH,1102u,8u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_CHIP,0u,ODG_ITEM_CAP_REPROGRAM,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_ASCENSION_CHIP,ODG_ITEM_CATEGORY_TECH,1103u,8u,ODG_MATERIAL_STONE,ODG_ITEM_FLAG_CHIP,0u,ODG_ITEM_CAP_UPGRADE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_AXE,ODG_ITEM_CATEGORY_TOOL,1201u,1u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_DURABILITY,240u,ODG_ITEM_CAP_HARVEST,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_PICKAXE,ODG_ITEM_CATEGORY_TOOL,1202u,1u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_DURABILITY,260u,ODG_ITEM_CAP_MINE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_TURRET,ODG_ITEM_CATEGORY_DEPLOYABLE,1301u,1u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_ARTIFACT,0u,ODG_ITEM_CAP_PLACE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_WORKBENCH,ODG_ITEM_CATEGORY_DEPLOYABLE,1302u,1u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_ARTIFACT|ODG_ITEM_FLAG_PROTECTED,0u,ODG_ITEM_CAP_PLACE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_SMITHY,ODG_ITEM_CATEGORY_DEPLOYABLE,1303u,1u,ODG_MATERIAL_STONE,ODG_ITEM_FLAG_ARTIFACT,0u,ODG_ITEM_CAP_PLACE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_CHEST,ODG_ITEM_CATEGORY_DEPLOYABLE,1304u,1u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_ARTIFACT,0u,ODG_ITEM_CAP_PLACE|ODG_ITEM_CAP_STORE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_BACKPACK,ODG_ITEM_CATEGORY_EQUIPMENT,1401u,1u,ODG_MATERIAL_WOOD,0u,0u,ODG_ITEM_CAP_EXPAND_INVENTORY,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_APPLE,ODG_ITEM_CATEGORY_FOOD,1501u,12u,ODG_MATERIAL_NONE,ODG_ITEM_FLAG_FOOD,0u,ODG_ITEM_CAP_CONSUME,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_APPLE_SEED,ODG_ITEM_CATEGORY_SEED,1502u,24u,ODG_MATERIAL_NONE,ODG_ITEM_FLAG_SEED,0u,ODG_ITEM_CAP_PLANT,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_BIRD_TRAP,ODG_ITEM_CATEGORY_DEPLOYABLE,1503u,1u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_ARTIFACT,0u,ODG_ITEM_CAP_PLACE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_LEATHER,ODG_ITEM_CATEGORY_RESOURCE,1504u,32u,ODG_MATERIAL_NONE,ODG_ITEM_FLAG_RESOURCE,0u,0u,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_RAW_MEAT,ODG_ITEM_CATEGORY_FOOD,1505u,16u,ODG_MATERIAL_NONE,ODG_ITEM_FLAG_FOOD,0u,ODG_ITEM_CAP_CONSUME,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_HUNTING_KNIFE,ODG_ITEM_CATEGORY_TOOL,1506u,1u,ODG_MATERIAL_STONE,ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_DURABILITY,220u,ODG_ITEM_CAP_HUNT|ODG_ITEM_CAP_ATTACK,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_SWORD,ODG_ITEM_CATEGORY_TOOL,1507u,1u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_DURABILITY,260u,ODG_ITEM_CAP_ATTACK,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_WATER_FLASK,ODG_ITEM_CATEGORY_TOOL,1508u,1u,ODG_MATERIAL_IRON,0u,0u,ODG_ITEM_CAP_COLLECT_WATER|ODG_ITEM_CAP_IRRIGATE|ODG_ITEM_CAP_DRINK,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_RAIN_BARREL,ODG_ITEM_CATEGORY_DEPLOYABLE,1509u,1u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_ARTIFACT,0u,ODG_ITEM_CAP_PLACE|ODG_ITEM_CAP_STORE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_COAL,ODG_ITEM_CATEGORY_RESOURCE,1510u,64u,ODG_MATERIAL_NONE,ODG_ITEM_FLAG_RESOURCE,0u,0u,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_TORCH,ODG_ITEM_CATEGORY_DEPLOYABLE,1511u,16u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_ARTIFACT,0u,ODG_ITEM_CAP_PLACE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_NIGHT_SHARD,ODG_ITEM_CATEGORY_DEPLOYABLE,1512u,16u,ODG_MATERIAL_NONE,ODG_ITEM_FLAG_ARTIFACT,0u,ODG_ITEM_CAP_PLACE,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_RAW_FISH,ODG_ITEM_CATEGORY_FOOD,1513u,16u,ODG_MATERIAL_NONE,ODG_ITEM_FLAG_FOOD,0u,ODG_ITEM_CAP_CONSUME,{0u,0u,0u}},
    /* One construction item, material variants. Runtime construction policy operates on
     * capability+material instead of three duplicated wood/stone/iron object types. */
    {sizeof(odg_item_definition),ODG_ITEM_BUILDING_BLOCK,ODG_ITEM_CATEGORY_DEPLOYABLE,1601u,32u,ODG_MATERIAL_WOOD,0u,0u,ODG_ITEM_CAP_PLACE|ODG_ITEM_CAP_CONSTRUCT,{0u,0u,0u}},
    {sizeof(odg_item_definition),ODG_ITEM_RAFT,ODG_ITEM_CATEGORY_DEPLOYABLE,1602u,1u,ODG_MATERIAL_WOOD,ODG_ITEM_FLAG_ARTIFACT,0u,ODG_ITEM_CAP_PLACE,{0u,0u,0u}}
};

int odg_item_stack_empty_internal(const odg_item_stack *stack) {
    return stack == NULL || stack->type_id == ODG_ITEM_NONE || stack->quantity == 0u;
}

int odg_item_stack_protected_internal(const odg_item_stack *stack) {
    const odg_item_definition *definition;
    if(odg_item_stack_empty_internal(stack))return 0;
    definition=odg_item_definition_internal(stack->type_id);
    return (stack->flags&ODG_ITEM_FLAG_PROTECTED)!=0u ||
           (definition!=NULL&&(definition->flags&ODG_ITEM_FLAG_PROTECTED)!=0u);
}

int odg_item_stack_normalize_internal(odg_item_stack *stack) {
    const odg_item_definition *definition;
    const uint32_t known_static_flags=ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_RESOURCE|
        ODG_ITEM_FLAG_CHIP|ODG_ITEM_FLAG_ARTIFACT|ODG_ITEM_FLAG_DURABILITY|
        ODG_ITEM_FLAG_FOOD|ODG_ITEM_FLAG_SEED;
    uint32_t dynamic_flags,unknown_flags;
    if(odg_item_stack_empty_internal(stack))return 0;
    definition=odg_item_definition_internal(stack->type_id);
    if(definition==NULL||definition->max_stack==0u)return 0;

    /* Category/capability flags belong to the item registry, not to whichever subsystem
     * happened to construct this stack. PROTECTED is instance state and is preserved. */
    dynamic_flags=stack->flags&ODG_ITEM_FLAG_PROTECTED;
    unknown_flags=stack->flags&~(known_static_flags|ODG_ITEM_FLAG_PROTECTED);
    stack->flags=definition->flags|dynamic_flags|unknown_flags;
    if(stack->material_tier==ODG_MATERIAL_NONE&&definition->default_material_tier!=ODG_MATERIAL_NONE)
        stack->material_tier=definition->default_material_tier;

    if(definition->max_stack>1u)stack->instance_id=0u;
    if((definition->flags&ODG_ITEM_FLAG_DURABILITY)!=0u){
        uint32_t maximum=odg_item_max_durability_internal(stack->type_id,stack->material_tier);
        if(maximum==0u)return 0;
        if(stack->max_durability==0u){
            stack->max_durability=maximum;
            if(stack->durability==0u)stack->durability=maximum;
        }else if(stack->durability>stack->max_durability){
            stack->durability=stack->max_durability;
        }
    }else{
        stack->durability=0u;
        stack->max_durability=0u;
    }
    return 1;
}

int odg_item_stack_metadata_compatible_internal(const odg_item_stack *a,const odg_item_stack *b) {
    const odg_item_definition *definition;
    const uint32_t static_mask=ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_RESOURCE|ODG_ITEM_FLAG_CHIP|
        ODG_ITEM_FLAG_ARTIFACT|ODG_ITEM_FLAG_DURABILITY|ODG_ITEM_FLAG_FOOD|ODG_ITEM_FLAG_SEED;
    uint32_t a_dynamic,b_dynamic;
    if (odg_item_stack_empty_internal(a) || odg_item_stack_empty_internal(b)) return 0;
    if(a->type_id!=b->type_id || a->material_tier!=b->material_tier || a->payload_id!=b->payload_id)return 0;
    definition=odg_item_definition_internal(a->type_id);
    if(definition==NULL)return 0;
    /* Ignore all registry-derived category flags when comparing legacy/current stacks.
     * PROTECTED and unknown future instance bits remain semantically significant. */
    a_dynamic=a->flags&~static_mask;
    b_dynamic=b->flags&~static_mask;
    if(a_dynamic!=b_dynamic)return 0;
    if(definition!=NULL && definition->max_stack>1u){
        /* Stackable biological content keeps payload identity (species/variety) while
         * discarding per-instance identity. This is what lets future seed varieties
         * survive fruit -> inventory -> planting without special-case stacks. */
        return a->durability==b->durability && a->max_durability==b->max_durability;
    }
    return a->durability==b->durability && a->max_durability==b->max_durability &&
           a->instance_id==b->instance_id;
}

const odg_item_definition *odg_item_definition_internal(uint32_t type_id) {
    if (type_id >= ODG_ITEM_TYPE_COUNT) return NULL;
    return &g_item_definitions[type_id];
}

int odg_item_material_variant_valid_internal(uint32_t type_id,uint32_t material_tier){
    const odg_item_definition *definition=odg_item_definition_internal(type_id);
    if(definition==NULL||type_id==ODG_ITEM_NONE||material_tier>ODG_MATERIAL_IRON)return 0;
    if(material_tier==definition->default_material_tier)return 1;
    return odg_recipe_find_output_internal(type_id,material_tier)!=0u;
}

uint32_t odg_item_inventory_expander_slots_internal(uint32_t item_type){
    const odg_inventory_expander_profile *profile=inventory_expander_profile(item_type);
    return profile!=NULL?profile->slot_count:0u;
}

int odg_item_inventory_expander_recovery_internal(uint32_t item_type){
    const odg_inventory_expander_profile *profile=inventory_expander_profile(item_type);
    return profile!=NULL&&profile->recovery_cache!=0u;
}

int odg_inventory_expanders_validate_internal(void){
    uint32_t i,j;
    const uint32_t count=(uint32_t)(sizeof(g_inventory_expanders)/sizeof(g_inventory_expanders[0]));
    for(i=0u;i<count;++i){
        const odg_inventory_expander_profile *profile=&g_inventory_expanders[i];
        const odg_item_definition *item=odg_item_definition_internal(profile->item_type);
        if(item==NULL||(item->capability_bits&ODG_ITEM_CAP_EXPAND_INVENTORY)==0u||
           item->category!=ODG_ITEM_CATEGORY_EQUIPMENT||profile->slot_count<=ODG_INVENTORY_BASE_SLOTS||
           profile->slot_count>ODG_INVENTORY_MAX_SLOTS||
           (profile->recovery_cache!=0u&&profile->slot_count>ODG_CHEST_SLOTS))return 0;
        for(j=i+1u;j<count;++j)if(g_inventory_expanders[j].item_type==profile->item_type)return 0;
    }
    for(i=1u;i<ODG_ITEM_TYPE_COUNT;++i){
        const odg_item_definition *item=odg_item_definition_internal(i);
        if(item!=NULL&&(item->capability_bits&ODG_ITEM_CAP_EXPAND_INVENTORY)!=0u&&inventory_expander_profile(i)==NULL)return 0;
    }
    return 1;
}

int odg_inventory_equip_expander_type_internal(odg_inventory *inventory,uint32_t item_type){
    uint32_t slots;
    if(inventory==NULL||inventory->equipped_backpack_type!=ODG_ITEM_NONE)return 0;
    slots=odg_item_inventory_expander_slots_internal(item_type);if(slots==0u)return 0;
    inventory->equipped_backpack_type=item_type;inventory->slot_count=slots;
    if(inventory->selected_slot>=slots)inventory->selected_slot=0u;
    return 1;
}

int odg_inventory_equip_first_expander_internal(odg_inventory *inventory){
    odg_inventory staged;uint32_t slot,type,slots;
    if(inventory==NULL||inventory->equipped_backpack_type!=ODG_ITEM_NONE)return 0;
    if(!odg_inventory_find_capability_internal(inventory,ODG_ITEM_CAP_EXPAND_INVENTORY,ODG_MATERIAL_NONE,&slot))return 0;
    type=inventory->slots[slot].type_id;slots=odg_item_inventory_expander_slots_internal(type);if(slots==0u)return 0;
    staged=*inventory;
    if(!odg_inventory_remove_from_slot(&staged,slot,1u,NULL))return 0;
    if(!odg_inventory_equip_expander_type_internal(&staged,type))return 0;
    *inventory=staged;
    return 1;
}

static const odg_attack_profile *odg_attack_profile_internal(uint32_t item_type) {
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_attack_profiles)/sizeof(g_attack_profiles[0]));++i){
        if(g_attack_profiles[i].item_type==item_type)return &g_attack_profiles[i];
    }
    return NULL;
}

uint32_t odg_item_attack_damage_internal(const odg_item_stack *stack) {
    const odg_item_definition *item;
    const odg_attack_profile *profile;
    uint32_t damage;
    if(stack==NULL||stack->type_id==ODG_ITEM_NONE||stack->quantity==0u)return 0u;
    item=odg_item_definition_internal(stack->type_id);
    if(item==NULL||(item->capability_bits&ODG_ITEM_CAP_ATTACK)==0u)return 0u;
    profile=odg_attack_profile_internal(stack->type_id);
    if(profile==NULL)return 0u;
    damage=profile->base_damage;
    if(stack->material_tier==ODG_MATERIAL_STONE)damage+=profile->stone_bonus;
    else if(stack->material_tier==ODG_MATERIAL_IRON)damage+=profile->iron_bonus;
    return damage;
}

int odg_item_attack_profile_valid_internal(uint32_t item_type) {
    const odg_item_definition *item=odg_item_definition_internal(item_type);
    const odg_attack_profile *profile=odg_attack_profile_internal(item_type);
    if(item==NULL)return 0;
    if((item->capability_bits&ODG_ITEM_CAP_ATTACK)!=0u)return profile!=NULL&&profile->base_damage!=0u;
    return profile==NULL;
}

uint32_t odg_item_max_durability_internal(uint32_t type_id,uint32_t material_tier) {
    const odg_item_definition *definition=odg_item_definition_internal(type_id);
    uint32_t maximum;
    if(definition==NULL||(definition->flags&ODG_ITEM_FLAG_DURABILITY)==0u||definition->base_durability==0u)return 0u;
    maximum=definition->base_durability;
    if(material_tier==ODG_MATERIAL_STONE)maximum=(maximum*22u)/10u;
    else if(material_tier==ODG_MATERIAL_IRON)maximum*=5u;
    return maximum;
}

void odg_item_wear_internal(odg_item_stack *stack,uint32_t amount) {
    uint32_t maximum;
    if(stack==NULL||stack->type_id==ODG_ITEM_NONE||amount==0u)return;
    maximum=odg_item_max_durability_internal(stack->type_id,stack->material_tier);
    if(maximum==0u)return;
    if(stack->max_durability==0u){stack->max_durability=maximum;stack->durability=maximum;}
    if(amount>=stack->durability)stack->durability=0u;else stack->durability-=amount;
    if(stack->durability==0u)odg_memset(stack,0,sizeof(*stack));
}

uint32_t odg_item_definition_count(void) { return ODG_ITEM_TYPE_COUNT - 1u; }

int32_t odg_item_definition_get(uint32_t type_id,odg_item_definition *out_definition,
                                uint64_t capacity,uint64_t *out_required) {
    const odg_item_definition *definition=odg_item_definition_internal(type_id);
    if (out_required != NULL) *out_required=(uint64_t)sizeof(odg_item_definition);
    if (definition == NULL || type_id == ODG_ITEM_NONE) return ODG_STATUS_INVALID_ARGUMENT;
    if (out_definition == NULL || capacity < (uint64_t)sizeof(*out_definition)) return ODG_STATUS_BUFFER_TOO_SMALL;
    *out_definition=*definition;
    return ODG_STATUS_OK;
}

void odg_inventory_init(odg_inventory *inventory) {
    if (inventory == NULL) return;
    odg_memset(inventory,0,sizeof(*inventory));
    inventory->slot_count=ODG_INVENTORY_BASE_SLOTS;
    inventory->selected_slot=0u;
}

uint32_t odg_inventory_capacity(const odg_inventory *inventory) {
    if (inventory == NULL) return 0u;
    if (inventory->slot_count < ODG_INVENTORY_BASE_SLOTS) return ODG_INVENTORY_BASE_SLOTS;
    return inventory->slot_count > ODG_INVENTORY_MAX_SLOTS ? ODG_INVENTORY_MAX_SLOTS : inventory->slot_count;
}

odg_item_stack *odg_inventory_selected(odg_inventory *inventory) {
    uint32_t capacity=odg_inventory_capacity(inventory);
    if (inventory == NULL || inventory->selected_slot >= capacity) return NULL;
    return &inventory->slots[inventory->selected_slot];
}

const odg_item_stack *odg_inventory_selected_const(const odg_inventory *inventory) {
    uint32_t capacity=odg_inventory_capacity(inventory);
    if (inventory == NULL || inventory->selected_slot >= capacity) return NULL;
    return &inventory->slots[inventory->selected_slot];
}

int odg_slots_add(odg_item_stack *slots,uint32_t capacity,const odg_item_stack *stack) {
    odg_item_stack staged[ODG_CHEST_SLOTS];
    odg_item_stack remaining;
    const odg_item_definition *definition;
    uint32_t i,max_stack;
    _Static_assert(ODG_INVENTORY_MAX_SLOTS<=ODG_CHEST_SLOTS,"slot transaction staging must cover inventory");
    if (slots == NULL || capacity == 0u || capacity>ODG_CHEST_SLOTS || odg_item_stack_empty_internal(stack)) return 0;
    definition=odg_item_definition_internal(stack->type_id);
    if (definition == NULL || definition->max_stack == 0u) return 0;
    remaining=*stack;
    if(!odg_item_stack_normalize_internal(&remaining))return 0;
    for(i=0u;i<capacity;++i){
        staged[i]=slots[i];
        if(!odg_item_stack_empty_internal(&staged[i])&&!odg_item_stack_normalize_internal(&staged[i]))return 0;
    }
    max_stack=definition->max_stack;
    if (max_stack > 1u) {
        for (i=0u;i<capacity && remaining.quantity!=0u;++i) {
            odg_item_stack *dst=&staged[i];
            uint32_t room,move;
            if (!odg_item_stack_metadata_compatible_internal(dst,&remaining) || dst->quantity>=max_stack) continue;
            room=max_stack-dst->quantity;
            move=remaining.quantity<room?remaining.quantity:room;
            dst->quantity+=move;
            remaining.quantity-=move;
        }
    }
    for (i=0u;i<capacity && remaining.quantity!=0u;++i) {
        odg_item_stack *dst=&staged[i];
        uint32_t move;
        if (!odg_item_stack_empty_internal(dst)) continue;
        move=remaining.quantity<max_stack?remaining.quantity:max_stack;
        *dst=remaining;
        dst->quantity=move;
        if (max_stack>1u) {
            dst->instance_id=0u;
            /* payload_id is content identity for stackable biological items and must
             * not be erased (e.g. apple seed species/variant). */
        }
        remaining.quantity-=move;
    }
    if(remaining.quantity!=0u)return 0;
    for(i=0u;i<capacity;++i)slots[i]=staged[i];
    return 1;
}

int odg_slots_remove(odg_item_stack *slots,uint32_t capacity,uint32_t slot,uint32_t quantity,odg_item_stack *removed) {
    odg_item_stack *source;
    if (slots == NULL || quantity==0u || slot>=capacity) return 0;
    source=&slots[slot];
    if (odg_item_stack_empty_internal(source) || source->quantity<quantity) return 0;
    if (removed != NULL) { *removed=*source; removed->quantity=quantity; }
    source->quantity-=quantity;
    if (source->quantity==0u) odg_memset(source,0,sizeof(*source));
    return 1;
}

int odg_inventory_add(odg_inventory *inventory,const odg_item_stack *stack) {
    if (inventory == NULL) return 0;
    return odg_slots_add(inventory->slots,odg_inventory_capacity(inventory),stack);
}

int odg_inventory_remove_from_slot(odg_inventory *inventory,uint32_t slot,uint32_t quantity,odg_item_stack *removed) {
    if (inventory == NULL) return 0;
    return odg_slots_remove(inventory->slots,odg_inventory_capacity(inventory),slot,quantity,removed);
}

int odg_inventory_find_type(const odg_inventory *inventory,uint32_t type_id,uint32_t material_tier,uint32_t *out_slot) {
    uint32_t i,capacity=odg_inventory_capacity(inventory);
    if (inventory == NULL) return 0;
    for (i=0u;i<capacity;++i) {
        const odg_item_stack *stack=&inventory->slots[i];
        if (!odg_item_stack_empty_internal(stack) && stack->type_id==type_id &&
            (material_tier==ODG_MATERIAL_NONE || stack->material_tier==material_tier)) {
            if (out_slot != NULL) *out_slot=i;
            return 1;
        }
    }
    return 0;
}

int odg_inventory_find_capability_internal(const odg_inventory *inventory,uint32_t capability_bits,uint32_t material_tier,uint32_t *out_slot) {
    uint32_t i,capacity=odg_inventory_capacity(inventory);
    if(inventory==NULL||capability_bits==0u)return 0;
    for(i=0u;i<capacity;++i){
        const odg_item_stack *stack=&inventory->slots[i];
        const odg_item_definition *definition;
        if(odg_item_stack_empty_internal(stack)||(material_tier!=ODG_MATERIAL_NONE&&stack->material_tier!=material_tier))continue;
        definition=odg_item_definition_internal(stack->type_id);
        if(definition==NULL||(definition->capability_bits&capability_bits)!=capability_bits)continue;
        if(out_slot!=NULL)*out_slot=i;
        return 1;
    }
    return 0;
}

uint32_t odg_inventory_total(const odg_inventory *inventory,uint32_t type_id,uint32_t material_tier) {
    uint32_t i,total=0u,capacity=odg_inventory_capacity(inventory);
    if (inventory == NULL) return 0u;
    for (i=0u;i<capacity;++i) {
        const odg_item_stack *stack=&inventory->slots[i];
        if (!odg_item_stack_empty_internal(stack) && stack->type_id==type_id &&
            (material_tier==ODG_MATERIAL_NONE || stack->material_tier==material_tier)) {
            if (UINT32_MAX-total < stack->quantity) return UINT32_MAX;
            total+=stack->quantity;
        }
    }
    return total;
}

int odg_inventory_consume(odg_inventory *inventory,uint32_t type_id,uint32_t material_tier,uint32_t quantity) {
    uint32_t i,capacity;
    if (inventory == NULL || quantity==0u || odg_inventory_total(inventory,type_id,material_tier)<quantity) return 0;
    capacity=odg_inventory_capacity(inventory);
    for (i=0u;i<capacity && quantity!=0u;++i) {
        odg_item_stack *stack=&inventory->slots[i];
        uint32_t take;
        if (odg_item_stack_empty_internal(stack) || stack->type_id!=type_id ||
            (material_tier!=ODG_MATERIAL_NONE && stack->material_tier!=material_tier)) continue;
        take=stack->quantity<quantity?stack->quantity:quantity;
        stack->quantity-=take;
        quantity-=take;
        if (stack->quantity==0u) odg_memset(stack,0,sizeof(*stack));
    }
    return quantity==0u;
}

uint64_t odg_next_instance_id(void) {
    uint64_t id;
    if(g_odg.next_instance_id==0u)g_odg.next_instance_id=UINT64_C(1);
    /* The high bit belongs exclusively to deterministic procedural identities. Never
     * wrap the sequential allocator into that namespace or back to an already-used ID.
     * Exhaustion is fail-closed: callers must preserve their transaction when 0 returns. */
    if(g_odg.next_instance_id>=ODG_INSTANCE_ID_PROCEDURAL_BIT)return 0u;
    id=g_odg.next_instance_id;
    ++g_odg.next_instance_id;
    return id;
}


static void pickup_set_ground_lifetime(odg_world_pickup *pickup) {
    const odg_food_definition *food;
    if(pickup==NULL)return;
    pickup->age_ticks=0u;
    food=odg_food_definition_internal(pickup->stack.type_id);
    pickup->lifetime_ticks=food!=NULL?food->ground_lifetime_ticks:0u;
}

void odg_world_pickup_deactivate_internal(odg_world_pickup *pickup) {
    uint32_t id;
    if(pickup==NULL)return;
    id=pickup->id;
    odg_memset(pickup,0,sizeof(*pickup));
    pickup->id=id;
    odg_entities_spatial_mark_dirty();
}

int odg_world_pickups_prepare_internal(uint32_t additional) {
    uint32_t i,inactive=0u,needed_new;
    if(additional==0u)return 1;
    for(i=0u;i<g_odg.pickup_count;++i)if(!g_odg_pickups[i].active)++inactive;
    if(inactive>=additional)return 1;
    needed_new=additional-inactive;
    if(g_odg.pickup_count>UINT32_MAX-needed_new)return 0;
    return odg_entities_reserve_pickups(g_odg.pickup_count+needed_new);
}

int odg_spawn_world_pickup(const odg_item_stack *stack,int32_t x,int32_t z,uint32_t pickup_cd) {
    uint32_t i;
    odg_item_stack normalized;
    if (odg_item_stack_empty_internal(stack) || odg_item_definition_internal(stack->type_id)==NULL) return 0;
    normalized=*stack;
    if(!odg_item_stack_normalize_internal(&normalized))return 0;
    if(!odg_world_pickups_prepare_internal(1u))return 0;
    if(normalized.instance_id==0u&&odg_item_definition_internal(normalized.type_id)->max_stack==1u){
        normalized.instance_id=odg_next_instance_id();
        if(normalized.instance_id==0u)return 0;
    }
    for (i=0u;i<g_odg.pickup_count;++i) {
        odg_world_pickup *pickup=&g_odg_pickups[i];
        if (pickup->active) continue;
        odg_memset(pickup,0,sizeof(*pickup));pickup->active=1u;pickup->id=i;
        pickup->x=x;pickup->z=z;pickup->local_resident=1u;odg_local_fx_to_global_fx_internal(x,z,&pickup->global_fx_x,&pickup->global_fx_z);
        pickup->pickup_cd=pickup_cd;pickup->stack=normalized;pickup_set_ground_lifetime(pickup);odg_entities_spatial_mark_dirty();return 1;
    }
    if(g_odg.pickup_count>=g_odg_pickup_capacity)return 0;
    i=g_odg.pickup_count++;
    odg_memset(&g_odg_pickups[i],0,sizeof(g_odg_pickups[i]));
    g_odg_pickups[i].active=1u;g_odg_pickups[i].id=i;g_odg_pickups[i].x=x;g_odg_pickups[i].z=z;
    g_odg_pickups[i].local_resident=1u;odg_local_fx_to_global_fx_internal(x,z,&g_odg_pickups[i].global_fx_x,&g_odg_pickups[i].global_fx_z);
    g_odg_pickups[i].pickup_cd=pickup_cd;g_odg_pickups[i].stack=normalized;pickup_set_ground_lifetime(&g_odg_pickups[i]);odg_entities_spatial_mark_dirty();return 1;
}

int32_t odg_copy_inventory(uint32_t actor_id,odg_inventory_snapshot *out_inventory,
                           uint64_t capacity,uint64_t *out_required) {
    const odg_inventory *inventory;
    uint32_t i,slots;
    if (out_required != NULL) *out_required=(uint64_t)sizeof(odg_inventory_snapshot);
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (actor_id>=ODG_MAX_ACTORS) return ODG_STATUS_INVALID_ARGUMENT;
    if (out_inventory==NULL || capacity<(uint64_t)sizeof(*out_inventory)) return ODG_STATUS_BUFFER_TOO_SMALL;
    inventory=&g_odg.actors[actor_id].inventory;
    odg_memset(out_inventory,0,sizeof(*out_inventory));
    out_inventory->struct_size=(uint32_t)sizeof(*out_inventory);
    out_inventory->schema_version=1u;
    out_inventory->actor_id=actor_id;
    out_inventory->slot_count=odg_inventory_capacity(inventory);
    out_inventory->base_slot_count=ODG_INVENTORY_BASE_SLOTS;
    out_inventory->selected_slot=inventory->selected_slot;
    out_inventory->equipped_backpack_type=inventory->equipped_backpack_type;
    slots=out_inventory->slot_count;
    for (i=0u;i<slots;++i) out_inventory->slots[i]=inventory->slots[i];
    return ODG_STATUS_OK;
}
