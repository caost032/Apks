#include "game_internal.h"

#include <stdint.h>

/* Fluid identity is independent from the item that contains it.  payload_id is a stable
 * compact pair: high 32 bits fluid ID, low 32 bits amount.  Empty containers use zero. */
static const odg_fluid_definition g_fluids[] = {
    {sizeof(odg_fluid_definition),ODG_FLUID_WATER,1601u,10u,
     ODG_FLUID_FLAG_POTABLE|ODG_FLUID_FLAG_IRRIGATION|ODG_FLUID_FLAG_RAIN_SOURCE|ODG_FLUID_FLAG_NATURAL_SOURCE,
     {0u,0u,0u}}
};

static const odg_fluid_container_definition g_containers[] = {
    {sizeof(odg_fluid_container_definition),ODG_ITEM_WATER_FLASK,100u,
     ODG_FLUID_FLAG_POTABLE|ODG_FLUID_FLAG_IRRIGATION,
     ODG_FLUID_CONTAINER_FLAG_PORTABLE|ODG_FLUID_CONTAINER_FLAG_SEALED,{0u,0u,0u}}
};

const odg_fluid_definition *odg_fluid_definition_internal(uint32_t fluid_id){
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_fluids)/sizeof(g_fluids[0]));++i)
        if(g_fluids[i].fluid_id==fluid_id)return &g_fluids[i];
    return NULL;
}

const odg_fluid_container_definition *odg_fluid_container_definition_internal(uint32_t item_type){
    uint32_t i;
    for(i=0u;i<(uint32_t)(sizeof(g_containers)/sizeof(g_containers[0]));++i)
        if(g_containers[i].item_type==item_type)return &g_containers[i];
    return NULL;
}

uint32_t odg_fluid_payload_id_internal(uint64_t payload_id){return (uint32_t)(payload_id>>32u);}
uint32_t odg_fluid_payload_units_internal(uint64_t payload_id){return (uint32_t)(payload_id&UINT64_C(0xffffffff));}
uint64_t odg_fluid_payload_make_internal(uint32_t fluid_id,uint32_t units){
    if(fluid_id==ODG_FLUID_NONE||units==0u)return UINT64_C(0);
    return ((uint64_t)fluid_id<<32u)|(uint64_t)units;
}

uint32_t odg_fluid_definition_count(void){return (uint32_t)(sizeof(g_fluids)/sizeof(g_fluids[0]));}
int32_t odg_fluid_definition_get(uint32_t index,odg_fluid_definition *out_definition,
                                 uint64_t capacity,uint64_t *out_required){
    if(out_required!=NULL)*out_required=(uint64_t)sizeof(odg_fluid_definition);
    if(index>=odg_fluid_definition_count())return ODG_STATUS_INVALID_ARGUMENT;
    if(out_definition==NULL||capacity<(uint64_t)sizeof(*out_definition))return ODG_STATUS_BUFFER_TOO_SMALL;
    *out_definition=g_fluids[index];return ODG_STATUS_OK;
}

uint32_t odg_fluid_container_definition_count(void){return (uint32_t)(sizeof(g_containers)/sizeof(g_containers[0]));}
int32_t odg_fluid_container_definition_get(uint32_t index,odg_fluid_container_definition *out_definition,
                                           uint64_t capacity,uint64_t *out_required){
    if(out_required!=NULL)*out_required=(uint64_t)sizeof(odg_fluid_container_definition);
    if(index>=odg_fluid_container_definition_count())return ODG_STATUS_INVALID_ARGUMENT;
    if(out_definition==NULL||capacity<(uint64_t)sizeof(*out_definition))return ODG_STATUS_BUFFER_TOO_SMALL;
    *out_definition=g_containers[index];return ODG_STATUS_OK;
}
