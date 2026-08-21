#include "game_internal.h"

#include <stdint.h>

static const odg_food_definition g_foods[] = {
    {.struct_size=sizeof(odg_food_definition),.item_type=ODG_ITEM_APPLE,
     .satiety_restore=220u,.hydration_restore=80u,.heal_amount=2u,.flags=ODG_FOOD_FLAG_PLANT,
     .ground_lifetime_ticks=8u*60u*ODG_TICK_RATE,.reserved_u32=0u},
    {.struct_size=sizeof(odg_food_definition),.item_type=ODG_ITEM_RAW_MEAT,
     .satiety_restore=320u,.hydration_restore=20u,.heal_amount=0u,.flags=ODG_FOOD_FLAG_ANIMAL|ODG_FOOD_FLAG_RAW,
     .ground_lifetime_ticks=6u*60u*ODG_TICK_RATE,.reserved_u32=0u},
    {.struct_size=sizeof(odg_food_definition),.item_type=ODG_ITEM_RAW_FISH,
     .satiety_restore=250u,.hydration_restore=45u,.heal_amount=0u,.flags=ODG_FOOD_FLAG_ANIMAL|ODG_FOOD_FLAG_RAW,
     .ground_lifetime_ticks=5u*60u*ODG_TICK_RATE,.reserved_u32=0u}
};

const odg_food_definition *odg_food_definition_internal(uint32_t item_type) {
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_foods)/sizeof(g_foods[0]));++i)
        if(g_foods[i].item_type==item_type)return &g_foods[i];
    return NULL;
}

uint32_t odg_food_definition_count(void) {
    return (uint32_t)(sizeof(g_foods)/sizeof(g_foods[0]));
}

int32_t odg_food_definition_get(uint32_t index,odg_food_definition *out_definition,
                                uint64_t capacity,uint64_t *out_required) {
    if(out_required!=NULL)*out_required=(uint64_t)sizeof(odg_food_definition);
    if(index>=odg_food_definition_count())return ODG_STATUS_INVALID_ARGUMENT;
    if(out_definition==NULL || capacity<(uint64_t)sizeof(*out_definition))return ODG_STATUS_BUFFER_TOO_SMALL;
    *out_definition=g_foods[index];
    return ODG_STATUS_OK;
}

static int food_slot_for_actor(const odg_actor *actor,uint32_t *out_slot) {
    uint32_t i,capacity;
    const odg_food_definition *best=NULL;
    uint32_t best_slot=UINT32_MAX;
    if(actor==NULL)return 0;
    capacity=odg_inventory_capacity(&actor->inventory);
    for(i=0u;i<capacity;++i){
        const odg_item_stack *s=&actor->inventory.slots[i];
        const odg_food_definition *f;
        if(odg_item_stack_empty_internal(s))continue;
        f=odg_food_definition_internal(s->type_id);
        if(f==NULL)continue;
        /* Prefer the smallest adequate food to reduce waste, then stable slot order. */
        if(best==NULL || f->satiety_restore<best->satiety_restore){best=f;best_slot=i;}
    }
    if(best_slot==UINT32_MAX)return 0;
    if(out_slot!=NULL)*out_slot=best_slot;
    return 1;
}

int odg_actor_consume_food_internal(uint32_t actor_id,uint32_t slot) {
    odg_actor *actor;odg_inventory staged_inventory;odm_rng staged_rng;
    odg_item_stack removed,seed;const odg_food_definition *food;const odg_flora_species_definition *flora;
    uint32_t capacity,new_satiety;int seed_to_drop=0;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];
    if(!actor->active||actor->hp==0u)return 0;
    capacity=odg_inventory_capacity(&actor->inventory);
    if(slot>=capacity)return 0;
    food=odg_food_definition_internal(actor->inventory.slots[slot].type_id);
    if(food==NULL)return 0;
    if(actor->satiety_permille>=ODG_ACTOR_SATIETY_MAX &&
       actor->hydration_permille>=ODG_ACTOR_HYDRATION_MAX &&
       (food->heal_amount==0u || actor->hp>=actor->max_hp))return 0;

    /* Food consumption and fruit-seed recovery are one transaction. Stage inventory and
     * ecology RNG first; if a recovered seed cannot fit in the inventory, reserve and
     * create its ground pickup before committing the consumed food. No successful bite
     * may silently delete a seed because every inventory slot happened to be occupied. */
    staged_inventory=actor->inventory;staged_rng=g_odg.ecology_rng;odg_memset(&seed,0,sizeof(seed));
    if(!odg_inventory_remove_from_slot(&staged_inventory,slot,1u,&removed))return 0;
    flora=odg_flora_species_for_fruit_internal(removed.type_id,removed.payload_id);
    if(flora!=NULL&&flora->seed_item_type!=ODG_ITEM_NONE&&flora->fruit_seed_recovery_permille!=0u&&
       odg_rand_bounded(&staged_rng,1000u)<flora->fruit_seed_recovery_permille){
        const odg_item_definition *seed_def=odg_item_definition_internal(flora->seed_item_type);
        if(seed_def==NULL)return 0;
        seed.type_id=flora->seed_item_type;seed.quantity=1u;seed.flags=seed_def->flags;
        seed.material_tier=seed_def->default_material_tier;seed.payload_id=(uint64_t)flora->species_id;
        if(!odg_item_stack_normalize_internal(&seed))return 0;
        if(!odg_inventory_add(&staged_inventory,&seed))seed_to_drop=1;
    }
    if(seed_to_drop&&!odg_spawn_world_pickup(&seed,actor->x,actor->z,ODG_MANUAL_DROP_REPICKUP_TICKS))return 0;

    actor->inventory=staged_inventory;g_odg.ecology_rng=staged_rng;
    new_satiety=actor->satiety_permille+food->satiety_restore;
    actor->satiety_permille=new_satiety>ODG_ACTOR_SATIETY_MAX?ODG_ACTOR_SATIETY_MAX:new_satiety;
    if(food->hydration_restore>0u){
        uint32_t hydration=actor->hydration_permille+food->hydration_restore;
        actor->hydration_permille=hydration>ODG_ACTOR_HYDRATION_MAX?ODG_ACTOR_HYDRATION_MAX:hydration;
        actor->dehydration_accum=0u;
    }
    if(food->heal_amount>0u && actor->hp<actor->max_hp){
        uint32_t hp=actor->hp+food->heal_amount;
        actor->hp=hp>actor->max_hp?actor->max_hp:hp;
    }
    return 1;
}

int odg_actor_drink_fluid_internal(uint32_t actor_id,uint32_t fluid_id,uint32_t available_units,uint32_t *out_used_units){
    odg_actor *actor;const odg_fluid_definition *fluid;uint32_t missing,per_unit,needed,used,restore;
    if(out_used_units!=NULL)*out_used_units=0u;
    if(actor_id>=ODG_MAX_ACTORS||available_units==0u)return 0;
    actor=&g_odg.actors[actor_id];
    if(!actor->active||actor->hp==0u||actor->hydration_permille>=ODG_ACTOR_HYDRATION_MAX)return 0;
    fluid=odg_fluid_definition_internal(fluid_id);
    if(fluid==NULL||(fluid->flags&ODG_FLUID_FLAG_POTABLE)==0u||fluid->hydration_restore_per_unit==0u)return 0;
    missing=ODG_ACTOR_HYDRATION_MAX-actor->hydration_permille;per_unit=fluid->hydration_restore_per_unit;
    needed=(missing+per_unit-1u)/per_unit;used=available_units<needed?available_units:needed;
    restore=used*per_unit;actor->hydration_permille=actor->hydration_permille+restore>ODG_ACTOR_HYDRATION_MAX?ODG_ACTOR_HYDRATION_MAX:actor->hydration_permille+restore;
    actor->dehydration_accum=0u;if(out_used_units!=NULL)*out_used_units=used;return used!=0u;
}

int odg_actor_drink_selected_internal(uint32_t actor_id){
    odg_actor *actor;odg_item_stack *stack;const odg_fluid_container_definition *container;const odg_fluid_definition *fluid;uint32_t fluid_id,units,used=0u;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];
    stack=odg_inventory_selected(&actor->inventory);
    if(stack==NULL||stack->quantity==0u)return 0;
    container=odg_fluid_container_definition_internal(stack->type_id);
    if(container==NULL)return 0;
    fluid_id=odg_fluid_payload_id_internal(stack->payload_id);units=odg_fluid_payload_units_internal(stack->payload_id);fluid=odg_fluid_definition_internal(fluid_id);
    if(fluid==NULL||units==0u||(fluid->flags&container->accepted_fluid_flags)==0u)return 0;
    if(!odg_actor_drink_fluid_internal(actor_id,fluid_id,units,&used)||used==0u)return 0;
    units-=used;stack->payload_id=odg_fluid_payload_make_internal(units==0u?ODG_FLUID_NONE:fluid_id,units);return 1;
}

void odg_nutrition_tick(void) {
    uint32_t i;
    for(i=0u;i<ODG_MAX_ACTORS;++i){
        odg_actor *actor=&g_odg.actors[i];
        uint32_t food_slot;
        if(!actor->active||actor->hp==0u)continue;
        if(++actor->satiety_decay_accum>=ODG_ACTOR_SATIETY_DECAY_TICKS){
            actor->satiety_decay_accum=0u;
            if(actor->satiety_permille>0u)--actor->satiety_permille;
        }
        if(++actor->hydration_decay_accum>=ODG_ACTOR_HYDRATION_DECAY_TICKS){
            actor->hydration_decay_accum=0u;
            if(actor->hydration_permille>0u)--actor->hydration_permille;
        }
        if(actor->type==ODG_ACTOR_BOT && actor->satiety_permille<520u && food_slot_for_actor(actor,&food_slot))
            (void)odg_actor_consume_food_internal(i,food_slot);
        if(actor->satiety_permille==0u){
            if(++actor->starvation_accum>=ODG_STARVATION_DAMAGE_TICKS){
                actor->starvation_accum=0u;
                odg_actor_apply_damage_internal(i,UINT32_MAX,1u,ODG_DEATH_STARVATION);
            }
        }else actor->starvation_accum=0u;
        if(actor->hydration_permille==0u){
            if(++actor->dehydration_accum>=ODG_DEHYDRATION_DAMAGE_TICKS){
                actor->dehydration_accum=0u;
                odg_actor_apply_damage_internal(i,UINT32_MAX,1u,ODG_DEATH_DEHYDRATION);
            }
        }else actor->dehydration_accum=0u;
    }
}

uint32_t odg_player_satiety_permille(void){
    return g_odg.initialized?g_odg.actors[ODG_PLAYER_ID].satiety_permille:0u;
}
uint32_t odg_player_hydration_permille(void){
    return g_odg.initialized?g_odg.actors[ODG_PLAYER_ID].hydration_permille:0u;
}
uint32_t odg_player_trail_broken(void){
    return g_odg.initialized?g_odg.actors[ODG_PLAYER_ID].trail_broken:0u;
}
