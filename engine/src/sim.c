#include "game_internal.h"

#include <stdint.h>

static const int32_t dir16[16][2] = {
    { 32767,     0}, { 30273, 12539}, { 23170, 23170}, { 12539, 30273},
    {     0, 32767}, {-12539, 30273}, {-23170, 23170}, {-30273, 12539},
    {-32767,     0}, {-30273,-12539}, {-23170,-23170}, {-12539,-30273},
    {     0,-32767}, { 12539,-30273}, { 23170,-23170}, { 30273,-12539}
};

void odg_update_turret_ownership_internal(void);
static void turret_clear_target(odg_turret *t);
static void set_territory_global(int64_t gx,int64_t gz,uint8_t owner);
static int global_cell_center_local_fx(int64_t gx,int64_t gz,int32_t *out_x,int32_t *out_z);

static uint32_t cell_x(uint32_t cell) { return cell & (ODG_GRID_SIZE - 1u); }
static uint32_t cell_z(uint32_t cell) { return cell >> ODG_GRID_SHIFT; }


static void initialize_open_domain_compat_mask(void) {
    /* SAVE compatibility tombstone only. Open Domain has no playable-mask authority:
     * every resident cell maps to valid global terrain. Keep these bytes canonical so old
     * raw layouts remain stable, but gameplay/navigation must never branch on them. */
    g_odg.playable_count=ODG_CELL_COUNT;
    odg_memset(g_odg.playable,1,sizeof(g_odg.playable));
}

static int32_t approach_signed(int32_t current, int32_t target, int32_t step) {
    if (current < target) {
        current += step;
        if (current > target) current = target;
    } else if (current > target) {
        current -= step;
        if (current < target) current = target;
    }
    return current;
}

/* Deterministic angular inertia. The old constant-angle turn made every actor feel like
 * a token rotating on a table. Here steering first builds angular velocity, then bleeds
 * it away as the body converges on the requested heading. Translation remains aligned
 * to the physical forward vector, so arcs are real rather than lateral sliding. */
static void rotate_vec_inertial_toward(int32_t *x, int32_t *z, int32_t tx, int32_t tz,
                                       int32_t *turn_rate_q15, int32_t max_sin_q15,
                                       int32_t accel_q15, int32_t *fallback_sign) {
    int32_t dot;
    int32_t cross;
    int32_t sign;
    int32_t error_mag;
    int32_t desired_rate;
    int32_t rate;
    int32_t sin_step;
    int32_t cos_step;
    int32_t nx;
    int32_t nz;
    int32_t new_cross;
    if (!x || !z || !turn_rate_q15 || (tx == 0 && tz == 0)) return;
    dot = (int32_t)(((int64_t)(*x) * tx + (int64_t)(*z) * tz) / ODG_Q15_ONE);
    dot = odg_clamp_i32(dot, -ODG_Q15_ONE, ODG_Q15_ONE);
    cross = (int32_t)(((int64_t)(*x) * tz - (int64_t)(*z) * tx) / ODG_Q15_ONE);
    cross = odg_clamp_i32(cross, -ODG_Q15_ONE, ODG_Q15_ONE);

    if (dot > 32754 && odg_abs_i32(cross) < 920) {
        *x = tx;
        *z = tz;
        *turn_rate_q15 = approach_signed(*turn_rate_q15, 0, accel_q15);
        return;
    }

    if (cross > 0) sign = 1;
    else if (cross < 0) sign = -1;
    else sign = (fallback_sign && *fallback_sign < 0) ? -1 : 1;
    if (fallback_sign) *fallback_sign = sign;

    error_mag = odg_abs_i32(cross);
    /* At ~180 degrees the cross product approaches zero even though the heading error
     * is maximal. Keep a full-rate turn in that case using the remembered turn side. */
    if (dot < 0) {
        int32_t reverse_mag = (ODG_Q15_ONE - dot) / 2;
        if (reverse_mag > error_mag) error_mag = reverse_mag;
    }
    if (error_mag < 1800) error_mag = 1800;
    if (error_mag > ODG_Q15_ONE) error_mag = ODG_Q15_ONE;
    desired_rate = sign * error_mag;
    rate = approach_signed(*turn_rate_q15, desired_rate, accel_q15);
    *turn_rate_q15 = rate;

    sin_step = (int32_t)(((int64_t)max_sin_q15 * odg_abs_i32(rate)) / ODG_Q15_ONE);
    if (sin_step < 1) sin_step = 1;
    /* Small-angle cos approximation is deterministic and accurate at our <3 degree step. */
    cos_step = ODG_Q15_ONE - (int32_t)(((int64_t)sin_step * sin_step) / (2 * ODG_Q15_ONE));
    if (rate > 0) {
        nx = (int32_t)(((int64_t)(*x) * cos_step - (int64_t)(*z) * sin_step) / ODG_Q15_ONE);
        nz = (int32_t)(((int64_t)(*x) * sin_step + (int64_t)(*z) * cos_step) / ODG_Q15_ONE);
    } else {
        nx = (int32_t)(((int64_t)(*x) * cos_step + (int64_t)(*z) * sin_step) / ODG_Q15_ONE);
        nz = (int32_t)((-(int64_t)(*x) * sin_step + (int64_t)(*z) * cos_step) / ODG_Q15_ONE);
    }
    odg_normalize_q15(nx, nz, x, z);
    new_cross = (int32_t)(((int64_t)(*x) * tz - (int64_t)(*z) * tx) / ODG_Q15_ONE);
    /* Never orbit past the requested direction. */
    if ((cross > 0 && new_cross <= 0) || (cross < 0 && new_cross >= 0)) {
        *x = tx;
        *z = tz;
        *turn_rate_q15 = 0;
    }
}

uint32_t odg_cell_from_world(int32_t x, int32_t z) {
    int32_t cx = (x + ODG_WORLD_HALF_FX) / ODG_CELL_FX;
    int32_t cz = (z + ODG_WORLD_HALF_FX) / ODG_CELL_FX;
    cx = odg_clamp_i32(cx, 0, (int32_t)ODG_GRID_SIZE - 1);
    cz = odg_clamp_i32(cz, 0, (int32_t)ODG_GRID_SIZE - 1);
    return (uint32_t)cz * ODG_GRID_SIZE + (uint32_t)cx;
}

int32_t odg_cell_center_x(uint32_t cell) {
    int32_t cx = (int32_t)cell_x(cell);
    return (cx - (int32_t)(ODG_GRID_SIZE / 2u)) * ODG_CELL_FX + ODG_CELL_FX / 2;
}

int32_t odg_cell_center_z(uint32_t cell) {
    int32_t cz = (int32_t)cell_z(cell);
    return (cz - (int32_t)(ODG_GRID_SIZE / 2u)) * ODG_CELL_FX + ODG_CELL_FX / 2;
}

static int position_clear(int32_t x,int32_t z,int32_t radius) {
    return odg_position_clear_internal(x,z,radius);
}

static int camera_segment_clear(const odg_actor *player,int32_t dir_x_q15,int32_t dir_z_q15,int32_t distance_fx) {
    int32_t distance;
    if(player==NULL)return 1;
    /* Sample the entire chase ray, not only its endpoint. Camera geometry consumes the
     * same obstacle-overlap authority as gameplay collision. */
    for(distance=ODG_FX_ONE/2;distance<=distance_fx;distance+=ODG_FX_ONE/4){
        int32_t px=player->x-(int32_t)(((int64_t)dir_x_q15*distance)/ODG_Q15_ONE);
        int32_t pz=player->z-(int32_t)(((int64_t)dir_z_q15*distance)/ODG_Q15_ONE);
        uint32_t i;
        for(i=0u;i<g_odg.obstacle_count;++i){
            if(odg_world_circle_aabb_overlap_internal(px,pz,ODG_CAMERA_COLLISION_RADIUS_FX,&g_odg.obstacles[i]))return 0;
        }
    }
    return 1;
}

static int32_t actor_support_height_milli(const odg_surface_sample *surface){
    int64_t support;
    if(surface==NULL)return 0;
    support=surface->height_milli;
    if((surface->flags&ODG_SURFACE_FLAG_WATER)!=0u&&surface->water_depth_milli>=ODG_SWIM_MIN_DEPTH_MILLI){
        uint32_t lift=surface->water_depth_milli>ODG_SWIM_DRAFT_MILLI?
            surface->water_depth_milli-ODG_SWIM_DRAFT_MILLI:0u;
        support+=(int64_t)lift;
    }
    return support<INT32_MIN?INT32_MIN:(support>INT32_MAX?INT32_MAX:(int32_t)support);
}

static int actor_terrain_transition_clear(const odg_actor *a,int32_t x,int32_t z) {
    odg_surface_sample from,to;
    int32_t rise_milli;
    int32_t clearance_milli;
    int from_swim,to_swim;
    if (a==NULL) return 0;
    if (!odg_environment_surface_local(a->x,a->z,&from) || !odg_environment_surface_local(x,z,&to)) return 0;
    from_swim=(from.flags&ODG_SURFACE_FLAG_WATER)!=0u&&from.water_depth_milli>=ODG_SWIM_MIN_DEPTH_MILLI;
    to_swim=(to.flags&ODG_SURFACE_FLAG_WATER)!=0u&&to.water_depth_milli>=ODG_SWIM_MIN_DEPTH_MILLI;
    rise_milli=actor_support_height_milli(&to)-actor_support_height_milli(&from);
    /* Swimming compares against the water-supported body height, not the lake bottom.
     * Otherwise a perfectly ordinary bank looks like a multi-metre cliff and the actor
     * can enter deep water but can never climb back out. */
    if(from_swim||to_swim){
        if(rise_milli>ODG_SWIM_SHORE_STEP_MILLI)return 0;
        return 1;
    }
    clearance_milli=(int32_t)(((int64_t)a->vertical_offset_fx*1000)/(int64_t)ODG_FX_ONE);
    if (a->grounded!=0u) {
        if (rise_milli > (ODG_MAX_STEP_HEIGHT_FX*1000)/ODG_FX_ONE) return 0;
    } else if (rise_milli > clearance_milli + 80) {
        return 0;
    }
    return 1;
}

static int actor_position_clear(const odg_actor *a,int32_t x,int32_t z) {
    if (!a || !position_clear(x,z,a->radius)) return 0;
    /* Territory runners intentionally remain non-solid to one another: exposed-trail
     * topology owns PvP contact. Wildlife is different physical matter and must not be
     * traversed as if it were only a visual effect. */
    if(!odg_fauna_bodies_clear_internal(x,z,a->radius,UINT32_MAX))return 0;
    return actor_terrain_transition_clear(a,x,z);
}


static int bot_nav_edge_clear(uint32_t a,uint32_t b) {
    const uint32_t blocked_flags=ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP;
    const int32_t max_step_milli=(ODG_MAX_STEP_HEIGHT_FX*1000)/ODG_FX_ONE;
    odg_surface_sample from,mid,to;
    int32_t ax,az,bx,bz,mx,mz;
    if (a>=ODG_CELL_COUNT||b>=ODG_CELL_COUNT) return 0;
    ax=odg_cell_center_x(a);az=odg_cell_center_z(a);
    bx=odg_cell_center_x(b);bz=odg_cell_center_z(b);
    mx=(ax+bx)/2;mz=(az+bz)/2;
    /* The navigation graph is a terrain graph, not a snapshot of dynamic props. Trees,
     * turrets and artifacts are resolved by actor collision/steering at movement time.
     * This keeps the graph stable as props change while still deriving water/slope from
     * the same authoritative surface used by physics and fauna. */
    if(!odg_environment_surface_local(ax,az,&from)||
       !odg_environment_surface_local(mx,mz,&mid)||
       !odg_environment_surface_local(bx,bz,&to))return 0;
    if(((from.flags|mid.flags|to.flags)&blocked_flags)!=0u)return 0;
    if(odg_abs_i32(to.height_milli-from.height_milli)>max_step_milli)return 0;
    return 1;
}

uint8_t odg_bot_navigation_edge_mask_internal(uint32_t c) {
    uint32_t x,z;
    uint8_t bits=0u;
    if(c>=ODG_CELL_COUNT)return 0u;
    x=cell_x(c);z=cell_z(c);
    if(x>0u && bot_nav_edge_clear(c,c-1u))bits|=UINT8_C(1);
    if(x+1u<ODG_GRID_SIZE && bot_nav_edge_clear(c,c+1u))bits|=UINT8_C(2);
    if(z>0u && bot_nav_edge_clear(c,c-ODG_GRID_SIZE))bits|=UINT8_C(4);
    if(z+1u<ODG_GRID_SIZE && bot_nav_edge_clear(c,c+ODG_GRID_SIZE))bits|=UINT8_C(8);
    return bits;
}

void odg_bot_navigation_rebuild_internal(void) {
    static int32_t center_height_milli[ODG_CELL_COUNT];
    static uint8_t center_walkable[ODG_CELL_COUNT];
    const uint32_t blocked_flags=ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP;
    const int32_t max_step_milli=(ODG_MAX_STEP_HEIGHT_FX*1000)/ODG_FX_ONE;
    uint32_t c,x,z;
    /* Rebuild is derived data but runs on every floating-origin rebase. The old form
     * evaluated each undirected edge twice and re-sampled both endpoint surfaces for
     * every neighbour (~200k surface queries). Cache each cell center once, then sample
     * each unique midpoint once and write both directional bits symmetrically. */
    for(c=0u;c<ODG_CELL_COUNT;++c){
        odg_surface_sample sample;
        g_odg.bot_nav_edges[c]=0u;center_walkable[c]=0u;center_height_milli[c]=0;
        if(!odg_environment_surface_local(odg_cell_center_x(c),odg_cell_center_z(c),&sample))continue;
        center_height_milli[c]=sample.height_milli;
        if((sample.flags&blocked_flags)==0u)center_walkable[c]=1u;
    }
    for(z=0u;z<ODG_GRID_SIZE;++z)for(x=0u;x<ODG_GRID_SIZE;++x){
        odg_surface_sample mid;
        c=z*ODG_GRID_SIZE+x;
        if(center_walkable[c]==0u)continue;
        if(x+1u<ODG_GRID_SIZE){
            uint32_t n=c+1u;
            if(center_walkable[n]!=0u&&
               odg_abs_i32(center_height_milli[n]-center_height_milli[c])<=max_step_milli&&
               odg_environment_surface_local((odg_cell_center_x(c)+odg_cell_center_x(n))/2,
                                             (odg_cell_center_z(c)+odg_cell_center_z(n))/2,&mid)&&
               (mid.flags&blocked_flags)==0u){
                g_odg.bot_nav_edges[c]|=UINT8_C(2);g_odg.bot_nav_edges[n]|=UINT8_C(1);
            }
        }
        if(z+1u<ODG_GRID_SIZE){
            uint32_t n=c+ODG_GRID_SIZE;
            if(center_walkable[n]!=0u&&
               odg_abs_i32(center_height_milli[n]-center_height_milli[c])<=max_step_milli&&
               odg_environment_surface_local((odg_cell_center_x(c)+odg_cell_center_x(n))/2,
                                             (odg_cell_center_z(c)+odg_cell_center_z(n))/2,&mid)&&
               (mid.flags&blocked_flags)==0u){
                g_odg.bot_nav_edges[c]|=UINT8_C(8);g_odg.bot_nav_edges[n]|=UINT8_C(4);
            }
        }
    }
}

static void resolve_actor_obstacles(odg_actor *a) {
    uint32_t i;
    for (i = 0u; i < g_odg.obstacle_count; ++i) {
        const odg_obstacle *o = &g_odg.obstacles[i];
        int32_t minx = o->x - o->hx - a->radius;
        int32_t maxx = o->x + o->hx + a->radius;
        int32_t minz = o->z - o->hz - a->radius;
        int32_t maxz = o->z + o->hz + a->radius;
        if (a->x > minx && a->x < maxx && a->z > minz && a->z < maxz) {
            int32_t dl = a->x - minx;
            int32_t dr = maxx - a->x;
            int32_t db = a->z - minz;
            int32_t dt = maxz - a->z;
            int32_t m = dl;
            uint32_t side = 0u;
            if (dr < m) { m = dr; side = 1u; }
            if (db < m) { m = db; side = 2u; }
            if (dt < m) { side = 3u; }
            if (side == 0u) { a->x = minx; if (a->vx > 0) a->vx = 0; }
            else if (side == 1u) { a->x = maxx; if (a->vx < 0) a->vx = 0; }
            else if (side == 2u) { a->z = minz; if (a->vz > 0) a->vz = 0; }
            else { a->z = maxz; if (a->vz < 0) a->vz = 0; }
        }
    }
}

static void sync_actor_score(uint32_t id) {
    odg_actor *a;
    uint32_t tier;
    if (id >= ODG_MAX_ACTORS) return;
    a = &g_odg.actors[id];
    a->score = g_odg.territory_count[id];
    tier = 1u + a->score / 64u;
    a->level = tier > 20u ? 20u : tier;
}

static void set_territory_owner(uint32_t cell, uint8_t owner) {
    int64_t gx,gz;
    if (cell >= ODG_CELL_COUNT) return;
    gx=g_odg.world_origin_cell_x+(int64_t)cell_x(cell);
    gz=g_odg.world_origin_cell_z+(int64_t)cell_z(cell);
    odg_chunk_set_owner_at_global_cell(gx,gz,owner);
}

static void stamp_initial_territory(odg_actor *a) {
    uint32_t center = a->home_cell;
    int32_t cx = (int32_t)cell_x(center);
    int32_t cz = (int32_t)cell_z(center);
    int32_t dz;
    uint8_t owner = ODG_OWNER_FROM_ID(a->id);
    for (dz = -4; dz <= 4; ++dz) {
        int32_t dx;
        for (dx = -4; dx <= 4; ++dx) {
            int32_t x = cx + dx;
            int32_t z = cz + dz;
            if (x < 0 || z < 0 || x >= (int32_t)ODG_GRID_SIZE || z >= (int32_t)ODG_GRID_SIZE) continue;
            if (dx * dx + dz * dz > 16) continue;
            set_territory_owner((uint32_t)z * ODG_GRID_SIZE + (uint32_t)x, owner);
        }
    }
    sync_actor_score(a->id);
}

static int actor_spawn_surface_valid(int32_t x,int32_t z){
    return odg_world_position_safe_ground_internal(x,z);
}

static int actor_spawn_candidate_valid(const odg_actor *actor,uint32_t id,int32_t x,int32_t z,int aesthetic_spacing){
    uint32_t prior;
    if(actor==NULL)return 0;
    if(!actor_spawn_surface_valid(x,z))return 0;
    if(odg_chunk_procedural_turret_reserves_local_circle_internal(x,z,actor->radius+2*ODG_FX_ONE))return 0;
    if(!position_clear(x,z,actor->radius+2*ODG_FX_ONE))return 0;
    for(prior=0u;prior<id;++prior){
        const odg_actor *other=&g_odg.actors[prior];int32_t separation;
        if(!other->active)continue;
        separation=aesthetic_spacing!=0?17*ODG_FX_ONE:actor->radius+other->radius;
        if(odg_dist2(x,z,other->x,other->z)<(int64_t)separation*separation)return 0;
    }
    return 1;
}

static int spawn_actor(uint32_t id, uint32_t type) {
    odg_actor *a;
    odm_rng rng;
    int32_t x;
    int32_t z;
    if (id >= ODG_MAX_ACTORS) return 0;
    a = &g_odg.actors[id];
    (void)odm_rng_seed_derived(&rng, g_odg.seed, UINT64_C(0x5445525249544f52), id + 1u);
    odg_memset(a, 0, sizeof(*a));
    a->active = 1u;
    a->type = type;
    a->id = id;
    a->name_code = id;
    /* Collider tracks the compact cube silhouette.  The previous ~0.43 m radius was
     * inherited from the larger avatar and left an invisible halo after the graphics
     * scale pass.  Keep a tiny player margin, but make what the player sees authoritative. */
    a->radius = type == ODG_ACTOR_PLAYER ? 330 : 320;
    a->max_hp = 100u;
    a->hp = 100u;
    a->satiety_permille = ODG_ACTOR_SATIETY_MAX;
    a->hydration_permille = ODG_ACTOR_HYDRATION_MAX;
    a->grounded = 1u;
    a->level = 1u;
    a->turn_sign = (id & 1u) ? 1 : -1;
    odg_inventory_init(&a->inventory);
    a->trail_head_cell = UINT32_MAX;
    a->trail_render_anchor_cell = UINT32_MAX;
    a->ai_plan_cell = UINT32_MAX;
    a->ai_plan_global_cell_x=INT64_MIN;a->ai_plan_global_cell_z=INT64_MIN;
    a->rng = rng;
    {
        uint32_t fd = odg_rand_bounded(&a->rng, 16u);
        a->face_x_q15 = dir16[fd][0];
        a->face_z_q15 = dir16[fd][1];
    }
    {
        uint32_t attempt;
        int found = 0;
        x = 0; z = 0;
        for (attempt = 0u; attempt < 240u; ++attempt) {
            uint32_t cell = odg_rand_bounded(&a->rng, ODG_CELL_COUNT);
            x = odg_cell_center_x(cell);
            z = odg_cell_center_z(cell);
            if (actor_spawn_candidate_valid(a,id,x,z,1)) { found = 1; break; }
        }
        if (!found) {
            uint32_t start=odg_rand_bounded(&a->rng,ODG_CELL_COUNT);uint32_t pass;
            /* Exhaust the whole resident window before relaxing spawn quality. Pass 0
             * keeps the normal 17 m nation spacing; pass 1 relaxes only that aesthetic
             * margin and still requires strict physical non-overlap. */
            for(pass=0u;pass<2u&&!found;++pass){
                uint32_t scan;
                for(scan=0u;scan<ODG_CELL_COUNT;++scan){
                    uint32_t cell=(start+scan)%ODG_CELL_COUNT;
                    x=odg_cell_center_x(cell);z=odg_cell_center_z(cell);
                    if(actor_spawn_candidate_valid(a,id,x,z,pass==0u)){found=1;break;}
                }
            }
        }
        if(!found){a->active=0u;a->hp=0u;return 0;}
    }
    a->x = x;
    a->z = z;
    odg_local_fx_to_global_fx_internal(x,z,&a->global_fx_x,&a->global_fx_z);
    a->local_resident=1u;
    a->progress_x = x;
    a->progress_z = z;
    a->home_cell = odg_cell_from_world(x, z);
    a->home_global_cell_x = g_odg.world_origin_cell_x + (int64_t)cell_x(a->home_cell);
    a->home_global_cell_z = g_odg.world_origin_cell_z + (int64_t)cell_z(a->home_cell);
    a->last_global_cell_x=a->home_global_cell_x;a->last_global_cell_z=a->home_global_cell_z;
    a->trail_head_global_cell_x=INT64_MIN;a->trail_head_global_cell_z=INT64_MIN;
    a->last_cell = a->home_cell;
    a->bot_mode = ODG_BOT_INSIDE;
    a->think_cd = 14u + odg_rand_bounded(&a->rng, 30u);
    return 1;
}

static void clear_actor_trail(odg_actor *a) {
    uint32_t cell;
    uint8_t owner;
    if (!a) return;
    owner = ODG_OWNER_FROM_ID(a->id);
    /* Committed trail persists in the chunk ledger even when it left the current
     * precision window. Clearing therefore clears every resident chunk, not merely
     * what is currently visible. trail_broken is deliberately NOT changed here: a
     * successful capture clears normally, while an enemy cut calls break_actor_trail. */
    (void)cell;
    odg_chunk_clear_trail_owner(owner);
    a->trail_len = 0u;
    a->trail_active = 0u;
    a->trail_head_cell = UINT32_MAX;
    a->trail_render_anchor_cell = UINT32_MAX;
    a->trail_head_global_cell_x=INT64_MIN;a->trail_head_global_cell_z=INT64_MIN;
    a->trail_path_len = 0u;
}

static void break_actor_trail(odg_actor *victim,uint32_t cutter_id) {
    if(victim==NULL||!victim->active||victim->hp==0u)return;
    if(!victim->trail_active&&victim->trail_len==0u)return;
    clear_actor_trail(victim);
    victim->trail_broken=1u;
    victim->bot_mode=ODG_BOT_RETURN;
    victim->think_cd=1u;
    if(cutter_id<ODG_MAX_ACTORS&&cutter_id!=victim->id){
        /* Trail cutting is tactical disruption, not a kill. Keep kill/death counters
         * semantically honest and use a small visual response only. */
        odg_emit_particles(victim->x,victim->z,0xffb45bffu,10u);
    }
}

static void reset_actor_transient_motion(odg_actor *actor){
    if(actor==NULL)return;
    actor->vx=0;actor->vz=0;actor->speed_fx=0;actor->steer_q15=0;actor->turn_rate_q15=0;
    actor->control_raw_x_q15=0;actor->control_raw_z_q15=0;actor->dash_cd=0u;actor->dash_ticks=0u;
    actor->flash_ticks=0u;actor->melee_cooldown_ticks=0u;
    actor->slide_lock_ticks=0u;actor->slide_axis=0u;actor->slide_dir_x_q15=0;actor->slide_dir_z_q15=0;
    actor->vertical_offset_fx=0;actor->vertical_velocity_fx=0;actor->grounded=1u;
    actor->ai_x_q15=0;actor->ai_z_q15=0;actor->bot_out_x_q15=0;actor->bot_out_z_q15=0;
    actor->ai_commit_ticks=0u;actor->ai_plan_cell=UINT32_MAX;
    actor->ai_plan_global_cell_x=INT64_MIN;actor->ai_plan_global_cell_z=INT64_MIN;
    actor->progress_x=actor->x;actor->progress_z=actor->z;actor->progress_ticks=0u;actor->stuck_windows=0u;
}

static void eliminate_actor(odg_actor *victim, uint32_t killer_id, uint32_t reason) {
    uint32_t slot;
    uint32_t capacity;
    if (!victim || !victim->active || victim->hp == 0u) return;
    victim->hp = 0u;
    reset_actor_transient_motion(victim);
    victim->death_reason = reason;
    victim->respawn_ticks = 3u * ODG_TICK_RATE;
    victim->bot_economy_item_type=0u;
    victim->bot_economy_target_id=UINT32_MAX;
    if(victim->id==ODG_PLAYER_ID){
        g_odg.control_active=0u;g_odg.control_heading_x_q15=0;g_odg.control_heading_z_q15=0;g_odg.control_strength_q15=0;
    }
    ++victim->deaths;

    /* Equipped backpacks become long-lived recovery containers. Without a backpack we
     * preserve the legacy physical-drop fallback, so death never silently deletes cargo. */
    if(!odg_artifact_create_death_cache(victim->id)){
        uint32_t drop_count=0u;
        capacity=odg_inventory_capacity(&victim->inventory);
        for(slot=0u;slot<capacity;++slot){
            const odg_item_stack *stack=&victim->inventory.slots[slot];
            if(odg_item_stack_empty_internal(stack)||odg_item_stack_protected_internal(stack))continue;
            ++drop_count;
        }
        /* Death without recovery equipment is a batch transaction with respect to world
         * capacity. Reserve every required pickup slot before moving the first stack, so
         * allocation pressure cannot leave half the cargo on the ground and half hidden
         * on a dead actor merely because the Nth reserve failed. If reservation fails,
         * fail safe by preserving the complete inventory for the eventual respawn. */
        if(odg_world_pickups_prepare_internal(drop_count)){
            for (slot=0u;slot<capacity;++slot) {
                odg_item_stack *stack=&victim->inventory.slots[slot];
                if (odg_item_stack_empty_internal(stack)||odg_item_stack_protected_internal(stack))continue;
                (void)odg_drop_inventory_slot(victim->id,slot,stack->quantity,30u);
            }
        }
    }
    clear_actor_trail(victim);
    if (killer_id < ODG_MAX_ACTORS && killer_id != victim->id) {
        ++g_odg.actors[killer_id].kills;
        odg_emit_particles(victim->x, victim->z, 0xff6b7dffu, 18u);
    }
}

void odg_actor_apply_damage_internal(uint32_t victim_id,uint32_t killer_id,uint32_t damage,uint32_t reason) {
    odg_actor *victim;
    if(victim_id>=ODG_MAX_ACTORS||damage==0u)return;
    victim=&g_odg.actors[victim_id];
    if(!victim->active||victim->hp==0u)return;
    if(damage>=victim->hp){eliminate_actor(victim,killer_id,reason);return;}
    victim->hp-=damage;victim->flash_ticks=ODG_DAMAGE_FLASH_TICKS;
}

/* Open Domain never deletes a nation. If all of its territory was conquered, the
 * authoritative respawn path establishes a very small recovery enclave on neutral
 * playable ground. This is deliberately smaller than the initial nation footprint and
 * does not steal enemy territory while neutral land exists. */
static int recovery_neutral_safe_cell(const odg_actor *a,int64_t *out_gx,int64_t *out_gz){
    uint32_t radius;
    int64_t home_cx,home_cz,min_cx,max_cx,min_cz,max_cz;
    uint32_t i;
    if(a==NULL||out_gx==NULL||out_gz==NULL)return 0;
    /* Prefer a nearby neutral dry cell so defeat does not teleport a nation merely
     * because its exact old home was flooded/steep or conquered. */
    for(radius=0u;radius<=64u;++radius){
        int64_t r=(int64_t)radius,dz;
        for(dz=-r;dz<=r;++dz){
            int64_t dx;
            for(dx=-r;dx<=r;++dx){
                int64_t gx,gz;
                if(radius!=0u&&(dx!=-r&&dx!=r)&&(dz!=-r&&dz!=r))continue;
                gx=a->home_global_cell_x+dx;gz=a->home_global_cell_z+dz;
                if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_NONE)continue;
                if(!odg_world_cell_safe_ground_internal(gx,gz))continue;
                *out_gx=gx;*out_gz=gz;return 1;
            }
        }
    }
    /* Last-resort Open Domain search. Any owned/modified cell must have a runtime chunk
     * record. Search just outside the finite record bounding box; those chunks are
     * necessarily neutral, then require canonical dry/non-steep terrain before use. */
    home_cx=odg_floor_div_i64_internal(a->home_global_cell_x,(int64_t)ODG_CHUNK_SIZE_CELLS);
    home_cz=odg_floor_div_i64_internal(a->home_global_cell_z,(int64_t)ODG_CHUNK_SIZE_CELLS);
    min_cx=max_cx=home_cx;min_cz=max_cz=home_cz;
    for(i=0u;i<g_odg.chunk_cache_used;++i){
        const odg_chunk_runtime *record=&g_odg_chunk_cache[i];if(record->used==0u)continue;
        if(record->chunk_x<min_cx)min_cx=record->chunk_x;
        if(record->chunk_x>max_cx)max_cx=record->chunk_x;
        if(record->chunk_z<min_cz)min_cz=record->chunk_z;
        if(record->chunk_z>max_cz)max_cz=record->chunk_z;
    }
    for(radius=1u;radius<=64u;++radius){
        int64_t candidates[4][2]={{max_cx+(int64_t)radius,home_cz},{min_cx-(int64_t)radius,home_cz},
                                  {home_cx,max_cz+(int64_t)radius},{home_cx,min_cz-(int64_t)radius}};
        uint32_t c;
        for(c=0u;c<4u;++c){
            int32_t oz,ox;
            for(oz=0;oz<(int32_t)ODG_CHUNK_SIZE_CELLS;++oz)for(ox=0;ox<(int32_t)ODG_CHUNK_SIZE_CELLS;++ox){
                int64_t gx=candidates[c][0]*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)ox;
                int64_t gz=candidates[c][1]*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)oz;
                if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_NONE)continue;
                if(!odg_world_cell_safe_ground_internal(gx,gz))continue;
                *out_gx=gx;*out_gz=gz;return 1;
            }
        }
    }
    return 0;
}

/* Open Domain never deletes a nation. If no safe owned respawn remains, establish a
 * tiny recovery enclave on neutral, physically traversable ground. Water territory may
 * continue to exist, but it can never suppress this safety path. */
static int stamp_recovery_territory(odg_actor *a) {
    uint8_t owner;int64_t center_x,center_z;
    if(a==NULL||a->id>=ODG_MAX_ACTORS)return 0;
    owner=ODG_OWNER_FROM_ID(a->id);
    if(!recovery_neutral_safe_cell(a,&center_x,&center_z))return 0;
    {
        int32_t dz;
        for(dz=-1;dz<=1;++dz){
            int32_t dx;
            for(dx=-1;dx<=1;++dx){
                int64_t gx=center_x+(int64_t)dx,gz=center_z+(int64_t)dz;
                uint8_t old=odg_chunk_owner_at_global_cell(gx,gz);
                if(old==ODG_OWNER_NONE||old==owner)set_territory_global(gx,gz,owner);
            }
        }
    }
    a->home_global_cell_x=center_x;a->home_global_cell_z=center_z;
    (void)odg_global_cell_to_local_internal(center_x,center_z,&a->home_cell);
    sync_actor_score(a->id);return 1;
}

static uint64_t cell_axis_distance_u64(int64_t a,int64_t b){
    return a>=b?(uint64_t)a-(uint64_t)b:(uint64_t)b-(uint64_t)a;
}

static int nearest_owned_respawn_global(const odg_actor *a,int64_t *out_gx,int64_t *out_gz) {
    uint8_t owner;uint64_t best_distance=UINT64_MAX;int found=0;int64_t best_gx=0,best_gz=0;uint32_t i;
    if(a==NULL||out_gx==NULL||out_gz==NULL)return 0;
    owner=ODG_OWNER_FROM_ID(a->id);
    if(odg_chunk_owner_at_global_cell(a->home_global_cell_x,a->home_global_cell_z)==owner&&
       odg_world_cell_safe_ground_internal(a->home_global_cell_x,a->home_global_cell_z)){
        *out_gx=a->home_global_cell_x;*out_gz=a->home_global_cell_z;return 1;
    }
    /* Ownership is persistent only in chunk records. Scan records that actually contain
     * this actor's territory and pick the nearest safe cell to home. This removes the old
     * arbitrary 64-cell horizon and never falls back to an unowned/unsafe home. */
    for(i=0u;i<g_odg.chunk_cache_used;++i){
        const odg_chunk_runtime *record=&g_odg_chunk_cache[i];uint32_t ordinal;
        if(record->used==0u||record->territory_cells[a->id]==0u)continue;
        for(ordinal=0u;ordinal<ODG_CHUNK_CELL_COUNT;++ordinal){
            int64_t gx,gz;uint64_t dx,dz,d;
            if(odg_chunk_runtime_owner_at_ordinal_internal(record,ordinal)!=owner)continue;
            gx=record->chunk_x*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)(ordinal%(uint32_t)ODG_CHUNK_SIZE_CELLS);
            gz=record->chunk_z*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)(ordinal/(uint32_t)ODG_CHUNK_SIZE_CELLS);
            if(!odg_world_cell_safe_ground_internal(gx,gz))continue;
            dx=cell_axis_distance_u64(gx,a->home_global_cell_x);dz=cell_axis_distance_u64(gz,a->home_global_cell_z);d=dx>dz?dx:dz;
            if(!found||d<best_distance||(d==best_distance&&(gz<best_gz||(gz==best_gz&&gx<best_gx)))){
                found=1;best_distance=d;best_gx=gx;best_gz=gz;
            }
        }
    }
    if(!found)return 0;
    *out_gx=best_gx;*out_gz=best_gz;return 1;
}

static void update_respawn(odg_actor *a) {
    int64_t gx,gz;
    int32_t lx,lz;
    uint32_t local_cell;
    if(a==NULL||!a->active||a->hp!=0u)return;
    if(a->respawn_ticks>0u){--a->respawn_ticks;return;}
    if(!nearest_owned_respawn_global(a,&gx,&gz)){
        if(!stamp_recovery_territory(a)||!nearest_owned_respawn_global(a,&gx,&gz)){
            /* Never resurrect into water, extreme slope or foreign land. A rare failed
             * recovery search retries later rather than manufacturing an invalid spawn. */
            a->respawn_ticks=ODG_TICK_RATE;return;
        }
    }
    if(!global_cell_center_local_fx(gx,gz,&lx,&lz)){
        /* Never turn a valid far-away global respawn into an unrelated local-centre
         * resurrection. Until the floating origin can represent this owned safe cell,
         * keep the actor dead and retry; invalid placement must fail closed. */
        a->respawn_ticks=ODG_TICK_RATE;return;
    }
    a->x=lx;a->z=lz;
    odg_survival_reset_actor(a->id);
    a->global_fx_x=gx*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
    a->global_fx_z=gz*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
    a->local_resident=1u;reset_actor_transient_motion(a);a->hp=a->max_hp;a->death_reason=ODG_DEATH_NONE;
    if(a->satiety_permille<550u)a->satiety_permille=550u;
    if(a->hydration_permille<600u)a->hydration_permille=600u;
    a->starvation_accum=0u;
    a->dehydration_accum=0u;
    a->last_global_cell_x=gx;a->last_global_cell_z=gz;
    a->trail_head_global_cell_x=INT64_MIN;a->trail_head_global_cell_z=INT64_MIN;
    a->last_cell=odg_global_cell_to_local_internal(gx,gz,&local_cell)?local_cell:UINT32_MAX;
    a->trail_head_cell=UINT32_MAX;a->trail_render_anchor_cell=UINT32_MAX;a->trail_path_len=0u;a->trail_broken=0u;
    a->progress_x=a->x;a->progress_z=a->z;a->progress_ticks=0u;a->stuck_windows=0u;a->think_cd=8u;
    odg_emit_particles(a->x,a->z,0x7be7ffffu,14u);
}

int odg_actor_request_ready_respawn_internal(uint32_t actor_id) {
    odg_actor *actor;
    if(actor_id>=ODG_MAX_ACTORS)return 0;
    actor=&g_odg.actors[actor_id];
    /* REQUEST_RESPAWN is a public command retained for host/API compatibility, but it
     * does not bypass the normal death delay. It only asks the authoritative respawn
     * path to retry once that timer is already ready. */
    if(!actor->active||actor->hp!=0u||actor->respawn_ticks!=0u)return 0;
    update_respawn(actor);return actor->hp!=0u;
}


static void set_territory_global(int64_t gx,int64_t gz,uint8_t owner) {
    odg_chunk_set_owner_at_global_cell(gx,gz,owner);
}

static int point_in_trail_polygon(const odg_actor *a,int64_t px,int64_t pz) {
    uint32_t i,j;int inside=0;
    if(a==NULL||a->trail_path_len<3u)return 0;
    j=a->trail_path_len-1u;
    for(i=0u;i<a->trail_path_len;j=i++){
        int64_t xi=a->trail_path_x[i],zi=a->trail_path_z[i];
        int64_t xj=a->trail_path_x[j],zj=a->trail_path_z[j];
        int crosses=((zi>pz)!=(zj>pz));
        if(crosses){
            int64_t denom=zj-zi;
            int64_t hit=xi+((xj-xi)*(pz-zi))/denom;
            if(px<hit)inside=!inside;
        }
    }
    return inside;
}

static void capture_actor(odg_actor *a) {
    uint32_t i,cell,gained=0u;uint8_t owner;
    int64_t minx=INT64_MAX,maxx=INT64_MIN,minz=INT64_MAX,maxz=INT64_MIN;
    int64_t mincx,maxcx,mincz,maxcz,cx,cz;
    if(!a||!a->trail_active||a->trail_len==0u)return;
    owner=ODG_OWNER_FROM_ID(a->id);
    /* The visual path is also a fixed-point geometry record. It is shifted with the
     * floating origin, so its coordinates remain mutually consistent even after several
     * chunk recenters. Closing last->first yields the same territorial loop independent
     * of which resident window happens to be active when the actor returns home. */
    for(i=0u;i<a->trail_path_len;++i){
        if((int64_t)a->trail_path_x[i]<minx) minx=a->trail_path_x[i];
        if((int64_t)a->trail_path_x[i]>maxx) maxx=a->trail_path_x[i];
        if((int64_t)a->trail_path_z[i]<minz) minz=a->trail_path_z[i];
        if((int64_t)a->trail_path_z[i]>maxz) maxz=a->trail_path_z[i];
    }
    mincx=odg_floor_div_i64_internal(minx,(int64_t)ODG_FX_ONE)-1;maxcx=odg_floor_div_i64_internal(maxx,(int64_t)ODG_FX_ONE)+1;
    mincz=odg_floor_div_i64_internal(minz,(int64_t)ODG_FX_ONE)-1;maxcz=odg_floor_div_i64_internal(maxz,(int64_t)ODG_FX_ONE)+1;
    /* Claim the enclosed polygon. Existing own territory is naturally idempotent; enemy
     * and neutral cells are transferred through one global owner transaction. */
    for(cz=mincz;cz<=maxcz;++cz)for(cx=mincx;cx<=maxcx;++cx){
        int64_t pxf=cx*(int64_t)ODG_FX_ONE+ODG_FX_ONE/2;
        int64_t pzf=cz*(int64_t)ODG_FX_ONE+ODG_FX_ONE/2;
        if(point_in_trail_polygon(a,pxf,pzf)){
            int64_t gx=odg_global_center_cell_x_internal()+cx;
            int64_t gz=odg_global_center_cell_z_internal()+cz;
            if(odg_chunk_owner_at_global_cell(gx,gz)!=owner){set_territory_global(gx,gz,owner);++gained;}
        }
    }
    /* Every committed trail cell becomes part of the domain too, including segments
     * that are now sleeping in a chunk outside the active render window. */
    for(i=0u;i<g_odg.chunk_cache_used;++i){
        odg_chunk_runtime *record=&g_odg_chunk_cache[i];uint32_t ordinal;
        if(record->used==0u)continue;
        for(ordinal=0u;ordinal<ODG_CHUNK_CELL_COUNT;++ordinal){
            int64_t gx,gz;
            if(odg_chunk_trail_at_global_cell(record->chunk_x*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)(ordinal%(uint32_t)ODG_CHUNK_SIZE_CELLS),
                                              record->chunk_z*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)(ordinal/(uint32_t)ODG_CHUNK_SIZE_CELLS))!=owner)continue;
            gx=record->chunk_x*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)(ordinal%(uint32_t)ODG_CHUNK_SIZE_CELLS);
            gz=record->chunk_z*(int64_t)ODG_CHUNK_SIZE_CELLS+(int64_t)(ordinal/(uint32_t)ODG_CHUNK_SIZE_CELLS);
            if(odg_chunk_owner_at_global_cell(gx,gz)!=owner){set_territory_global(gx,gz,owner);++gained;}
        }
    }
    clear_actor_trail(a);sync_actor_score(a->id);
    if(gained>0u){uint32_t particles=8u+gained/8u;if(particles>24u)particles=24u;odg_emit_particles(a->x,a->z,0x62e7ffffu,particles);}
    for(cell=0u;cell<ODG_MAX_ACTORS;++cell)sync_actor_score(cell);
    /* Losing the last cell is no longer an implicit death sentence. A separate recovery
     * rule restores a tiny neutral foothold, preserving continuity without gifting a
     * combat kill or deleting inventory. */
    odg_update_turret_ownership_internal();
}

static void trail_path_append(odg_actor *a,int32_t x,int32_t z) {
    uint32_t i;
    if(a==NULL) return;
    if(a->trail_path_len!=0u) {
        uint32_t last=a->trail_path_len-1u;
        int64_t dx=(int64_t)x-a->trail_path_x[last];
        int64_t dz=(int64_t)z-a->trail_path_z[last];
        /* Avoid dense points created by jitter around one cell boundary. */
        if(dx*dx+dz*dz < (int64_t)(ODG_FX_ONE/8)*(ODG_FX_ONE/8)) return;
    }
    if(a->trail_path_len>=ODG_MAX_TRAIL_PATH_POINTS) {
        /* Deterministic visual-path decimation keeps both endpoints and every second
         * historical turn. Gameplay collision/capture remains backed by trail_owner, so
         * this capacity can never make a long loop non-lethal or uncapturable. */
        uint32_t out=1u;
        for(i=2u;i<a->trail_path_len;i+=2u) {
            a->trail_path_x[out]=a->trail_path_x[i];
            a->trail_path_z[out]=a->trail_path_z[i];
            ++out;
        }
        a->trail_path_len=out;
    }
    a->trail_path_x[a->trail_path_len]=x;
    a->trail_path_z[a->trail_path_len]=z;
    ++a->trail_path_len;
}

static void trail_commit_head(odg_actor *a) {
    uint8_t owner;uint32_t mapped;
    if(a==NULL||!a->trail_active||a->trail_head_global_cell_x==INT64_MIN||a->trail_head_global_cell_z==INT64_MIN)return;
    owner=ODG_OWNER_FROM_ID(a->id);
    if(odg_chunk_trail_at_global_cell(a->trail_head_global_cell_x,a->trail_head_global_cell_z)!=owner){
        odg_chunk_set_trail_at_global_cell(a->trail_head_global_cell_x,a->trail_head_global_cell_z,owner);
        if(a->trail_len!=UINT32_MAX)++a->trail_len;
    }
    if(odg_global_cell_to_local_internal(a->trail_head_global_cell_x,a->trail_head_global_cell_z,&mapped))a->trail_render_anchor_cell=mapped;
    else a->trail_render_anchor_cell=UINT32_MAX;
    trail_path_append(a,a->x,a->z);
}

static void append_global_cell_center_to_path(odg_actor *a,int64_t gx,int64_t gz) {
    int64_t lx=(gx-odg_global_center_cell_x_internal())*(int64_t)ODG_FX_ONE+ODG_FX_ONE/2;
    int64_t lz=(gz-odg_global_center_cell_z_internal())*(int64_t)ODG_FX_ONE+ODG_FX_ONE/2;
    if(lx>=INT32_MIN&&lx<=INT32_MAX&&lz>=INT32_MIN&&lz<=INT32_MAX)trail_path_append(a,(int32_t)lx,(int32_t)lz);
}

static void trail_advance_head_global(odg_actor *a,int64_t gx,int64_t gz,int64_t previous_gx,int64_t previous_gz) {
    uint32_t mapped;
    if(a==NULL)return;
    if(!a->trail_active){
        a->trail_active=1u;a->trail_len=0u;a->trail_path_len=0u;
        append_global_cell_center_to_path(a,previous_gx,previous_gz);
        if(a->type==ODG_ACTOR_BOT&&a->bot_mode==ODG_BOT_INSIDE)a->bot_mode=ODG_BOT_OUTBOUND;
    }else trail_commit_head(a);
    a->trail_head_global_cell_x=gx;a->trail_head_global_cell_z=gz;
    a->trail_head_cell=odg_global_cell_to_local_internal(gx,gz,&mapped)?mapped:UINT32_MAX;
}

static void process_actor_global_cell(odg_actor *a,int64_t gx,int64_t gz) {
    uint8_t owner,trail,ground;uint32_t mapped;
    int64_t previous_gx,previous_gz;
    if(!a||a->hp==0u||(gx==a->last_global_cell_x&&gz==a->last_global_cell_z))return;
    owner=ODG_OWNER_FROM_ID(a->id);previous_gx=a->last_global_cell_x;previous_gz=a->last_global_cell_z;
    trail=odg_chunk_trail_at_global_cell(gx,gz);
    if(trail!=ODG_OWNER_NONE){
        uint32_t victim_id=ODG_ID_FROM_OWNER(trail);
        if(victim_id!=a->id&&victim_id<ODG_MAX_ACTORS)break_actor_trail(&g_odg.actors[victim_id],a->id);
    }
    ground=odg_chunk_owner_at_global_cell(gx,gz);
    if(ground==owner){
        if(a->trail_active){trail_commit_head(a);capture_actor(a);}
        /* Returning home is the authoritative reset after an enemy cut. Merely moving
         * around neutral/enemy land can never create a fresh trail. */
        a->trail_broken=0u;
        if(a->type==ODG_ACTOR_BOT){a->bot_mode=ODG_BOT_INSIDE;a->think_cd=4u+odg_rand_bounded(&a->rng,12u);}
    }else if(a->trail_broken==0u){
        trail_advance_head_global(a,gx,gz,previous_gx,previous_gz);
    }
    a->last_global_cell_x=gx;a->last_global_cell_z=gz;
    a->last_cell=odg_global_cell_to_local_internal(gx,gz,&mapped)?mapped:UINT32_MAX;
}


static void resolve_trail_contacts(void) {
    uint32_t cutter_id;
    /* Cell transitions catch normal crossings. This pass also catches a trail drawn
     * underneath a cube already occupying that cell. Ground ownership never grants
     * immunity: trail_owner and territory are deliberately separate layers. */
    for (cutter_id=0u;cutter_id<ODG_MAX_ACTORS;++cutter_id) {
        odg_actor *cutter=&g_odg.actors[cutter_id];
        uint8_t trail;
        uint32_t victim_id;
        if (!cutter->active || cutter->hp==0u) continue;
        {
            int64_t gx,gz;
            odg_local_fx_to_global_cell_internal(cutter->x,cutter->z,&gx,&gz);
            trail=odg_chunk_trail_at_global_cell(gx,gz);
        }
        if (trail==ODG_OWNER_NONE || trail==ODG_OWNER_FROM_ID(cutter_id)) continue;
        victim_id=ODG_ID_FROM_OWNER(trail);
        if (victim_id<ODG_MAX_ACTORS && g_odg.actors[victim_id].hp!=0u)
            break_actor_trail(&g_odg.actors[victim_id],cutter_id);
    }
}

static int direction_clear_for_actor(const odg_actor *a, int32_t dx_q15, int32_t dz_q15) {
    int32_t look = 2 * ODG_FX_ONE;
    int32_t x = a->x + (int32_t)(((int64_t)dx_q15 * look) / ODG_Q15_ONE);
    int32_t z = a->z + (int32_t)(((int64_t)dz_q15 * look) / ODG_Q15_ONE);
    /* Preview exactly the same terrain + solid-body transition that update_actor will
     * accept. The old prop-only probe could steer a bot repeatedly into deep water or a
     * cliff for two metres, then have movement reject it one tick later. Trails remain an
     * independent vulnerable topology layer and are deliberately not treated as walls. */
    return actor_position_clear(a,x,z);
}

static int global_cell_center_local_fx(int64_t gx,int64_t gz,int32_t *out_x,int32_t *out_z) {
    int64_t lx=(gx-odg_global_center_cell_x_internal())*(int64_t)ODG_FX_ONE+ODG_FX_ONE/2;
    int64_t lz=(gz-odg_global_center_cell_z_internal())*(int64_t)ODG_FX_ONE+ODG_FX_ONE/2;
    if(out_x==NULL||out_z==NULL||lx<INT32_MIN||lx>INT32_MAX||lz<INT32_MIN||lz>INT32_MAX)return 0;
    *out_x=(int32_t)lx;*out_z=(int32_t)lz;return 1;
}

static uint8_t territory_at_local_fx(int32_t x,int32_t z) {
    int64_t gx,gz;odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
    return odg_chunk_owner_at_global_cell(gx,gz);
}

static uint8_t trail_at_local_fx(int32_t x,int32_t z) {
    int64_t gx,gz;odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
    return odg_chunk_trail_at_global_cell(gx,gz);
}

static int bot_find_safe_home_step(odg_actor *a, int32_t *out_x, int32_t *out_z) {
    int32_t hx,hz;
    int64_t current_gx,current_gz;
    uint8_t owner;
    uint32_t radius;
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    owner=ODG_OWNER_FROM_ID(a->id);
    odg_global_fx_to_global_cell_internal(a->global_fx_x,a->global_fx_z,&current_gx,&current_gz);
    /* Prefer nearby owned cells so a return closes quickly instead of tracing all the
     * way back to the nation spawn. This search is global and therefore survives a
     * floating-origin recenter. */
    for(radius=1u;radius<=18u;++radius){
        int64_t r=(int64_t)radius;
        int64_t dz;
        for(dz=-r;dz<=r;++dz){
            int64_t dx;
            for(dx=-r;dx<=r;++dx){
                int64_t gx,gz;
                if((dx!=-r&&dx!=r)&&(dz!=-r&&dz!=r))continue;
                gx=current_gx+dx;gz=current_gz+dz;
                if(odg_chunk_owner_at_global_cell(gx,gz)!=owner)continue;
                if(global_cell_center_local_fx(gx,gz,&hx,&hz)){
                    odg_normalize_q15(hx-a->x,hz-a->z,out_x,out_z);
                    return *out_x!=0||*out_z!=0;
                }
            }
        }
    }
    if(!global_cell_center_local_fx(a->home_global_cell_x,a->home_global_cell_z,&hx,&hz))return 0;
    odg_normalize_q15(hx-a->x,hz-a->z,out_x,out_z);
    return *out_x!=0||*out_z!=0;
}
static uint32_t bot_enemy_turret_risk(const odg_actor *a, int32_t x, int32_t z) {
    uint32_t i;
    uint32_t risk=0u;
    uint8_t own;
    if (!a) return 0u;
    own=ODG_OWNER_FROM_ID(a->id);
    for (i=0u;i<g_odg.turret_count;++i) {
        const odg_turret *t=&g_odg_turrets[i];
        int32_t margin;
        if (!t->active || t->local_resident==0u || t->owner==ODG_TURRET_NEUTRAL || t->owner==own || t->ammo==0u || t->carried_by!=ODG_TURRET_NONE) continue;
        margin=t->range_fx+2*ODG_FX_ONE;
        if (odg_dist2(x,z,t->x,t->z)<=(int64_t)margin*margin) ++risk;
    }
    return risk;
}

static int turret_is_reprogrammable_enemy(const odg_turret *t,uint8_t own) {
    uint32_t owner_id;
    if (!t || !t->active || t->local_resident==0u || t->carried_by!=ODG_TURRET_NONE ||
        t->owner==ODG_TURRET_NEUTRAL || t->owner==own) return 0;
    owner_id=ODG_ID_FROM_OWNER(t->owner);
    return owner_id<ODG_MAX_ACTORS;
}

static void bot_set_side_direction(odg_actor *a) {
    int32_t x = a->bot_out_x_q15;
    int32_t z = a->bot_out_z_q15;
    if (a->turn_sign > 0) { a->ai_x_q15 = -z; a->ai_z_q15 = x; }
    else { a->ai_x_q15 = z; a->ai_z_q15 = -x; }
    a->ai_commit_ticks = ODG_BOT_STEER_COMMIT_TICKS;
}

static void bot_set_return_direction(odg_actor *a) {
    a->ai_x_q15 = -a->bot_out_x_q15;
    a->ai_z_q15 = -a->bot_out_z_q15;
    a->bot_mode = ODG_BOT_RETURN;
    a->ai_plan_cell = UINT32_MAX;
    a->ai_plan_global_cell_x=INT64_MIN;a->ai_plan_global_cell_z=INT64_MIN;
    a->ai_commit_ticks = ODG_BOT_STEER_COMMIT_TICKS;
    a->think_cd = 0u;
}

static void bot_choose_expansion(odg_actor *a) {
    uint32_t start = odg_rand_bounded(&a->rng, 16u);
    uint32_t k;
    int32_t best_x = 0;
    int32_t best_z = ODG_Q15_ONE;
    int32_t best_score = INT32_MIN;
    uint8_t owner = ODG_OWNER_FROM_ID(a->id);
    for (k = 0u; k < 16u; ++k) {
        uint32_t d = (start + k) & 15u;
        int32_t dx = dir16[d][0];
        int32_t dz = dir16[d][1];
        int32_t score = 0;
        uint32_t step;
        if (!direction_clear_for_actor(a, dx, dz)) continue;
        for (step = 3u; step <= 8u; step += 1u) {
            int32_t dist = (int32_t)step * ODG_CELL_FX;
            int32_t sx = a->x + (int32_t)(((int64_t)dx * dist) / ODG_Q15_ONE);
            int32_t sz = a->z + (int32_t)(((int64_t)dz * dist) / ODG_Q15_ONE);
            if (territory_at_local_fx(sx,sz) != owner) score += 4;
            if (trail_at_local_fx(sx,sz) != ODG_OWNER_NONE) score -= 12;
            score -= (int32_t)(bot_enemy_turret_risk(a,sx,sz)*7u);
        }
        score += (int32_t)odg_rand_bounded(&a->rng, 5u);
        if (score > best_score) { best_score = score; best_x = dx; best_z = dz; }
    }
    a->ai_x_q15 = best_x;
    a->ai_z_q15 = best_z;
    a->bot_out_x_q15 = best_x;
    a->bot_out_z_q15 = best_z;
    a->bot_mode = ODG_BOT_INSIDE;
    a->bot_leg_target = 4u + odg_rand_bounded(&a->rng, 5u);
    a->turn_sign = odg_rand_bounded(&a->rng, 2u) ? 1 : -1;
    a->ai_commit_ticks = ODG_BOT_STEER_COMMIT_TICKS;
}

static int bot_supply_direction(odg_actor *a, int32_t *out_x, int32_t *out_z) {
    uint32_t i,best=UINT32_MAX;
    int64_t best_d2=INT64_MAX;
    uint8_t own;
    int32_t tx=0,tz=0;
    uint32_t chip_slot=UINT32_MAX;
    uint32_t ammo=0u;
    if(!a || !out_x || !out_z || a->trail_active) return 0;
    own=ODG_OWNER_FROM_ID(a->id);

    if(odg_inventory_find_type(&a->inventory,ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_NONE,&chip_slot)) {
        const odg_item_stack *chip=&a->inventory.slots[chip_slot];
        for(i=0u;i<g_odg.turret_count;++i) {
            const odg_turret *t=&g_odg_turrets[i];int64_t d2;
            if(!turret_is_reprogrammable_enemy(t,own) || t->material_tier!=chip->material_tier) continue;
            d2=odg_dist2(a->x,a->z,t->x,t->z);if(d2<best_d2){best=i;best_d2=d2;}
        }
        if(best<g_odg.turret_count){tx=g_odg_turrets[best].x;tz=g_odg_turrets[best].z;goto finish;}
    } else {
        best=UINT32_MAX;best_d2=(int64_t)(22*ODG_FX_ONE)*(22*ODG_FX_ONE);
        for(i=0u;i<g_odg.pickup_count;++i) {
            const odg_world_pickup *p=&g_odg_pickups[i];int64_t d2;
            if(!p->active || p->local_resident==0u || p->pickup_cd!=0u || p->stack.type_id!=ODG_ITEM_REPROGRAM_CHIP) continue;
            d2=odg_dist2(a->x,a->z,p->x,p->z);if(d2<best_d2){best=i;best_d2=d2;}
        }
        if(best<g_odg.pickup_count){tx=g_odg_pickups[best].x;tz=g_odg_pickups[best].z;goto finish;}
    }

    /* Carry compatible ascension chips back to owned infrastructure before routine ammo. */
    best=UINT32_MAX;best_d2=INT64_MAX;
    for(i=0u;i<g_odg.turret_count;++i){
        const odg_turret *t=&g_odg_turrets[i];uint32_t wanted,slot;int64_t d2;
        if(!t->active||t->local_resident==0u||t->owner!=own||t->carried_by!=ODG_TURRET_NONE)continue;
        wanted=t->material_tier==ODG_MATERIAL_WOOD?ODG_MATERIAL_STONE:(t->material_tier==ODG_MATERIAL_STONE?ODG_MATERIAL_IRON:ODG_MATERIAL_NONE);
        if(wanted==ODG_MATERIAL_NONE||!odg_inventory_find_type(&a->inventory,ODG_ITEM_ASCENSION_CHIP,wanted,&slot))continue;
        d2=odg_dist2(a->x,a->z,t->x,t->z);if(d2<best_d2){best=i;best_d2=d2;}
    }
    if(best<g_odg.turret_count){tx=g_odg_turrets[best].x;tz=g_odg_turrets[best].z;goto finish;}

    ammo=odg_inventory_total(&a->inventory,ODG_ITEM_AMMO,ODG_MATERIAL_NONE);
    best=UINT32_MAX;best_d2=INT64_MAX;
    if(ammo!=0u) {
        for(i=0u;i<g_odg.turret_count;++i) {
            const odg_turret *t=&g_odg_turrets[i];int64_t d2;
            if(!t->active || t->local_resident==0u || t->owner!=own || t->carried_by!=ODG_TURRET_NONE || t->ammo>=t->max_ammo) continue;
            d2=odg_dist2(a->x,a->z,t->x,t->z);if(d2<best_d2){best=i;best_d2=d2;}
        }
        if(best<g_odg.turret_count){tx=g_odg_turrets[best].x;tz=g_odg_turrets[best].z;goto finish;}
    }
    return 0;
finish:
    odg_normalize_q15(tx-a->x,tz-a->z,out_x,out_z);
    return direction_clear_for_actor(a,*out_x,*out_z);
}

static int bot_nav_toward(odg_actor *a,int32_t tx,int32_t tz,int32_t *out_x,int32_t *out_z);
static int bot_nav_land_toward(odg_actor *a,int32_t tx,int32_t tz,int32_t *out_x,int32_t *out_z);

static int bot_vehicle_direction_clear(const odg_actor *a,int32_t dir_x,int32_t dir_z){
    const int32_t probe_fx=ODG_FX_ONE;
    int32_t dx,dz;
    if(a==NULL||(dir_x==0&&dir_z==0))return 0;
    dx=(int32_t)(((int64_t)dir_x*probe_fx)/ODG_Q15_ONE);
    dz=(int32_t)(((int64_t)dir_z*probe_fx)/ODG_Q15_ONE);
    return odg_artifact_vehicle_can_move_actor_internal(a,dx,dz);
}

static int bot_nav_vehicle_toward(odg_actor *a,int32_t tx,int32_t tz,int32_t *out_x,int32_t *out_z){
    int32_t dx,dz,left_x,left_z,right_x,right_z;
    odg_surface_sample target_surface;
    int64_t target_d2;
    if(a==NULL||out_x==NULL||out_z==NULL||odg_artifact_actor_vehicle_internal(a->id)==UINT32_MAX)return 0;
    target_d2=odg_dist2(a->x,a->z,tx,tz);
    odg_normalize_q15(tx-a->x,tz-a->z,&dx,&dz);
    if(dx==0&&dz==0){*out_x=0;*out_z=0;return 1;}
    if(bot_vehicle_direction_clear(a,dx,dz)){*out_x=dx;*out_z=dz;return 1;}

    /* Water navigation deliberately has its own steering layer instead of treating
     * WATER as walkable land-nav. Probe two 45-degree alternatives first so a raft can
     * flow around banks/rocks without the oscillation caused by swapping hard left/right. */
    odg_normalize_q15(dx-dz,dz+dx,&left_x,&left_z);
    odg_normalize_q15(dx+dz,dz-dx,&right_x,&right_z);
    if(a->turn_sign>0){
        if(bot_vehicle_direction_clear(a,left_x,left_z)){*out_x=left_x;*out_z=left_z;return 1;}
        if(bot_vehicle_direction_clear(a,right_x,right_z)){*out_x=right_x;*out_z=right_z;return 1;}
    }else{
        if(bot_vehicle_direction_clear(a,right_x,right_z)){*out_x=right_x;*out_z=right_z;return 1;}
        if(bot_vehicle_direction_clear(a,left_x,left_z)){*out_x=left_x;*out_z=left_z;return 1;}
    }

    /* A dry destination close to a blocked craft means the remaining route is land.
     * Use the vehicle's ordinary dismount transaction; it prefers a safe dry bank and
     * falls back to swimming only if no bank exists. */
    if(target_d2<=(int64_t)(8*ODG_FX_ONE)*(8*ODG_FX_ONE)&&
       odg_environment_surface_local(tx,tz,&target_surface)&&
       (target_surface.flags&ODG_SURFACE_FLAG_WATER)==0u&&
       odg_artifact_vehicle_toggle_internal(a->id,odg_artifact_actor_vehicle_internal(a->id))){
        *out_x=0;*out_z=0;return 1;
    }
    *out_x=0;*out_z=0;return 1;
}

int odg_bot_route_requires_raft_internal(const odg_actor *a,int32_t tx,int32_t tz){
    int32_t span_x,span_z,max_span;uint32_t steps,i,wet_run=0u;
    if(a==NULL)return 0;
    span_x=odg_abs_i32(tx-a->x);span_z=odg_abs_i32(tz-a->z);max_span=span_x>span_z?span_x:span_z;
    if(max_span<6*ODG_FX_ONE)return 0;
    steps=(uint32_t)(max_span/(2*ODG_FX_ONE));
    if(steps<3u)steps=3u;
    if(steps>32u)steps=32u;
    for(i=1u;i<steps;++i){
        int32_t x=a->x+(int32_t)(((int64_t)(tx-a->x)*(int64_t)i)/(int64_t)steps);
        int32_t z=a->z+(int32_t)(((int64_t)(tz-a->z)*(int64_t)i)/(int64_t)steps);
        if(odg_artifact_surface_allows_item_internal(ODG_ITEM_RAFT,x,z)){
            if(++wet_run>=2u)return 1;
        }else wet_run=0u;
    }
    return 0;
}

static uint32_t bot_find_accessible_empty_raft(const odg_actor *a){
    uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)(24*ODG_FX_ONE)*(24*ODG_FX_ONE);
    if(a==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *ar=&g_odg_artifacts[i];int64_t d2;
        if(!ar->active||!ar->local_resident||ar->item_type!=ODG_ITEM_RAFT||ar->aux_u32!=0u)continue;
        if(!odg_artifact_actor_can_access_internal(a->id,ar))continue;
        d2=odg_dist2(a->x,a->z,ar->x,ar->z);if(d2<best_d2){best_d2=d2;best=i;}
    }
    return best;
}


/* -------------------------------------------------------------------------
 * Bot survival / recovery planner
 * -------------------------------------------------------------------------
 * Survival is intentionally independent from industrial progression.  A bot may pause
 * its economy to recover a death backpack or acquire food, but it cannot synthesize
 * either.  Territorial access is checked before a wild pickup/tree becomes a target.
 */
static uint32_t bot_find_own_death_cache(const odg_actor *a) {
    uint32_t i,best=UINT32_MAX;int64_t best_d2=INT64_MAX;
    if(a==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *ar=&g_odg_artifacts[i];int64_t d2;
        if(!ar->active||ar->local_resident==0u||ar->owner_actor_id!=a->id||!odg_artifact_is_death_cache(ar))continue;
        d2=odg_dist2(a->x,a->z,ar->x,ar->z);
        if(d2<best_d2){best=i;best_d2=d2;}
    }
    return best;
}

static uint32_t bot_find_accessible_food_pickup(const odg_actor *a) {
    uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)(30*ODG_FX_ONE)*(30*ODG_FX_ONE);
    if(a==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg.pickup_count;++i){
        const odg_world_pickup *p=&g_odg_pickups[i];int64_t d2;
        if(!p->active||p->local_resident==0u||p->pickup_cd!=0u||odg_food_definition_internal(p->stack.type_id)==NULL)continue;
        if(!odg_territory_allows_environment_action(a->id,p->x,p->z))continue;
        d2=odg_dist2(a->x,a->z,p->x,p->z);
        if(d2<best_d2){best=i;best_d2=d2;}
    }
    return best;
}

static uint32_t bot_find_accessible_fruit_resource(const odg_actor *a) {
    uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)(34*ODG_FX_ONE)*(34*ODG_FX_ONE);
    if(a==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg.resource_count;++i){
        const odg_resource_node *r=&g_odg_resources[i];int64_t d2;
        if(!r->active||r->local_resident==0u||r->state!=ODG_RESOURCE_STATE_AVAILABLE||r->species_id==0u||r->fruit_count==0u)continue;
        if(!odg_territory_allows_environment_action(a->id,r->x,r->z))continue;
        d2=odg_dist2(a->x,a->z,r->x,r->z);
        if(d2<best_d2){best=i;best_d2=d2;}
    }
    return best;
}

static int bot_try_inventory_water(odg_actor *a){
    uint32_t capacity,slot;
    if(a==NULL)return 0;
    capacity=odg_inventory_capacity(&a->inventory);
    for(slot=0u;slot<capacity;++slot){
        odg_item_stack *stack=&a->inventory.slots[slot];
        const odg_fluid_container_definition *container;
        const odg_fluid_definition *fluid;
        uint32_t fluid_id,units;
        if(stack->type_id==ODG_ITEM_NONE||stack->quantity==0u)continue;
        container=odg_fluid_container_definition_internal(stack->type_id);
        if(container==NULL)continue;
        fluid_id=odg_fluid_payload_id_internal(stack->payload_id);
        units=odg_fluid_payload_units_internal(stack->payload_id);
        fluid=odg_fluid_definition_internal(fluid_id);
        if(units==0u||fluid==NULL||(fluid->flags&ODG_FLUID_FLAG_POTABLE)==0u)continue;
        a->inventory.selected_slot=slot;
        return odg_actor_drink_selected_internal(a->id);
    }
    return 0;
}

static int bot_find_accessible_water(const odg_actor *a,int32_t *out_x,int32_t *out_z){
    static const int32_t dirs[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    static const uint32_t radii[]={2u,4u,8u,12u,18u,24u,32u};
    uint32_t ri,di;
    int64_t best=(int64_t)(36*ODG_FX_ONE)*(36*ODG_FX_ONE);
    int found=0;
    int32_t bx=0,bz=0;
    if(a==NULL)return 0;
    for(ri=0u;ri<(uint32_t)(sizeof(radii)/sizeof(radii[0]));++ri){
        int32_t radius=(int32_t)radii[ri]*ODG_FX_ONE;
        for(di=0u;di<8u;++di){
            int32_t tx=a->x+dirs[di][0]*radius;
            int32_t tz=a->z+dirs[di][1]*radius;
            odg_surface_sample sample;
            int64_t d2;
            if(!odg_environment_surface_local(tx,tz,&sample)||(sample.flags&ODG_SURFACE_FLAG_WATER)==0u)continue;
            if(!odg_territory_allows_environment_action(a->id,tx,tz))continue;
            d2=odg_dist2(a->x,a->z,tx,tz);
            if(d2<best){best=d2;bx=tx;bz=tz;found=1;}
        }
        if(found)break;
    }
    if(!found)return 0;
    if(out_x!=NULL)*out_x=bx;
    if(out_z!=NULL)*out_z=bz;
    return 1;
}

static int bot_find_shore(const odg_actor *a,int32_t *out_x,int32_t *out_z){
    static const int32_t dirs[16][2]={{32767,0},{30274,12539},{23170,23170},{12539,30274},{0,32767},{-12539,30274},{-23170,23170},{-30274,12539},{-32767,0},{-30274,-12539},{-23170,-23170},{-12539,-30274},{0,-32767},{12539,-30274},{23170,-23170},{30274,-12539}};
    static const uint32_t radii[]={2u,3u,4u,6u,8u,12u,16u};
    uint32_t ri,di;int found=0;int64_t best=INT64_MAX;int32_t bx=0,bz=0;
    if(a==NULL)return 0;
    for(ri=0u;ri<(uint32_t)(sizeof(radii)/sizeof(radii[0]));++ri){
        int32_t radius=(int32_t)radii[ri]*ODG_FX_ONE;
        for(di=0u;di<16u;++di){
            int32_t tx=a->x+(int32_t)(((int64_t)dirs[di][0]*radius)/ODG_Q15_ONE);
            int32_t tz=a->z+(int32_t)(((int64_t)dirs[di][1]*radius)/ODG_Q15_ONE);
            odg_surface_sample surface;int64_t d2;
            if(!odg_environment_surface_local(tx,tz,&surface))continue;
            if((surface.flags&ODG_SURFACE_FLAG_WATER)!=0u&&surface.water_depth_milli>=ODG_SWIM_MIN_DEPTH_MILLI)continue;
            if((surface.flags&ODG_SURFACE_FLAG_STEEP)!=0u||!position_clear(tx,tz,a->radius))continue;
            d2=odg_dist2(a->x,a->z,tx,tz);
            if(d2<best){best=d2;bx=tx;bz=tz;found=1;}
        }
        if(found)break;
    }
    if(!found)return 0;
    if(out_x!=NULL)*out_x=bx;
    if(out_z!=NULL)*out_z=bz;
    return 1;
}

static int bot_escape_water_direction(odg_actor *a,int32_t *out_x,int32_t *out_z){
    int32_t sx,sz;
    if(a==NULL||out_x==NULL||out_z==NULL||!odg_actor_is_swimming_internal(a))return 0;
    if(!bot_find_shore(a,&sx,&sz))return 0;
    return bot_nav_toward(a,sx,sz,out_x,out_z);
}

static int bot_water_direction(odg_actor *a,int32_t *out_x,int32_t *out_z){
    int32_t wx,wz,nx,nz;
    int64_t d2;
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    if(bot_try_inventory_water(a)){*out_x=0;*out_z=0;return 1;}
    if(!bot_find_accessible_water(a,&wx,&wz))return 0;
    d2=odg_dist2(a->x,a->z,wx,wz);
    odg_normalize_q15(wx-a->x,wz-a->z,&nx,&nz);
    if(d2<=(int64_t)(2*ODG_FX_ONE)*(2*ODG_FX_ONE)){
        a->face_x_q15=nx;a->face_z_q15=nz;
        if(odg_actor_drink_environment_internal(a->id)){*out_x=0;*out_z=0;return 1;}
    }
    return bot_nav_toward(a,wx,wz,out_x,out_z);
}

static int bot_food_direction(odg_actor *a,int32_t *out_x,int32_t *out_z) {
    uint32_t pickup,resource;
    int64_t gather2=(int64_t)ODG_RESOURCE_INTERACT_RANGE_FX*ODG_RESOURCE_INTERACT_RANGE_FX;
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    pickup=bot_find_accessible_food_pickup(a);
    if(pickup<g_odg.pickup_count){
        if(odg_dist2(a->x,a->z,g_odg_pickups[pickup].x,g_odg_pickups[pickup].z)<=(int64_t)ODG_PICKUP_RANGE_FX*ODG_PICKUP_RANGE_FX){
            *out_x=0;*out_z=0;return 1; /* interactions tick performs the same pickup transaction */
        }
        return bot_nav_toward(a,g_odg_pickups[pickup].x,g_odg_pickups[pickup].z,out_x,out_z);
    }
    resource=bot_find_accessible_fruit_resource(a);
    if(resource<g_odg.resource_count){
        if(odg_dist2(a->x,a->z,g_odg_resources[resource].x,g_odg_resources[resource].z)<=gather2){
            if(odg_ecology_gather_fruit(a->id,resource)){*out_x=0;*out_z=0;return 1;}
            return 0;
        }
        return bot_nav_toward(a,g_odg_resources[resource].x,g_odg_resources[resource].z,out_x,out_z);
    }
    return 0;
}

static int bot_survival_direction(odg_actor *a,int32_t *out_x,int32_t *out_z) {
    uint32_t cache;
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    /* Water is traversable, but drowning is not a valid strategic choice. A bot that is
     * still submerged after buoyancy or has already consumed a meaningful oxygen reserve
     * exits toward the nearest reachable shore even if that shore is enemy territory. */
    if(odg_actor_is_swimming_internal(a)&&
       g_odg_persistent_runtime.actors[a->id].oxygen_permille<ODG_BOT_OXYGEN_ESCAPE_PERMILLE&&
       bot_escape_water_direction(a,out_x,out_z))return 1;
    /* Critical hunger preempts travel.  At ordinary hunger, reclaiming a backpack is
     * more valuable because it may itself contain food and hours of accumulated work. */
    if(a->hydration_permille<240u && bot_water_direction(a,out_x,out_z))return 1;
    if(a->satiety_permille<280u && bot_food_direction(a,out_x,out_z))return 1;
    cache=bot_find_own_death_cache(a);
    if(cache<g_odg.artifact_count){
        const odg_artifact *ar=&g_odg_artifacts[cache];
        if(odg_dist2(a->x,a->z,ar->x,ar->z)<=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX){
            if(odg_artifact_recover_death_cache(a->id,cache)){*out_x=0;*out_z=0;return 1;}
            return 0;
        }
        if(bot_nav_toward(a,ar->x,ar->z,out_x,out_z))return 1;
    }
    if(a->hydration_permille<620u && bot_water_direction(a,out_x,out_z))return 1;
    if(a->satiety_permille<650u && bot_food_direction(a,out_x,out_z))return 1;
    return 0;
}

/* -------------------------------------------------------------------------
 * Bot economy planner
 * -------------------------------------------------------------------------
 * Bots deliberately use the same inventory, harvesting, recipe and artifact
 * transactions as the human actor.  The planner only decides where to walk and
 * which existing action to attempt; it never injects resources or bypasses a
 * station requirement.  Re-evaluation is deterministic and cheap for ten actors.
 */
static int bot_inventory_has(const odg_actor *a,uint32_t type,uint32_t tier) {
    return a!=NULL && odg_inventory_find_type(&a->inventory,type,tier,NULL);
}

static uint32_t bot_best_tool_slot(const odg_actor *a,uint32_t type,uint32_t min_tier) {
    static const uint32_t tiers[]={ODG_MATERIAL_IRON,ODG_MATERIAL_STONE,ODG_MATERIAL_WOOD};
    uint32_t i,slot;
    if(a==NULL)return UINT32_MAX;
    for(i=0u;i<3u;++i) if(tiers[i]>=min_tier && odg_inventory_find_type(&a->inventory,type,tiers[i],&slot)) return slot;
    return UINT32_MAX;
}

static uint32_t bot_best_capability_slot(const odg_actor *a,uint32_t capability,uint32_t min_tier) {
    static const uint32_t tiers[]={ODG_MATERIAL_IRON,ODG_MATERIAL_STONE,ODG_MATERIAL_WOOD};
    uint32_t i,slot;
    if(a==NULL||capability==0u)return UINT32_MAX;
    for(i=0u;i<3u;++i)
        if(tiers[i]>=min_tier&&odg_inventory_find_capability_internal(&a->inventory,capability,tiers[i],&slot))return slot;
    return UINT32_MAX;
}

static int bot_select_empty_hand(odg_actor *a) {
    uint32_t i,capacity;
    if(a==NULL)return 0;
    capacity=odg_inventory_capacity(&a->inventory);
    for(i=0u;i<capacity;++i){
        if(odg_item_stack_empty_internal(&a->inventory.slots[i])){a->inventory.selected_slot=i;return 1;}
    }
    return 0;
}

static int bot_nav_land_toward(odg_actor *a,int32_t tx,int32_t tz,int32_t *out_x,int32_t *out_z) {
    int32_t dx,dz,px,pz;int64_t target_d2;
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    target_d2=odg_dist2(a->x,a->z,tx,tz);
    odg_normalize_q15(tx-a->x,tz-a->z,&dx,&dz);
    if(dx==0&&dz==0){*out_x=0;*out_z=0;return 1;}
    /* A swimmer escaping danger must not be trapped by the dry-land navigation layer.
     * Buoyancy/physics remains the authority and the emergency shore finder supplied a
     * physically valid target already. */
    if(odg_actor_is_swimming_internal(a)){
        if(direction_clear_for_actor(a,dx,dz)){*out_x=dx;*out_z=dz;return 1;}
    }
    /* Resources/stations are collision-bearing targets; close-range steering intentionally
     * approaches them directly. At long range, consume the cached terrain graph that is
     * rebuilt on every floating-origin rebase instead of keeping it as dead infrastructure. */
    if(target_d2>(int64_t)(14*ODG_FX_ONE)*(14*ODG_FX_ONE)&&!odg_actor_is_swimming_internal(a)){
        int64_t agx,agz;uint32_t c;
        odg_global_fx_to_global_cell_internal(a->global_fx_x,a->global_fx_z,&agx,&agz);
        if(odg_global_cell_to_local_internal(agx,agz,&c)){
            uint8_t edges=g_odg.bot_nav_edges[c];
            uint32_t cx=cell_x(c),cz=cell_z(c),best=UINT32_MAX;int64_t best_d2=INT64_MAX;
            const uint8_t masks[4]={UINT8_C(1),UINT8_C(2),UINT8_C(4),UINT8_C(8)};
            const int32_t ox[4]={-1,1,0,0},oz[4]={0,0,-1,1};uint32_t k;
            for(k=0u;k<4u;++k){
                int32_t nx=(int32_t)cx+ox[k],nz=(int32_t)cz+oz[k];uint32_t nc;int64_t d2;
                if((edges&masks[k])==0u||nx<0||nz<0||nx>=(int32_t)ODG_GRID_SIZE||nz>=(int32_t)ODG_GRID_SIZE)continue;
                nc=(uint32_t)nz*ODG_GRID_SIZE+(uint32_t)nx;
                d2=odg_dist2(odg_cell_center_x(nc),odg_cell_center_z(nc),tx,tz);
                if(d2<best_d2){best_d2=d2;best=nc;}
            }
            if(best!=UINT32_MAX){
                odg_normalize_q15(odg_cell_center_x(best)-a->x,odg_cell_center_z(best)-a->z,&px,&pz);
                if(direction_clear_for_actor(a,px,pz)){*out_x=px;*out_z=pz;return 1;}
            }
        }
    }
    if(direction_clear_for_actor(a,dx,dz)){*out_x=dx;*out_z=dz;return 1;}
    px=a->turn_sign>0?-dz:dz;pz=a->turn_sign>0?dx:-dx;
    if(direction_clear_for_actor(a,px,pz)){*out_x=px;*out_z=pz;return 1;}
    px=-px;pz=-pz;
    if(direction_clear_for_actor(a,px,pz)){*out_x=px;*out_z=pz;return 1;}
    return 0;
}

static int bot_nav_toward(odg_actor *a,int32_t tx,int32_t tz,int32_t *out_x,int32_t *out_z) {
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    if(odg_artifact_actor_vehicle_internal(a->id)!=UINT32_MAX)
        return bot_nav_vehicle_toward(a,tx,tz,out_x,out_z);
    if(odg_bot_logistics_prepare_vehicle_internal(a,tx,tz,out_x,out_z))return 1;
    return bot_nav_land_toward(a,tx,tz,out_x,out_z);
}

static uint32_t bot_find_resource_item(const odg_actor *a,uint32_t item_type) {
    uint32_t pass,i,best=UINT32_MAX;int64_t best_d2=INT64_MAX;
    if(a==NULL)return UINT32_MAX;
    /* Pass 0 is the deterministic progression bootstrap generated around this
     * nation. It is affinity, not ownership: any actor may still harvest any node. */
    for(pass=0u;pass<2u;++pass){
        best=UINT32_MAX;best_d2=INT64_MAX;
        for(i=0u;i<g_odg.resource_count;++i){
            const odg_resource_node *r=&g_odg_resources[i];int64_t d2;
            if(!r->active||r->local_resident==0u||r->state!=ODG_RESOURCE_STATE_AVAILABLE||odg_resource_harvest_item_type_internal(r)!=item_type)continue;
            if(!odg_territory_allows_environment_action(a->id,r->x,r->z))continue;
            if(r->harvest_actor!=UINT32_MAX && r->harvest_actor!=a->id && r->harvest_grace>0u)continue;
            if(pass==0u && r->bootstrap_actor_id!=a->id)continue;
            d2=odg_dist2(a->x,a->z,r->x,r->z);
            if(d2<best_d2){best_d2=d2;best=i;}
        }
        if(best<g_odg.resource_count)return best;
    }
    return UINT32_MAX;
}

static uint32_t bot_find_own_artifact(const odg_actor *a,uint32_t item_type) {
    uint32_t i,best=UINT32_MAX;int64_t best_d2=INT64_MAX;
    if(a==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg.artifact_count;++i){
        const odg_artifact *ar=&g_odg_artifacts[i];int64_t d2;
        if(!ar->active||ar->local_resident==0u||ar->owner_actor_id!=a->id||ar->item_type!=item_type)continue;
        d2=odg_dist2(a->x,a->z,ar->x,ar->z);
        if(d2<best_d2){best_d2=d2;best=i;}
    }
    return best;
}

static int bot_recipe_affordable(const odg_actor *a,uint32_t recipe_id,odg_recipe_definition *out) {
    odg_recipe_definition r;uint64_t req=0u;uint32_t i;
    if(a==NULL||odg_recipe_get(recipe_id,&r,sizeof(r),&req)!=ODG_STATUS_OK)return 0;
    for(i=0u;i<r.ingredient_count;++i)
        if(odg_inventory_total(&a->inventory,r.ingredients[i].item_type,r.ingredients[i].material_tier)<r.ingredients[i].quantity)return 0;
    if(out!=NULL) *out=r;
    return 1;
}

static int bot_move_to_station(odg_actor *a,uint32_t station,int32_t *out_x,int32_t *out_z) {
    uint32_t id=bot_find_own_artifact(a,station);
    int64_t near2=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX;
    if(id>=g_odg.artifact_count)return 0;
    if(odg_dist2(a->x,a->z,g_odg_artifacts[id].x,g_odg_artifacts[id].z)<=near2){*out_x=0;*out_z=0;return 2;}
    return bot_nav_toward(a,g_odg_artifacts[id].x,g_odg_artifacts[id].z,out_x,out_z)?1:0;
}

static int bot_craft_goal(odg_actor *a,uint32_t recipe_id,int32_t *out_x,int32_t *out_z) {
    odg_recipe_definition r;int m;
    if(!bot_recipe_affordable(a,recipe_id,&r))return 0;
    m=bot_move_to_station(a,r.station_item_type,out_x,out_z);
    if(m==0)return 0;
    if(m==2){
        if(odg_craft(a->id,recipe_id,1u)!=ODG_STATUS_OK)return 0;
        a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;
    }
    return 1;
}

static int bot_harvest_goal(odg_actor *a,uint32_t item_type,int32_t *out_x,int32_t *out_z) {
    uint32_t id,slot=UINT32_MAX,tool_type,tool_capability,min_tier;
    int64_t near2=(int64_t)ODG_RESOURCE_INTERACT_RANGE_FX*ODG_RESOURCE_INTERACT_RANGE_FX;
    int result;
    if(a==NULL)return 0;

    /* Persist a desired OUTPUT item, not an implementation resource kind. Future flora
     * may also yield wood, and the planner should use it without learning a new enum. */
    id=a->bot_economy_item_type==item_type?a->bot_economy_target_id:UINT32_MAX;
    if(id>=g_odg.resource_count || !g_odg_resources[id].active || g_odg_resources[id].local_resident==0u ||
       g_odg_resources[id].state!=ODG_RESOURCE_STATE_AVAILABLE ||
       odg_resource_harvest_item_type_internal(&g_odg_resources[id])!=item_type ||
       !odg_territory_allows_environment_action(a->id,g_odg_resources[id].x,g_odg_resources[id].z) ||
       (g_odg_resources[id].harvest_actor!=UINT32_MAX &&
        g_odg_resources[id].harvest_actor!=a->id && g_odg_resources[id].harvest_grace>0u)) {
        id=bot_find_resource_item(a,item_type);
        if(id>=g_odg.resource_count){a->bot_economy_item_type=0u;a->bot_economy_target_id=UINT32_MAX;return 0;}
        a->bot_economy_item_type=item_type;a->bot_economy_target_id=id;
    }
    tool_type=odg_resource_harvest_tool_type_internal(&g_odg_resources[id]);
    tool_capability=odg_resource_harvest_tool_capability_internal(&g_odg_resources[id]);
    min_tier=odg_resource_harvest_min_tool_tier_internal(&g_odg_resources[id]);
    if(tool_type!=ODG_ITEM_NONE||tool_capability!=0u){
        slot=tool_capability!=0u?bot_best_capability_slot(a,tool_capability,min_tier):bot_best_tool_slot(a,tool_type,min_tier);
        if(slot==UINT32_MAX){
            if(!odg_resource_harvest_allows_hand_internal(&g_odg_resources[id]) || !bot_select_empty_hand(a))return 0;
        }
    }
    if(slot!=UINT32_MAX)a->inventory.selected_slot=slot;
    if(odg_dist2(a->x,a->z,g_odg_resources[id].x,g_odg_resources[id].z)<=near2){
        *out_x=0;*out_z=0;
        result=odg_resource_hold_tick(a->id,id);
        a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;
        if(result==2 || !g_odg_resources[id].active || g_odg_resources[id].state!=ODG_RESOURCE_STATE_AVAILABLE){
            a->bot_economy_item_type=0u;a->bot_economy_target_id=UINT32_MAX;
            if(a->trail_active) bot_set_return_direction(a);
        }
        return 1;
    }
    if(bot_nav_toward(a,g_odg_resources[id].x,g_odg_resources[id].z,out_x,out_z)){a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;}
    return 0;
}


/* Recipe demand is read from the same table that executes crafting. This removes a second
 * shadow economy from AI: changing an ingredient quantity changes player and bot behavior
 * together. The current progression recipes use directly harvestable inputs; unsupported
 * future intermediates fail closed instead of being granted or guessed. */
static int bot_recipe_goal(odg_actor *a,uint32_t recipe_id,int32_t *out_x,int32_t *out_z){
    odg_recipe_definition recipe;uint64_t required=0u;uint32_t i;
    if(a==NULL||out_x==NULL||out_z==NULL||
       odg_recipe_get(recipe_id,&recipe,sizeof(recipe),&required)!=ODG_STATUS_OK)return 0;
    for(i=0u;i<recipe.ingredient_count;++i){
        const odg_recipe_ingredient *ingredient=&recipe.ingredients[i];
        if(odg_inventory_total(&a->inventory,ingredient->item_type,ingredient->material_tier)>=ingredient->quantity)continue;
        return bot_harvest_goal(a,ingredient->item_type,out_x,out_z);
    }
    return bot_craft_goal(a,recipe_id,out_x,out_z);
}


/* Multimodal logistics closes the complete raft cycle for bots. Detection alone is not
 * enough: a bot must reuse an accessible craft, carry/place a crafted one, or acquire
 * its real inputs at its real workbench. Every branch uses ordinary inventory, crafting,
 * placement, ownership and interaction rules; there is no free vehicle or teleport. */
static uint32_t bot_find_land_reachable_resource_item(const odg_actor *a,uint32_t item_type){
    uint32_t pass,i,best=UINT32_MAX;int64_t best_d2=INT64_MAX;
    if(a==NULL)return UINT32_MAX;
    for(pass=0u;pass<2u;++pass){
        best=UINT32_MAX;best_d2=INT64_MAX;
        for(i=0u;i<g_odg.resource_count;++i){
            const odg_resource_node *r=&g_odg_resources[i];int64_t d2;
            if(!r->active||!r->local_resident||r->state!=ODG_RESOURCE_STATE_AVAILABLE||
               odg_resource_harvest_item_type_internal(r)!=item_type)continue;
            if(!odg_territory_allows_environment_action(a->id,r->x,r->z))continue;
            if(r->harvest_actor!=UINT32_MAX&&r->harvest_actor!=a->id&&r->harvest_grace>0u)continue;
            if(pass==0u&&r->bootstrap_actor_id!=a->id)continue;
            /* Do not solve a missing-raft problem by selecting wood that itself needs a
             * raft. The ordinary economy can reconsider remote resources after crossing. */
            if(odg_bot_route_requires_raft_internal(a,r->x,r->z))continue;
            d2=odg_dist2(a->x,a->z,r->x,r->z);
            if(d2<best_d2){best_d2=d2;best=i;}
        }
        if(best<g_odg.resource_count)return best;
    }
    return UINT32_MAX;
}

static int bot_gather_resource_land_only(odg_actor *a,uint32_t item_type,int32_t *out_x,int32_t *out_z){
    uint32_t id,slot=UINT32_MAX,tool_type,tool_capability,min_tier;
    int64_t near2=(int64_t)ODG_RESOURCE_INTERACT_RANGE_FX*ODG_RESOURCE_INTERACT_RANGE_FX;
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    id=bot_find_land_reachable_resource_item(a,item_type);
    if(id>=g_odg.resource_count)return 0;
    a->bot_economy_item_type=item_type;a->bot_economy_target_id=id;
    tool_type=odg_resource_harvest_tool_type_internal(&g_odg_resources[id]);
    tool_capability=odg_resource_harvest_tool_capability_internal(&g_odg_resources[id]);
    min_tier=odg_resource_harvest_min_tool_tier_internal(&g_odg_resources[id]);
    if(tool_type!=ODG_ITEM_NONE||tool_capability!=0u){
        slot=tool_capability!=0u?bot_best_capability_slot(a,tool_capability,min_tier):bot_best_tool_slot(a,tool_type,min_tier);
        if(slot==UINT32_MAX){
            if(!odg_resource_harvest_allows_hand_internal(&g_odg_resources[id])||!bot_select_empty_hand(a))return 0;
        }else a->inventory.selected_slot=slot;
    }
    if(odg_dist2(a->x,a->z,g_odg_resources[id].x,g_odg_resources[id].z)<=near2){
        int result=odg_resource_hold_tick(a->id,id);*out_x=0;*out_z=0;
        a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;
        if(result==2||!g_odg_resources[id].active||g_odg_resources[id].state!=ODG_RESOURCE_STATE_AVAILABLE){
            a->bot_economy_item_type=0u;a->bot_economy_target_id=UINT32_MAX;
        }
        return 1;
    }
    if(bot_nav_land_toward(a,g_odg_resources[id].x,g_odg_resources[id].z,out_x,out_z)){
        a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;
    }
    return 0;
}

int odg_bot_logistics_prepare_vehicle_internal(odg_actor *a,int32_t tx,int32_t tz,int32_t *out_x,int32_t *out_z){
    uint32_t id,slot;int64_t near2;
    int32_t fx,fz;
    if(a==NULL||out_x==NULL||out_z==NULL||a->id==ODG_PLAYER_ID)return 0;
    if(odg_artifact_actor_vehicle_internal(a->id)!=UINT32_MAX)return 0;
    if(!odg_bot_route_requires_raft_internal(a,tx,tz))return 0;
    near2=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX;

    /* Reuse is strictly preferred over production. Ignore boats stranded across the same
     * barrier because walking toward them would recreate the deadlock this planner fixes. */
    id=bot_find_accessible_empty_raft(a);
    if(id<g_odg.artifact_count&&!odg_bot_route_requires_raft_internal(a,g_odg_artifacts[id].x,g_odg_artifacts[id].z)){
        if(odg_dist2(a->x,a->z,g_odg_artifacts[id].x,g_odg_artifacts[id].z)<=near2){
            if(odg_artifact_vehicle_toggle_internal(a->id,id)){*out_x=0;*out_z=0;return 1;}
        }else if(bot_nav_land_toward(a,g_odg_artifacts[id].x,g_odg_artifacts[id].z,out_x,out_z))return 1;
    }

    /* A carried raft becomes a world object through the same selected-slot placement
     * transaction used by the player. Face the route so the deterministic placement fan
     * searches the shoreline ahead instead of an unrelated side. */
    if(odg_inventory_find_type(&a->inventory,ODG_ITEM_RAFT,ODG_MATERIAL_WOOD,&slot)){
        odg_normalize_q15(tx-a->x,tz-a->z,&fx,&fz);
        if(fx!=0||fz!=0){a->face_x_q15=fx;a->face_z_q15=fz;}
        a->inventory.selected_slot=slot;
        if(odg_artifact_place_selected(a->id)){*out_x=0;*out_z=0;a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;}
        /* No legal launch point yet: continue on foot until the owned shoreline enters
         * the ordinary placement fan. Never materialize the craft in neutral/enemy water. */
        if(bot_nav_land_toward(a,tx,tz,out_x,out_z)){a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;}
        *out_x=0;*out_z=0;return 1;
    }

    {
        odg_recipe_definition recipe;uint64_t required=0u;uint32_t i;
        if(odg_recipe_get(ODG_RECIPE_RAFT,&recipe,sizeof(recipe),&required)!=ODG_STATUS_OK)return 0;
        for(i=0u;i<recipe.ingredient_count;++i){
            const odg_recipe_ingredient *ingredient=&recipe.ingredients[i];
            if(odg_inventory_total(&a->inventory,ingredient->item_type,ingredient->material_tier)>=ingredient->quantity)continue;
            return bot_gather_resource_land_only(a,ingredient->item_type,out_x,out_z);
        }
    }

    /* A recovered workbench item is useful infrastructure, not dead cargo. Restore it
     * through ordinary placement when the bot has no deployed station. */
    id=bot_find_own_artifact(a,ODG_ITEM_WORKBENCH);
    if(id>=g_odg.artifact_count){
        if(odg_inventory_find_type(&a->inventory,ODG_ITEM_WORKBENCH,ODG_MATERIAL_NONE,&slot)){
            a->inventory.selected_slot=slot;
            if(odg_artifact_place_selected(a->id)){*out_x=0;*out_z=0;a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;}
        }
        return 0;
    }
    if(odg_dist2(a->x,a->z,g_odg_artifacts[id].x,g_odg_artifacts[id].z)>near2)
        return bot_nav_land_toward(a,g_odg_artifacts[id].x,g_odg_artifacts[id].z,out_x,out_z);
    if(odg_craft(a->id,ODG_RECIPE_RAFT,1u)==ODG_STATUS_OK){
        *out_x=0;*out_z=0;a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;
    }
    return 0;
}

static int bot_equip_inventory_expander(odg_actor *a) {
    if(a==NULL)return 0;
    return odg_inventory_equip_first_expander_internal(&a->inventory);
}

static int bot_place_owned_artifact(odg_actor *a,uint32_t item_type,int32_t *out_x,int32_t *out_z) {
    uint32_t slot;
    if(a==NULL||!odg_inventory_find_type(&a->inventory,item_type,ODG_MATERIAL_NONE,&slot))return 0;
    a->inventory.selected_slot=slot;
    if(odg_artifact_place_selected(a->id)){*out_x=0;*out_z=0;a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;}
    /* A blocked forward fan is transient while the bot walks inside its territory. */
    return 0;
}

static uint32_t bot_owned_turret_count(const odg_actor *a,uint32_t *out_best_tier) {
    uint32_t i,count=0u,best=ODG_MATERIAL_NONE;
    if(a==NULL)return 0u;
    for(i=0u;i<g_odg.turret_count;++i){const odg_turret *t=&g_odg_turrets[i];
        if(!t->active||t->carried_by!=ODG_TURRET_NONE||t->owner!=ODG_OWNER_FROM_ID(a->id))continue;
        ++count;if(t->material_tier>best)best=t->material_tier;
    }
    if(out_best_tier!=NULL)*out_best_tier=best;
    return count;
}

static int bot_home_fortification_target(const odg_actor *a,int64_t *out_gx,int64_t *out_gz,uint32_t *out_controlled) {
    static const int32_t offsets[][2]={{2,0},{0,2},{-2,0},{0,-2},{2,2},{-2,2},{-2,-2},{2,-2}};
    uint32_t i,j,controlled=0u;int found=0;int64_t target_x=0,target_z=0;
    if(a==NULL)return 0;
    for(i=0u;i<(uint32_t)(sizeof(offsets)/sizeof(offsets[0]));++i){
        int64_t gx=a->home_global_cell_x+offsets[i][0],gz=a->home_global_cell_z+offsets[i][1];
        int occupied=0;int32_t lx,lz;odg_surface_sample surface;
        if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_FROM_ID(a->id))continue;
        for(j=0u;j<g_odg_construction_count;++j){
            const odg_construction_block *b=&g_odg_construction_blocks[j];int64_t bx,bz;
            if(!b->active)continue;
            odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&bx,&bz);
            if(bx==gx&&bz==gz){occupied=1;++controlled;break;}
        }
        if(occupied||found)continue;
        if(!odg_global_cell_center_to_local_fx_internal(gx,gz,&lx,&lz))continue;
        if(!odg_environment_surface_local(lx,lz,&surface)||(surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)continue;
        if(!odg_position_clear_internal(lx,lz,ODG_FX_ONE*18/25))continue;
        if(!odg_dynamic_position_clear_internal(lx,lz,ODG_FX_ONE*18/25,a->id,UINT32_MAX))continue;
        target_x=gx;target_z=gz;found=1;
    }
    if(out_controlled!=NULL)*out_controlled=controlled;
    if(found&&out_gx!=NULL)*out_gx=target_x;
    if(found&&out_gz!=NULL)*out_gz=target_z;
    return found;
}

static uint32_t bot_find_damaged_home_construction(const odg_actor *a){
    uint32_t i,best=UINT32_MAX;int64_t best_d2=INT64_MAX;
    if(a==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg_construction_count;++i){
        const odg_construction_block *b=&g_odg_construction_blocks[i];int64_t gx,gz,hdx,hdz,d2;
        if(!b->active||!b->local_resident||b->health==0u||b->health>=b->max_health)continue;
        odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&gx,&gz);
        if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_FROM_ID(a->id))continue;
        hdx=gx-a->home_global_cell_x;hdz=gz-a->home_global_cell_z;
        if(hdx < -3 || hdx > 3 || hdz < -3 || hdz > 3)continue;
        d2=odg_dist2(a->x,a->z,b->x,b->z);if(d2<best_d2){best_d2=d2;best=i;}
    }
    return best;
}

static int bot_repair_home_construction_goal(odg_actor *a,int32_t *out_x,int32_t *out_z){
    uint32_t id,slot=UINT32_MAX,recipe_id;
    const int64_t near2=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX;
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    id=bot_find_damaged_home_construction(a);if(id>=g_odg_construction_count)return 0;
    if(odg_inventory_find_type(&a->inventory,ODG_ITEM_BUILDING_BLOCK,g_odg_construction_blocks[id].material_tier,&slot)){
        if(odg_dist2(a->x,a->z,g_odg_construction_blocks[id].x,g_odg_construction_blocks[id].z)<=near2){
            if(odg_construction_repair_internal(a->id,id)){*out_x=0;*out_z=0;a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;}
            return 0;
        }
        return bot_nav_toward(a,g_odg_construction_blocks[id].x,g_odg_construction_blocks[id].z,out_x,out_z);
    }
    recipe_id=odg_recipe_find_output_internal(ODG_ITEM_BUILDING_BLOCK,g_odg_construction_blocks[id].material_tier);
    if(recipe_id==0u)return 0;
    return bot_recipe_goal(a,recipe_id,out_x,out_z);
}

static uint32_t bot_find_salvageable_construction(const odg_actor *a) {
    uint32_t i,best=UINT32_MAX;int64_t best_d2=(int64_t)(20*ODG_FX_ONE)*(20*ODG_FX_ONE);
    if(a==NULL)return UINT32_MAX;
    for(i=0u;i<g_odg_construction_count;++i){
        const odg_construction_block *b=&g_odg_construction_blocks[i];int64_t gx,gz,hdx,hdz,d2;uint8_t land_owner;
        if(!b->active||!b->local_resident||b->owner_actor_id==a->id||
           !odg_construction_actor_may_dismantle_internal(a->id,i))continue;
        odg_global_fx_to_global_cell_internal(b->global_fx_x,b->global_fx_z,&gx,&gz);
        land_owner=odg_chunk_owner_at_global_cell(gx,gz);
        if(land_owner!=ODG_OWNER_NONE&&land_owner!=ODG_OWNER_FROM_ID(a->id))continue;
        hdx=gx-a->home_global_cell_x;hdz=gz-a->home_global_cell_z;
        /* A conquered module already serving the home perimeter is useful infrastructure;
         * keep it instead of dismantling it merely because its historical builder differs. */
        if(hdx>=-3&&hdx<=3&&hdz>=-3&&hdz<=3)continue;
        d2=odg_dist2(a->x,a->z,b->x,b->z);if(d2<best_d2){best_d2=d2;best=i;}
    }
    return best;
}

static int bot_salvage_construction_goal(odg_actor *a,int32_t *out_x,int32_t *out_z) {
    uint32_t id;const int64_t near2=(int64_t)ODG_ARTIFACT_INTERACT_RANGE_FX*ODG_ARTIFACT_INTERACT_RANGE_FX;
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    id=bot_find_salvageable_construction(a);if(id>=g_odg_construction_count)return 0;
    if(odg_dist2(a->x,a->z,g_odg_construction_blocks[id].x,g_odg_construction_blocks[id].z)<=near2){
        if(odg_construction_dismantle_internal(a->id,id)){*out_x=0;*out_z=0;a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;}
        return 0;
    }
    return bot_nav_toward(a,g_odg_construction_blocks[id].x,g_odg_construction_blocks[id].z,out_x,out_z);
}

static int bot_fortify_home_goal(odg_actor *a,int32_t *out_x,int32_t *out_z) {
    int64_t gx=0,gz=0;uint32_t controlled=0u,slot;int32_t hx,hz;
    const int64_t home_reach2=(int64_t)(ODG_FX_ONE*3/2)*(ODG_FX_ONE*3/2);
    if(a==NULL||out_x==NULL||out_z==NULL)return 0;
    /* Preserve existing infrastructure before expanding it. Repairs use the same material
     * transaction as the player and may trigger real gather/craft/station work. */
    if(bot_repair_home_construction_goal(a,out_x,out_z))return 1;
    if(!bot_home_fortification_target(a,&gx,&gz,&controlled)||controlled>=4u)return 0;
    slot=bot_best_capability_slot(a,ODG_ITEM_CAP_CONSTRUCT,ODG_MATERIAL_WOOD);
    if(slot==UINT32_MAX){
        /* Reuse already-existing material before cutting another tree. Conquest/neutral
         * land makes an abandoned hostile module salvageable through ordinary authority. */
        if(bot_salvage_construction_goal(a,out_x,out_z))return 1;
        return bot_recipe_goal(a,ODG_RECIPE_BUILD_BLOCK_WOOD,out_x,out_z);
    }
    if(!global_cell_center_local_fx(a->home_global_cell_x,a->home_global_cell_z,&hx,&hz))return 0;
    if(odg_dist2(a->x,a->z,hx,hz)>home_reach2)return bot_nav_toward(a,hx,hz,out_x,out_z);
    a->inventory.selected_slot=slot;
    /* Fortification is intentionally a sparse ground obstacle. Runtime build mode is
     * actor-local and may have been changed by other AI work; never let that incidental
     * state turn a defensive perimeter into floors, openings, or unsupported roofs. */
    if(!odg_construction_set_shape_internal(a->id,ODG_CONSTRUCTION_SHAPE_WALL))return 0;
    if(odg_construction_place_selected_at_global_cell_internal(a->id,gx,gz)){
        *out_x=0;*out_z=0;a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;
    }
    return 0;
}

static int bot_place_turret_item(odg_actor *a,int32_t *out_x,int32_t *out_z) {
    uint32_t slot;
    if(a==NULL||!odg_inventory_find_type(&a->inventory,ODG_ITEM_TURRET,ODG_MATERIAL_NONE,&slot))return 0;
    a->inventory.selected_slot=slot;
    if(odg_turret_place_selected(a->id)){*out_x=0;*out_z=0;a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;return 1;}
    {
        int32_t hx,hz;
        if(global_cell_center_local_fx(a->home_global_cell_x,a->home_global_cell_z,&hx,&hz))return bot_nav_toward(a,hx,hz,out_x,out_z);
    }
    return 0;
}

int odg_bot_economy_direction_internal(odg_actor *a,int32_t *out_x,int32_t *out_z) {
    uint32_t owned_turrets,best_turret_tier;
    int has_wood_axe,has_wood_pick,has_stone_pick,has_stone_axe,has_iron_pick;
    if(a==NULL||out_x==NULL||out_z==NULL||a->id==ODG_PLAYER_ID)return 0;
    /* Strategic order lives here; economic quantities do not. Every acquisition below
     * asks the authoritative recipe table what is missing before harvesting/crafting. */
    has_wood_axe=bot_best_tool_slot(a,ODG_ITEM_AXE,ODG_MATERIAL_WOOD)!=UINT32_MAX;
    has_wood_pick=bot_best_tool_slot(a,ODG_ITEM_PICKAXE,ODG_MATERIAL_WOOD)!=UINT32_MAX;
    has_stone_pick=bot_inventory_has(a,ODG_ITEM_PICKAXE,ODG_MATERIAL_STONE)||bot_inventory_has(a,ODG_ITEM_PICKAXE,ODG_MATERIAL_IRON);
    has_stone_axe=bot_inventory_has(a,ODG_ITEM_AXE,ODG_MATERIAL_STONE)||bot_inventory_has(a,ODG_ITEM_AXE,ODG_MATERIAL_IRON);
    has_iron_pick=bot_inventory_has(a,ODG_ITEM_PICKAXE,ODG_MATERIAL_IRON);
    owned_turrets=bot_owned_turret_count(a,&best_turret_tier);

    /* Bootstrap invests in efficiency early; only the priority is AI policy. */
    if(!has_wood_axe)return bot_recipe_goal(a,ODG_RECIPE_AXE_WOOD,out_x,out_z);
    if(!has_wood_pick)return bot_recipe_goal(a,ODG_RECIPE_PICKAXE_WOOD,out_x,out_z);

    if(a->inventory.equipped_backpack_type==ODG_ITEM_NONE){
        if(bot_equip_inventory_expander(a)){*out_x=0;*out_z=0;return 1;}
        return bot_recipe_goal(a,ODG_RECIPE_BACKPACK,out_x,out_z);
    }
    if(!has_stone_axe)return bot_recipe_goal(a,ODG_RECIPE_AXE_STONE,out_x,out_z);
    if(!has_stone_pick)return bot_recipe_goal(a,ODG_RECIPE_PICKAXE_STONE,out_x,out_z);

    if(bot_find_own_artifact(a,ODG_ITEM_SMITHY)>=g_odg.artifact_count){
        if(bot_inventory_has(a,ODG_ITEM_SMITHY,ODG_MATERIAL_NONE)){
            if(bot_place_owned_artifact(a,ODG_ITEM_SMITHY,out_x,out_z))return 1;
            {
                int32_t hx,hz;
                if(global_cell_center_local_fx(a->home_global_cell_x,a->home_global_cell_z,&hx,&hz))
                    return bot_nav_toward(a,hx,hz,out_x,out_z);
            }
            return 0;
        }
        return bot_recipe_goal(a,ODG_RECIPE_SMITHY,out_x,out_z);
    }

    if(!has_iron_pick)return bot_recipe_goal(a,ODG_RECIPE_PICKAXE_IRON,out_x,out_z);

    if(owned_turrets==0u){
        if(bot_inventory_has(a,ODG_ITEM_TURRET,ODG_MATERIAL_NONE))return bot_place_turret_item(a,out_x,out_z);
        return bot_recipe_goal(a,ODG_RECIPE_TURRET_WOOD,out_x,out_z);
    }
    if(best_turret_tier==ODG_MATERIAL_WOOD&&!bot_inventory_has(a,ODG_ITEM_ASCENSION_CHIP,ODG_MATERIAL_STONE))
        return bot_recipe_goal(a,ODG_RECIPE_ASCEND_STONE,out_x,out_z);
    if(best_turret_tier==ODG_MATERIAL_STONE&&!bot_inventory_has(a,ODG_ITEM_ASCENSION_CHIP,ODG_MATERIAL_IRON))
        return bot_recipe_goal(a,ODG_RECIPE_ASCEND_IRON,out_x,out_z);

    /* Mature stock targets are policy; each production batch still resolves its real
     * ingredient demand from the recipe, so balance edits cannot desynchronize bots. */
    if(odg_inventory_total(&a->inventory,ODG_ITEM_AMMO,ODG_MATERIAL_NONE)<24u)
        if(bot_recipe_goal(a,ODG_RECIPE_AMMO_X12,out_x,out_z))return 1;
    if(!bot_inventory_has(a,ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_IRON))
        if(bot_recipe_goal(a,ODG_RECIPE_REPROGRAM_IRON,out_x,out_z))return 1;

    if(bot_fortify_home_goal(a,out_x,out_z))return 1;
    return 0;
}

static void bot_control(odg_actor *a, int32_t *out_x, int32_t *out_z) {
    uint32_t current_cell;
    int64_t current_global_x,current_global_z;
    if (!a || !out_x || !out_z) return;
    if (a->think_cd > 0u) --a->think_cd;
    if (a->ai_commit_ticks > 0u) --a->ai_commit_ticks;
    current_cell=UINT32_MAX;
    odg_global_fx_to_global_cell_internal(a->global_fx_x,a->global_fx_z,&current_global_x,&current_global_z);
    /* ai_plan_cell is only a resident-window cache. Never clamp an off-window global
     * actor onto a fake border cell; global plan coordinates remain the authority. */
    (void)odg_global_cell_to_local_internal(current_global_x,current_global_z,&current_cell);

    /* Survival/recovery has a bounded priority over industry. It never grants items:
     * the bot walks to real food or its real death cache and uses ordinary transactions. */
    {
        int32_t sx=0,sz=0;
        if(bot_survival_direction(a,&sx,&sz)){
            a->ai_x_q15=sx;a->ai_z_q15=sz;*out_x=sx;*out_z=sz;return;
        }
    }

    /* Economy only targets resources the actor is currently allowed to exploit. Enemy
     * land must be conquered first, so the planner cannot deadlock forever harvesting a
     * protected node through the territory policy. */
    {
        int32_t ex=0,ez=0;
        if(odg_bot_economy_direction_internal(a,&ex,&ez)){
            a->ai_x_q15=ex;a->ai_z_q15=ez;*out_x=ex;*out_z=ez;return;
        }
    }

    /* Logistics steering is sampled, not recomputed every tick. Re-pointing at a
     * moving/near target every 1/120 s was one source of left-right chatter. */
    if (!a->trail_active) {
        int32_t sx=0,sz=0;
        if ((a->ai_commit_ticks==0u || (a->ai_x_q15==0 && a->ai_z_q15==0)) &&
            bot_supply_direction(a,&sx,&sz)) {
            a->ai_x_q15=sx;
            a->ai_z_q15=sz;
            a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;
            a->think_cd=12u;
            *out_x=a->ai_x_q15; *out_z=a->ai_z_q15;
            return;
        }
    }

    if (!a->trail_active) {
        if ((a->think_cd == 0u && a->ai_commit_ticks==0u) ||
            (a->ai_x_q15 == 0 && a->ai_z_q15 == 0)) {
            bot_choose_expansion(a);
            a->think_cd = 28u + odg_rand_bounded(&a->rng, 38u);
        }
    } else if (a->bot_mode == ODG_BOT_OUTBOUND && a->trail_len >= a->bot_leg_target) {
        bot_set_side_direction(a);
        a->bot_mode = ODG_BOT_SIDELEG;
        a->bot_leg_target = a->trail_len + 2u + odg_rand_bounded(&a->rng, 3u);
    } else if (a->bot_mode == ODG_BOT_SIDELEG && a->trail_len >= a->bot_leg_target) {
        bot_set_return_direction(a);
    }

    /* A return path is replanned only after entering another territory cell. The previous
     * four-tick replanner could alternately prefer X then Z while the cube was still
     * inside one cell, visibly producing left-right-left-right motion. */
    if (a->bot_mode == ODG_BOT_RETURN &&
        (a->ai_plan_global_cell_x!=current_global_x || a->ai_plan_global_cell_z!=current_global_z ||
         (a->ai_x_q15==0 && a->ai_z_q15==0))) {
        int32_t hx = a->ai_x_q15;
        int32_t hz = a->ai_z_q15;
        if (bot_find_safe_home_step(a, &hx, &hz)) {
            a->ai_x_q15 = hx;
            a->ai_z_q15 = hz;
            a->ai_plan_cell = current_cell;
            a->ai_plan_global_cell_x=current_global_x;a->ai_plan_global_cell_z=current_global_z;
            a->ai_commit_ticks = ODG_BOT_STEER_COMMIT_TICKS;
        }
    }

    if (a->trail_active && bot_enemy_turret_risk(a,a->x,a->z)!=0u && a->bot_mode!=ODG_BOT_RETURN) {
        if (a->bot_mode==ODG_BOT_OUTBOUND) {
            bot_set_side_direction(a); a->bot_mode=ODG_BOT_SIDELEG; a->bot_leg_target=a->trail_len+2u;
        } else {
            bot_set_return_direction(a);
        }
    }

    if (a->trail_active && a->trail_len + 4u >= ODG_BOT_TRAIL_SOFT_LIMIT &&
        a->bot_mode == ODG_BOT_SIDELEG) {
        bot_set_return_direction(a);
    }

    /* Obstacle avoidance has hysteresis. Once a side is selected, keep it long enough
     * for the actor's physical turn to make progress instead of choosing the opposite
     * side on the next tick. */
    if (!direction_clear_for_actor(a, a->ai_x_q15, a->ai_z_q15) && a->ai_commit_ticks==0u) {
        int32_t x = a->ai_x_q15;
        int32_t z = a->ai_z_q15;
        int32_t preferred_x = a->turn_sign > 0 ? -z : z;
        int32_t preferred_z = a->turn_sign > 0 ? x : -x;
        int32_t other_x = -preferred_x;
        int32_t other_z = -preferred_z;
        if (direction_clear_for_actor(a, preferred_x, preferred_z)) {
            a->ai_x_q15=preferred_x; a->ai_z_q15=preferred_z;
            a->ai_commit_ticks=2u*ODG_BOT_STEER_COMMIT_TICKS;
        } else if (direction_clear_for_actor(a, other_x, other_z)) {
            a->turn_sign=-a->turn_sign;
            a->ai_x_q15=other_x; a->ai_z_q15=other_z;
            a->ai_commit_ticks=2u*ODG_BOT_STEER_COMMIT_TICKS;
        } else if (!a->trail_active) {
            bot_choose_expansion(a);
        }
    }


    *out_x = a->ai_x_q15;
    *out_z = a->ai_z_q15;
}

static int32_t q15_dot(int32_t ax, int32_t az, int32_t bx, int32_t bz) {
    int64_t d = ((int64_t)ax * bx + (int64_t)az * bz) / ODG_Q15_ONE;
    return odg_clamp_i32((int32_t)d, -ODG_Q15_ONE, ODG_Q15_ONE);
}

static int32_t q15_cross(int32_t ax, int32_t az, int32_t bx, int32_t bz) {
    int64_t c = ((int64_t)ax * bz - (int64_t)az * bx) / ODG_Q15_ONE;
    return odg_clamp_i32((int32_t)c, -ODG_Q15_ONE, ODG_Q15_ONE);
}

/* Steering and facing are intentionally separate. The stick names an exact WORLD
 * heading; translation bends toward it immediately with a bounded angular step while
 * the visible cube keeps its own inertial body rotation. This preserves a readable arc
 * without the old failure mode where the cube continued straight until facing caught up. */
static void steer_translation_heading(int32_t current_x, int32_t current_z,
                                      int32_t target_x, int32_t target_z,
                                      int32_t max_sin_q15,
                                      int32_t *out_x, int32_t *out_z) {
    int32_t dot;
    int32_t cross;
    int32_t sign;
    int32_t error;
    int32_t sin_step;
    int32_t cos_step;
    int32_t nx;
    int32_t nz;
    int32_t new_cross;
    if (!out_x || !out_z) return;
    if ((target_x == 0 && target_z == 0) || (current_x == 0 && current_z == 0)) {
        *out_x = target_x;
        *out_z = target_z;
        return;
    }
    odg_normalize_q15(current_x, current_z, &current_x, &current_z);
    odg_normalize_q15(target_x, target_z, &target_x, &target_z);
    dot = q15_dot(current_x, current_z, target_x, target_z);
    cross = q15_cross(current_x, current_z, target_x, target_z);
    if (dot > 32754 && odg_abs_i32(cross) < 920) {
        *out_x = target_x;
        *out_z = target_z;
        return;
    }
    /* A near-opposite request is handled by approach_heading_velocity(): it brakes the
     * old trajectory before inversion. Returning the target here avoids an arbitrary
     * left/right choice at exactly 180 degrees. */
    if (dot < -22000) {
        *out_x = target_x;
        *out_z = target_z;
        return;
    }
    sign = cross >= 0 ? 1 : -1;
    error = odg_abs_i32(cross);
    if (dot < 0) {
        int32_t reverse_mag = (ODG_Q15_ONE - dot) / 2;
        if (reverse_mag > error) error = reverse_mag;
    }
    if (error < 1800) error = 1800;
    if (error > ODG_Q15_ONE) error = ODG_Q15_ONE;
    sin_step = (int32_t)(((int64_t)max_sin_q15 * error) / ODG_Q15_ONE);
    if (sin_step < 1) sin_step = 1;
    cos_step = ODG_Q15_ONE - (int32_t)(((int64_t)sin_step * sin_step) / (2 * ODG_Q15_ONE));
    if (sign > 0) {
        nx = (int32_t)(((int64_t)current_x * cos_step - (int64_t)current_z * sin_step) / ODG_Q15_ONE);
        nz = (int32_t)(((int64_t)current_x * sin_step + (int64_t)current_z * cos_step) / ODG_Q15_ONE);
    } else {
        nx = (int32_t)(((int64_t)current_x * cos_step + (int64_t)current_z * sin_step) / ODG_Q15_ONE);
        nz = (int32_t)((-(int64_t)current_x * sin_step + (int64_t)current_z * cos_step) / ODG_Q15_ONE);
    }
    odg_normalize_q15(nx, nz, &nx, &nz);
    new_cross = q15_cross(nx, nz, target_x, target_z);
    if ((cross > 0 && new_cross <= 0) || (cross < 0 && new_cross >= 0)) {
        nx = target_x;
        nz = target_z;
    }
    *out_x = nx;
    *out_z = nz;
}

/* Translation uses a scalar speed plus an authoritative requested heading. Component-wise
 * inertia made a fresh diagonal command inherit the old axis for several ticks. v8 keeps
 * acceleration in speed, not in direction. Only a near-opposite reversal brakes along the
 * old vector before changing sign; normal steering obeys the requested heading immediately. */
static void approach_heading_velocity(odg_actor *a, int32_t dir_x_q15, int32_t dir_z_q15,
                                      int32_t target_speed, int32_t accel, int32_t brake) {
    int32_t current_speed;
    int32_t next_speed;
    int32_t use_x=dir_x_q15,use_z=dir_z_q15;
    int32_t cur_x=0,cur_z=0;
    int reversing=0;
    if (!a) return;
    current_speed=a->speed_fx;
    if (current_speed<0) current_speed=0;
    if (a->vx!=0 || a->vz!=0) odg_normalize_q15(a->vx,a->vz,&cur_x,&cur_z);

    if (target_speed<=0 || (dir_x_q15==0 && dir_z_q15==0)) {
        next_speed=approach_signed(current_speed,0,brake);
        if (cur_x!=0 || cur_z!=0) {use_x=cur_x;use_z=cur_z;}
        else {use_x=a->face_x_q15;use_z=a->face_z_q15;}
    } else {
        if ((cur_x!=0 || cur_z!=0) && current_speed>6) {
            int32_t d=q15_dot(cur_x,cur_z,dir_x_q15,dir_z_q15);
            /* >~132 degree reversal: brake before translating backward. A 90 degree or
             * diagonal change is NOT delayed; precision wins there. */
            if (d < -22000) reversing=1;
        }
        if (reversing) {
            next_speed=approach_signed(current_speed,0,brake);
            use_x=cur_x;use_z=cur_z;
        } else {
            int32_t step=target_speed<current_speed?brake:accel;
            next_speed=approach_signed(current_speed,target_speed,step);
            use_x=dir_x_q15;use_z=dir_z_q15;
        }
    }
    a->speed_fx=next_speed;
    a->vx=(int32_t)(((int64_t)use_x*next_speed)/ODG_Q15_ONE);
    a->vz=(int32_t)(((int64_t)use_z*next_speed)/ODG_Q15_ONE);
}

/* Collision response is continuous enough for a 120 Hz fixed step: try the requested
 * displacement, then retain the unobstructed tangent component instead of zeroing the
 * whole velocity.  This removes the old stop-go feeling along rocks/buildings/coast. */
static void rotate_dir_q15(int32_t x, int32_t z, int32_t cos_q15, int32_t sin_q15,
                           int sign, int32_t *out_x, int32_t *out_z) {
    int32_t nx,nz;
    if (sign >= 0) {
        nx=(int32_t)(((int64_t)x*cos_q15-(int64_t)z*sin_q15)/ODG_Q15_ONE);
        nz=(int32_t)(((int64_t)x*sin_q15+(int64_t)z*cos_q15)/ODG_Q15_ONE);
    } else {
        nx=(int32_t)(((int64_t)x*cos_q15+(int64_t)z*sin_q15)/ODG_Q15_ONE);
        nz=(int32_t)((-(int64_t)x*sin_q15+(int64_t)z*cos_q15)/ODG_Q15_ONE);
    }
    odg_normalize_q15(nx,nz,out_x,out_z);
}

static int contact_candidate_clear(const odg_actor *a,int32_t dx_q15,int32_t dz_q15,int32_t speed,
                                   int32_t *out_x,int32_t *out_z) {
    int32_t vx=(int32_t)(((int64_t)dx_q15*speed)/ODG_Q15_ONE);
    int32_t vz=(int32_t)(((int64_t)dz_q15*speed)/ODG_Q15_ONE);
    if (vx==0 && vz==0) return 0;
    if (!actor_position_clear(a,a->x+vx,a->z+vz)) return 0;
    if (out_x) *out_x=vx;
    if (out_z) *out_z=vz;
    return 1;
}

/* v10 contact steering: collision may change the ACTUAL displacement, never the user's
 * commanded world heading.  When the requested step is blocked, search a narrow angular
 * fan for the closest legal tangent and latch that contact side briefly.  This removes
 * the old X/Z stair-step at organic coastlines and obstacle corners while a true head-on
 * collision still stops rather than inventing a route. */
static void move_actor_with_slide(odg_actor *a) {
    /* 11.25 .. 78.75 degrees. The broad end of this fan is only reached when the
     * requested direction is genuinely blocked; nearest legal angle always wins. */
    static const int32_t rot[][2] = {
        {32137,6393},{30273,12539},{27245,18204},{23170,23170},
        {18204,27245},{12539,30273},{6393,32137}
    };
    int32_t ox,oz,req_x,req_z,req_dir_x=0,req_dir_z=0,speed;
    int32_t chosen_vx=0,chosen_vz=0,chosen_dx=0,chosen_dz=0;
    uint32_t i;
    int chosen_sign=0;
    if (!a) return;
    if (a->slide_lock_ticks>0u) --a->slide_lock_ticks;
    ox=a->x;oz=a->z;req_x=a->vx;req_z=a->vz;
    speed=(int32_t)odg_isqrt_u64((uint64_t)((int64_t)req_x*req_x+(int64_t)req_z*req_z));
    if (speed<=0) {a->slide_lock_ticks=0u;a->slide_axis=0u;return;}
    odg_normalize_q15(req_x,req_z,&req_dir_x,&req_dir_z);

    if (actor_position_clear(a,ox+req_x,oz+req_z)) {
        a->x=ox+req_x;a->z=oz+req_z;
        a->slide_lock_ticks=0u;a->slide_axis=0u;
        a->slide_dir_x_q15=0;a->slide_dir_z_q15=0;
        return;
    }

    /* Reuse a still-valid tangent. This hysteresis prevents corner chatter. */
    if (a->slide_lock_ticks>0u && (a->slide_dir_x_q15!=0 || a->slide_dir_z_q15!=0) &&
        q15_dot(req_dir_x,req_dir_z,a->slide_dir_x_q15,a->slide_dir_z_q15)>=ODG_CONTACT_STEER_MIN_DOT_Q15 &&
        contact_candidate_clear(a,a->slide_dir_x_q15,a->slide_dir_z_q15,speed,&chosen_vx,&chosen_vz)) {
        chosen_dx=a->slide_dir_x_q15;chosen_dz=a->slide_dir_z_q15;
        chosen_sign=(a->slide_axis==2u)?-1:1;
    } else {
        /* Nearest angle wins. If both sides are legal, preserve the last side or use
         * the actor's deterministic turn preference. */
        for (i=0u;i<(uint32_t)(sizeof(rot)/sizeof(rot[0])) && chosen_sign==0;++i) {
            int order0=(a->slide_axis==2u || (a->slide_axis==0u && a->turn_sign<0))?-1:1;
            int pass;
            for (pass=0;pass<2;++pass) {
                int sign=pass==0?order0:-order0;
                int32_t dx,dz,vx,vz;
                rotate_dir_q15(req_dir_x,req_dir_z,rot[i][0],rot[i][1],sign,&dx,&dz);
                if (q15_dot(req_dir_x,req_dir_z,dx,dz)<ODG_CONTACT_STEER_MIN_DOT_Q15) continue;
                if (contact_candidate_clear(a,dx,dz,speed,&vx,&vz)) {
                    chosen_vx=vx;chosen_vz=vz;chosen_dx=dx;chosen_dz=dz;chosen_sign=sign;break;
                }
            }
        }
    }

    if (chosen_sign!=0) {
        a->x=ox+chosen_vx;a->z=oz+chosen_vz;
        a->vx=chosen_vx;a->vz=chosen_vz;
        a->slide_dir_x_q15=chosen_dx;a->slide_dir_z_q15=chosen_dz;
        a->slide_axis=chosen_sign>0?1u:2u;
        a->slide_lock_ticks=ODG_SLIDE_LOCK_TICKS;
    } else {
        /* A full 120 Hz step can be rejected while a shorter step is still legal.
         * Preserve that sub-step before declaring a true head-on stop. */
        int32_t divisor;
        int moved=0;
        for (divisor=2;divisor<=8;divisor*=2) {
            int32_t vx=req_x/divisor;
            int32_t vz=req_z/divisor;
            if ((vx!=0 || vz!=0) && actor_position_clear(a,ox+vx,oz+vz)) {
                a->x=ox+vx;a->z=oz+vz;a->vx=vx;a->vz=vz;
                a->slide_axis=0u;a->slide_lock_ticks=0u;
                a->slide_dir_x_q15=0;a->slide_dir_z_q15=0;
                moved=1;break;
            }
        }
        if (!moved) {
            a->vx=0;a->vz=0;a->speed_fx=0;
            a->slide_axis=0u;a->slide_lock_ticks=0u;
            a->slide_dir_x_q15=0;a->slide_dir_z_q15=0;
        }
    }
    {
        uint32_t mag=odg_isqrt_u64((uint64_t)((int64_t)a->vx*a->vx+(int64_t)a->vz*a->vz));
        a->speed_fx=mag>INT32_MAX?INT32_MAX:(int32_t)mag;
    }
}

static int32_t terrain_speed_factor_q15(const odg_actor *a, int32_t dir_x_q15, int32_t dir_z_q15) {
    const int32_t sample = ODG_FX_ONE / 2;
    int32_t ax,az,bx,bz;
    int32_t dh;
    int32_t factor=ODG_Q15_ONE;
    if (!a) return factor;
    if (dir_x_q15==0 && dir_z_q15==0) return factor;
    ax=a->x+(int32_t)(((int64_t)dir_x_q15*sample)/ODG_Q15_ONE);
    az=a->z+(int32_t)(((int64_t)dir_z_q15*sample)/ODG_Q15_ONE);
    bx=a->x-(int32_t)(((int64_t)dir_x_q15*sample)/ODG_Q15_ONE);
    bz=a->z-(int32_t)(((int64_t)dir_z_q15*sample)/ODG_Q15_ONE);
    {
        odg_surface_sample ahead,behind,here;
        if(odg_environment_surface_local(ax,az,&ahead)&&odg_environment_surface_local(bx,bz,&behind)){
            int32_t ah=actor_support_height_milli(&ahead);
            int32_t bh=actor_support_height_milli(&behind);
            dh=(int32_t)(((int64_t)(ah-bh)*ODG_FX_ONE)/1000);
        }else dh=odg_terrain_height_fx(ax,az)-odg_terrain_height_fx(bx,bz);
        /* Uphill work is visible in speed without turning slopes into sticky walls.
         * Deep-water movement uses the water-supported body height, so submerged basin
         * geometry does not apply a fake hill penalty while the actor is swimming. */
        if (dh>0) factor-=odg_clamp_i32(dh*18,0,9000);
        else if (dh<0) factor+=odg_clamp_i32((-dh)*6,0,2500);
        if (odg_environment_surface_local(a->x,a->z,&here) && (here.flags&ODG_SURFACE_FLAG_WATER)!=0u)
            factor=(int32_t)(((int64_t)factor*ODG_WATER_MOVE_FACTOR_Q15)/ODG_Q15_ONE);
    }
    return odg_clamp_i32(factor,15000,ODG_Q15_ONE+2500);
}

static int32_t steering_speed_factor_q15(int32_t face_x,int32_t face_z,
                                         int32_t desired_x,int32_t desired_z) {
    int32_t dot=q15_dot(face_x,face_z,desired_x,desired_z);
    if (dot>=28000) return ODG_Q15_ONE;
    if (dot>=0) return 19000+(int32_t)(((int64_t)dot*13767)/28000);
    if (dot>=-24000) return 8000+(int32_t)(((int64_t)(dot+24000)*11000)/24000);
    return 2500+(int32_t)(((int64_t)(dot+ODG_Q15_ONE)*5500)/(ODG_Q15_ONE-24000));
}


static void update_actor(odg_actor *a) {
    if(a==NULL||!a->active)return;
    if(!a->local_resident){
        int32_t lx=0,lz=0;
        if(odg_global_fx_to_local_internal(a->global_fx_x,a->global_fx_z,&lx,&lz)){a->x=lx;a->z=lz;a->local_resident=1u;}
        else{if(a->hp==0u)update_respawn(a);return;}
    }
    int32_t desired_x = 0;
    int32_t desired_z = 0;
    int32_t move_x = 0;
    int32_t move_z = 0;
    int32_t strength_q15 = 0;
    int32_t base_speed;
    int32_t target_speed = 0;
    uint32_t mounted_vehicle;
    int was_swimming;
    if (!a || !a->active || g_odg.match_over) return;
    mounted_vehicle=odg_artifact_actor_vehicle_internal(a->id);
    was_swimming=odg_actor_is_swimming_internal(a);
    if (a->hp == 0u) { update_respawn(a); return; }
    if(g_odg.territory_count[a->id]==0u){
        if(a->territory_recovery_ticks<ODG_TERRITORY_RECOVERY_DELAY_TICKS)++a->territory_recovery_ticks;
        if(a->territory_recovery_ticks>=ODG_TERRITORY_RECOVERY_DELAY_TICKS){stamp_recovery_territory(a);a->territory_recovery_ticks=0u;}
    }else a->territory_recovery_ticks=0u;

    if (a->dash_cd > 0u) --a->dash_cd;
    if (a->dash_ticks > 0u) --a->dash_ticks;
    if (a->flash_ticks > 0u) --a->flash_ticks;
    if (a->melee_cooldown_ticks > 0u) --a->melee_cooldown_ticks;

    if (a->type==ODG_ACTOR_PLAYER && mounted_vehicle==UINT32_MAX && !was_swimming &&
        (g_odg.input.buttons&ODG_BUTTON_JUMP)!=0u && a->grounded!=0u) {
        a->grounded=0u;
        a->vertical_velocity_fx=ODG_JUMP_INITIAL_VY_FX;
        a->vertical_offset_fx=1;
    }

    if (a->type == ODG_ACTOR_PLAYER) {
        int has_heading=0;
        if (g_odg.input.world_heading_mode!=0u) {
            /* v12 exact world-control path: the host has already resolved the fixed joystick
             * against the current camera and supplies one stable WORLD heading. Camera
             * chase can rotate freely without feeding back into translation, while a
             * new drag delta can bend this heading immediately on the next tick. */
            strength_q15=odg_clamp_i32(g_odg.input.move_strength_q15,0,ODG_Q15_ONE);
            if (strength_q15>ODG_PLAYER_INPUT_DEADZONE &&
                (g_odg.input.move_x_q15!=0 || g_odg.input.move_z_q15!=0)) {
                desired_x=g_odg.input.move_x_q15;
                desired_z=g_odg.input.move_z_q15;
                odg_normalize_q15(desired_x,desired_z,&desired_x,&desired_z);
                g_odg.control_heading_x_q15=desired_x;
                g_odg.control_heading_z_q15=desired_z;
                g_odg.control_strength_q15=strength_q15;
                g_odg.control_basis_x_q15=g_odg.camera_dir_x_q15;
                g_odg.control_basis_z_q15=g_odg.camera_dir_z_q15;
                g_odg.control_active=1u;
                a->control_raw_x_q15=0;
                a->control_raw_z_q15=0;
                has_heading=1;
            }
        } else {
            int32_t lx=g_odg.input.move_x_q15;
            int32_t lf=g_odg.input.move_z_q15;
            uint32_t mag=odg_isqrt_u64((uint64_t)((int64_t)lx*lx+(int64_t)lf*lf));
            if(mag>(uint32_t)ODG_PLAYER_INPUT_DEADZONE){
                int32_t raw_x,raw_z;
                int32_t fx=g_odg.camera_dir_x_q15,fz=g_odg.camera_dir_z_q15;
                int32_t rx=fz,rz=-fx;
                uint32_t cmag=mag>(uint32_t)ODG_Q15_ONE?(uint32_t)ODG_Q15_ONE:mag;
                odg_normalize_q15(lx,lf,&raw_x,&raw_z);
                strength_q15=(int32_t)(((uint64_t)(cmag-(uint32_t)ODG_PLAYER_INPUT_DEADZONE)*(uint32_t)ODG_Q15_ONE)/
                                       (uint32_t)(ODG_Q15_ONE-ODG_PLAYER_INPUT_DEADZONE));
                /* v12 camera-relative locomotion: the fixed joystick is a local vector,
                 * never a world-heading dial. Holding UP while the independently controlled
                 * camera rotates continuously rotates the requested world velocity too. */
                desired_x=(int32_t)((((int64_t)rx*raw_x)+((int64_t)fx*raw_z))/ODG_Q15_ONE);
                desired_z=(int32_t)((((int64_t)rz*raw_x)+((int64_t)fz*raw_z))/ODG_Q15_ONE);
                odg_normalize_q15(desired_x,desired_z,&desired_x,&desired_z);
                g_odg.control_basis_x_q15=fx;g_odg.control_basis_z_q15=fz;
                g_odg.control_heading_x_q15=desired_x;g_odg.control_heading_z_q15=desired_z;
                g_odg.control_strength_q15=strength_q15;g_odg.control_active=1u;
                a->control_raw_x_q15=raw_x;a->control_raw_z_q15=raw_z;
                has_heading=1;
            }
        }
        if (!has_heading) {
            g_odg.control_active = 0u;
            g_odg.control_heading_x_q15 = 0;
            g_odg.control_heading_z_q15 = 0;
            g_odg.control_strength_q15 = 0;
            a->control_raw_x_q15 = 0;
            a->control_raw_z_q15 = 0;
            a->steer_q15 = 0;
            strength_q15=0;
        } else {
            a->steer_q15=q15_cross(a->face_x_q15,a->face_z_q15,desired_x,desired_z);
            rotate_vec_inertial_toward(&a->face_x_q15,&a->face_z_q15,desired_x,desired_z,
                                       &a->turn_rate_q15,ODG_PLAYER_TURN_MAX_SIN_Q15,
                                       ODG_PLAYER_TURN_ACCEL_Q15,&a->turn_sign);
            /* Precision rule: the joystick owns ground-path direction immediately. Body
             * orientation is a separate inertial presentation/steering state, so it may lag
             * visually without dragging the player's trajectory with it. Scalar speed still
             * accelerates/brakes smoothly, and approach_heading_velocity preserves the one
             * deliberate exception: a near-180 degree reversal brakes before translating
             * backward. This makes camera-relative UP follow camera yaw on the same tick. */
            move_x=desired_x;
            move_z=desired_z;
        }
        if (mounted_vehicle==UINT32_MAX && !was_swimming &&
            (g_odg.input.buttons & ODG_BUTTON_DASH) != 0u && a->dash_cd == 0u && strength_q15 != 0) {
            a->dash_cd = ODG_DASH_COOLDOWN_TICKS;
            a->dash_ticks = ODG_DASH_DURATION_TICKS;
        }
        base_speed = ODG_PLAYER_SPEED_FX;
    } else {
        bot_control(a,&desired_x,&desired_z);
        strength_q15=(desired_x!=0 || desired_z!=0)?ODG_Q15_ONE:0;
        if (strength_q15!=0) {
            odg_normalize_q15(desired_x,desired_z,&desired_x,&desired_z);
            a->steer_q15=q15_cross(a->face_x_q15,a->face_z_q15,desired_x,desired_z);
            rotate_vec_inertial_toward(&a->face_x_q15,&a->face_z_q15,desired_x,desired_z,
                                       &a->turn_rate_q15,ODG_BOT_TURN_MAX_SIN_Q15,
                                       ODG_BOT_TURN_ACCEL_Q15,&a->turn_sign);
            if (a->vx != 0 || a->vz != 0) {
                steer_translation_heading(a->vx, a->vz, desired_x, desired_z,
                                           ODG_BOT_MOVE_TURN_MAX_SIN_Q15,
                                           &move_x, &move_z);
            } else {
                steer_translation_heading(a->face_x_q15, a->face_z_q15,
                                           desired_x, desired_z,
                                           ODG_BOT_MOVE_TURN_MAX_SIN_Q15,
                                           &move_x, &move_z);
            }
        } else {
            a->steer_q15=0;
        }
        base_speed=ODG_BOT_SPEED_FX;
    }

    if(mounted_vehicle!=UINT32_MAX)base_speed=ODG_RAFT_SPEED_FX;
    if (strength_q15!=0) {
        int32_t steering_factor = a->type == ODG_ACTOR_PLAYER ? ODG_Q15_ONE :
            steering_speed_factor_q15(a->face_x_q15,a->face_z_q15,desired_x,desired_z);
        target_speed=(int32_t)(((int64_t)base_speed*strength_q15)/ODG_Q15_ONE);
        target_speed=(int32_t)(((int64_t)target_speed*steering_factor)/ODG_Q15_ONE);
        if(mounted_vehicle==UINT32_MAX)
            target_speed=(int32_t)(((int64_t)target_speed*terrain_speed_factor_q15(a,move_x,move_z))/ODG_Q15_ONE);
    }
    if (mounted_vehicle==UINT32_MAX && a->type==ODG_ACTOR_PLAYER && a->dash_ticks>0u)
        target_speed=(target_speed*ODG_DASH_SPEED_NUM)/ODG_DASH_SPEED_DEN;

    {
        int32_t accel=a->type==ODG_ACTOR_PLAYER?8:6;
        int32_t brake=a->type==ODG_ACTOR_PLAYER?11:8;
        approach_heading_velocity(a,move_x,move_z,target_speed,accel,brake);
    }

    if(mounted_vehicle!=UINT32_MAX){
        if(!odg_artifact_vehicle_move_actor_internal(a,a->vx,a->vz)){a->vx=0;a->vz=0;a->speed_fx=0;}
    }else{
        move_actor_with_slide(a);
        resolve_actor_obstacles(a);
    }
    {
        int swimming=odg_actor_is_swimming_internal(a);
        if(mounted_vehicle!=UINT32_MAX){
            /* Vehicle movement already synchronized deck height and horizontal position. */
            (void)odg_artifact_vehicle_move_actor_internal(a,0,0);
        }else if(swimming){
            int32_t target=odg_actor_swim_target_offset_fx_internal(a);
            a->grounded=0u;a->vertical_velocity_fx=0;
            if(a->vertical_offset_fx<target){
                int32_t gap=target-a->vertical_offset_fx;
                a->vertical_offset_fx+=gap>ODG_SWIM_BUOYANCY_STEP_FX?ODG_SWIM_BUOYANCY_STEP_FX:gap;
            }else if(a->vertical_offset_fx>target){
                int32_t gap=a->vertical_offset_fx-target;
                a->vertical_offset_fx-=gap>ODG_SWIM_BUOYANCY_STEP_FX?ODG_SWIM_BUOYANCY_STEP_FX:gap;
            }
        }else if(was_swimming){
            /* Reaching a valid bank is an exit from the swim state, not a ballistic
             * launch caused by carrying lake-relative vertical offset onto dry terrain. */
            a->vertical_offset_fx=0;a->vertical_velocity_fx=0;a->grounded=1u;
        }else if (a->grounded==0u) {
            a->vertical_offset_fx+=a->vertical_velocity_fx;
            a->vertical_velocity_fx-=ODG_GRAVITY_FX;
            if (a->vertical_offset_fx<=0) {
                a->vertical_offset_fx=0;
                a->vertical_velocity_fx=0;
                a->grounded=1u;
            }
        } else {
            a->vertical_offset_fx=0;
            a->vertical_velocity_fx=0;
        }
    }
    odg_local_fx_to_global_fx_internal(a->x,a->z,&a->global_fx_x,&a->global_fx_z);
    a->local_resident=1u;
    {
        int64_t after_gx,after_gz;
        odg_global_fx_to_global_cell_internal(a->global_fx_x,a->global_fx_z,&after_gx,&after_gz);
        process_actor_global_cell(a,after_gx,after_gz);
    }

    if (a->type==ODG_ACTOR_BOT) {
        ++a->progress_ticks;
        if (a->progress_ticks>=ODG_BOT_PROGRESS_WINDOW_TICKS) {
            int64_t pd2=odg_dist2(a->x,a->z,a->progress_x,a->progress_z);
            int64_t min2=(int64_t)ODG_BOT_PROGRESS_MIN_FX*ODG_BOT_PROGRESS_MIN_FX;
            if (strength_q15!=0 && pd2<min2) {
                ++a->stuck_windows;
                a->ai_commit_ticks=0u;
                a->think_cd=0u;
                a->turn_sign=-a->turn_sign;
                a->slide_lock_ticks=0u;
                a->slide_axis=0u;
                a->slide_dir_x_q15=0;a->slide_dir_z_q15=0;
                if (a->trail_active) bot_set_return_direction(a);
                else bot_choose_expansion(a);
            } else if (a->stuck_windows>0u) {
                --a->stuck_windows;
            }
            a->progress_x=a->x;a->progress_z=a->z;a->progress_ticks=0u;
        }
    }
}


static int turret_capture_cell_eligible(int64_t gx,int64_t gz) {
    odg_surface_sample surface;
    uint64_t required=0u;
    /* Territorial paint may exist in water because swimmers/rafts are legitimate world
     * occupants, but neutral infrastructure is commissioned from surrounding ground.
     * Query the canonical terrain authority here instead of the resident playable cache:
     * Open Domain deliberately marks every resident cell as addressable, so that cache is
     * not a land/ocean mask. Fail closed if a surface sample cannot be resolved. */
    if(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)!=ODG_STATUS_OK)return 0;
    return (surface.flags&ODG_SURFACE_FLAG_WATER)==0u;
}

void odg_update_turret_ownership_internal(void) {
    uint32_t ti;
    /* Neutral infrastructure is commissioned by territorial control. Once programmed,
     * ground paint alone can never flip it again: changing an enemy turret still requires
     * a physical reprogram chip. A strict majority of the playable cells in the local
     * 5x5 neighborhood makes coastal turrets attainable without counting ocean cells. */
    for (ti=0u;ti<g_odg.turret_count;++ti) {
        odg_turret *t=&g_odg_turrets[ti];
        uint32_t counts[ODG_MAX_ACTORS]={0u};
        uint32_t playable=0u;
        uint32_t best_id=UINT32_MAX;
        uint32_t best_count=0u;
        int64_t center_gx,center_gz;
        int32_t dz;
        if (!t->active || t->owner!=ODG_TURRET_NEUTRAL || t->carried_by!=ODG_TURRET_NONE) continue;
        odg_local_fx_to_global_cell_internal(t->x,t->z,&center_gx,&center_gz);
        for (dz=-ODG_TURRET_CAPTURE_RADIUS;dz<=ODG_TURRET_CAPTURE_RADIUS;++dz) {
            int32_t dx;
            for (dx=-ODG_TURRET_CAPTURE_RADIUS;dx<=ODG_TURRET_CAPTURE_RADIUS;++dx) {
                int64_t gx=center_gx+(int64_t)dx,gz=center_gz+(int64_t)dz;
                uint32_t id;
                uint8_t owner;
                if(!turret_capture_cell_eligible(gx,gz))continue;
                ++playable;
                owner=odg_chunk_owner_at_global_cell(gx,gz);
                if (owner==ODG_OWNER_NONE) continue;
                id=ODG_ID_FROM_OWNER(owner);
                if (id<ODG_MAX_ACTORS && g_odg.actors[id].active && g_odg.actors[id].hp!=0u) ++counts[id];
            }
        }
        {
            uint32_t id;
            for (id=0u;id<ODG_MAX_ACTORS;++id) {
            if (counts[id]>best_count) {best_count=counts[id];best_id=id;}
            }
        }
        if (best_id==UINT32_MAX || playable==0u || best_count<=playable/2u) continue;
        if(!odg_turret_prepare_procedural_persist(t))continue;
        t->owner=ODG_OWNER_FROM_ID(best_id);
        t->fire_cd=t->fire_period;
        t->beam_ticks=0u;
        t->target_kind=ODG_TURRET_TARGET_NONE;
        t->last_target_cell=UINT32_MAX;
        t->target_global_cell_x=INT64_MIN;
        t->target_global_cell_z=INT64_MIN;
        t->target_actor_id=UINT32_MAX;
        t->aim_ticks=0u;
        t->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;
        odg_turret_persist_procedural(t);
        odg_emit_particles(t->x,t->z,0x8ce8ffffu,18u);
    }
}

static int turret_chunk_in_set(int64_t cx,int64_t cz,const int64_t *xs,const int64_t *zs,uint32_t count) {
    uint32_t i;for(i=0u;i<count;++i)if(xs[i]==cx&&zs[i]==cz)return 1;return 0;
}

int odg_turret_prepare_procedural_persist(const odg_turret *turret) {
    if(turret==NULL)return 0;
    if(turret->procedural==0u)return 1;
    return odg_chunk_prepare_procedural_turret_state_internal(turret->source_chunk_x,turret->source_chunk_z);
}

int odg_turret_persist_procedural(const odg_turret *turret) {
    odg_chunk_turret_state state;
    if(turret==NULL)return 0;
    if(turret->procedural==0u)return 1;
    /* Safe convenience wrapper for non-transactional internal callers/tests. Critical
     * gameplay paths still preflight explicitly before mutating inventory/ammo/ownership. */
    if(!odg_turret_prepare_procedural_persist(turret))return 0;
    odg_memset(&state,0,sizeof(state));state.state=1u;state.owner=turret->owner;
    state.material_tier=turret->material_tier;state.mode=turret->mode;state.ammo=turret->ammo;
    state.shots_fired=turret->shots_fired;state.cells_conquered=turret->cells_conquered;state.instance_id=turret->instance_id;
    return odg_chunk_store_procedural_turret_state(turret->source_chunk_x,turret->source_chunk_z,&state);
}

static uint32_t turret_stream_free_slot(void) {
    uint32_t i;
    /* A sleeping natural turret has already committed its persistent state to the chunk.
     * Its dynamic slot is therefore a clean cache tombstone, just like any genuinely
     * free turret slot. Portable manual turrets retain a live instance_id and must never
     * be reused while an inventory/storage payload still points at them. */
    for(i=0u;i<g_odg.turret_count;++i)if(!g_odg_turrets[i].active && g_odg_turrets[i].instance_id==0u)return i;
    if(!odg_entities_reserve_turrets(g_odg.turret_count+1u))return UINT32_MAX;
    return g_odg.turret_count++;
}

void odg_turrets_stream_refresh(void) {
    static const int32_t offsets[9][2]={{0,0},{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}};
    int64_t xs[96],zs[96];uint32_t count=0u,i,oi;
    int64_t pgx,pgz,pcx,pcz;
    odg_global_fx_to_global_cell_internal(g_odg.actors[ODG_PLAYER_ID].global_fx_x,g_odg.actors[ODG_PLAYER_ID].global_fx_z,&pgx,&pgz);
    pcx=odg_floor_div_i64_internal(pgx,(int64_t)ODG_CHUNK_SIZE_CELLS);pcz=odg_floor_div_i64_internal(pgz,(int64_t)ODG_CHUNK_SIZE_CELLS);
    for(oi=0u;oi<9u;++oi){xs[count]=pcx+offsets[oi][0];zs[count]=pcz+offsets[oi][1];++count;}
    for(i=1u;i<ODG_MAX_ACTORS&&count<96u;++i){
        const odg_actor *a=&g_odg.actors[i];int64_t gx,gz,cx,cz;
        if(!a->active||a->hp==0u)continue;
        odg_global_fx_to_global_cell_internal(a->global_fx_x,a->global_fx_z,&gx,&gz);
        cx=odg_floor_div_i64_internal(gx,(int64_t)ODG_CHUNK_SIZE_CELLS);cz=odg_floor_div_i64_internal(gz,(int64_t)ODG_CHUNK_SIZE_CELLS);
        if(!turret_chunk_in_set(cx,cz,xs,zs,count)){xs[count]=cx;zs[count]=cz;++count;}
    }
    /* TALA is autonomous infrastructure. Once enabled, keep that exact chunk simulated
     * so tree impacts remain physical instead of turning into an off-screen shortcut. */
    for(i=0u;i<g_odg.turret_count&&count<96u;++i){
        const odg_turret *t=&g_odg_turrets[i];
        if(!t->active||t->procedural==0u||t->mode!=ODG_TURRET_MODE_HARVEST)continue;
        if(!turret_chunk_in_set(t->source_chunk_x,t->source_chunk_z,xs,zs,count)){
            xs[count]=t->source_chunk_x;zs[count]=t->source_chunk_z;++count;
        }
    }
    /* Sleep procedural turrets whose chunk left the exact working set. */
    for(i=0u;i<g_odg.turret_count;++i){
        odg_turret *t=&g_odg_turrets[i];
        if(!t->active||t->procedural==0u)continue;
        if(!turret_chunk_in_set(t->source_chunk_x,t->source_chunk_z,xs,zs,count)){
            uint32_t id=t->id;
            if(!odg_turret_prepare_procedural_persist(t)||!odg_turret_persist_procedural(t))continue;
            /* Chunk state is the authority while a natural turret sleeps. Keeping a
             * second inactive odg_turret with floating-origin coordinates duplicates
             * that state and leaves stale local caches after recentering. Release the
             * materialized record completely; streaming reconstructs it deterministically. */
            odg_memset(t,0,sizeof(*t));t->id=id;
        }
    }
    /* Materialize the one deterministic natural turret, if any, in each exact chunk. */
    for(oi=0u;oi<count;++oi){
        odg_chunk_descriptor descriptor;odg_chunk_turret_state saved;uint64_t required=0u;
        int64_t gx,gz;uint32_t slot;odg_turret *t;int exists=0;int32_t local_x=0,local_z=0;int local_resident;
        if(odg_chunk_descriptor_get(xs[oi],zs[oi],&descriptor,sizeof(descriptor),&required)!=ODG_STATUS_OK ||
           descriptor.has_procedural_turret==0u || !odg_chunk_procedural_turret_cell(xs[oi],zs[oi],&gx,&gz))continue;
        for(i=0u;i<g_odg.turret_count;++i){
            const odg_turret *cur=&g_odg_turrets[i];
            if(cur->active&&cur->procedural!=0u&&cur->source_chunk_x==xs[oi]&&cur->source_chunk_z==zs[oi]){exists=1;break;}
        }
        if(exists)continue;
        odg_memset(&saved,0,sizeof(saved));
        if(odg_chunk_procedural_turret_state(xs[oi],zs[oi],&saved) && saved.state==2u)continue;
        {
            int64_t global_fx_x=gx*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
            int64_t global_fx_z=gz*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
            local_resident=odg_global_fx_to_local_internal(global_fx_x,global_fx_z,&local_x,&local_z);
        }
        /* Older saves or unusual runtime ordering can still contain a solid object on a
         * sleeping turret cell. Never resolve that conflict by overlapping colliders;
         * defer materialization until the obstruction is genuinely gone. */
        if(local_resident&&(((odg_worldgen_version()>=ODG_WORLDGEN_VERSION_SAFE_TURRETS)&&
                            !odg_world_position_safe_ground_internal(local_x,local_z))||
                           !position_clear(local_x,local_z,ODG_TURRET_COLLISION_RADIUS_FX)||
                           !odg_dynamic_position_clear_internal(local_x,local_z,ODG_TURRET_COLLISION_RADIUS_FX,
                                                                UINT32_MAX,UINT32_MAX)))continue;
        slot=turret_stream_free_slot();if(slot==UINT32_MAX)continue;t=&g_odg_turrets[slot];odg_memset(t,0,sizeof(*t));
        t->active=1u;t->id=slot;t->procedural=1u;t->source_chunk_x=xs[oi];t->source_chunk_z=zs[oi];
        t->instance_id=ODG_INSTANCE_ID_PROCEDURAL_BIT|
                       (descriptor.stable_id&ODG_INSTANCE_ID_SEQUENTIAL_MAX);
        t->owner=ODG_TURRET_NEUTRAL;t->mode=ODG_TURRET_MODE_DEFENSE;
        odg_apply_turret_tier(t,descriptor.turret_material_tier,0);
        if(saved.state==1u){
            t->owner=(uint8_t)saved.owner;t->mode=saved.mode<=ODG_TURRET_MODE_HARVEST?saved.mode:ODG_TURRET_MODE_DEFENSE;
            odg_apply_turret_tier(t,saved.material_tier,0);t->ammo=saved.ammo>t->max_ammo?t->max_ammo:saved.ammo;
            t->shots_fired=saved.shots_fired;t->cells_conquered=saved.cells_conquered;
            if(saved.instance_id!=0u)t->instance_id=saved.instance_id;
        }
        t->global_fx_x=gx*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
        t->global_fx_z=gz*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
        if(!local_resident){t->local_resident=0u;t->x=0;t->z=0;}else{t->local_resident=1u;t->x=local_x;t->z=local_z;}
        t->carried_by=ODG_TURRET_NONE;t->last_target_cell=UINT32_MAX;t->target_global_cell_x=INT64_MIN;t->target_global_cell_z=INT64_MIN;
        t->target_kind=ODG_TURRET_TARGET_NONE;t->target_actor_id=UINT32_MAX;t->target_resource_stable_id=0u;
        t->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;t->fire_cd=t->fire_period;t->head_z_q15=ODG_Q15_ONE;t->head_turn_sign=1;
    }
    odg_entities_spatial_mark_dirty();
}

int odg_turret_drop_candidate_internal(const odg_actor *p, int32_t *out_x, int32_t *out_z) {
    /* The preview is still a single authoritative C candidate, but it searches a compact
     * forward fan. That prevents a workbench/tree immediately in front of the player from
     * making deployment feel randomly unavailable while keeping the ghost close and predictable. */
    /* Keep the first candidates directly in front for predictable touch placement, then
     * fan sideways/inward.  The shortest ring is essential at the initial 4-cell-radius
     * home domain: an initial protected workbench must not accidentally make every legal
     * turret ghost unreachable. */
    static const int32_t distances[] = {13 * ODG_FX_ONE / 10, 19 * ODG_FX_ONE / 10, 25 * ODG_FX_ONE / 10, 31 * ODG_FX_ONE / 10, 37 * ODG_FX_ONE / 10};
    static const int32_t lateral[] = {0, 6 * ODG_FX_ONE / 10, -6 * ODG_FX_ONE / 10, 12 * ODG_FX_ONE / 10, -12 * ODG_FX_ONE / 10, 18 * ODG_FX_ONE / 10, -18 * ODG_FX_ONE / 10};
    uint32_t di,li,i;
    uint8_t own;
    if(!p || !out_x || !out_z) return 0;
    own=ODG_OWNER_FROM_ID(p->id);
    for (di=0u;di<(uint32_t)(sizeof(distances)/sizeof(distances[0]));++di) {
        for (li=0u;li<(uint32_t)(sizeof(lateral)/sizeof(lateral[0]));++li) {
            int32_t right_x=p->face_z_q15;
            int32_t right_z=-p->face_x_q15;
            int32_t x=p->x+(int32_t)(((int64_t)p->face_x_q15*distances[di])/ODG_Q15_ONE)
                         +(int32_t)(((int64_t)right_x*lateral[li])/ODG_Q15_ONE);
            int32_t z=p->z+(int32_t)(((int64_t)p->face_z_q15*distances[di])/ODG_Q15_ONE)
                         +(int32_t)(((int64_t)right_z*lateral[li])/ODG_Q15_ONE);
            int64_t gx,gz;
            int valid_domain=0;
            odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
            if(odg_chunk_owner_at_global_cell(gx,gz)==own) valid_domain=1;
            else if(odg_chunk_owner_at_global_cell(gx,gz)==ODG_OWNER_NONE) {
                if(odg_chunk_owner_at_global_cell(gx-1,gz)==own||
                   odg_chunk_owner_at_global_cell(gx+1,gz)==own||
                   odg_chunk_owner_at_global_cell(gx,gz-1)==own||
                   odg_chunk_owner_at_global_cell(gx,gz+1)==own)valid_domain=1;
            }
            if(!valid_domain || !odg_world_position_safe_ground_internal(x,z) ||
               odg_chunk_procedural_turret_reserves_local_circle_internal(x,z,ODG_FX_ONE) ||
               !position_clear(x,z,ODG_FX_ONE) ||
               !odg_dynamic_position_clear_internal(x,z,ODG_TURRET_COLLISION_RADIUS_FX,p->id,UINT32_MAX)) continue;
            for(i=0u;i<g_odg.turret_count;++i) {
                const odg_turret *t=&g_odg_turrets[i];
                if(t->active && t->local_resident!=0u && t->carried_by==ODG_TURRET_NONE &&
                   odg_dist2(x,z,t->x,t->z)<(int64_t)(2*ODG_FX_ONE)*(2*ODG_FX_ONE)) break;
            }
            if(i<g_odg.turret_count) continue;
            *out_x=x;*out_z=z;return 1;
        }
    }
    return 0;
}


static void turret_cache_target_local_cell(odg_turret *t) {
    uint32_t cell=UINT32_MAX;
    if(t==NULL || t->target_kind==ODG_TURRET_TARGET_NONE ||
       t->target_global_cell_x==INT64_MIN || t->target_global_cell_z==INT64_MIN) {
        if(t!=NULL) t->last_target_cell=UINT32_MAX;
        return;
    }
    t->last_target_cell=odg_global_cell_to_local_internal(t->target_global_cell_x,t->target_global_cell_z,&cell)?cell:UINT32_MAX;
}

static odg_resource_node *turret_resource_target(uint64_t stable_id) {
    uint32_t i;
    if(stable_id==0u)return NULL;
    for(i=0u;i<g_odg.resource_count;++i)
        if(g_odg_resources[i].active && g_odg_resources[i].local_resident!=0u && g_odg_resources[i].stable_id==stable_id)return &g_odg_resources[i];
    return NULL;
}

static int turret_target_local_fx(const odg_turret *t,int32_t *out_x,int32_t *out_z) {
    if(t==NULL || t->target_kind==ODG_TURRET_TARGET_NONE || out_x==NULL || out_z==NULL)return 0;
    if(t->target_kind==ODG_TURRET_TARGET_RESOURCE){
        const odg_resource_node *r=turret_resource_target(t->target_resource_stable_id);
        if(r==NULL)return 0;
        *out_x=r->x;*out_z=r->z;return 1;
    }
    if(t->target_global_cell_x==INT64_MIN || t->target_global_cell_z==INT64_MIN)return 0;
    return odg_global_cell_center_to_local_fx_internal(t->target_global_cell_x,t->target_global_cell_z,out_x,out_z);
}

static int turret_find_trail_target_for_actor(const odg_turret *t,uint32_t actor_filter,
                                               int64_t *out_gx,int64_t *out_gz) {
    int32_t r;
    int64_t center_gx,center_gz;
    int64_t best_d2=INT64_MAX;
    int64_t best_gx=0,best_gz=0;
    int64_t min_d2=(int64_t)ODG_TURRET_MIN_TARGET_FX*ODG_TURRET_MIN_TARGET_FX;
    int32_t dz;
    int found=0;
    if(t==NULL || out_gx==NULL || out_gz==NULL) return 0;
    r=t->range_fx/ODG_CELL_FX;
    odg_local_fx_to_global_cell_internal(t->x,t->z,&center_gx,&center_gz);
    for(dz=-r;dz<=r;++dz) {
        int32_t dx;
        for(dx=-r;dx<=r;++dx) {
            int64_t gx=center_gx+(int64_t)dx,gz=center_gz+(int64_t)dz;
            uint8_t tr=odg_chunk_trail_at_global_cell(gx,gz);
            uint32_t trail_id;
            int32_t tx,tz;
            int64_t d2;
            if(tr==ODG_OWNER_NONE || tr==t->owner) continue;
            trail_id=ODG_ID_FROM_OWNER(tr);
            if(trail_id>=ODG_MAX_ACTORS || (actor_filter<ODG_MAX_ACTORS && trail_id!=actor_filter)) continue;
            if(g_odg.actors[trail_id].trail_len<ODG_TURRET_TRAIL_MIN_CELLS) continue;
            if(!odg_global_cell_center_to_local_fx_internal(gx,gz,&tx,&tz)) continue;
            d2=odg_dist2(t->x,t->z,tx,tz);
            if(d2<min_d2 || d2>(int64_t)t->range_fx*t->range_fx) continue;
            if(d2<best_d2){best_d2=d2;best_gx=gx;best_gz=gz;found=1;}
        }
    }
    if(found){*out_gx=best_gx;*out_gz=best_gz;}
    return found;
}

static int turret_find_trail_target(const odg_turret *t,int64_t *out_gx,int64_t *out_gz) {
    return turret_find_trail_target_for_actor(t,UINT32_MAX,out_gx,out_gz);
}

static int turret_find_territory_target(const odg_turret *t,int64_t *out_gx,int64_t *out_gz) {
    int32_t r;
    int64_t center_gx,center_gz;
    int64_t best_d2=INT64_MAX;
    int64_t best_gx=0,best_gz=0;
    int32_t dz;
    int found=0;
    if(t==NULL || out_gx==NULL || out_gz==NULL) return 0;
    r=t->range_fx/ODG_CELL_FX;
    odg_local_fx_to_global_cell_internal(t->x,t->z,&center_gx,&center_gz);
    for(dz=-r;dz<=r;++dz) {
        int32_t dx;
        for(dx=-r;dx<=r;++dx) {
            int64_t gx=center_gx+(int64_t)dx,gz=center_gz+(int64_t)dz;
            uint8_t owner=odg_chunk_owner_at_global_cell(gx,gz);
            int32_t tx,tz;
            int64_t d2;
            if(owner==ODG_OWNER_NONE || owner==t->owner) continue;
            if(!odg_global_cell_center_to_local_fx_internal(gx,gz,&tx,&tz)) continue;
            d2=odg_dist2(t->x,t->z,tx,tz);
            if(d2<=(int64_t)t->range_fx*t->range_fx && d2<best_d2){best_d2=d2;best_gx=gx;best_gz=gz;found=1;}
        }
    }
    if(found){*out_gx=best_gx;*out_gz=best_gz;}
    return found;
}

static int turret_find_harvest_target(const odg_turret *t,uint64_t *out_stable_id,int64_t *out_gx,int64_t *out_gz) {
    uint32_t i;
    int64_t best_d2=INT64_MAX;
    uint64_t best_stable=0u;
    int64_t best_gx=0,best_gz=0;
    if(t==NULL || out_stable_id==NULL || out_gx==NULL || out_gz==NULL)return 0;
    for(i=0u;i<g_odg.resource_count;++i){
        const odg_resource_node *r=&g_odg_resources[i];const odg_flora_species_definition *flora;int64_t d2,gx,gz;
        if(!r->active||r->local_resident==0u||r->state!=ODG_RESOURCE_STATE_AVAILABLE)continue;
        flora=odg_resource_flora_definition_internal(r);
        if(flora==NULL||(flora->harvest_flags&ODG_FLORA_HARVEST_TURRET_ELIGIBLE)==0u)continue;
        d2=odg_dist2(t->x,t->z,r->x,r->z);
        if(d2>(int64_t)t->range_fx*t->range_fx || d2>=best_d2)continue;
        odg_local_fx_to_global_cell_internal(r->x,r->z,&gx,&gz);
        best_d2=d2;best_stable=r->stable_id;best_gx=gx;best_gz=gz;
    }
    if(best_stable==0u)return 0;
    *out_stable_id=best_stable;*out_gx=best_gx;*out_gz=best_gz;return 1;
}

static int turret_target_valid(const odg_turret *t) {
    int32_t tx,tz;
    if(!t || t->owner==ODG_TURRET_NEUTRAL || t->target_kind==ODG_TURRET_TARGET_NONE)return 0;
    if(t->target_kind!=ODG_TURRET_TARGET_RESOURCE &&
       (t->target_global_cell_x==INT64_MIN || t->target_global_cell_z==INT64_MIN))return 0;
    if(!turret_target_local_fx(t,&tx,&tz)) return 0;
    if(odg_dist2(t->x,t->z,tx,tz)>(int64_t)t->range_fx*t->range_fx) return 0;
    if(t->target_kind==ODG_TURRET_TARGET_TRAIL) {
        uint8_t tr=odg_chunk_trail_at_global_cell(t->target_global_cell_x,t->target_global_cell_z);
        if(tr==ODG_OWNER_NONE || tr==t->owner) return 0;
        if(t->target_actor_id<ODG_MAX_ACTORS && ODG_ID_FROM_OWNER(tr)!=t->target_actor_id) return 0;
        return odg_dist2(t->x,t->z,tx,tz)>=(int64_t)ODG_TURRET_MIN_TARGET_FX*ODG_TURRET_MIN_TARGET_FX;
    }
    if(t->target_kind==ODG_TURRET_TARGET_TERRITORY) {
        uint8_t owner=odg_chunk_owner_at_global_cell(t->target_global_cell_x,t->target_global_cell_z);
        return owner!=ODG_OWNER_NONE && owner!=t->owner;
    }
    if(t->target_kind==ODG_TURRET_TARGET_RESOURCE) {
        const odg_resource_node *r=turret_resource_target(t->target_resource_stable_id);
        if(t->mode!=ODG_TURRET_MODE_HARVEST || r==NULL || r->state!=ODG_RESOURCE_STATE_AVAILABLE)return 0;
        {const odg_flora_species_definition *flora=odg_resource_flora_definition_internal(r);return flora!=NULL&&(flora->harvest_flags&ODG_FLORA_HARVEST_TURRET_ELIGIBLE)!=0u;}
    }
    return 0;
}

static void turret_clear_target(odg_turret *t) {
    if(!t) return;
    t->target_kind=ODG_TURRET_TARGET_NONE;
    t->target_actor_id=UINT32_MAX;
    t->target_resource_stable_id=0u;
    t->target_global_cell_x=INT64_MIN;
    t->target_global_cell_z=INT64_MIN;
    t->aim_ticks=0u;
    if(t->beam_ticks==0u) t->last_target_cell=UINT32_MAX;
}

static void turret_fire_locked(odg_turret *t) {
    uint32_t owner_id;
    int32_t tx=0,tz=0;
    if(!t || t->owner==ODG_TURRET_NEUTRAL || t->ammo==0u || t->carried_by!=ODG_TURRET_NONE || !turret_target_valid(t)) {
        turret_clear_target(t);return;
    }
    owner_id=ODG_ID_FROM_OWNER(t->owner);
    if(owner_id>=ODG_MAX_ACTORS || g_odg.actors[owner_id].hp==0u){turret_clear_target(t);return;}
    if(!turret_target_local_fx(t,&tx,&tz)){turret_clear_target(t);return;}
    if(!odg_turret_prepare_procedural_persist(t))return;
    if(t->target_kind==ODG_TURRET_TARGET_TRAIL) {
        uint8_t victim_owner=odg_chunk_trail_at_global_cell(t->target_global_cell_x,t->target_global_cell_z);
        uint32_t victim_id=ODG_ID_FROM_OWNER(victim_owner);
        --t->ammo;++t->shots_fired;t->fire_cd=t->fire_period;t->beam_ticks=14u;
        odg_emit_particles(tx,tz,0xffd36bffu,14u);
        if(victim_id<ODG_MAX_ACTORS) break_actor_trail(&g_odg.actors[victim_id],owner_id);
        odg_turret_persist_procedural(t);
        turret_clear_target(t);
        return;
    }
    if(t->target_kind==ODG_TURRET_TARGET_RESOURCE) {
        int hit=odg_resource_turret_hit(t->target_resource_stable_id,t->material_tier);
        if(hit==0){turret_clear_target(t);return;}
        --t->ammo;++t->shots_fired;t->fire_cd=t->fire_period;t->beam_ticks=14u;
        odg_emit_particles(tx,tz,0xffd36bffu,7u);
        odg_turret_persist_procedural(t);
        turret_clear_target(t);
        return;
    }
    if(t->target_kind==ODG_TURRET_TARGET_TERRITORY) {
        uint8_t old=odg_chunk_owner_at_global_cell(t->target_global_cell_x,t->target_global_cell_z);
        uint32_t old_id=ODG_ID_FROM_OWNER(old);
        --t->ammo;++t->shots_fired;++t->cells_conquered;t->fire_cd=t->fire_period;t->beam_ticks=14u;
        set_territory_global(t->target_global_cell_x,t->target_global_cell_z,t->owner);
        sync_actor_score(owner_id);
        if(old_id<ODG_MAX_ACTORS) sync_actor_score(old_id);
        odg_emit_particles(tx,tz,0xffd36bffu,8u);
        odg_turret_persist_procedural(t);
        odg_update_turret_ownership_internal();
        turret_clear_target(t);
    }
}

static void turret_acquire_target(odg_turret *t) {
    int64_t gx=0,gz=0;
    int found=0;
    if(!t || t->owner==ODG_TURRET_NEUTRAL || t->ammo==0u || t->retarget_cd!=0u) return;
    found=turret_find_trail_target(t,&gx,&gz);
    if(found) {
        uint8_t tr=odg_chunk_trail_at_global_cell(gx,gz);
        t->target_kind=ODG_TURRET_TARGET_TRAIL;
        t->target_actor_id=tr!=ODG_OWNER_NONE?ODG_ID_FROM_OWNER(tr):UINT32_MAX;
    } else if(turret_find_territory_target(t,&gx,&gz)) {
        found=1;t->target_kind=ODG_TURRET_TARGET_TERRITORY;t->target_actor_id=UINT32_MAX;
    } else if(t->mode==ODG_TURRET_MODE_HARVEST) {
        uint64_t stable_id=0u;
        if(turret_find_harvest_target(t,&stable_id,&gx,&gz)){
            found=1;t->target_kind=ODG_TURRET_TARGET_RESOURCE;t->target_actor_id=UINT32_MAX;
            t->target_resource_stable_id=stable_id;
        }
    }
    if(found) {
        t->target_global_cell_x=gx;t->target_global_cell_z=gz;
        turret_cache_target_local_cell(t);
        t->aim_ticks=t->aim_required;
    }
}

static void update_turrets(void) {
    uint32_t i;
    for(i=0u;i<g_odg.turret_count;++i) {
        odg_turret *t=&g_odg_turrets[i];
        if(!t->active || t->local_resident==0u) continue;
        if(t->beam_ticks>0u)--t->beam_ticks;
        if(t->retarget_cd>0u)--t->retarget_cd;
        if(t->carried_by!=ODG_TURRET_NONE) continue;
        if(t->owner==ODG_TURRET_NEUTRAL || t->ammo==0u){turret_clear_target(t);continue;}
        turret_cache_target_local_cell(t);

        /* Head yaw follows the global committed target. A floating-origin recenter can
         * never change the selected world cell or cause a local-edge snap. */
        if(t->target_kind!=ODG_TURRET_TARGET_NONE) {
            int32_t tx,tz;
            if(turret_target_local_fx(t,&tx,&tz)) {
                int32_t dx=tx-t->x,dz=tz-t->z;
                int32_t qx=0,qz=0;
                odg_normalize_q15(dx,dz,&qx,&qz);
                if(qx!=0 || qz!=0)
                    rotate_vec_inertial_toward(&t->head_x_q15,&t->head_z_q15,qx,qz,
                                               &t->head_turn_rate_q15,2200,3800,&t->head_turn_sign);
            }
        } else {
            t->head_turn_rate_q15=approach_signed(t->head_turn_rate_q15,0,2500);
        }

        if(t->fire_cd>0u){--t->fire_cd;continue;}
        if(t->target_kind==ODG_TURRET_TARGET_NONE){turret_acquire_target(t);continue;}
        if(!turret_target_valid(t)){
            turret_clear_target(t);t->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;continue;
        }
        if(t->aim_ticks>0u){--t->aim_ticks;continue;}
        turret_fire_locked(t);
    }
}


static int generic_pickup_separated(int32_t x,int32_t z) {
    uint32_t i;
    for(i=0u;i<g_odg.turret_count;++i) if(g_odg_turrets[i].active && g_odg_turrets[i].local_resident!=0u &&
        odg_dist2(x,z,g_odg_turrets[i].x,g_odg_turrets[i].z)<(int64_t)(4*ODG_FX_ONE)*(4*ODG_FX_ONE)) return 0;
    for(i=0u;i<g_odg.pickup_count;++i) if(g_odg_pickups[i].active && g_odg_pickups[i].local_resident!=0u &&
        odg_dist2(x,z,g_odg_pickups[i].x,g_odg_pickups[i].z)<(int64_t)(5*ODG_FX_ONE)*(5*ODG_FX_ONE)) return 0;
    return 1;
}

static void spawn_initial_pickups(void) {
    uint32_t i;
    /* Ammunition is manufactured at a smithy. The only ambient tech pickups left are
     * reprogram chips, which represent scarce world salvage rather than free combat fuel. */
    const uint32_t count=4u;
    for(i=0u;i<count;++i) {
        uint32_t attempt;int spawned=0;
        uint32_t roll=odg_rand_bounded(&g_odg.rng,100u);
        odg_item_stack stack;
        odg_memset(&stack,0,sizeof(stack));
        stack.type_id=ODG_ITEM_REPROGRAM_CHIP;stack.quantity=1u;
        stack.material_tier=roll<60u?ODG_MATERIAL_WOOD:(roll<90u?ODG_MATERIAL_STONE:ODG_MATERIAL_IRON);
        for(attempt=0u;attempt<500u&&!spawned;++attempt) {
            uint32_t cell=odg_rand_bounded(&g_odg.rng,ODG_CELL_COUNT);
            int32_t x,z;int64_t gx,gz;
            x=odg_cell_center_x(cell);z=odg_cell_center_z(cell);
            odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
            if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_NONE)continue;
            if(!odg_world_cell_safe_ground_internal(gx,gz))continue;
            if(odg_chunk_procedural_turret_reserves_local_circle_internal(x,z,ODG_FX_ONE/2))continue;
            if(!position_clear(x,z,ODG_FX_ONE/2) || !generic_pickup_separated(x,z)) continue;
            spawned=odg_spawn_world_pickup(&stack,x,z,0u);
        }
        if(!spawned){
            uint64_t entropy=g_odg.seed^UINT64_C(0x4348495053414c56)^((uint64_t)i*UINT64_C(0x9e3779b97f4a7c15));
            uint32_t start;uint32_t scan;
            entropy^=entropy>>32u;start=(uint32_t)(entropy%(uint64_t)ODG_CELL_COUNT);
            for(scan=0u;scan<ODG_CELL_COUNT&&!spawned;++scan){
                uint32_t cell=(start+scan)%ODG_CELL_COUNT;int32_t x,z;int64_t gx,gz;
                x=odg_cell_center_x(cell);z=odg_cell_center_z(cell);
                odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
                if(odg_chunk_owner_at_global_cell(gx,gz)!=ODG_OWNER_NONE)continue;
                if(!odg_world_cell_safe_ground_internal(gx,gz))continue;
                if(odg_chunk_procedural_turret_reserves_local_circle_internal(x,z,ODG_FX_ONE/2))continue;
                if(!position_clear(x,z,ODG_FX_ONE/2)||!generic_pickup_separated(x,z))continue;
                spawned=odg_spawn_world_pickup(&stack,x,z,0u);
            }
        }
    }
}

static void handle_drop_button(void) {
    odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
    uint32_t now=g_odg.input.buttons&ODG_BUTTON_DROP;
    uint32_t prev=g_odg.prev_buttons&ODG_BUTTON_DROP;
    if(now!=0u && prev==0u && p->hp!=0u)
        (void)odg_drop_inventory_slot(ODG_PLAYER_ID,p->inventory.selected_slot,1u,ODG_MANUAL_DROP_REPICKUP_TICKS);
}

void odg_reset_presentation_rng_internal(void) {
    /* Derive a fresh visual stream from logical time on init/load. Its exact continuation
     * is intentionally presentation-only: gameplay cannot observe particle randomness. */
    (void)odm_rng_seed_derived(&g_odg.visual_rng,g_odg.seed,
                               UINT64_C(0x56495355414c5f52),g_odg.tick+UINT64_C(1));
}

void odg_emit_particles(int32_t x, int32_t z, uint32_t color, uint32_t count) {
    uint32_t i,j;
    for (i=0u;i<count;++i) {
        for (j=0u;j<ODG_MAX_PARTICLES;++j) {
            if (!g_odg.particles[j].active) {
                odg_particle *p=&g_odg.particles[j];
                p->active=1u;p->x=x;p->z=z;p->y_fx=240;
                p->vx=odg_rand_range_fx(&g_odg.visual_rng,-80,81);
                p->vz=odg_rand_range_fx(&g_odg.visual_rng,-80,81);
                p->vy_fx=65+odg_rand_range_fx(&g_odg.visual_rng,0,75);
                p->life=22u+odg_rand_bounded(&g_odg.visual_rng,26u);p->color=color;
                break;
            }
        }
    }
}

static void update_particles(void) {
    uint32_t i;
    for (i = 0u; i < ODG_MAX_PARTICLES; ++i) {
        odg_particle *p = &g_odg.particles[i];
        if (!p->active) continue;
        p->x += p->vx / 8;
        p->z += p->vz / 8;
        p->y_fx += p->vy_fx / 8;
        p->vy_fx -= 18;
        p->vx = (p->vx * 15) / 16;
        p->vz = (p->vz * 15) / 16;
        if (p->life > 0u) --p->life;
        if (p->life == 0u || p->y_fx < 0) p->active = 0u;
    }
}

static void check_match_end(void) {
    /* DOMINIO ABIERTO: 55% is a leaderboard signal, never a terminal condition. */
    g_odg.match_over=0u;g_odg.winner_id=UINT32_MAX;
}

static int world_bootstrap_postconditions(void){
    uint32_t actor,i,chips=0u;
    for(actor=0u;actor<ODG_MAX_ACTORS;++actor){
        const odg_actor *a=&g_odg.actors[actor];uint32_t trees=0u,stone=0u,workbenches=0u;int64_t gx,gz;
        if(!a->active||a->hp==0u||a->max_hp==0u||a->local_resident==0u)return 0;
        odg_global_fx_to_global_cell_internal(a->global_fx_x,a->global_fx_z,&gx,&gz);
        if(!odg_world_cell_safe_ground_internal(gx,gz))return 0;
        if(odg_chunk_owner_at_global_cell(a->home_global_cell_x,a->home_global_cell_z)!=ODG_OWNER_FROM_ID(actor))return 0;
        for(i=0u;i<g_odg.resource_count;++i){
            const odg_resource_node *r=&g_odg_resources[i];
            if(!r->active||r->bootstrap_actor_id!=actor)continue;
            if(r->kind==ODG_RESOURCE_TREE)++trees;else if(r->kind==ODG_RESOURCE_STONE)++stone;
        }
        for(i=0u;i<g_odg.artifact_count;++i){
            const odg_artifact *artifact=&g_odg_artifacts[i];
            if(!artifact->active||artifact->owner_actor_id!=actor||artifact->item_type!=ODG_ITEM_WORKBENCH)continue;
            if((artifact->state&ODG_ARTIFACT_STATE_PROTECTED)==0u||
               !odg_artifact_surface_allows_item_internal(artifact->item_type,artifact->x,artifact->z))return 0;
            ++workbenches;
        }
        if(trees<3u||stone<2u||workbenches!=1u)return 0;
    }
    for(i=0u;i<g_odg.pickup_count;++i){
        const odg_world_pickup *pickup=&g_odg_pickups[i];int64_t gx,gz;
        if(!pickup->active||pickup->stack.type_id!=ODG_ITEM_REPROGRAM_CHIP)continue;
        odg_global_fx_to_global_cell_internal(pickup->global_fx_x,pickup->global_fx_z,&gx,&gz);
        if(!odg_world_cell_safe_ground_internal(gx,gz))return 0;
        ++chips;
    }
    return chips==4u;
}

int odg_world_build(uint64_t seed) {
    uint32_t i;
    g_odg.seed = seed;
    odg_survival_reset_new_world();
    g_odg.tick = 0u;
    g_odg.tick_accum_scaled = 0u;
    g_odg.obstacle_count = 0u;
    g_odg.turret_count = 0u;
    g_odg.pickup_count = 0u;
    g_odg.resource_count = 0u;
    g_odg.artifact_count = 0u;
    g_odg.opened_artifact_id = UINT32_MAX;
    g_odg.next_instance_id = UINT64_C(1);
    odg_chunks_reset_runtime();
    odg_memset(&g_odg.commands,0,sizeof(g_odg.commands));
    g_odg.interact_ticks=0u;g_odg.interact_hold_fired=0u;g_odg.interact_pressed_prev=0u;
    odg_memset(&g_odg.interaction_hint,0,sizeof(g_odg.interaction_hint));
    g_odg.prev_buttons = 0u;
    g_odg.match_over = 0u;
    g_odg.winner_id = UINT32_MAX;
    (void)odm_rng_seed(&g_odg.rng, seed, UINT64_C(0x5249465454455252));
    (void)odm_rng_seed_derived(&g_odg.ecology_rng,seed,UINT64_C(0x45434f4c4f475931),1u);
    odg_reset_presentation_rng_internal();
    g_odg.weather_rain_permille=0u;g_odg.save_reserved_weather_u32=0u;

    initialize_open_domain_compat_mask();
    odg_memset(g_odg.territory, 0, sizeof(g_odg.territory));
    odg_memset(g_odg.trail_owner, 0, sizeof(g_odg.trail_owner));
    odg_memset(g_odg.territory_count, 0, sizeof(g_odg.territory_count));
    odg_memset(g_odg.particles, 0, sizeof(g_odg.particles));
    odg_entities_reset_runtime();

    /* v15 has no fixed arena landmarks. Collision-bearing scenery comes from globally
     * deterministic resources/artifacts so nothing teleports or repeats at recenter. */

    for (i = 0u; i < ODG_MAX_ACTORS; ++i)
        if(!spawn_actor(i, i == ODG_PLAYER_ID ? ODG_ACTOR_PLAYER : ODG_ACTOR_BOT))return 0;
    for (i = 0u; i < ODG_MAX_ACTORS; ++i) stamp_initial_territory(&g_odg.actors[i]);
    odg_resources_build();
    odg_artifacts_build_initial();
    spawn_initial_pickups();
    /* Persist the bootstrap nations, then turn the old finite mask into an open-world
     * precision window. From this point every local cell is terrain; chunk data decides
     * persistent ownership instead of a coastline around a 128x128 arena. */
    odg_chunks_capture_active_window();
    odg_chunks_load_active_window();
    odg_resources_stream_refresh();
    odg_turrets_stream_refresh();
    /* Fauna is seeded only after the first static streaming pass. spawn_fauna() now
     * validates static + dynamic occupancy, so initial animals can never be born clean
     * and then immediately have a procedural tree/ore/turret materialized through them. */
    odg_fauna_build_initial();
    odg_chunks_refresh_summaries();
    odg_bot_navigation_rebuild_internal();
    odg_rebuild_interaction_hint();
    /* Third-person camera begins behind the player's actual facing. It then rotates
     * with its own slower angular limit instead of snapping to input. */
    g_odg.camera_dir_x_q15 = g_odg.actors[ODG_PLAYER_ID].face_x_q15;
    g_odg.camera_dir_z_q15 = g_odg.actors[ODG_PLAYER_ID].face_z_q15;
    g_odg.camera_turn_rate_q15 = 0;
    g_odg.camera_manual_ticks = 0u;
    g_odg.camera_pitch_q15 = ODG_CAMERA_PITCH_DEFAULT_Q15;
    g_odg.camera_height_fx = odg_terrain_height_fx(g_odg.actors[ODG_PLAYER_ID].x, g_odg.actors[ODG_PLAYER_ID].z) + ODG_CAMERA_PLAYER_HEIGHT_FX;
    g_odg.camera_mode = ODG_CAMERA_MODE_MEDIUM;
    g_odg.camera_target_distance_fx = ODG_CAMERA_DISTANCE_FX;
    g_odg.camera_distance_fx = ODG_CAMERA_DISTANCE_FX;
    g_odg.music_reactivity_q16 = UINT32_C(65535);
    g_odg.remote_view_active = 0u;
    g_odg.avatar_preview_active = 0u;
    g_odg.avatar_preview_yaw_q16 = 0u;
    g_odg.control_basis_x_q15 = g_odg.camera_dir_x_q15;
    g_odg.control_basis_z_q15 = g_odg.camera_dir_z_q15;
    g_odg.control_heading_x_q15 = 0;
    g_odg.control_heading_z_q15 = 0;
    g_odg.control_strength_q15 = 0;
    g_odg.control_active = 0u;
    g_odg.camera_anchor_x = g_odg.actors[ODG_PLAYER_ID].x;
    g_odg.camera_anchor_z = g_odg.actors[ODG_PLAYER_ID].z;
    return world_bootstrap_postconditions();
}

void odg_sim_step(void) {
    uint32_t i;
    ++g_odg.tick;

    if ((g_odg.input.buttons & ODG_BUTTON_RESTART) != 0u) {
        uint64_t next_seed = g_odg.seed ^ (g_odg.tick * UINT64_C(0x9e3779b97f4a7c15));
        if(!odg_world_build(next_seed))g_odg.initialized=0u;
        return;
    }

    if (!g_odg.match_over) {
        odg_environment_tick();
        odg_nutrition_tick();
        odg_ecology_tick();
        odg_fauna_tick();
        /* Resolve already-exposed trail contact before movement so an actor cannot
         * escape a trail cut merely because its AI/control step moves first. The
         * post-movement pass below still catches trails committed during this tick. */
        resolve_trail_contacts();
        /* Player first keeps local control latency deterministic and minimal. */
        update_actor(&g_odg.actors[ODG_PLAYER_ID]);
        odg_chunks_maybe_recenter();
        if (g_odg.actors[ODG_PLAYER_ID].hp != 0u) {
            odg_actor *player = &g_odg.actors[ODG_PLAYER_ID];
            /* The chase camera is position-locked to the player so the avatar stays
             * centered. Realistic lag is rotational only; positional lag makes steering
             * look imprecise because the controlled body drifts across the screen. */
            g_odg.camera_anchor_x = player->x;
            g_odg.camera_anchor_z = player->z;
            {
                int32_t look_x=g_odg.input.aim_x_q15;
                int32_t look_z=g_odg.input.aim_z_q15;
                int32_t look_abs=odg_abs_i32(look_x);
                if (look_abs>ODG_CAMERA_LOOK_DEADZONE) {
                    int32_t strength=(int32_t)(((int64_t)(look_abs-ODG_CAMERA_LOOK_DEADZONE)*ODG_Q15_ONE)/
                                               (ODG_Q15_ONE-ODG_CAMERA_LOOK_DEADZONE));
                    int32_t sin_step=(int32_t)(((int64_t)ODG_CAMERA_LOOK_MAX_SIN_Q15*strength)/ODG_Q15_ONE);
                    int32_t cos_step;
                    int32_t nx,nz;
                    if (sin_step<1) sin_step=1;
                    cos_step=ODG_Q15_ONE-(int32_t)(((int64_t)sin_step*sin_step)/(2*ODG_Q15_ONE));
                    rotate_dir_q15(g_odg.camera_dir_x_q15,g_odg.camera_dir_z_q15,
                                   cos_step,sin_step,look_x>0?-1:1,&nx,&nz);
                    g_odg.camera_dir_x_q15=nx;
                    g_odg.camera_dir_z_q15=nz;
                    g_odg.camera_turn_rate_q15=0;
                    g_odg.camera_manual_ticks=ODG_CAMERA_MANUAL_HOLD_TICKS;
                }
                if (odg_abs_i32(look_z)>ODG_CAMERA_LOOK_DEADZONE) {
                    int32_t delta=(int32_t)(((int64_t)look_z*ODG_CAMERA_PITCH_STEP_Q15)/ODG_Q15_ONE);
                    g_odg.camera_pitch_q15=odg_clamp_i32(g_odg.camera_pitch_q15-delta,
                                                         ODG_CAMERA_PITCH_MIN_Q15,
                                                         ODG_CAMERA_PITCH_MAX_Q15);
                    g_odg.camera_manual_ticks=ODG_CAMERA_MANUAL_HOLD_TICKS;
                }
                if(look_abs<=ODG_CAMERA_LOOK_DEADZONE &&
                   odg_abs_i32(look_z)<=ODG_CAMERA_LOOK_DEADZONE){
                    if(g_odg.camera_manual_ticks>0u)--g_odg.camera_manual_ticks;
                    /* Fortnite-style free chase view: releasing the look surface freezes
                     * the chosen yaw instead of dragging it back behind the cube. Movement
                     * then interprets joystick UP against this persistent camera heading. */
                    g_odg.camera_turn_rate_q15=approach_signed(g_odg.camera_turn_rate_q15,0,ODG_CAMERA_TURN_ACCEL_Q15);
                }
            }
            {
                int32_t desired_dist=g_odg.camera_target_distance_fx;
                int32_t probe;
                if (g_odg.camera_mode == ODG_CAMERA_MODE_FIRST_PERSON) {
                    desired_dist = 0;
                } else {
                    if (desired_dist < ODG_CAMERA_MIN_DISTANCE_FX) desired_dist = ODG_CAMERA_MIN_DISTANCE_FX;
                    if (desired_dist > ODG_CAMERA_FAR_DISTANCE_FX) desired_dist = ODG_CAMERA_FAR_DISTANCE_FX;
                    /* Camera collision is presentation-only. Search from the selected profile
                     * distance inward, preserving close/medium/far preference when space allows. */
                    for (probe=desired_dist;probe>=ODG_CAMERA_MIN_DISTANCE_FX;probe-=180) {
                        if (camera_segment_clear(player,g_odg.camera_dir_x_q15,g_odg.camera_dir_z_q15,probe)) {
                            desired_dist=probe;break;
                        }
                        desired_dist=ODG_CAMERA_MIN_DISTANCE_FX;
                    }
                }
                g_odg.camera_distance_fx=approach_signed(g_odg.camera_distance_fx,desired_dist,
                                                          desired_dist<g_odg.camera_distance_fx?180:28);
                {
                    int32_t cam_x = player->x - (int32_t)(((int64_t)g_odg.camera_dir_x_q15 * g_odg.camera_distance_fx) / ODG_Q15_ONE);
                    int32_t cam_z = player->z - (int32_t)(((int64_t)g_odg.camera_dir_z_q15 * g_odg.camera_distance_fx) / ODG_Q15_ONE);
                    int32_t desired = odg_terrain_height_fx(player->x, player->z) +
                        (g_odg.camera_mode==ODG_CAMERA_MODE_FIRST_PERSON ? (ODG_FX_ONE*82/100) : ODG_CAMERA_PLAYER_HEIGHT_FX);
                    int32_t clearance = odg_terrain_height_fx(cam_x, cam_z) +
                        (g_odg.camera_mode==ODG_CAMERA_MODE_FIRST_PERSON ? (ODG_FX_ONE*72/100) : ODG_CAMERA_GROUND_CLEARANCE_FX);
                    int32_t target = desired > clearance ? desired : clearance;
                    int32_t step = target > g_odg.camera_height_fx ? 24 : 10;
                    g_odg.camera_height_fx = approach_signed(g_odg.camera_height_fx, target, step);
                    if (g_odg.camera_height_fx < clearance) g_odg.camera_height_fx = clearance;
                }
            }
        }
        for (i = 1u; i < ODG_MAX_ACTORS; ++i) update_actor(&g_odg.actors[i]);
        resolve_trail_contacts();
        odg_survival_tick();
        odg_process_commands();
        odg_handle_interaction();
        handle_drop_button();
        update_turrets();
        odg_resources_tick();
        if((g_odg.tick%ODG_TICK_RATE)==0u) {
            odg_resources_stream_refresh();odg_turrets_stream_refresh();odg_chunks_refresh_summaries();
        }
        odg_artifacts_tick();
        odg_update_world_pickups();
        /* Territory runners are non-solid to each other. Contact is decided by
         * exposed-trail topology, not by a body solver that can fabricate cell moves. */
        check_match_end();
    }
    g_odg.prev_buttons = g_odg.input.buttons;
    odg_entities_spatial_mark_dirty();
    odg_music_decay_visual_tick();
    update_particles();
}
