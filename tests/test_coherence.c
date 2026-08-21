#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

static int failures=0;
#define CHECK(expr) do { if(!(expr)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);++failures;} } while(0)

static void mark_recipe_flows(uint8_t *source,uint8_t *sink){
    uint32_t i,j;
    for(i=1u;i<=odg_recipe_count();++i){
        odg_recipe_definition r;uint64_t required=0u;
        CHECK(odg_recipe_get(i,&r,sizeof(r),&required)==ODG_STATUS_OK);
        if(r.output_item_type<ODG_ITEM_TYPE_COUNT)source[r.output_item_type]=1u;
        if(r.station_item_type<ODG_ITEM_TYPE_COUNT)sink[r.station_item_type]=1u;
        for(j=0u;j<r.ingredient_count;++j){
            if(r.ingredients[j].item_type<ODG_ITEM_TYPE_COUNT)sink[r.ingredients[j].item_type]=1u;
        }
    }
}

static void mark_world_sources(uint8_t *source){
    uint32_t i,j;
    const uint32_t deposit_kinds[]={ODG_RESOURCE_STONE,ODG_RESOURCE_IRON,ODG_RESOURCE_COAL};
    for(i=0u;i<(uint32_t)(sizeof(deposit_kinds)/sizeof(deposit_kinds[0]));++i){
        odg_resource_node r;uint32_t item;
        odg_memset(&r,0,sizeof(r));r.kind=deposit_kinds[i];
        item=odg_resource_harvest_item_type_internal(&r);
        CHECK(item!=ODG_ITEM_NONE&&item<ODG_ITEM_TYPE_COUNT);
        if(item<ODG_ITEM_TYPE_COUNT)source[item]=1u;
    }
    for(i=0u;i<odg_flora_species_count();++i){
        odg_flora_species_definition f;uint64_t required=0u;
        CHECK(odg_flora_species_get(i,&f,sizeof(f),&required)==ODG_STATUS_OK);
        if(f.harvest_item_type<ODG_ITEM_TYPE_COUNT)source[f.harvest_item_type]=1u;
        if(f.fruit_item_type<ODG_ITEM_TYPE_COUNT)source[f.fruit_item_type]=1u;
        if(f.seed_item_type<ODG_ITEM_TYPE_COUNT)source[f.seed_item_type]=1u;
    }
    for(i=0u;i<odg_loot_table_count();++i){
        odg_loot_table_definition table;uint64_t required=0u;
        CHECK(odg_loot_table_get(i,&table,sizeof(table),&required)==ODG_STATUS_OK);
        for(j=0u;j<table.entry_count;++j){
            if(table.entries[j].item_type<ODG_ITEM_TYPE_COUNT)source[table.entries[j].item_type]=1u;
        }
    }
    /* Workbench is the one deliberate progression bootstrap: every actor begins with a
     * protected physical bench so the first craft does not depend on an impossible
     * pre-existing station. Any future bootstrap item must be declared deliberately. */
    source[ODG_ITEM_WORKBENCH]=1u;
}

static void mark_capability_sinks(uint8_t *sink){
    uint32_t i;
    for(i=1u;i<ODG_ITEM_TYPE_COUNT;++i){
        const odg_item_definition *d=odg_item_definition_internal(i);
        CHECK(d!=NULL);
        if(d!=NULL&&d->capability_bits!=0u)sink[i]=1u;
    }
}

static void check_item_cycles(void){
    uint8_t source[ODG_ITEM_TYPE_COUNT]={0};
    uint8_t sink[ODG_ITEM_TYPE_COUNT]={0};
    uint32_t i;
    const uint32_t wear_caps=ODG_ITEM_CAP_HARVEST|ODG_ITEM_CAP_MINE|ODG_ITEM_CAP_HUNT|ODG_ITEM_CAP_ATTACK;
    mark_recipe_flows(source,sink);
    mark_world_sources(source);
    mark_capability_sinks(sink);
    for(i=1u;i<ODG_ITEM_TYPE_COUNT;++i){
        const odg_item_definition *d=odg_item_definition_internal(i);
        if(!source[i]){fprintf(stderr,"ORPHAN SOURCE item=%u display=%u\n",i,d!=NULL?d->display_code:0u);++failures;}
        if(!sink[i]){fprintf(stderr,"ORPHAN SINK item=%u display=%u\n",i,d!=NULL?d->display_code:0u);++failures;}
        if(d!=NULL&&(d->flags&ODG_ITEM_FLAG_DURABILITY)!=0u){
            if(d->base_durability==0u||(d->capability_bits&wear_caps)==0u){
                fprintf(stderr,"MEANINGLESS DURABILITY item=%u caps=%u base=%u\n",i,d->capability_bits,d->base_durability);
                ++failures;
            }
        }else if(d!=NULL&&d->base_durability!=0u){
            fprintf(stderr,"UNFLAGGED DURABILITY item=%u base=%u\n",i,d->base_durability);++failures;
        }
    }
}

static void check_food_expiry(void){
    uint32_t i;
    for(i=0u;i<odg_food_definition_count();++i){
        odg_food_definition food;uint64_t required=0u;
        CHECK(odg_food_definition_get(i,&food,sizeof(food),&required)==ODG_STATUS_OK);
        CHECK(food.ground_lifetime_ticks>0u);
    }
    {
        odg_item_stack stack;uint32_t before,index;
        odg_memset(&stack,0,sizeof(stack));stack.type_id=ODG_ITEM_RAW_FISH;stack.quantity=1u;
        before=g_odg.pickup_count;
        CHECK(odg_spawn_world_pickup(&stack,40*ODG_FX_ONE,40*ODG_FX_ONE,UINT32_MAX));
        CHECK(g_odg.pickup_count>=before+1u);
        index=g_odg.pickup_count-1u;
        CHECK(g_odg_pickups[index].lifetime_ticks>0u);
        g_odg_pickups[index].age_ticks=g_odg_pickups[index].lifetime_ticks-1u;
        odg_update_world_pickups();
        CHECK(g_odg_pickups[index].active==0u);
    }
}

static void check_authority_boundaries(void){
    const odg_item_definition *block=odg_item_definition_internal(ODG_ITEM_BUILDING_BLOCK);
    CHECK(block!=NULL);
    CHECK((block->capability_bits&ODG_ITEM_CAP_CONSTRUCT)!=0u);
    CHECK((block->flags&ODG_ITEM_FLAG_ARTIFACT)==0u);
    CHECK(!odg_artifact_item_deployable_internal(ODG_ITEM_BUILDING_BLOCK));
    CHECK(sizeof(odg_construction_block)*8u<sizeof(odg_artifact));
    CHECK(odg_recipe_profiles_validate_internal());
    CHECK(odg_construction_profiles_validate_internal());
    CHECK(odg_artifact_profiles_validate_internal());
}

int main(void){
    CHECK(odg_init(UINT64_C(0x434f484552454e43),720u,1280u)==ODG_STATUS_OK);
    check_item_cycles();
    check_food_expiry();
    check_authority_boundaries();
    if(failures!=0){fprintf(stderr,"COHERENCE FAILED failures=%d\n",failures);return 1;}
    printf("COHERENCE OK items=%u sources+sinks=closed repair=capability-driven ground-food=expires construction=lightweight\n",odg_item_definition_count());
    return 0;
}
