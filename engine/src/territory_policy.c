#include "game_internal.h"

/* Territory is an access policy, not ownership of nature. Flora/fauna keep their own
 * identity; the cell owner only decides whether a foreign actor may exploit them. */
int odg_territory_allows_environment_action(uint32_t actor_id,int32_t x,int32_t z) {
    int64_t gx,gz;
    uint8_t owner;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
    owner=odg_chunk_owner_at_global_cell(gx,gz);
    return owner==ODG_OWNER_NONE || owner==ODG_OWNER_FROM_ID(actor_id);
}

int odg_territory_actor_controls_position(uint32_t actor_id,int32_t x,int32_t z) {
    int64_t gx,gz;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
    return odg_chunk_owner_at_global_cell(gx,gz)==ODG_OWNER_FROM_ID(actor_id);
}
