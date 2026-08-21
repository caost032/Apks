#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

static int failures = 0;
static uint8_t save_buffer[700000u];
static odg_map_sample map_samples[64u * 64u];
static odg_map_marker map_markers[ODG_MAP_MAX_MARKERS];
static uint8_t avatar_texture_test[ODG_AVATAR_TEXTURE_SIZE * ODG_AVATAR_TEXTURE_SIZE * 4u];
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); ++failures; } } while (0)

static uint64_t run_script(uint64_t seed) {
    uint32_t i;
    CHECK(odg_init(seed, 320u, 180u) == ODG_STATUS_OK);
    for (i = 0u; i < 1600u && !odg_match_over(); ++i) {
        int32_t mx = (i % 360u < 180u) ? 25000 : -21000;
        int32_t mz = (i % 520u < 260u) ? 19000 : -24000;
        uint32_t buttons = (i % 400u == 0u) ? ODG_BUTTON_DASH : 0u;
        odg_set_input(mx, mz, 0, 0, buttons);
        odg_step_ticks(1u);
    }
    return odg_state_hash();
}

static int counts_are_consistent(void) {
    uint32_t counts[ODG_MAX_ACTORS] = {0u};
    uint32_t trails[ODG_MAX_ACTORS] = {0u};
    uint32_t c;
    for(c=0u;c<g_odg.chunk_cache_used;++c) {
        const odg_chunk_runtime *record=&g_odg_chunk_cache[c];uint32_t ordinal;
        if(record->used==0u) continue;
        for(ordinal=0u;ordinal<ODG_CHUNK_CELL_COUNT;++ordinal) {
            uint32_t byte_index=ordinal>>1u;
            uint8_t ob=record->territory_packed[byte_index],tb=record->trail_packed[byte_index];
            uint8_t o=(ordinal&1u)!=0u?(uint8_t)(ob>>4u):(uint8_t)(ob&UINT8_C(0x0f));
            uint8_t t=(ordinal&1u)!=0u?(uint8_t)(tb>>4u):(uint8_t)(tb&UINT8_C(0x0f));
            if(o!=ODG_OWNER_NONE){uint32_t id=ODG_ID_FROM_OWNER(o);if(id>=ODG_MAX_ACTORS)return 0;++counts[id];}
            if(t!=ODG_OWNER_NONE){uint32_t id=ODG_ID_FROM_OWNER(t);if(id>=ODG_MAX_ACTORS)return 0;++trails[id];}
        }
    }
    for (c = 0u; c < ODG_MAX_ACTORS; ++c) {
        if (counts[c] != g_odg.territory_count[c]) return 0;
        if (trails[c] != g_odg.actors[c].trail_len) return 0;
    }
    return 1;
}

static uint32_t count_rgb_difference(const uint8_t *a, const uint8_t *b, uint32_t bytes) {
    uint32_t i,n=0u;
    for(i=0u;i+3u<bytes;i+=4u) {
        int32_t dr=(int32_t)a[i]-(int32_t)b[i];
        int32_t dg=(int32_t)a[i+1u]-(int32_t)b[i+1u];
        int32_t db=(int32_t)a[i+2u]-(int32_t)b[i+2u];
        uint32_t delta=(uint32_t)(dr<0?-dr:dr)+(uint32_t)(dg<0?-dg:dg)+
                       (uint32_t)(db<0?-db:db);
        if(delta>24u) ++n;
    }
    return n;
}

static uint32_t find_adjacent_playable(uint32_t center) {
    uint32_t x = center & (ODG_GRID_SIZE - 1u);
    uint32_t z = center >> ODG_GRID_SHIFT;
    if (x + 1u < ODG_GRID_SIZE) return center + 1u;
    if (x > 0u) return center - 1u;
    if (z + 1u < ODG_GRID_SIZE) return center + ODG_GRID_SIZE;
    if (z > 0u) return center - ODG_GRID_SIZE;
    return UINT32_MAX;
}

static void set_test_territory_owner(uint32_t cell,uint8_t owner) {
    int64_t gx,gz;
    if (cell>=ODG_CELL_COUNT) return;
    gx=g_odg.world_origin_cell_x+(int64_t)(cell&(ODG_GRID_SIZE-1u));
    gz=g_odg.world_origin_cell_z+(int64_t)(cell>>ODG_GRID_SHIFT);
    /* Chunk ownership is the authority and maintains territory_count/score/level itself.
     * Older tests predated that centralization and double-adjusted the derived counters. */
    odg_chunk_set_owner_at_global_cell(gx,gz,owner);
}

static void set_test_trail_owner(uint32_t cell,uint8_t owner) {
    int64_t gx,gz;
    if (cell>=ODG_CELL_COUNT) return;
    gx=g_odg.world_origin_cell_x+(int64_t)(cell&(ODG_GRID_SIZE-1u));
    gz=g_odg.world_origin_cell_z+(int64_t)(cell>>ODG_GRID_SHIFT);
    odg_chunk_set_trail_at_global_cell(gx,gz,owner);
}

static void clear_all_test_territory(void) {
    uint32_t c;
    /* Open Domain ownership lives in the persisted chunk ledger, not only in the
     * current 128x128 resident cache. Tests that want an artificial empty world must
     * therefore clear every loaded chunk and then rebuild the derived counters/cache. */
    for(c=0u;c<g_odg.chunk_cache_used;++c) {
        const odg_chunk_runtime *record=&g_odg_chunk_cache[c];
        uint32_t ordinal;
        if(record->used==0u) continue;
        for(ordinal=0u;ordinal<ODG_CHUNK_CELL_COUNT;++ordinal) {
            int64_t gx=record->chunk_x*(int64_t)ODG_CHUNK_SIZE_CELLS+
                       (int64_t)(ordinal&(ODG_CHUNK_SIZE_CELLS-1u));
            int64_t gz=record->chunk_z*(int64_t)ODG_CHUNK_SIZE_CELLS+
                       (int64_t)(ordinal>>5u);
            if(odg_chunk_runtime_owner_at_ordinal_internal(record,ordinal)!=ODG_OWNER_NONE)
                odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_NONE);
        }
    }
    odg_chunks_refresh_summaries();
}

/* Procedural infrastructure is intentionally sparse in Open Domain, so a test seed is
 * not entitled to contain a natural turret in its current streamed chunks. Interaction
 * regressions that need one create a local non-procedural fixture instead of coupling
 * their correctness to incidental world-generation density. */
static odg_turret *ensure_test_turret(void) {
    odg_turret *t;
    odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
    if(g_odg.turret_count!=0u&&g_odg_turrets!=NULL)return &g_odg_turrets[0];
    if(!odg_entities_reserve_turrets(1u)||g_odg_turrets==NULL)return NULL;
    g_odg.turret_count=1u;t=&g_odg_turrets[0];odg_memset(t,0,sizeof(*t));
    t->active=1u;t->id=0u;t->owner=ODG_TURRET_NEUTRAL;t->carried_by=ODG_TURRET_NONE;
    t->instance_id=odg_next_instance_id();t->x=p->x;t->z=p->z;t->local_resident=1u;
    t->head_x_q15=ODG_Q15_ONE;t->head_z_q15=0;t->target_kind=ODG_TURRET_TARGET_NONE;
    t->target_actor_id=UINT32_MAX;t->last_target_cell=UINT32_MAX;
    t->target_global_cell_x=INT64_MIN;t->target_global_cell_z=INT64_MIN;
    odg_local_fx_to_global_fx_internal(t->x,t->z,&t->global_fx_x,&t->global_fx_z);
    odg_apply_turret_tier(t,ODG_MATERIAL_WOOD,0);odg_entities_spatial_mark_dirty();
    return t;
}

int main(void) {
    uint64_t a, b, c;
    uintptr_t fb;
    uint32_t bytes;
    uint32_t i;
    uint32_t nonzero = 0u;
    int32_t spawn_x, spawn_z;

    CHECK(odg_api_version() == ODG_API_VERSION);
    CHECK(odg_init(1u, 0u, 180u) == ODG_STATUS_INVALID_ARGUMENT);
    CHECK(odg_init(1u, ODG_MAX_RENDER_WIDTH + 1u, 180u) == ODG_STATUS_INVALID_ARGUMENT);

    CHECK(odg_init(UINT64_C(0x123456789abcdef0), 480u, 270u) == ODG_STATUS_OK);
    CHECK(g_odg.playable_count == ODG_CELL_COUNT);
    CHECK(g_odg.playable[0] != 0u && g_odg.playable[ODG_CELL_COUNT - 1u] != 0u);
    CHECK(odg_territory_total_cells() > 0u && odg_territory_total_cells() < g_odg.playable_count);
    CHECK(odg_alive_count() == ODG_MAX_ACTORS);
    CHECK(odg_turret_count() > 0u);
    CHECK(odg_ammo_crate_count() == 0u);
    /* Ambient ammunition was removed from world generation; initial tech salvage is now
     * the four reprogram chips spawned by spawn_initial_pickups(). */
    CHECK(g_odg.pickup_count >= 4u);
    CHECK(counts_are_consistent());
    for (i=0u;i<g_odg.turret_count;++i) {
        uint32_t tc=odg_cell_from_world(g_odg_turrets[i].x,g_odg_turrets[i].z);
        CHECK(g_odg_turrets[i].owner == ODG_TURRET_NEUTRAL);
        CHECK(g_odg_turrets[i].ammo == g_odg_turrets[i].max_ammo);
        CHECK(g_odg.playable[tc] != 0u);
    }

    /* Terrain is authoritative/deterministic rather than a visual-only plane. The chase
     * camera must remain above the terrain under its own position while traversing hills. */
    {
        int32_t h_hill=odg_terrain_height_fx(-31*ODG_FX_ONE,-9*ODG_FX_ONE);
        int32_t h_valley=odg_terrain_height_fx(4*ODG_FX_ONE,4*ODG_FX_ONE);
        CHECK(h_hill!=h_valley);
    }
    CHECK(odg_init(UINT64_C(0x5445525241494e33), 320u, 180u) == ODG_STATUS_OK);
    odg_set_input(18000,28000,0,0,0u);
    for(i=0u;i<420u && g_odg.actors[0].hp!=0u;++i){
        odg_actor *p=&g_odg.actors[0];
        int32_t cam_x,cam_z,clearance;
        odg_step_ticks(1u);
        CHECK(g_odg.camera_anchor_x==p->x && g_odg.camera_anchor_z==p->z);
        CHECK(g_odg.camera_distance_fx>=ODG_CAMERA_MIN_DISTANCE_FX && g_odg.camera_distance_fx<=ODG_CAMERA_DISTANCE_FX);
        cam_x=p->x-(int32_t)(((int64_t)g_odg.camera_dir_x_q15*g_odg.camera_distance_fx)/ODG_Q15_ONE);
        cam_z=p->z-(int32_t)(((int64_t)g_odg.camera_dir_z_q15*g_odg.camera_distance_fx)/ODG_Q15_ONE);
        clearance=odg_terrain_height_fx(cam_x,cam_z)+ODG_CAMERA_GROUND_CLEARANCE_FX;
        CHECK(g_odg.camera_height_fx>=clearance);
    }

    /* Spawn positions are seeded, not hard-coded. */
    spawn_x=g_odg.actors[0].x; spawn_z=g_odg.actors[0].z;
    CHECK(odg_init(UINT64_C(0x123456789abcdef1), 480u, 270u) == ODG_STATUS_OK);
    CHECK(g_odg.actors[0].x != spawn_x || g_odg.actors[0].z != spawn_z);

    /* v10 contact steering: a diagonal command into an obstacle corner must retain
     * forward/tangential progress instead of degenerating into repeated stop/X/Z ticks. */
    CHECK(odg_init(UINT64_C(0x434f4e5441435431), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t sx,sz;
        p->x=-10600;p->z=-9600;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=23170;p->face_z_q15=23170;
        p->last_cell=odg_cell_from_world(p->x,p->z);
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        sx=p->x;sz=p->z;
        odg_set_input(23170,23170,0,0,0u);
        for(i=0u;i<90u && p->hp!=0u;++i) odg_step_ticks(1u);
        CHECK(odg_dist2(sx,sz,p->x,p->z)>(int64_t)ODG_FX_ONE*ODG_FX_ONE);
        CHECK(p->slide_lock_ticks<=ODG_SLIDE_LOCK_TICKS);
    }

    /* Third-person camera retracts before entering world geometry, but never teleports
     * the controlled actor or drops below its configured minimum chase distance. */
    CHECK(odg_init(UINT64_C(0x43414d434f4c4c31), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        p->x=-9*ODG_FX_ONE;p->z=-5*ODG_FX_ONE;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;p->last_cell=odg_cell_from_world(p->x,p->z);
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;
        g_odg.camera_distance_fx=ODG_CAMERA_DISTANCE_FX;g_odg.control_active=0u;
        /* Open Domain no longer has the v14 static arena building at this coordinate.
         * Install one deliberate blocker on the chase ray so this test measures camera
         * collision rather than relying on deleted scenery. */
        g_odg.obstacle_count=1u;
        g_odg.obstacles[0].x=p->x;g_odg.obstacles[0].z=p->z-2*ODG_FX_ONE;
        g_odg.obstacles[0].hx=ODG_FX_ONE/2;g_odg.obstacles[0].hz=ODG_FX_ONE/2;
        odg_set_input(0,0,0,0,0u);
        for(i=0u;i<16u;++i) odg_step_ticks(1u);
        CHECK(g_odg.camera_distance_fx<ODG_CAMERA_DISTANCE_FX);
        CHECK(g_odg.camera_distance_fx>=ODG_CAMERA_MIN_DISTANCE_FX);
        p->x=0;p->z=0;p->last_cell=odg_cell_from_world(0,0);
        for(i=0u;i<120u;++i) odg_step_ticks(1u);
        CHECK(g_odg.camera_distance_fx>ODG_CAMERA_MIN_DISTANCE_FX);
    }

    /* v12 free-look contract: camera yaw is independent from locomotion. It rotates
     * while the player is stationary and, after release, HOLDS the chosen yaw instead of
     * recentering behind the cube. */
    CHECK(odg_init(UINT64_C(0x4c4f4f4b56313231), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t body_x,body_z,cam_after_x,cam_after_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;
        g_odg.camera_pitch_q15=ODG_CAMERA_PITCH_DEFAULT_Q15;
        g_odg.camera_manual_ticks=0u;g_odg.camera_turn_rate_q15=0;
        body_x=p->face_x_q15;body_z=p->face_z_q15;
        odg_set_input(0,0,ODG_Q15_ONE,0,0u);
        for(i=0u;i<24u;++i) odg_step_ticks(1u);
        CHECK(g_odg.camera_dir_x_q15>12000);
        CHECK(p->face_x_q15==body_x && p->face_z_q15==body_z);
        CHECK(p->x==0 && p->z==0);
        cam_after_x=g_odg.camera_dir_x_q15;cam_after_z=g_odg.camera_dir_z_q15;
        odg_set_input(0,0,0,0,0u);
        for(i=0u;i<ODG_CAMERA_MANUAL_HOLD_TICKS+140u;++i) odg_step_ticks(1u);
        CHECK(((int64_t)g_odg.camera_dir_x_q15*cam_after_x+
               (int64_t)g_odg.camera_dir_z_q15*cam_after_z)/ODG_Q15_ONE>32000);
        CHECK(p->face_x_q15==body_x && p->face_z_q15==body_z);
    }

    /* Theme choice is presentation-only and must not perturb the deterministic state. */
    CHECK(odg_init(UINT64_C(0x5448454d45563131), 320u, 180u) == ODG_STATUS_OK);
    {
        uint64_t h0=odg_state_hash();
        uintptr_t r0=odg_render_frame();
        const uint8_t *p0=(const uint8_t*)r0;
        uint32_t sky0=((uint32_t)p0[0]<<16)|((uint32_t)p0[1]<<8)|p0[2];
        odg_set_visual_theme(ODG_VISUAL_THEME_SOLAR_EMBER);
        CHECK(odg_visual_theme()==ODG_VISUAL_THEME_SOLAR_EMBER);
        CHECK(odg_state_hash()==h0);
        {
            uintptr_t r1=odg_render_frame();
            const uint8_t *p1=(const uint8_t*)r1;
            uint32_t sky1=((uint32_t)p1[0]<<16)|((uint32_t)p1[1]<<8)|p1[2];
            CHECK(sky0!=sky1); /* theme is a real C-renderer change, not host CSS */
        }
        odg_set_visual_theme(ODG_VISUAL_THEME_OBSIDIAN_PULSE);
        CHECK(odg_state_hash()==h0);
        odg_set_presentation_mode(ODG_PRESENTATION_SHOWCASE);
        CHECK(odg_presentation_mode()==ODG_PRESENTATION_SHOWCASE);
        CHECK(odg_state_hash()==h0);
        (void)odg_render_frame();
        CHECK(odg_state_hash()==h0);
        odg_set_presentation_mode(ODG_PRESENTATION_GAMEPLAY);
        CHECK(odg_presentation_mode()==ODG_PRESENTATION_GAMEPLAY);
    }

    /* v12 precision locomotion contract: normal direction changes own the ground path on
     * the same tick. The cube body rotates inertially afterward, but old forward velocity
     * must not make a RIGHT/diagonal command continue straight. */
    CHECK(odg_init(UINT64_C(0x5150415449414c), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t initial_fx,initial_fz,initial_camx,initial_camz,turn_start_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        odg_set_input(0,30000,0,0,0u);
        for(i=0u;i<40u;++i) odg_step_ticks(1u);
        CHECK(p->vz>30 && odg_abs_i32(p->vx)<8);
        initial_fx=p->face_x_q15;initial_fz=p->face_z_q15;
        initial_camx=g_odg.camera_dir_x_q15;initial_camz=g_odg.camera_dir_z_q15;
        turn_start_z=p->z;
        odg_set_input(30000,0,0,0,0u);
        odg_step_ticks(1u);
        CHECK(p->face_x_q15>0); /* body rotation starts, but does not gate translation */
        CHECK(p->vx>30 && odg_abs_i32(p->vz)<8); /* RIGHT is physically RIGHT immediately */
        CHECK((int64_t)p->face_x_q15*initial_fx+(int64_t)p->face_z_q15*initial_fz>0); /* body does not snap */
        CHECK((int64_t)g_odg.camera_dir_x_q15*initial_camx+(int64_t)g_odg.camera_dir_z_q15*initial_camz>0);
        for(i=0u;i<12u;++i) odg_step_ticks(1u);
        CHECK(p->vx>30);
        CHECK(odg_abs_i32(p->z-turn_start_z)<40); /* no hidden forward drift while steering right */
        for(i=0u;i<48u;++i) odg_step_ticks(1u);
        CHECK(p->vx>35);
        CHECK(p->face_x_q15>18000);
        CHECK(p->hp==p->max_hp);
    }

    /* A true 180-degree reversal is the exception to instant heading authority: speed
     * brakes along the old trajectory before changing sign, while body/camera rotate. */
    CHECK(odg_init(UINT64_C(0x5245564552534538), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t before_speed,old_face_z;
        /* Isolate locomotion from streamed resource/artifact collision. Open Domain
         * may legitimately materialize either at the old hard-coded v14 origin. */
        g_odg.obstacle_count=0u;g_odg.resource_count=0u;g_odg.artifact_count=0u;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        odg_set_input(0,30000,0,0,0u);for(i=0u;i<50u;++i)odg_step_ticks(1u);
        before_speed=p->speed_fx;old_face_z=p->face_z_q15;
        odg_set_input(0,-30000,0,0,0u);odg_step_ticks(1u);
        CHECK(p->vz>0); /* still braking forward, not teleporting into reverse */
        CHECK(p->speed_fx<before_speed);
        CHECK(p->face_z_q15>0 && old_face_z>0); /* orientation has begun a physical turn, no snap */
        for(i=0u;i<80u;++i)odg_step_ticks(1u);
        CHECK(p->vz<0);
        CHECK(p->face_z_q15<0);
    }

    /* Diagonal intent must create a curved ground trajectory, not a stale-axis march.
     * A held gesture owns a persistent world heading; rotating the finger rotates that
     * heading directly instead of allowing camera follow to redefine it. */
    CHECK(odg_init(UINT64_C(0x444941474f4e414c), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t x0,z0;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        x0=p->x;z0=p->z;
        odg_set_input(23000,-23000,0,0,0u);
        for(i=0u;i<180u;++i) odg_step_ticks(1u);
        CHECK(p->x > x0 + 4*ODG_FX_ONE);
        CHECK(p->z < z0 - 2*ODG_FX_ONE);
        CHECK(p->steer_q15!=0 || p->face_x_q15>10000);
        CHECK(p->face_x_q15>9000 && p->face_z_q15<0);
        {
            int32_t old_hx=g_odg.control_heading_x_q15,old_hz=g_odg.control_heading_z_q15;
            int32_t old_bx=g_odg.control_basis_x_q15,old_bz=g_odg.control_basis_z_q15;
            odg_set_input(0,30000,0,0,0u);
            odg_step_ticks(1u);
            CHECK((int64_t)g_odg.control_heading_x_q15*old_hx+
                  (int64_t)g_odg.control_heading_z_q15*old_hz < 0);
            CHECK((int64_t)g_odg.control_basis_x_q15*old_bx+
                  (int64_t)g_odg.control_basis_z_q15*old_bz > INT64_C(1000000000));
        }
    }

    /* A rear diagonal is a genuine turn, not an instantaneous sideways teleport. The
     * lateral component begins on tick one, crosses through the old forward component,
     * then converges to the requested 45-degree heading without stop-go braking. */
    CHECK(odg_init(UINT64_C(0x4152434e4f534c49), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        uint32_t moving_ticks=0u;
        g_odg.obstacle_count=0u;g_odg.resource_count=0u;g_odg.artifact_count=0u;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        odg_set_input(22000,-22000,0,0,0u);
        for(i=0u;i<100u;++i){
            odg_step_ticks(1u);
            if(i<8u){ CHECK(p->vx>=0); CHECK(p->face_x_q15>0); }
            if(i==24u){ CHECK(p->vx>0); CHECK(p->vz<0); }
            if(p->speed_fx>0) ++moving_ticks;
        }
        CHECK(moving_ticks>96u);
        CHECK(p->x>ODG_FX_ONE);
        CHECK(p->z<-ODG_FX_ONE);
        CHECK(odg_abs_i32(p->vx + p->vz)<18); /* ~45 degree target */
    }

    /* Analog magnitude is authoritative. A small stick displacement must remain slower
     * than a full displacement instead of being normalized to 100 percent. */
    CHECK(odg_init(UINT64_C(0x414e414c4f473037), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t weak_speed,strong_speed;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        odg_set_input(0,12000,0,0,0u);for(i=0u;i<80u;++i)odg_step_ticks(1u);weak_speed=p->speed_fx;
        p->vx=0;p->vz=0;p->speed_fx=0;g_odg.control_active=0u;
        odg_set_input(0,30000,0,0,0u);for(i=0u;i<80u;++i)odg_step_ticks(1u);strong_speed=p->speed_fx;
        CHECK(weak_speed>0);
        CHECK(strong_speed>weak_speed*2);
    }

    /* v12 camera-relative movement. Joystick RIGHT must not rotate the camera by itself.
     * After explicit free-look rotates the camera, joystick UP follows that new view. */
    CHECK(odg_init(UINT64_C(0x43414d5631324c4f), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t cam0x=0,cam0z=ODG_Q15_ONE,turned_x,turned_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=cam0x;g_odg.camera_dir_z_q15=cam0z;
        g_odg.camera_turn_rate_q15=0;g_odg.control_active=0u;
        odg_set_input(30000,0,0,0,0u);
        for(i=0u;i<70u;++i) odg_step_ticks(1u);
        CHECK(g_odg.camera_dir_x_q15==cam0x && g_odg.camera_dir_z_q15==cam0z);
        CHECK(p->face_x_q15>25000);
        CHECK(odg_control_local_x_q15()>28000);
        odg_set_input(0,0,32767,0,0u);
        for(i=0u;i<24u;++i) odg_step_ticks(1u);
        turned_x=g_odg.camera_dir_x_q15;turned_z=g_odg.camera_dir_z_q15;
        CHECK(turned_x>12000);
        odg_set_input(0,30000,0,0,0u);
        odg_step_ticks(1u);
        CHECK(((int64_t)odg_control_heading_x_q15()*turned_x+
               (int64_t)odg_control_heading_z_q15()*turned_z)/ODG_Q15_ONE>30000);
        {
            int32_t vx=0,vz=0;
            odg_normalize_q15(p->vx,p->vz,&vx,&vz);
            CHECK(((int64_t)vx*turned_x+(int64_t)vz*turned_z)/ODG_Q15_ONE>30000);
        }
    }

    /* Fixed joystick in v12 is camera-local, not a rebasing world-heading dial. The
     * knob can stay physically UP/RIGHT while camera movement rotates the world request. */
    CHECK(odg_init(UINT64_C(0x4a4f595631324341), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t h0x,h0z,cam_after_x,cam_after_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;
        g_odg.camera_turn_rate_q15=0;g_odg.control_active=0u;
        odg_set_input(30000,0,0,0,0u);
        odg_step_ticks(1u);
        h0x=odg_control_heading_x_q15();h0z=odg_control_heading_z_q15();
        CHECK(odg_control_local_x_q15()>28000);
        for(i=1u;i<60u;++i)odg_step_ticks(1u);
        CHECK(odg_control_local_x_q15()>28000); /* camera did not move */
        odg_set_input(30000,0,32767,0,0u);
        for(i=0u;i<18u;++i)odg_step_ticks(1u);
        cam_after_x=g_odg.camera_dir_x_q15;cam_after_z=g_odg.camera_dir_z_q15;
        CHECK((int64_t)odg_control_heading_x_q15()*h0x+
              (int64_t)odg_control_heading_z_q15()*h0z<INT64_C(1000000000));
        CHECK(odg_control_local_x_q15()>26000);
        odg_set_input(0,30000,0,0,0u);
        odg_step_ticks(1u);
        CHECK(((int64_t)odg_control_heading_x_q15()*cam_after_x+
               (int64_t)odg_control_heading_z_q15()*cam_after_z)/ODG_Q15_ONE>30000);
        odg_set_input(0,0,0,0,0u);
        for(i=0u;i<ODG_CAMERA_MANUAL_HOLD_TICKS+170u;++i)odg_step_ticks(1u);
        CHECK(((int64_t)g_odg.camera_dir_x_q15*cam_after_x+
               (int64_t)g_odg.camera_dir_z_q15*cam_after_z)/ODG_Q15_ONE>32000);
    }

    /* Exact world-heading path remains available for native/replay hosts. Unlike local
     * joystick input, a supplied world vector is intentionally independent of camera yaw. */
    CHECK(odg_init(UINT64_C(0x574f524c44563132), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t first_hx,first_hz,before_x,before_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;
        g_odg.camera_turn_rate_q15=0;g_odg.control_active=0u;
        odg_set_world_input(ODG_Q15_ONE,0,30000,0,0,0u);
        odg_step_ticks(3u);
        CHECK(p->face_x_q15>0 && p->vx>0);
        first_hx=odg_control_heading_x_q15();first_hz=odg_control_heading_z_q15();
        /* Rotate only camera; exact world input must stay exactly world-right. */
        odg_set_world_input(ODG_Q15_ONE,0,30000,32767,0,0u);
        for(i=0u;i<18u;++i)odg_step_ticks(1u);
        CHECK((int64_t)odg_control_heading_x_q15()*first_hx+
              (int64_t)odg_control_heading_z_q15()*first_hz>INT64_C(1000000000));
        before_x=p->x;before_z=p->z;
        odg_set_world_input(0,-ODG_Q15_ONE,30000,0,0,0u);
        odg_step_ticks(2u);
        CHECK((int64_t)odg_control_heading_x_q15()*first_hx+
              (int64_t)odg_control_heading_z_q15()*first_hz<INT64_C(300000000));
        CHECK(p->x!=before_x || p->z!=before_z);
        CHECK(odg_abs_i32(p->steer_q15)>1000);
        odg_set_world_input(0,0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(odg_control_strength_q15()==0);
    }

    /* Bot return steering must not replan X/Z repeatedly while the actor is still in
     * the same cell. This specifically protects against the visible left-right chatter
     * reported in the previous controller. */
    CHECK(odg_init(UINT64_C(0x424f544e4f434841), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *bot=&g_odg.actors[1];
        uint32_t same_cell_changes=0u,prev_cell,prev_mode;
        int32_t prev_x,prev_z;
        g_odg.obstacle_count=0u;
        /* Keep the player inside the current floating-origin window so this focused
         * steering test does not trigger a resource-stream refresh via recenter. */
        g_odg.actors[ODG_PLAYER_ID].x=0;g_odg.actors[ODG_PLAYER_ID].z=0;
        g_odg.actors[ODG_PLAYER_ID].vx=0;g_odg.actors[ODG_PLAYER_ID].vz=0;
        /* This is a steering regression, not an economy test. Remove materialized
         * resource targets so the v15 utility planner cannot legitimately replace
         * RETURN with a harvesting trip during the observation window. */
        g_odg.resource_count=0u;
        odg_inventory_init(&bot->inventory);
        bot->bot_economy_item_type=0u;bot->bot_economy_target_id=UINT32_MAX;
        bot->bot_mode=ODG_BOT_RETURN;
        bot->trail_active=1u;
        bot->trail_len=8u;
        bot->ai_plan_cell=UINT32_MAX;
        bot->ai_commit_ticks=0u;
        prev_cell=odg_cell_from_world(bot->x,bot->z);
        prev_x=bot->ai_x_q15;prev_z=bot->ai_z_q15;prev_mode=bot->bot_mode;
        /* The historical defect replanned every four ticks; 100 ticks is enough to
         * detect it while staying below the periodic resource-stream refresh. */
        for(i=0u;i<100u && bot->hp!=0u;++i){
            uint32_t cell;
            odg_step_ticks(1u);
            cell=odg_cell_from_world(bot->x,bot->z);
            if(cell==prev_cell && prev_mode==ODG_BOT_RETURN && bot->bot_mode==ODG_BOT_RETURN &&
               (bot->ai_x_q15!=prev_x || bot->ai_z_q15!=prev_z)) ++same_cell_changes;
            prev_cell=cell;prev_x=bot->ai_x_q15;prev_z=bot->ai_z_q15;prev_mode=bot->bot_mode;
        }
        CHECK(same_cell_changes<=2u);
    }

    /* Self trail remains explicitly non-lethal. */
    CHECK(odg_init(UINT64_C(0x77771111), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        uint32_t cur=p->last_cell;
        uint32_t target=find_adjacent_playable(cur);
        CHECK(target != UINT32_MAX);
        if (target != UINT32_MAX) {
            int32_t dx=odg_cell_center_x(target)-odg_cell_center_x(cur);
            int32_t dz=odg_cell_center_z(target)-odg_cell_center_z(cur);
            uint8_t own=ODG_OWNER_FROM_ID(0u);
            p->x=odg_cell_center_x(cur); p->z=odg_cell_center_z(cur);
            p->vx=0; p->vz=0; p->trail_active=1u; p->trail_len=1u;
            set_test_trail_owner(target,own);
            g_odg.territory[target]=ODG_OWNER_NONE;
            odg_normalize_q15(dx,dz,&p->face_x_q15,&p->face_z_q15);
            g_odg.camera_dir_x_q15=p->face_x_q15; g_odg.camera_dir_z_q15=p->face_z_q15;
            g_odg.control_active=0u;
            odg_set_input(0,32767,0,0,0u);
            for(i=0u;i<30u;++i) odg_step_ticks(1u);
            CHECK(p->hp == p->max_hp);
            CHECK(p->death_reason != ODG_DEATH_SELF_CROSS);
        }
    }

    /* Trail-vs-ground priority: ground ownership never makes an exposed enemy trail
     * invulnerable. A bot can cut the player's trail on bot-owned ground and the player
     * can cut a bot trail while standing inside player-owned ground. */
    CHECK(odg_init(UINT64_C(0x545241494c505249), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0],*b1=&g_odg.actors[1];
        uint32_t pc=p->last_cell,bc=b1->last_cell;
        uint32_t pt=find_adjacent_playable(pc),bt=find_adjacent_playable(bc);
        CHECK(pt!=UINT32_MAX && bt!=UINT32_MAX);
        if(pt!=UINT32_MAX && bt!=UINT32_MAX){
            uint8_t po=ODG_OWNER_FROM_ID(0u),bo=ODG_OWNER_FROM_ID(1u);
            /* Bot-owned ground carrying a player trail: contact disrupts capture, never kills. */
            set_test_territory_owner(bt,bo);set_test_trail_owner(bt,po);p->trail_active=1u;p->trail_len=1u;
            b1->last_cell=bc;b1->x=odg_cell_center_x(bt);b1->z=odg_cell_center_z(bt);
            /* Force the next simulation step to process the transition by restoring old last cell. */
            b1->last_cell=bc;odg_step_ticks(1u);
            CHECK(p->hp==p->max_hp);CHECK(p->death_reason!=ODG_DEATH_TRAIL_CUT);
            CHECK(p->trail_active==0u && p->trail_len==0u && p->trail_broken==1u);
        }
    }
    /* A broken trail is a state machine, not just a one-frame effect: while the actor
     * remains outside owned territory no replacement trail may start. Returning home is
     * the sole reset, after which leaving again may begin a fresh capture trail. */
    CHECK(odg_init(UINT64_C(0x42524f4b454e5254), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        uint32_t home=(ODG_GRID_SIZE/2u)*ODG_GRID_SIZE+(ODG_GRID_SIZE/2u);
        uint32_t outside=home+1u,outside2=home+2u;
        uint8_t owner=ODG_OWNER_FROM_ID(0u);
        CHECK(g_odg.playable[home]!=0u && g_odg.playable[outside]!=0u && g_odg.playable[outside2]!=0u);
        if(g_odg.playable[home]!=0u && g_odg.playable[outside]!=0u && g_odg.playable[outside2]!=0u){
            uint32_t actor_id;
            for(actor_id=1u;actor_id<ODG_MAX_ACTORS;++actor_id){
                g_odg.actors[actor_id].active=0u;
                g_odg.actors[actor_id].hp=0u;
            }
            set_test_territory_owner(home,owner);
            set_test_territory_owner(outside,ODG_OWNER_NONE);
            set_test_territory_owner(outside2,ODG_OWNER_NONE);
            p->x=odg_cell_center_x(home);p->z=odg_cell_center_z(home);
            odg_local_fx_to_global_fx_internal(p->x,p->z,&p->global_fx_x,&p->global_fx_z);
            p->last_cell=home;
            p->last_global_cell_x=g_odg.world_origin_cell_x+(int64_t)(home&(ODG_GRID_SIZE-1u));
            p->last_global_cell_z=g_odg.world_origin_cell_z+(int64_t)(home>>ODG_GRID_SHIFT);
            p->vx=0;p->vz=0;p->trail_active=0u;p->trail_len=0u;p->trail_broken=1u;

            p->x=odg_cell_center_x(outside);p->z=odg_cell_center_z(outside);p->last_cell=home;
            odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
            CHECK(p->trail_broken==1u && p->trail_active==0u && p->trail_len==0u);

            p->x=odg_cell_center_x(home);p->z=odg_cell_center_z(home);p->last_cell=outside;
            odg_step_ticks(1u);
            CHECK(p->trail_broken==0u && p->trail_active==0u && p->trail_len==0u);

            p->x=odg_cell_center_x(outside);p->z=odg_cell_center_z(outside);p->last_cell=home;
            odg_step_ticks(1u);
            CHECK(p->trail_broken==0u && p->trail_active==1u);
            CHECK(p->trail_head_global_cell_x!=INT64_MIN && p->trail_head_global_cell_z!=INT64_MIN);

            p->x=odg_cell_center_x(outside2);p->z=odg_cell_center_z(outside2);p->last_cell=outside;
            odg_step_ticks(1u);
            CHECK(p->trail_broken==0u && p->trail_active==1u && p->trail_len>0u);
        }
    }

    CHECK(odg_init(UINT64_C(0x545241494c505232), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0],*b1=&g_odg.actors[1];
        uint32_t pc=p->last_cell,pt=find_adjacent_playable(pc);
        CHECK(pt!=UINT32_MAX);
        if(pt!=UINT32_MAX){
            uint8_t po=ODG_OWNER_FROM_ID(0u),bo=ODG_OWNER_FROM_ID(1u);
            set_test_territory_owner(pt,po);set_test_trail_owner(pt,bo);b1->trail_active=1u;b1->trail_len=1u;
            p->x=odg_cell_center_x(pt);p->z=odg_cell_center_z(pt);p->last_cell=pc;odg_step_ticks(1u);
            CHECK(b1->hp==b1->max_hp);CHECK(b1->death_reason!=ODG_DEATH_TRAIL_CUT);
            CHECK(b1->trail_active==0u && b1->trail_len==0u && b1->trail_broken==1u);
        }
    }

    /* Same-cell contact regression: a cube already occupying a cell must still cut
     * a newly exposed enemy trail drawn underneath it. This guards the contact resolver
     * in addition to normal cell-transition checks. */
    CHECK(odg_init(UINT64_C(0x53414d4543454c4c), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0],*b1=&g_odg.actors[1];
        uint32_t cell=b1->last_cell;
        CHECK(cell<ODG_CELL_COUNT && g_odg.playable[cell]!=0u);
        p->trail_active=1u;p->trail_len=ODG_TURRET_TRAIL_MIN_CELLS;
        set_test_trail_owner(cell,ODG_OWNER_FROM_ID(0u));
        b1->x=odg_cell_center_x(cell);b1->z=odg_cell_center_z(cell);b1->last_cell=cell;
        odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(p->hp==p->max_hp);CHECK(p->death_reason!=ODG_DEATH_TRAIL_CUT);
        CHECK(p->trail_active==0u && p->trail_len==0u && p->trail_broken==1u);
    }

    /* Disconnected territory is intentionally persistent. Losing a bridge cell does not
     * garbage-collect the owner's islands: the original owner may later reconnect them. */
    CHECK(odg_init(UINT64_C(0x49534c414e445631), 320u, 180u) == ODG_STATUS_OK);
    {
        uint32_t scan_cell=0u,left=UINT32_MAX,mid=UINT32_MAX,right=UINT32_MAX,j;
        int64_t left_gx=0,left_gz=0,mid_gx=0,mid_gz=0,right_gx=0,right_gz=0;
        uint8_t po=ODG_OWNER_FROM_ID(0u),bo=ODG_OWNER_FROM_ID(1u);
        for(scan_cell=1u;scan_cell+1u<ODG_CELL_COUNT;++scan_cell){
            if((scan_cell&(ODG_GRID_SIZE-1u))==0u || (scan_cell&(ODG_GRID_SIZE-1u))==ODG_GRID_SIZE-1u) continue;
            if(g_odg.playable[scan_cell-1u] && g_odg.playable[scan_cell] && g_odg.playable[scan_cell+1u]){left=scan_cell-1u;mid=scan_cell;right=scan_cell+1u;break;}
        }
        CHECK(left!=UINT32_MAX && mid!=UINT32_MAX && right!=UINT32_MAX);
        if(left!=UINT32_MAX){
            /* Ownership authority is the global chunk ledger. Mutating only the resident
             * 128x128 cache would be overwritten on the next tick/recenter. */
            clear_all_test_territory();
            set_test_territory_owner(left,po);set_test_territory_owner(mid,bo);set_test_territory_owner(right,po);
            left_gx=g_odg.world_origin_cell_x+(int64_t)(left&(ODG_GRID_SIZE-1u));
            left_gz=g_odg.world_origin_cell_z+(int64_t)(left>>ODG_GRID_SHIFT);
            mid_gx=g_odg.world_origin_cell_x+(int64_t)(mid&(ODG_GRID_SIZE-1u));
            mid_gz=g_odg.world_origin_cell_z+(int64_t)(mid>>ODG_GRID_SHIFT);
            right_gx=g_odg.world_origin_cell_x+(int64_t)(right&(ODG_GRID_SIZE-1u));
            right_gz=g_odg.world_origin_cell_z+(int64_t)(right>>ODG_GRID_SHIFT);
            for(j=1u;j<ODG_MAX_ACTORS;++j) g_odg.actors[j].active=0u;
            odg_set_input(0,0,0,0,0u);odg_step_ticks(4u);
            /* A floating-origin recenter may move these cells to different local indices;
             * persistence is defined by their global world coordinates. */
            CHECK(odg_chunk_owner_at_global_cell(left_gx,left_gz)==po);
            CHECK(odg_chunk_owner_at_global_cell(right_gx,right_gz)==po);
            CHECK(odg_chunk_owner_at_global_cell(mid_gx,mid_gz)==bo);
            CHECK(g_odg.territory_count[0]==2u);
        }
    }

    /* v14 neutral infrastructure belongs to the first living actor that controls a strict
     * majority of its local playable 5x5 neighborhood. A minority is insufficient and,
     * once commissioned, later ground painting cannot silently flip the programming. */
    CHECK(odg_init(UINT64_C(0x434c41494d), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_turret *t=ensure_test_turret();CHECK(t!=NULL);if(t==NULL)return 1;
        uint32_t tc=odg_cell_from_world(t->x,t->z);
        uint32_t local[25],local_count=0u,j;
        int32_t cx=(int32_t)(tc & (ODG_GRID_SIZE-1u)),cz=(int32_t)(tc >> ODG_GRID_SHIFT),dz;
        uint8_t own=ODG_OWNER_FROM_ID(0u),other=ODG_OWNER_FROM_ID(1u);
        t->owner=ODG_TURRET_NEUTRAL;t->carried_by=ODG_TURRET_NONE;
        for(dz=-2;dz<=2;++dz){int32_t dx;for(dx=-2;dx<=2;++dx){int32_t x=cx+dx,z=cz+dz;if(x>=0&&z>=0&&x<(int32_t)ODG_GRID_SIZE&&z<(int32_t)ODG_GRID_SIZE){uint32_t cc=(uint32_t)z*ODG_GRID_SIZE+(uint32_t)x;if(g_odg.playable[cc]){local[local_count++]=cc;set_test_territory_owner(cc,ODG_OWNER_NONE);}}}}
        CHECK(local_count>0u);
        for(j=0u;j<local_count/2u;++j)set_test_territory_owner(local[j],own);
        odg_update_turret_ownership_internal();CHECK(t->owner==ODG_TURRET_NEUTRAL);
        set_test_territory_owner(local[local_count/2u],own);
        odg_update_turret_ownership_internal();CHECK(t->owner==own);
        for(j=0u;j<local_count;++j)set_test_territory_owner(local[j],other);
        odg_update_turret_ownership_internal();CHECK(t->owner==own);
        CHECK(counts_are_consistent());
    }

    /* API 15 neutral infrastructure: a neutral turret is commissioned only by territorial
     * control. A reprogram chip is not consumed merely because the actor is nearby. */
    CHECK(odg_init(UINT64_C(0x4e45555452414c43), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0]; odg_turret *t=ensure_test_turret();CHECK(t!=NULL);if(t==NULL)return 1;
        odg_item_stack chip; odg_interaction_hint hint; uint64_t required=0u;
        odg_memset(&chip,0,sizeof(chip)); chip.type_id=ODG_ITEM_REPROGRAM_CHIP; chip.quantity=1u; chip.material_tier=t->material_tier;
        CHECK(odg_inventory_add(&p->inventory,&chip)!=0);
        p->x=t->x;p->z=t->z;t->owner=ODG_TURRET_NEUTRAL;t->carried_by=ODG_TURRET_NONE;
        odg_rebuild_interaction_hint();
        CHECK(odg_copy_interaction_hint(&hint,sizeof(hint),&required)==ODG_STATUS_OK);
        CHECK(hint.action!=ODG_INTERACTION_REPROGRAM);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_REPROGRAM_CHIP,t->material_tier)==1u);
    }

    /* Enemy-programmed turrets ignore ground painting. The universal TAP interaction
     * consumes exactly one compatible chip and changes only that turret's allegiance. */
    CHECK(odg_init(UINT64_C(0x434849504841434b), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];odg_turret *t=ensure_test_turret();CHECK(t!=NULL);if(t==NULL)return 1;
        odg_item_stack chip;odg_interaction_hint hint;uint64_t required=0u;
        uint8_t own=ODG_OWNER_FROM_ID(0u),enemy=ODG_OWNER_FROM_ID(1u);
        uint32_t tc=odg_cell_from_world(t->x,t->z);int32_t cx=(int32_t)(tc&(ODG_GRID_SIZE-1u)),cz=(int32_t)(tc>>ODG_GRID_SHIFT),dz;
        t->owner=enemy;t->carried_by=ODG_TURRET_NONE;odg_apply_turret_tier(t,ODG_MATERIAL_STONE,0);
        for(dz=-2;dz<=2;++dz){int32_t dx;for(dx=-2;dx<=2;++dx){int32_t x=cx+dx,z=cz+dz;if(x>=0&&z>=0&&x<(int32_t)ODG_GRID_SIZE&&z<(int32_t)ODG_GRID_SIZE){uint32_t cc=(uint32_t)z*ODG_GRID_SIZE+(uint32_t)x;if(g_odg.playable[cc])set_test_territory_owner(cc,own);}}}
        odg_update_turret_ownership_internal();CHECK(t->owner==enemy);
        odg_memset(&chip,0,sizeof(chip));chip.type_id=ODG_ITEM_REPROGRAM_CHIP;chip.quantity=1u;chip.material_tier=ODG_MATERIAL_STONE;
        CHECK(odg_inventory_add(&p->inventory,&chip)!=0);
        p->inventory.selected_slot=0u;p->x=t->x;p->z=t->z;odg_rebuild_interaction_hint();
        CHECK(odg_copy_interaction_hint(&hint,sizeof(hint),&required)==ODG_STATUS_OK);
        CHECK(hint.action==ODG_INTERACTION_REPROGRAM && hint.valid!=0u);
        odg_set_input(0,0,0,0,ODG_BUTTON_INTERACT);odg_step_ticks(1u);
        odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(t->owner==own);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_STONE)==0u);
        CHECK(t->target_kind==ODG_TURRET_TARGET_NONE && t->aim_ticks==0u);
    }

    /* Placement uses the selected inventory slot and C validation. Hostile territory is
     * rejected; valid own-domain placement consumes the inventory item transactionally. */
    CHECK(odg_init(UINT64_C(0x444f4d41494e5452), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];odg_item_stack item;odg_interaction_hint hint;uint64_t required=0u;
        uint32_t foreign=0u;
        odg_memset(&item,0,sizeof(item));item.type_id=ODG_ITEM_TURRET;item.quantity=1u;item.material_tier=ODG_MATERIAL_WOOD;item.instance_id=odg_next_instance_id();
        CHECK(odg_inventory_add(&p->inventory,&item)!=0);p->inventory.selected_slot=0u;
        while(foreign<ODG_CELL_COUNT && (!g_odg.playable[foreign] || g_odg.territory[foreign]!=ODG_OWNER_NONE ||
              odg_dist2(p->x,p->z,odg_cell_center_x(foreign),odg_cell_center_z(foreign))<(int64_t)(12*ODG_FX_ONE)*(12*ODG_FX_ONE))) ++foreign;
        CHECK(foreign<ODG_CELL_COUNT);
        if(foreign<ODG_CELL_COUNT){set_test_territory_owner(foreign,ODG_OWNER_FROM_ID(1u));p->x=odg_cell_center_x(foreign);p->z=odg_cell_center_z(foreign);}
        odg_rebuild_interaction_hint();CHECK(odg_copy_interaction_hint(&hint,sizeof(hint),&required)==ODG_STATUS_OK);
        CHECK(hint.action==ODG_INTERACTION_PLACE && hint.valid==0u);
        p->x=odg_cell_center_x(p->home_cell);p->z=odg_cell_center_z(p->home_cell);odg_rebuild_interaction_hint();
        CHECK(odg_copy_interaction_hint(&hint,sizeof(hint),&required)==ODG_STATUS_OK);
        CHECK(hint.action==ODG_INTERACTION_PLACE && hint.valid!=0u);
        odg_set_input(0,0,0,0,ODG_BUTTON_INTERACT);odg_step_ticks(1u);odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_TURRET,ODG_MATERIAL_WOOD)==0u);
    }

    /* HOLD picks up an owned turret as a stateful ItemInstance rather than using the old
     * player_carried_turret field. Manual turrets preserve identity; a natural procedural
     * turret deliberately transfers into the low-bit portable namespace on pickup. In both
     * cases re-placement must preserve the portable identity, tier, and ammunition. */
    CHECK(odg_init(UINT64_C(0x545552524554), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_turret *t=ensure_test_turret();odg_actor *p=&g_odg.actors[0];uint32_t k;CHECK(t!=NULL);if(t==NULL)return 1;
        uint64_t original_instance,portable_instance=0u;uint32_t ammo,was_procedural;
        t->owner=ODG_OWNER_FROM_ID(0u);t->x=p->x;t->z=p->z;t->carried_by=ODG_TURRET_NONE;odg_apply_turret_tier(t,ODG_MATERIAL_IRON,0);t->ammo=17u;
        odg_local_fx_to_global_fx_internal(t->x,t->z,&t->global_fx_x,&t->global_fx_z);t->local_resident=1u;
        set_test_territory_owner(odg_cell_from_world(t->x,t->z),ODG_OWNER_FROM_ID(0u));original_instance=t->instance_id;ammo=t->ammo;was_procedural=t->procedural;
        odg_set_input(0,0,0,0,ODG_BUTTON_INTERACT);
        for(k=0u;k<ODG_INTERACT_HOLD_TICKS+2u;++k)odg_step_ticks(1u);
        odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(t->active==0u);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_TURRET,ODG_MATERIAL_IRON)==1u);
        {
            uint32_t slot=0u;
            CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_TURRET,ODG_MATERIAL_IRON,&slot)!=0);
            if(slot<odg_inventory_capacity(&p->inventory)){portable_instance=p->inventory.slots[slot].instance_id;p->inventory.selected_slot=slot;}
        }
        CHECK(portable_instance!=0u);CHECK((portable_instance&ODG_INSTANCE_ID_PROCEDURAL_BIT)==0u);
        CHECK(t->procedural==0u);CHECK(t->instance_id==portable_instance);
        if(was_procedural!=0u)CHECK(portable_instance!=original_instance);else CHECK(portable_instance==original_instance);
        odg_set_input(0,0,0,0,ODG_BUTTON_INTERACT);odg_step_ticks(1u);odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(t->active!=0u);CHECK(t->instance_id==portable_instance);CHECK(t->material_tier==ODG_MATERIAL_IRON);CHECK(t->ammo==ammo);
    }

    /* Generic ammo stacks refill an owned turret via TAP, and manual DROP creates a
     * world pickup protected by the centralized 90-tick anti-repickup window. */
    CHECK(odg_init(UINT64_C(0x535550504c59), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];odg_turret *t=ensure_test_turret();odg_item_stack ammo,wood;uint32_t i0,slot=0u;CHECK(t!=NULL);if(t==NULL)return 1;
        t->owner=ODG_OWNER_FROM_ID(0u);t->ammo=1u;t->carried_by=ODG_TURRET_NONE;odg_apply_turret_tier(t,ODG_MATERIAL_STONE,1);p->x=t->x;p->z=t->z;
        odg_memset(&ammo,0,sizeof(ammo));ammo.type_id=ODG_ITEM_AMMO;ammo.quantity=12u;ammo.material_tier=ODG_MATERIAL_IRON;CHECK(odg_inventory_add(&p->inventory,&ammo)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_AMMO,ODG_MATERIAL_NONE,&slot)!=0);p->inventory.selected_slot=slot;
        odg_set_input(0,0,0,0,ODG_BUTTON_INTERACT);odg_step_ticks(1u);odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(t->ammo>1u);CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_AMMO,ODG_MATERIAL_NONE)<12u);
        odg_memset(&wood,0,sizeof(wood));wood.type_id=ODG_ITEM_WOOD;wood.quantity=2u;wood.material_tier=ODG_MATERIAL_WOOD;CHECK(odg_inventory_add(&p->inventory,&wood)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,&slot)!=0);p->inventory.selected_slot=slot;i0=g_odg.pickup_count;
        {odg_command cmd;odg_memset(&cmd,0,sizeof(cmd));cmd.struct_size=sizeof(cmd);cmd.type=ODG_COMMAND_DROP_SELECTED;CHECK(odg_command_submit(&cmd,sizeof(cmd))==ODG_STATUS_OK);}odg_step_ticks(1u);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD)==1u);
        {uint32_t found=0u,j;for(j=0u;j<g_odg.pickup_count;++j)if(g_odg_pickups[j].active&&g_odg_pickups[j].stack.type_id==ODG_ITEM_WOOD&&g_odg_pickups[j].pickup_cd>=ODG_MANUAL_DROP_REPICKUP_TICKS-1u){found=1u;break;}CHECK(found!=0u);}
        CHECK(g_odg.pickup_count>=i0);
    }

    /* Backpack is equipment: it expands capacity to 12 without occupying a permanent
     * load slot once equipped. */
    CHECK(odg_init(UINT64_C(0x4241434b5041434b), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];odg_item_stack pack;odg_command cmd;
        odg_memset(&pack,0,sizeof(pack));pack.type_id=ODG_ITEM_BACKPACK;pack.quantity=1u;pack.material_tier=ODG_MATERIAL_WOOD;CHECK(odg_inventory_add(&p->inventory,&pack)!=0);
        odg_memset(&cmd,0,sizeof(cmd));cmd.struct_size=sizeof(cmd);cmd.type=ODG_COMMAND_EQUIP_BACKPACK;CHECK(odg_command_submit(&cmd,sizeof(cmd))==ODG_STATUS_OK);odg_step_ticks(1u);
        CHECK(odg_inventory_capacity(&p->inventory)==ODG_INVENTORY_MAX_SLOTS);CHECK(p->inventory.equipped_backpack_type==ODG_ITEM_BACKPACK);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_BACKPACK,ODG_MATERIAL_NONE)==0u);
    }

    /* Crafting a backpack while all four base slots are occupied by the natural
     * progression loadout is valid: the result goes directly to equipment.backpack,
     * then the transaction exposes the additional eight slots. */
    CHECK(odg_init(UINT64_C(0x4241434b43524146), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];uint32_t j;odg_artifact *bench=NULL;
        for(j=0u;j<ODG_INVENTORY_MAX_SLOTS;++j)odg_memset(&p->inventory.slots[j],0,sizeof(p->inventory.slots[j]));
        p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;p->inventory.equipped_backpack_type=ODG_ITEM_NONE;
        p->inventory.slots[0]=(odg_item_stack){ODG_ITEM_AXE,1u,ODG_MATERIAL_WOOD,240u,240u,ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_DURABILITY,UINT64_C(1),0u};
        p->inventory.slots[1]=(odg_item_stack){ODG_ITEM_PICKAXE,1u,ODG_MATERIAL_WOOD,260u,260u,ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_DURABILITY,UINT64_C(2),0u};
        p->inventory.slots[2]=(odg_item_stack){ODG_ITEM_WOOD,12u,ODG_MATERIAL_WOOD,0u,0u,ODG_ITEM_FLAG_RESOURCE,0u,0u};
        p->inventory.slots[3]=(odg_item_stack){ODG_ITEM_STONE,4u,ODG_MATERIAL_STONE,0u,0u,ODG_ITEM_FLAG_RESOURCE,0u,0u};
        for(j=0u;j<g_odg.artifact_count;++j)if(g_odg_artifacts[j].active&&g_odg_artifacts[j].owner_actor_id==0u&&g_odg_artifacts[j].item_type==ODG_ITEM_WORKBENCH){bench=&g_odg_artifacts[j];break;}
        CHECK(bench!=NULL);if(bench!=NULL){bench->x=p->x+ODG_FX_ONE;bench->z=p->z;}
        CHECK(odg_recipe_max_craftable(0u,ODG_RECIPE_BACKPACK)>=1u);
        CHECK(odg_craft(0u,ODG_RECIPE_BACKPACK,1u)==ODG_STATUS_OK);
        CHECK(p->inventory.equipped_backpack_type==ODG_ITEM_BACKPACK);
        CHECK(odg_inventory_capacity(&p->inventory)==ODG_INVENTORY_MAX_SLOTS);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_NONE)==0u);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_STONE,ODG_MATERIAL_NONE)==0u);
    }

    /* Chest storage is a 24-slot C-owned container. Transfers are transactional in both
     * directions: a full destination must never destroy the source stack. */
    CHECK(odg_init(UINT64_C(0x434845535453544f), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];odg_artifact *ch=&g_odg_artifacts[0];odg_item_stack wood;
        odg_storage_snapshot storage;uint64_t required=0u;uint32_t wood_slot=0u;
        ch->active=1u;ch->item_type=ODG_ITEM_CHEST;ch->owner_actor_id=0u;ch->capability_bits=ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_STORE;ch->x=p->x;ch->z=p->z;
        odg_memset(&ch->storage,0,sizeof(ch->storage));
        odg_memset(&wood,0,sizeof(wood));wood.type_id=ODG_ITEM_WOOD;wood.quantity=9u;wood.material_tier=ODG_MATERIAL_WOOD;wood.flags=ODG_ITEM_FLAG_RESOURCE;
        CHECK(odg_inventory_add(&p->inventory,&wood)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD,&wood_slot)!=0);
        CHECK(odg_artifact_storage_deposit(0u,0u,wood_slot,6u)==ODG_STATUS_OK);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD)==3u);
        CHECK(odg_copy_artifact_storage(0u,0u,&storage,sizeof(storage),&required)==ODG_STATUS_OK);
        CHECK(storage.slot_count==ODG_CHEST_SLOTS && storage.used_slots==1u && storage.slots[0].quantity==6u);
        CHECK(odg_artifact_storage_withdraw(0u,0u,0u,4u)==ODG_STATUS_OK);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_WOOD,ODG_MATERIAL_WOOD)==7u);
        CHECK(odg_copy_artifact_storage(0u,0u,&storage,sizeof(storage),&required)==ODG_STATUS_OK);
        CHECK(storage.slots[0].quantity==2u);
    }

    /* Repair is station-gated, proportional to actual damage and never free. Stone tools
     * use the workbench; iron uses the smithy. */
    CHECK(odg_init(UINT64_C(0x5245504149525631), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];odg_artifact *bench=&g_odg_artifacts[0];odg_item_stack axe,stone;
        odg_repair_quote quote;uint64_t required=0u;uint32_t axe_slot=0u;
        bench->active=1u;bench->item_type=ODG_ITEM_WORKBENCH;bench->owner_actor_id=0u;bench->x=p->x;bench->z=p->z;
        odg_memset(&axe,0,sizeof(axe));axe.type_id=ODG_ITEM_AXE;axe.quantity=1u;axe.material_tier=ODG_MATERIAL_STONE;axe.flags=ODG_ITEM_FLAG_TOOL|ODG_ITEM_FLAG_DURABILITY;axe.max_durability=odg_item_max_durability_internal(ODG_ITEM_AXE,ODG_MATERIAL_STONE);axe.durability=axe.max_durability/2u;axe.instance_id=odg_next_instance_id();
        odg_memset(&stone,0,sizeof(stone));stone.type_id=ODG_ITEM_STONE;stone.quantity=10u;stone.material_tier=ODG_MATERIAL_STONE;stone.flags=ODG_ITEM_FLAG_RESOURCE;
        CHECK(odg_inventory_add(&p->inventory,&axe)!=0);CHECK(odg_inventory_add(&p->inventory,&stone)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_AXE,ODG_MATERIAL_STONE,&axe_slot)!=0);p->inventory.selected_slot=axe_slot;
        CHECK(odg_repair_quote_selected(0u,&quote,sizeof(quote),&required)==ODG_STATUS_OK);
        CHECK(quote.cost_quantity>0u && quote.cost_item_type==ODG_ITEM_STONE && quote.station_item_type==ODG_STATION_WORKBENCH);
        {uint32_t before=odg_inventory_total(&p->inventory,ODG_ITEM_STONE,ODG_MATERIAL_STONE);CHECK(odg_repair_selected(0u)==ODG_STATUS_OK);CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_STONE,ODG_MATERIAL_STONE)==before-quote.cost_quantity);}
        CHECK(p->inventory.slots[axe_slot].durability==p->inventory.slots[axe_slot].max_durability);
    }

    /* Presentation and rebuildable caches are not gameplay authority. Particle emission
     * must not advance the gameplay RNG; hint/nav cache changes must not alter the hash. */
    CHECK(odg_init(UINT64_C(0x41555448484f4c45), 320u, 180u) == ODG_STATUS_OK);
    {
        uint64_t hash_before=odg_state_hash();
        odm_rng rng_before=g_odg.rng;
        odg_interaction_hint hint_before=g_odg.interaction_hint;
        uint8_t nav_before=g_odg.bot_nav_edges[0];
        odg_emit_particles(g_odg.actors[0].x,g_odg.actors[0].z,UINT32_C(0xabcdef12),7u);
        CHECK(g_odg.rng.state==rng_before.state&&g_odg.rng.stream==rng_before.stream&&g_odg.rng.cookie==rng_before.cookie);
        CHECK(odg_state_hash()==hash_before);
        g_odg.interaction_hint.action^=UINT32_C(1);CHECK(odg_state_hash()==hash_before);g_odg.interaction_hint=hint_before;
        g_odg.bot_nav_edges[0]^=UINT8_C(0x0f);CHECK(odg_state_hash()==hash_before);g_odg.bot_nav_edges[0]=nav_before;
    }

    /* Save/load is an engine blob, not filesystem I/O. It preserves the exact logical
     * hash, rejects checksum damage, and leaves framebuffer dimensions under host control. */
    CHECK(odg_init(UINT64_C(0x534156454c4f4144), 320u, 180u) == ODG_STATUS_OK);
    {
        uint64_t required=0u,before_hash,after_hash;uint32_t before_w=odg_render_width(),before_h=odg_render_height();
        odg_set_input(17000,24000,0,0,0u);odg_step_ticks(240u);before_hash=odg_state_hash();
        CHECK(odg_save_schema_version()==ODG_SAVE_SCHEMA_VERSION);
        CHECK(odg_save_blob_size()<=sizeof(save_buffer));
        CHECK(odg_save_write(NULL,0u,&required)==ODG_STATUS_BUFFER_TOO_SMALL);
        CHECK(required==odg_save_blob_size());
        CHECK(odg_save_write(save_buffer,sizeof(save_buffer),&required)==ODG_STATUS_OK);
        odg_set_input(-23000,8000,0,0,0u);odg_step_ticks(90u);CHECK(odg_state_hash()!=before_hash);
        CHECK(odg_save_load(save_buffer,required)==ODG_STATUS_OK);after_hash=odg_state_hash();
        CHECK(after_hash==before_hash);CHECK(odg_render_width()==before_w&&odg_render_height()==before_h);
        CHECK(odg_save_schema_supported(ODG_SAVE_SCHEMA_VERSION)!=0u);
        CHECK(odg_save_schema_supported(14u)!=0u);CHECK(odg_save_schema_supported(13u)==0u);
        /* Header API/ABI are provenance, not world compatibility. Changing them must not
         * strand a world whose data schema is supported. Payload/checksum are untouched. */
        save_buffer[12]^=UINT8_C(0x5a);save_buffer[16]^=UINT8_C(0xa5);
        CHECK(odg_save_load(save_buffer,required)==ODG_STATUS_OK);
        save_buffer[12]^=UINT8_C(0x5a);save_buffer[16]^=UINT8_C(0xa5);
        save_buffer[required-1u]^=UINT8_C(0x5a);CHECK(odg_save_load(save_buffer,required)==ODG_STATUS_INVALID_ARGUMENT);save_buffer[required-1u]^=UINT8_C(0x5a);
    }

    /* Persistent dynamic entities own a 64-bit global fixed-point position. Floating-origin
     * recenter only rebuilds their local cache; it must never move the logical item. */
    CHECK(odg_init(UINT64_C(0x474c4f42454e5437), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_item_stack wood;odg_world_pickup *pickup;int64_t before_x,before_z;uint64_t required=0u;
        odg_actor *p=&g_odg.actors[0];
        odg_memset(&wood,0,sizeof(wood));wood.type_id=ODG_ITEM_WOOD;wood.quantity=3u;wood.material_tier=ODG_MATERIAL_WOOD;wood.flags=ODG_ITEM_FLAG_RESOURCE;
        CHECK(odg_spawn_world_pickup(&wood,p->x+2*ODG_FX_ONE,p->z,ODG_MANUAL_DROP_REPICKUP_TICKS)!=0);
        pickup=&g_odg_pickups[g_odg.pickup_count-1u];CHECK(pickup->local_resident!=0u);before_x=pickup->global_fx_x;before_z=pickup->global_fx_z;
        p->x=ODG_FLOATING_ORIGIN_TRIGGER_FX+ODG_FX_ONE;odg_chunks_maybe_recenter();
        CHECK(pickup->active!=0u&&pickup->global_fx_x==before_x&&pickup->global_fx_z==before_z);
        CHECK(pickup->local_resident!=0u);
        CHECK(odg_save_blob_size()<=sizeof(save_buffer));CHECK(odg_save_write(save_buffer,sizeof(save_buffer),&required)==ODG_STATUS_OK);
        pickup->global_fx_x+=INT64_C(123456)*ODG_FX_ONE;odg_entities_refresh_local_cache();
        CHECK(odg_save_load(save_buffer,required)==ODG_STATUS_OK);pickup=&g_odg_pickups[g_odg.pickup_count-1u];
        CHECK(pickup->global_fx_x==before_x&&pickup->global_fx_z==before_z);
        pickup->global_fx_x+=INT64_C(3000000)*ODG_FX_ONE;odg_entities_refresh_local_cache();
        CHECK(pickup->active!=0u&&pickup->local_resident==0u);
        g_odg.world_origin_cell_x=(pickup->global_fx_x/(int64_t)ODG_FX_ONE)-(int64_t)ODG_WORLD_HALF_CELLS;
        g_odg.world_origin_cell_z=(pickup->global_fx_z/(int64_t)ODG_FX_ONE)-(int64_t)ODG_WORLD_HALF_CELLS;
        odg_entities_refresh_local_cache();CHECK(pickup->local_resident!=0u);
    }

    /* Map data is queried only for the requested viewport/resolution. Markers remain
     * semantic C data and out-of-world samples do not clamp onto the border. */
    CHECK(odg_init(UINT64_C(0x4d41505649455731), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_map_query_desc q;uint64_t samples_required=0u;uint32_t marker_count=0u,j,found_player=0u;
        odg_actor *p=&g_odg.actors[0];int32_t px=(int32_t)(((int64_t)p->x*1000)/ODG_FX_ONE),pz=(int32_t)(((int64_t)p->z*1000)/ODG_FX_ONE);
        odg_memset(&q,0,sizeof(q));q.struct_size=sizeof(q);q.min_x_milli=px-12000;q.max_x_milli=px+12000;q.min_z_milli=pz-12000;q.max_z_milli=pz+12000;q.width=48u;q.height=48u;
        CHECK(odg_map_query(&q,map_samples,64u*64u,&samples_required,map_markers,ODG_MAP_MAX_MARKERS,&marker_count)==ODG_STATUS_OK);
        CHECK(samples_required==48u*48u);CHECK(marker_count>0u);
        for(j=0u;j<marker_count;++j)if(map_markers[j].kind==ODG_MAP_MARKER_ACTOR&&map_markers[j].id==0u)found_player=1u;
        CHECK(found_player!=0u);
        q.min_x_milli=200000;q.max_x_milli=210000;q.min_z_milli=200000;q.max_z_milli=210000;q.width=4u;q.height=4u;
        CHECK(odg_map_query(&q,map_samples,64u*64u,&samples_required,map_markers,ODG_MAP_MAX_MARKERS,&marker_count)==ODG_STATUS_OK);
        for(j=0u;j<16u;++j)CHECK((map_samples[j].flags&ODG_MAP_FLAG_PLAYABLE)!=0u&&map_samples[j].owner_actor_plus_one==ODG_OWNER_NONE);
    }

    /* If no exposed trail exists, a turret spends one shot to transfer exactly one
     * enemy territory cell instead of damaging an actor. */
    CHECK(odg_init(UINT64_C(0x43454c4c53484f54), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_turret *t=ensure_test_turret();CHECK(t!=NULL);if(t==NULL)return 1;
        uint32_t tc=odg_cell_from_world(t->x,t->z);
        uint32_t target=find_adjacent_playable(tc);
        CHECK(target!=UINT32_MAX);
        if(target!=UINT32_MAX){
            uint8_t own=ODG_OWNER_FROM_ID(0u),enemy=ODG_OWNER_FROM_ID(1u);
            int64_t target_gx=g_odg.world_origin_cell_x+(int64_t)(target&(ODG_GRID_SIZE-1u));
            int64_t target_gz=g_odg.world_origin_cell_z+(int64_t)(target>>ODG_GRID_SHIFT);
            set_test_territory_owner(target,enemy);
            t->owner=own; t->fire_cd=0u; t->ammo=4u; t->range_fx=5*ODG_FX_ONE;
            t->target_kind=ODG_TURRET_TARGET_TERRITORY;t->target_actor_id=UINT32_MAX;
            t->target_global_cell_x=target_gx;t->target_global_cell_z=target_gz;t->last_target_cell=target;
            t->aim_ticks=3u;
            odg_set_input(0,0,0,0,0u); odg_step_ticks(1u);
            CHECK(odg_chunk_owner_at_global_cell(target_gx,target_gz)==enemy);
            CHECK(t->target_kind==ODG_TURRET_TARGET_TERRITORY);
            CHECK(t->ammo==4u);
            odg_step_ticks(4u);
            CHECK(odg_chunk_owner_at_global_cell(target_gx,target_gz)==own);
            CHECK(t->ammo==3u);
            CHECK(g_odg.actors[1].hp==g_odg.actors[1].max_hp);
        }
    }

    /* Coplanar territory must remain visible from all cardinal chase-camera yaws. */
    CHECK(odg_init(UINT64_C(0x535441424c455245), 320u, 180u) == ODG_STATUS_OK);
    {
        static const int32_t dirs[8][2]={{0,ODG_Q15_ONE},{23170,23170},{ODG_Q15_ONE,0},{23170,-23170},{0,-ODG_Q15_ONE},{-23170,-23170},{-ODG_Q15_ONE,0},{-23170,23170}};
        static uint8_t owned_frame[320u*180u*4u];
        uint32_t d;
        odg_actor *p=&g_odg.actors[0];
        odg_memset(g_odg.territory,0,sizeof(g_odg.territory));
        odg_memset(g_odg.territory_count,0,sizeof(g_odg.territory_count));
        for(d=0u;d<ODG_CELL_COUNT;++d){
            int32_t cx=(int32_t)(d&(ODG_GRID_SIZE-1u))-(int32_t)(ODG_GRID_SIZE/2u);
            int32_t cz=(int32_t)(d>>ODG_GRID_SHIFT)-(int32_t)(ODG_GRID_SIZE/2u);
            if(g_odg.playable[d] && cx>=-7&&cx<=7&&cz>=-7&&cz<=7){g_odg.territory[d]=ODG_OWNER_FROM_ID(0u);++g_odg.territory_count[0];}
        }
        p->x=0;p->z=0;g_odg.camera_anchor_x=0;g_odg.camera_anchor_z=0;
        for(d=0u;d<8u;++d){
            uint32_t off;
            p->face_x_q15=dirs[d][0];p->face_z_q15=dirs[d][1];
            g_odg.camera_dir_x_q15=dirs[d][0];g_odg.camera_dir_z_q15=dirs[d][1];
            for(off=0u;off<3u;++off){
                uintptr_t rp;
                uint32_t changed,byte_count,cell;
                static const int32_t offs[3][2]={{0,0},{2*ODG_FX_ONE,ODG_FX_ONE},{-2*ODG_FX_ONE,-ODG_FX_ONE}};
                p->x=offs[off][0];p->z=offs[off][1];
                g_odg.camera_anchor_x=p->x;g_odg.camera_anchor_z=p->z;
                rp=odg_render_frame();
                byte_count=odg_framebuffer_bytes();
                odg_memcpy(owned_frame,(const uint8_t*)rp,byte_count);
                for(cell=0u;cell<ODG_CELL_COUNT;++cell)
                    if(g_odg.territory[cell]==ODG_OWNER_FROM_ID(0u))g_odg.territory[cell]=ODG_OWNER_NONE;
                rp=odg_render_frame();
                changed=count_rgb_difference(owned_frame,(const uint8_t*)rp,byte_count);
                CHECK(changed>260u);
                for(cell=0u;cell<ODG_CELL_COUNT;++cell){
                    int32_t cx=(int32_t)(cell&(ODG_GRID_SIZE-1u))-(int32_t)(ODG_GRID_SIZE/2u);
                    int32_t cz=(int32_t)(cell>>ODG_GRID_SHIFT)-(int32_t)(ODG_GRID_SIZE/2u);
                    if(g_odg.playable[cell]&&cx>=-7&&cx<=7&&cz>=-7&&cz<=7)
                        g_odg.territory[cell]=ODG_OWNER_FROM_ID(0u);
                }
            }
        }
    }

    a = run_script(UINT64_C(0x123456789abcdef0));
    b = run_script(UINT64_C(0x123456789abcdef0));
    c = run_script(UINT64_C(0x123456789abcdef1));
    CHECK(a == b);
    CHECK(a != c);
    CHECK(counts_are_consistent());

    CHECK(odg_resize(480u, 270u) == ODG_STATUS_OK);
    fb = odg_render_frame();
    CHECK(fb != (uintptr_t)0);
    bytes = odg_framebuffer_bytes();
    CHECK(bytes == 480u * 270u * 4u);
    {
        const uint8_t *px = (const uint8_t *)fb;
        for (i = 0u; i < bytes; i += 257u) if (px[i] != 0u) ++nonzero;
    }
    CHECK(nonzero > 100u);
    CHECK(odg_player_territory_permille() <= 1000u);

    /* Ultra preview target is a real engine resolution, not browser upscaling. */
    CHECK(odg_resize(1280u,720u)==ODG_STATUS_OK);
    fb=odg_render_frame();
    CHECK(fb!=(uintptr_t)0);
    CHECK(odg_framebuffer_bytes()==1280u*720u*4u);
    CHECK(odg_resize(480u,270u)==ODG_STATUS_OK);

    /* Opening population remains stable: no artificial self-cross deaths. */
    CHECK(odg_init(1234u, 320u, 180u) == ODG_STATUS_OK);
    for (i = 0u; i < 3600u; ++i) { odg_set_input(0,0,0,0,0u); odg_step_ticks(1u); }
    CHECK(odg_alive_count() >= 8u);

    /* Planner/physics agreement regression. The return BFS now excludes physically
     * obstructed cells, and locomotion has contact hysteresis. Across a deterministic
     * six-thousand-tick sample no bot may fall into rapid full-direction reversals or
     * accumulate repeated no-progress watchdog windows. */
    CHECK(odg_init(UINT64_C(0x424f544348415454), 320u, 180u) == ODG_STATUS_OK);
    {
        int32_t pvx[ODG_MAX_ACTORS]={0},pvz[ODG_MAX_ACTORS]={0};
        uint32_t reversals[ODG_MAX_ACTORS]={0u};
        uint32_t t,j;
        for(t=0u;t<6000u;++t){
            odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
            for(j=1u;j<ODG_MAX_ACTORS;++j){
                odg_actor *bot=&g_odg.actors[j];
                if(!bot->active||bot->hp==0u) continue;
                if((pvx[j]!=0||pvz[j]!=0) && (bot->vx!=0||bot->vz!=0)){
                    int64_t dot=(int64_t)pvx[j]*bot->vx+(int64_t)pvz[j]*bot->vz;
                    if(dot<-400) ++reversals[j];
                }
                pvx[j]=bot->vx;pvz[j]=bot->vz;
            }
        }
        for(j=1u;j<ODG_MAX_ACTORS;++j){
            CHECK(reversals[j]<=4u);
            CHECK(g_odg.actors[j].stuck_windows<=2u);
        }
    }


    /* Chunk worldgen is visit-order independent, seam exact, and supports coordinates
     * far outside the v14 compatibility arena without precision loss. */
    CHECK(odg_init(UINT64_C(0x4348554e4b544553),320u,180u)==ODG_STATUS_OK);
    {
        odg_chunk_descriptor a0,a1,a2,b0,b1,b2;uint64_t req=0u;int32_t h0=0,h1=0;uint32_t k;
        CHECK(odg_chunk_descriptor_get(0,0,&a0,sizeof(a0),&req)==ODG_STATUS_OK);
        CHECK(odg_chunk_descriptor_get(1,-2,&a1,sizeof(a1),&req)==ODG_STATUS_OK);
        CHECK(odg_chunk_descriptor_get(-3,4,&a2,sizeof(a2),&req)==ODG_STATUS_OK);
        CHECK(odg_chunk_descriptor_get(-3,4,&b2,sizeof(b2),&req)==ODG_STATUS_OK);
        CHECK(odg_chunk_descriptor_get(0,0,&b0,sizeof(b0),&req)==ODG_STATUS_OK);
        CHECK(odg_chunk_descriptor_get(1,-2,&b1,sizeof(b1),&req)==ODG_STATUS_OK);
        CHECK(a0.stable_id==b0.stable_id && a1.stable_id==b1.stable_id && a2.stable_id==b2.stable_id);
        for(k=0u;k<4u;++k) CHECK(a0.corner_height_milli[k]==b0.corner_height_milli[k]);
        CHECK(odg_chunk_descriptor_get(1,0,&b1,sizeof(b1),&req)==ODG_STATUS_OK);
        CHECK(a0.corner_height_milli[1]==b1.corner_height_milli[0]);
        CHECK(a0.corner_height_milli[3]==b1.corner_height_milli[2]);
        CHECK(odg_world_height_milli64(INT64_C(1099511627776),-INT64_C(1099511627776),&h0)==ODG_STATUS_OK);
        CHECK(odg_world_height_milli64(INT64_C(1099511627776),-INT64_C(1099511627776),&h1)==ODG_STATUS_OK);
        CHECK(h0==h1 && h0>=-420 && h0<=6200);
    }

    /* Camera profile, music master and remote artifact cameras are presentation-only. */
    {
        uint64_t before=odg_state_hash();
        uint64_t remote_required=0u;
        static uint8_t remote_rgba[ODG_REMOTE_VIEW_WIDTH*ODG_REMOTE_VIEW_HEIGHT*4u];
        uint64_t pixel_sum=0u;
        uint32_t k;
        CHECK(odg_camera_mode()==ODG_CAMERA_MODE_MEDIUM);
        odg_set_camera_mode(ODG_CAMERA_MODE_FIRST_PERSON);
        CHECK(odg_camera_mode()==ODG_CAMERA_MODE_FIRST_PERSON);
        (void)odg_render_frame();CHECK(odg_state_hash()==before);
        odg_set_camera_mode(ODG_CAMERA_MODE_FAR);
        CHECK(odg_camera_mode()==ODG_CAMERA_MODE_FAR);
        CHECK(odg_state_hash()==before);
        odg_set_music_reactivity_q16(ODG_MUSIC_REACTIVITY_MAX_Q16+999u);
        CHECK(odg_music_reactivity_q16()==ODG_MUSIC_REACTIVITY_MAX_Q16);
        CHECK(odg_state_hash()==before);
        if (g_odg.turret_count>0u) {
            g_odg_turrets[0].owner=ODG_OWNER_FROM_ID(ODG_PLAYER_ID);
            g_odg_turrets[0].carried_by=ODG_TURRET_NONE;
            before=odg_state_hash();
            CHECK(odg_render_artifact_view(g_odg_turrets[0].id,NULL,0u,&remote_required)==ODG_STATUS_BUFFER_TOO_SMALL);
            CHECK(remote_required==(uint64_t)sizeof(remote_rgba));
            CHECK(odg_render_artifact_view(g_odg_turrets[0].id,remote_rgba,sizeof(remote_rgba),&remote_required)==ODG_STATUS_OK);
            for(k=0u;k<(uint32_t)sizeof(remote_rgba);k+=97u) pixel_sum+=(uint64_t)remote_rgba[k];
            CHECK(pixel_sum>0u);
            CHECK(odg_state_hash()==before);
            {
                int32_t saved_x=g_odg_turrets[0].x,saved_z=g_odg_turrets[0].z;
                int64_t saved_gx=g_odg_turrets[0].global_fx_x,saved_gz=g_odg_turrets[0].global_fx_z;
                uint32_t saved_resident=g_odg_turrets[0].local_resident;
                g_odg_turrets[0].global_fx_x=INT64_C(10000000)*ODG_FX_ONE+ODG_FX_ONE/2;
                g_odg_turrets[0].global_fx_z=-INT64_C(7000000)*ODG_FX_ONE+ODG_FX_ONE/2;
                g_odg_turrets[0].local_resident=0u;g_odg_turrets[0].x=0;g_odg_turrets[0].z=0;
                odg_entities_spatial_mark_dirty();before=odg_state_hash();pixel_sum=0u;
                CHECK(odg_render_artifact_view(g_odg_turrets[0].id,remote_rgba,sizeof(remote_rgba),&remote_required)==ODG_STATUS_OK);
                for(k=0u;k<(uint32_t)sizeof(remote_rgba);k+=97u)pixel_sum+=(uint64_t)remote_rgba[k];
                CHECK(pixel_sum>0u);CHECK(odg_state_hash()==before);
                g_odg_turrets[0].x=saved_x;g_odg_turrets[0].z=saved_z;g_odg_turrets[0].global_fx_x=saved_gx;g_odg_turrets[0].global_fx_z=saved_gz;
                g_odg_turrets[0].local_resident=saved_resident;odg_entities_spatial_mark_dirty();before=odg_state_hash();
            }
        }
        {
            static uint8_t avatar_preview[ODG_AVATAR_PREVIEW_SIZE*ODG_AVATAR_PREVIEW_SIZE*4u];
            uint64_t preview_required=0u;uint64_t preview_sum=0u;uint32_t pxi;
            CHECK(odg_render_avatar_preview(17341u,avatar_preview,sizeof(avatar_preview),&preview_required)==ODG_STATUS_OK);
            CHECK(preview_required==(uint64_t)sizeof(avatar_preview));
            for(pxi=0u;pxi<(uint32_t)sizeof(avatar_preview);pxi+=131u)preview_sum+=(uint64_t)avatar_preview[pxi];
            CHECK(preview_sum>0u);CHECK(odg_state_hash()==before);
        }
        {
            uint64_t camera_required=0u,camera_sum=0u;uint32_t ci;
            before=odg_state_hash();
            CHECK(odg_render_camera_preview(ODG_CAMERA_MODE_FAR,UINT32_C(13337),INT32_C(6200),
                                            remote_rgba,sizeof(remote_rgba),&camera_required)==ODG_STATUS_OK);
            CHECK(camera_required==(uint64_t)sizeof(remote_rgba));
            for(ci=0u;ci<(uint32_t)sizeof(remote_rgba);ci+=101u)camera_sum+=(uint64_t)remote_rgba[ci];
            CHECK(camera_sum>0u);CHECK(odg_state_hash()==before);
        }
        odg_set_camera_mode(ODG_CAMERA_MODE_MEDIUM);
        odg_set_music_reactivity_q16(65535u);
        CHECK(odg_state_hash()==before);
    }

    /* Avatar textures are presentation state: real RGBA face data changes presentation
     * availability but can never enter the deterministic gameplay hash. */
    {
        uint64_t before=odg_state_hash();uint32_t k;
        for(k=0u;k<(uint32_t)sizeof(avatar_texture_test);k+=4u){avatar_texture_test[k]=0x31u;avatar_texture_test[k+1u]=0xd8u;avatar_texture_test[k+2u]=0xb4u;avatar_texture_test[k+3u]=0xffu;}
        CHECK(odg_avatar_texture_upload(ODG_AVATAR_FACE_FRONT,avatar_texture_test,ODG_AVATAR_TEXTURE_SIZE,ODG_AVATAR_TEXTURE_SIZE,ODG_AVATAR_TEXTURE_SIZE*4u)==ODG_STATUS_OK);
        CHECK(odg_avatar_texture_present(ODG_AVATAR_FACE_FRONT)==1u);
        CHECK(odg_state_hash()==before);
        CHECK(odg_avatar_texture_upload(ODG_AVATAR_FACE_FRONT,avatar_texture_test,128u,256u,512u)==ODG_STATUS_INVALID_ARGUMENT);
        CHECK(odg_avatar_texture_clear(ODG_AVATAR_FACE_FRONT)==ODG_STATUS_OK);
        CHECK(odg_avatar_texture_present(ODG_AVATAR_FACE_FRONT)==0u);
        CHECK(odg_state_hash()==before);
    }

    /* Chunk summaries are acceleration/presentation caches rebuilt after load and during
     * streaming. Refreshing them must never rewrite the persistent chunk lifecycle or
     * otherwise change authoritative simulation state. */
    {
        uint64_t before=odg_state_hash();
        odg_chunks_refresh_summaries();
        CHECK(odg_state_hash()==before);
        odg_chunks_refresh_summaries();
        CHECK(odg_state_hash()==before);
    }

    /* Distant/local bots execute the same economy as the player. On this deterministic
     * seed at least one nation must progress from physical harvesting through backpack,
     * stone tooling, smithy and a persistent turret without synthetic resource grants. */
    CHECK(odg_init(UINT64_C(0xB07EC0A1),320u,180u)==ODG_STATUS_OK);
    for(i=0u;i<24000u;++i){odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);}
    {
        uint32_t bot,materials=0u,wood_picks=0u,industrialized=0u;
        for(bot=1u;bot<ODG_MAX_ACTORS;++bot){
            uint32_t ai,smithies=0u,turrets=0u;
            materials+=odg_inventory_total(&g_odg.actors[bot].inventory,ODG_ITEM_WOOD,ODG_MATERIAL_NONE);
            materials+=odg_inventory_total(&g_odg.actors[bot].inventory,ODG_ITEM_STONE,ODG_MATERIAL_NONE);
            materials+=odg_inventory_total(&g_odg.actors[bot].inventory,ODG_ITEM_IRON,ODG_MATERIAL_NONE);
            if(odg_inventory_find_type(&g_odg.actors[bot].inventory,ODG_ITEM_PICKAXE,ODG_MATERIAL_WOOD,NULL))++wood_picks;
            for(ai=0u;ai<g_odg.artifact_count;++ai)if(g_odg_artifacts[ai].active&&g_odg_artifacts[ai].owner_actor_id==bot&&g_odg_artifacts[ai].item_type==ODG_ITEM_SMITHY)++smithies;
            for(ai=0u;ai<g_odg.turret_count;++ai)if(g_odg_turrets[ai].active&&g_odg_turrets[ai].carried_by==ODG_TURRET_NONE&&g_odg_turrets[ai].owner==ODG_OWNER_FROM_ID(bot))++turrets;
            if(g_odg.actors[bot].inventory.equipped_backpack_type==ODG_ITEM_BACKPACK&&
               (odg_inventory_find_type(&g_odg.actors[bot].inventory,ODG_ITEM_PICKAXE,ODG_MATERIAL_STONE,NULL)||smithies>0u)&&
               smithies>0u&&turrets>0u)++industrialized;
        }
        CHECK(materials>0u);CHECK(wood_picks>0u);CHECK(industrialized>0u);
    }

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("OK api=%u deterministic=%016llx framebuffer=%u claimed=%u turrets=%u\n",
           (unsigned)ODG_API_VERSION,(unsigned long long)a,bytes,odg_territory_total_cells(),odg_turret_count());
    return 0;
}
