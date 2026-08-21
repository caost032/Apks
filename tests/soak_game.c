#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

#ifndef ODG_SOAK_TICKS
#define ODG_SOAK_TICKS UINT32_C(60000)
#endif

static uint32_t rng32(uint32_t *s) { uint32_t x=*s; x^=x<<13; x^=x>>17; x^=x<<5; *s=x; return x; }

static int check_stack(const odg_item_stack *stack) {
    const odg_item_definition *definition;
    if (stack->type_id==ODG_ITEM_NONE || stack->quantity==0u) {
        return stack->type_id==ODG_ITEM_NONE && stack->quantity==0u;
    }
    definition=odg_item_definition_internal(stack->type_id);
    if (definition==NULL || definition->type_id==ODG_ITEM_NONE || definition->max_stack==0u) return 0;
    if (stack->quantity>definition->max_stack) return 0;
    if (stack->material_tier>ODG_MATERIAL_IRON) return 0;
    if ((definition->flags&ODG_ITEM_FLAG_DURABILITY)!=0u) {
        if (stack->max_durability==0u || stack->durability>stack->max_durability) return 0;
    } else if (stack->durability!=0u || stack->max_durability!=0u) return 0;
    if (definition->max_stack>1u && (stack->instance_id!=0u || stack->payload_id!=0u)) return 0;
    if (definition->max_stack==1u &&
        (stack->instance_id==0u || (stack->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u)) return 0;
    return 1;
}

static int check_inventory(const odg_inventory *inventory) {
    uint32_t i,capacity=odg_inventory_capacity(inventory);
    if (capacity!=ODG_INVENTORY_BASE_SLOTS && capacity!=ODG_INVENTORY_MAX_SLOTS) return 0;
    if (inventory->selected_slot>=capacity) return 0;
    if (capacity==ODG_INVENTORY_MAX_SLOTS && inventory->equipped_backpack_type!=ODG_ITEM_BACKPACK) return 0;
    if (capacity==ODG_INVENTORY_BASE_SLOTS && inventory->equipped_backpack_type!=ODG_ITEM_NONE) return 0;
    for (i=0u;i<ODG_INVENTORY_MAX_SLOTS;++i) {
        if (i>=capacity) {
            if (inventory->slots[i].type_id!=ODG_ITEM_NONE || inventory->slots[i].quantity!=0u) return 0;
        } else if (!check_stack(&inventory->slots[i])) return 0;
    }
    return 1;
}

static int check_world(void) {
    uint32_t territory_counts[ODG_MAX_ACTORS]={0u};
    uint32_t trail_counts[ODG_MAX_ACTORS]={0u};
    uint32_t playable=0u;
    uint32_t i;
    if(g_odg.match_over!=0u||g_odg.winner_id!=UINT32_MAX)return 0;
    for(i=0u;i<ODG_CELL_COUNT;++i){
        uint8_t o=g_odg.territory[i],t=g_odg.trail_owner[i];
        if(g_odg.playable[i])++playable;
        if(o!=ODG_OWNER_NONE&&ODG_ID_FROM_OWNER(o)>=ODG_MAX_ACTORS)return 0;
        if(t!=ODG_OWNER_NONE&&ODG_ID_FROM_OWNER(t)>=ODG_MAX_ACTORS)return 0;
    }
    /* Open Domain's resident grid is an addressable precision window, not a land mask. */
    if(playable!=g_odg.playable_count||playable!=ODG_CELL_COUNT)return 0;
    for(i=0u;i<g_odg.chunk_cache_used;++i){
        const odg_chunk_runtime *record=&g_odg_chunk_cache[i];uint32_t ordinal;
        if(record->used==0u)continue;
        for(ordinal=0u;ordinal<ODG_CHUNK_CELL_COUNT;++ordinal){
            uint8_t owner=odg_chunk_runtime_owner_at_ordinal_internal(record,ordinal);
            uint8_t trail=odg_chunk_runtime_trail_at_ordinal_internal(record,ordinal);
            if(owner!=ODG_OWNER_NONE){uint32_t id=ODG_ID_FROM_OWNER(owner);if(id>=ODG_MAX_ACTORS)return 0;++territory_counts[id];}
            if(trail!=ODG_OWNER_NONE){uint32_t id=ODG_ID_FROM_OWNER(trail);if(id>=ODG_MAX_ACTORS)return 0;++trail_counts[id];}
        }
    }
    for(i=0u;i<ODG_MAX_ACTORS;++i){
        odg_actor *a=&g_odg.actors[i];
        if(!a->active||a->max_hp==0u||a->hp>a->max_hp)return 0;
        if(!check_inventory(&a->inventory))return 0;
        if(territory_counts[i]!=g_odg.territory_count[i]||trail_counts[i]!=a->trail_len)return 0;
        if(a->trail_len!=0u&&a->trail_active==0u)return 0;
        if(a->trail_active!=0u){
            if(a->hp==0u||a->trail_head_global_cell_x==INT64_MIN||a->trail_head_global_cell_z==INT64_MIN||
               a->trail_path_len==0u||a->trail_path_len>ODG_MAX_TRAIL_PATH_POINTS)return 0;
        }else if(a->trail_len!=0u||a->trail_head_global_cell_x!=INT64_MIN||a->trail_head_global_cell_z!=INT64_MIN||
                 a->trail_path_len!=0u)return 0;
        if(a->hp==0u){
            if(a->trail_active!=0u||a->trail_len!=0u||a->respawn_ticks==0u||a->respawn_ticks>3u*ODG_TICK_RATE)return 0;
        }else{
            if(a->respawn_ticks!=0u)return 0;
            if(g_odg.territory_count[i]==0u&&a->territory_recovery_ticks>ODG_TERRITORY_RECOVERY_DELAY_TICKS)return 0;
        }
    }
    for(i=0u;i<g_odg.turret_count;++i){
        odg_turret *t=&g_odg_turrets[i];
        if((t->active||t->procedural!=0u||t->instance_id!=0u)&&
           (t->id!=i||t->instance_id==0u||
            ((t->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u)!=(t->procedural!=0u)))return 0;
        if(!t->active){if(t->target_kind!=ODG_TURRET_TARGET_NONE||t->aim_ticks!=0u)return 0;continue;}
        if(t->material_tier<ODG_MATERIAL_WOOD||t->material_tier>ODG_MATERIAL_IRON)return 0;
        if(t->ammo>t->max_ammo||t->max_ammo==0u||t->range_fx<=0||t->fire_period==0u||t->aim_required==0u)return 0;
        if(t->target_kind>ODG_TURRET_TARGET_RESOURCE)return 0;
        if(t->target_kind==ODG_TURRET_TARGET_NONE&&t->aim_ticks!=0u)return 0;
        if(t->aim_ticks>t->aim_required)return 0;
        if(t->owner!=ODG_TURRET_NEUTRAL&&ODG_ID_FROM_OWNER(t->owner)>=ODG_MAX_ACTORS)return 0;
    }
    if(g_odg.turret_count>g_odg_turret_capacity||g_odg.pickup_count>g_odg_pickup_capacity||
       g_odg.resource_count>g_odg_resource_capacity||g_odg.artifact_count>g_odg_artifact_capacity||
       g_odg_construction_count>g_odg_construction_capacity)return 0;
    for(i=0u;i<g_odg.pickup_count;++i){
        odg_world_pickup *pickup=&g_odg_pickups[i];
        if(!pickup->active)continue;
        if(pickup->id!=i||!check_stack(&pickup->stack))return 0;
    }
    for(i=0u;i<g_odg.resource_count;++i){
        odg_resource_node *resource=&g_odg_resources[i];
        if(!resource->active)continue;
        if(resource->id!=i||resource->stable_id==0u||resource->kind<ODG_RESOURCE_TREE||resource->kind>ODG_RESOURCE_COAL)return 0;
        if(resource->state!=ODG_RESOURCE_STATE_AVAILABLE&&resource->state!=ODG_RESOURCE_STATE_DEPLETED)return 0;
        if(resource->state==ODG_RESOURCE_STATE_AVAILABLE&&(resource->yield_min==0u||resource->yield_min>resource->yield_max))return 0;
        if(resource->harvest_actor!=UINT32_MAX&&resource->harvest_actor>=ODG_MAX_ACTORS)return 0;
        {
            int64_t gx,gz;odg_surface_sample surface;uint64_t required=0u;
            odg_global_fx_to_global_cell_internal(resource->global_fx_x,resource->global_fx_z,&gx,&gz);
            if(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)!=ODG_STATUS_OK)return 0;
            if((surface.flags&ODG_SURFACE_FLAG_WATER)!=0u)return 0;
            if(resource->kind!=ODG_RESOURCE_IRON&&resource->kind!=ODG_RESOURCE_COAL&&
               (surface.flags&ODG_SURFACE_FLAG_STEEP)!=0u)return 0;
        }
    }
    for(i=0u;i<g_odg.artifact_count;++i){
        odg_artifact *artifact=&g_odg_artifacts[i];uint32_t j;const odg_item_definition *item;
        if(!artifact->active)continue;
        item=odg_item_definition_internal(artifact->item_type);
        if(artifact->id!=i||artifact->instance_id==0u||
           (artifact->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u||
           artifact->owner_actor_id>=ODG_MAX_ACTORS||item==NULL||artifact->capability_bits==0u)return 0;
        if((item->flags&ODG_ITEM_FLAG_ARTIFACT)==0u&&!odg_item_inventory_expander_recovery_internal(artifact->item_type))return 0;
        for(j=0u;j<ODG_CHEST_SLOTS;++j)if(!check_stack(&artifact->storage.slots[j]))return 0;
    }
    for(i=0u;i<g_odg_construction_count;++i){
        odg_construction_block *block=&g_odg_construction_blocks[i];
        if(!block->active)continue;
        if(block->id!=i||block->instance_id==0u||
           (block->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u||block->owner_actor_id>=ODG_MAX_ACTORS||
           block->material_tier<ODG_MATERIAL_WOOD||block->material_tier>ODG_MATERIAL_IRON||
           block->shape<ODG_CONSTRUCTION_SHAPE_FLOOR||block->shape>ODG_CONSTRUCTION_SHAPE_ROOF||
           block->max_health==0u||block->health==0u||block->health>block->max_health||block->reserved_u32!=0u)return 0;
    }
    return odg_content_registry_validate()!=0&&odg_turret_profiles_validate_internal()!=0;
}

int main(void){
    uint32_t r=0x1234abcdu,i;
    if(odg_init(UINT64_C(0xf00dcafe12345678),480u,270u)!=0)return 2;
    if(!check_world()) {fprintf(stderr,"initial invariant failed\n");return 4;}
    for(i=0u;i<ODG_SOAK_TICKS;++i){
        int32_t mx=(int32_t)(rng32(&r)&65535u)-32768;
        int32_t mz=(int32_t)(rng32(&r)&65535u)-32768;
        uint32_t buttons=0u;
        if((rng32(&r)&511u)==0u) buttons|=ODG_BUTTON_DASH;
        if((rng32(&r)&2047u)==0u) buttons|=ODG_BUTTON_INTERACT;
        if((rng32(&r)&4095u)==0u) buttons|=ODG_BUTTON_DROP;
        odg_set_input(mx,mz,0,0,buttons); odg_step_ticks(1u);
        if(odg_match_over()!=0u){fprintf(stderr,"open-domain match_over tick=%u\n",i);return 5;}
        if((i%120u)==0u){
            uint32_t mode=(i/120u)%3u;
            if(mode==0u)(void)odg_resize(320u,180u); else if(mode==1u)(void)odg_resize(480u,270u); else (void)odg_resize(640u,360u);
            if(odg_render_frame()==(uintptr_t)0)return 3;
            if(!check_world()){fprintf(stderr,"invariant failed tick=%u\n",i);return 4;}
        }
    }
    printf("SOAK OPEN-DOMAIN OK ticks=%u hash=%016llx cells=%u alive=%u turrets=%u resources=%u artifacts=%u pickups=%u\n",
           ODG_SOAK_TICKS,           (unsigned long long)odg_state_hash(),odg_player_territory_cells(),odg_alive_count(),g_odg.turret_count,
           g_odg.resource_count,g_odg.artifact_count,g_odg.pickup_count);
    return 0;
}
