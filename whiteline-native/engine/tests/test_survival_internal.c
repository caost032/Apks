/* White-box invariant tests for the survival generator/geometry. This test is
 * deliberately compiled as its own translation unit so static generator/SAT
 * helpers can be verified without exposing test-only symbols in the public ABI. */
#include "../src/odwd_core.c"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do {                                                 \
    if (!(condition)) {                                                       \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures;                                                           \
    }                                                                         \
} while (0)

static void configure_envelope(survival_state *s) {
    memset(s, 0, sizeof(*s));
    s->envelope.half_length = VEHICLE_ENVELOPE_HALF_LENGTH;
    s->envelope.half_width = VEHICLE_ENVELOPE_HALF_WIDTH;
    s->envelope.half_height = 0.80;
    s->envelope.max_useful_speed = 38.0;
    s->envelope.acceleration = 8.5;
    s->envelope.braking = 10.5;
    s->envelope.lateral_capacity = 8.8;
    s->envelope.turn_radius = 5.2;
    s->envelope.jump_impulse = 9.6;
    s->envelope.jump_distance = 10.6;
    s->warning_time = 1.7;
    s->sector_duration = 6.8;
    s->obstacle_speed = 7.0;
    s->solution_count = 1u;
}

static int any_wall_contact(const survival_state *s,
                            const vehicle_internal *v) {
    uint32_t i;
    for (i = 0u; i < s->obstacle_count; ++i) {
        const survival_obstacle *o = &s->obstacles[i];
        if (o->type == ODWD_PROP_SURVIVAL_RAMP) continue;
        if (survival_obb_contact(v, o, NULL, NULL, NULL)) return 1;
    }
    return 0;
}

static vehicle_internal centered_car(double yaw) {
    vehicle_internal v;
    memset(&v, 0, sizeof(v));
    v.y = SURVIVAL_ARENA_Y + 0.58;
    v.yaw = yaw;
    return v;
}

static void test_vehicle_derived_gaps(void) {
    survival_state s;
    vehicle_internal v;
    double gap;

    configure_envelope(&s);
    s.difficulty = 1.0;
    gap = survival_gap_width(&s, 3u);
    CHECK(gap > s.envelope.half_width * 2.0);
    survival_add_wall_gap(&s, 0.0, 0.0, gap, 0.0, 0u);

    v = centered_car(0.0);
    CHECK(!any_wall_contact(&s, &v)); /* straight valid opening */
    v.yaw = 0.42;
    CHECK(any_wall_contact(&s, &v)); /* large orientation still spends margin */

    configure_envelope(&s);
    survival_add_wall_gap(&s, 0.0, 0.0,
                          s.envelope.half_width * 2.0 - 0.18,
                          0.0, 0u);
    v = centered_car(0.0);
    CHECK(any_wall_contact(&s, &v)); /* impossible visible width cannot pass */
}

static void test_contact_resolution_no_hidden_corner(void) {
    odwd_engine_internal e;
    vehicle_internal *v;
    double nx, nz, penetration;
    memset(&e, 0, sizeof(e));
    e.config.world_mode = ODWD_MODE_SURVIVAL;
    e.vehicle_count = 1u;
    configure_envelope(&e.survival);
    e.survival.sector_elapsed = 2.0;
    e.survival.warning_time = 0.0;
    survival_add_obstacle(&e.survival, ODWD_PROP_SURVIVAL_WALL,
                          2.20, 0.0, 1.0, 2.0, 0.72,
                          0.0, 0.0, 0.0, 0u);
    v = &e.vehicles[0];
    *v = centered_car(0.0);
    CHECK(survival_obb_contact(v, &e.survival.obstacles[0],
                               &nx, &nz, &penetration));
    CHECK(penetration > 0.0 && penetration < 0.5);
    survival_collide_obstacles(&e, v);
    CHECK(!survival_obb_contact(v, &e.survival.obstacles[0],
                                NULL, NULL, NULL));
}

static void test_jump_contract(void) {
    survival_state s;
    double apex_rise;
    double car_bottom_at_apex;
    double low_wall_top;
    double impossible_wall_top;
    configure_envelope(&s);
    apex_rise = s.envelope.jump_impulse * s.envelope.jump_impulse /
                (2.0 * 16.8);
    car_bottom_at_apex = 0.58 + apex_rise - 0.74;
    low_wall_top = 1.0;       /* generated SURVIVAL_LOW_WALL */
    impossible_wall_top = 10.0;
    CHECK(car_bottom_at_apex > low_wall_top + 1.0);
    CHECK(car_bottom_at_apex < impossible_wall_top);
    CHECK(s.envelope.jump_distance > s.envelope.half_length * 1.6);
}

static void configure_engine_for_generation(odwd_engine_internal *e,
                                            uint32_t seed) {
    uint32_t i;
    memset(e, 0, sizeof(*e));
    e->config.seed = seed;
    e->config.world_mode = ODWD_MODE_SURVIVAL;
    e->vehicle_count = 8u;
    for (i = 0u; i < e->vehicle_count; ++i) e->vehicles[i].is_player = i == 0u;
    survival_initialize(e);
}

static void test_generation_feasibility_history_and_determinism(void) {
    odwd_engine_internal a, b;
    double previous[3] = {0.0, 0.0, 0.0};
    uint32_t previous_count = 0u;
    uint32_t recent[6] = {UINT32_MAX, UINT32_MAX, UINT32_MAX,
                          UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t recent_count = 0u;
    uint32_t sector;
    configure_engine_for_generation(&a, UINT32_C(0x8f22c531));
    configure_engine_for_generation(&b, UINT32_C(0x8f22c531));
    for (sector = 0u; sector < 48u; ++sector) {
        uint32_t i;
        if (sector != 0u) {
            previous_count = a.survival.solution_count;
            if (previous_count > 3u) previous_count = 3u;
            for (i = 0u; i < previous_count; ++i)
                previous[i] = a.survival.solution_x[i];
            survival_generate_sector(&a, sector);
            survival_generate_sector(&b, sector);
        }
        CHECK(survival_sector_feasible(&a.survival));
        CHECK(survival_transition_feasible(&a.survival,
                                            previous, previous_count));
        CHECK(a.survival.family == b.survival.family);
        CHECK(a.survival.obstacle_count == b.survival.obstacle_count);
        CHECK(a.survival.platform_count == b.survival.platform_count);
        CHECK(state_hash_internal(&a) == state_hash_internal(&b));
        /* With 17 families and a six-sector memory, immediate/ABAB repetition
         * is not allowed unless a safety fallback was necessary. */
        if (recent_count > 0u && a.survival.family == recent[recent_count - 1u])
            CHECK(a.survival.family == SURVIVAL_WALL_GAP);
        if (recent_count > 1u && a.survival.family == recent[recent_count - 2u])
            CHECK(a.survival.family == SURVIVAL_WALL_GAP);
        if (recent_count < 6u) recent[recent_count++] = a.survival.family;
        else {
            for (i = 1u; i < 6u; ++i) recent[i - 1u] = recent[i];
            recent[5] = a.survival.family;
        }
    }
}

static void test_elimination_order_is_deterministic(void) {
    odwd_engine_internal e;
    memset(&e, 0, sizeof(e));
    e.config.world_mode = ODWD_MODE_SURVIVAL;
    e.vehicle_count = 3u;
    configure_envelope(&e.survival);
    e.survival.alive_count = 3u;
    e.vehicles[0].is_player = 1u;
    e.vehicles[0].y = SURVIVAL_ARENA_Y - 15.0; /* shallower failure */
    e.vehicles[1].y = SURVIVAL_ARENA_Y - 19.0; /* deeper => eliminated first */
    e.vehicles[2].y = SURVIVAL_ARENA_Y;
    survival_update_eliminations(&e);
    CHECK(e.survival.elimination_place[1] == 3u);
    CHECK(e.survival.elimination_place[0] == 2u);
    CHECK(e.vehicles[2].place == 1u);
    CHECK(e.survival.player_final_place == 2u);
    CHECK(e.survival.finished == 1u);
}

static void test_bot_and_player_share_obstacle_collision(void) {
    odwd_engine_internal player_case, bot_case;
    vehicle_internal initial;
    survival_obstacle obstacle;
    memset(&player_case, 0, sizeof(player_case));
    memset(&bot_case, 0, sizeof(bot_case));
    player_case.config.world_mode = bot_case.config.world_mode = ODWD_MODE_SURVIVAL;
    player_case.vehicle_count = bot_case.vehicle_count = 1u;
    configure_envelope(&player_case.survival);
    configure_envelope(&bot_case.survival);
    player_case.survival.warning_time = bot_case.survival.warning_time = 0.0;
    player_case.survival.sector_elapsed = bot_case.survival.sector_elapsed = 1.0;
    survival_add_obstacle(&player_case.survival, ODWD_PROP_SURVIVAL_WALL,
                          2.2, 0.0, 1.0, 2.0, 0.72, 0.0, 0.0, 0.0, 0u);
    bot_case.survival.obstacles[0] = player_case.survival.obstacles[0];
    bot_case.survival.obstacle_count = 1u;
    obstacle = player_case.survival.obstacles[0];
    initial = centered_car(0.0);
    initial.vx = 4.0;
    player_case.vehicles[0] = initial;
    bot_case.vehicles[0] = initial;
    player_case.vehicles[0].is_player = 1u;
    bot_case.vehicles[0].is_player = 0u;
    CHECK(survival_obb_contact(&initial, &obstacle, NULL, NULL, NULL));
    survival_collide_obstacles(&player_case, &player_case.vehicles[0]);
    survival_collide_obstacles(&bot_case, &bot_case.vehicles[0]);
    CHECK(double_bits(player_case.vehicles[0].x) == double_bits(bot_case.vehicles[0].x));
    CHECK(double_bits(player_case.vehicles[0].z) == double_bits(bot_case.vehicles[0].z));
    CHECK(double_bits(player_case.vehicles[0].vx) == double_bits(bot_case.vehicles[0].vx));
    CHECK(player_case.vehicles[0].collisions == bot_case.vehicles[0].collisions);
}

static void configure_music_engine(odwd_engine_internal *e, uint32_t seed) {
    uint32_t i;
    memset(e, 0, sizeof(*e));
    e->config.seed = seed;
    e->config.world_mode = ODWD_MODE_MUSIC_SURVIVAL;
    e->vehicle_count = 4u;
    for (i = 0u; i < e->vehicle_count; ++i) e->vehicles[i].is_player = i == 0u;
    music_survival_initialize(e);
}

static void test_bumper_arena_impact_contract(void) {
    odwd_engine_internal normal;
    odwd_engine_internal bumper;
    memset(&normal, 0, sizeof(normal));
    memset(&bumper, 0, sizeof(bumper));
    normal.config.world_mode = bumper.config.world_mode = ODWD_MODE_OPEN_FIELD;
    normal.vehicle_count = bumper.vehicle_count = 2u;
    normal.activity_zone = ODWD_ACTIVITY_EXPLORE;
    bumper.activity_zone = ODWD_ACTIVITY_BUMPER_ARENA;

    normal.vehicles[0].x = bumper.vehicles[0].x = OPEN_BUMPER_X - 1.2;
    normal.vehicles[0].z = bumper.vehicles[0].z = OPEN_BUMPER_Z;
    normal.vehicles[0].vx = bumper.vehicles[0].vx = 10.0;
    normal.vehicles[0].y = bumper.vehicles[0].y = 1.0;
    normal.vehicles[1].x = bumper.vehicles[1].x = OPEN_BUMPER_X + 1.2;
    normal.vehicles[1].z = bumper.vehicles[1].z = OPEN_BUMPER_Z;
    normal.vehicles[1].vx = bumper.vehicles[1].vx = 0.0;
    normal.vehicles[1].y = bumper.vehicles[1].y = 1.0;

    collide_vehicles(&normal);
    collide_vehicles(&bumper);
    CHECK(bumper.vehicles[0].last_collision_impulse >
          normal.vehicles[0].last_collision_impulse * 2.5);
    CHECK(bumper.vehicles[0].airborne == 1u);
    CHECK(bumper.vehicles[1].airborne == 1u);
    CHECK(bumper.vehicles[0].vy >= 3.0);
    CHECK(bumper.vehicles[1].vy > bumper.vehicles[0].vy);
    CHECK(normal.vehicles[0].airborne == 0u);
    CHECK(normal.vehicles[1].airborne == 0u);
}

static void test_music_flood_refuge_contract(void) {
    uint32_t seed;
    uint32_t levels_seen = 0u;
    uint32_t flood_events = 0u;
    for (seed = 1u; seed <= 96u; ++seed) {
        odwd_engine_internal e;
        uint32_t event;
        configure_music_engine(&e, seed * UINT32_C(2246822519));
        for (event = 0u; event < 24u; ++event) {
            uint32_t i;
            uint32_t survivable = 0u;
            music_generate_event(&e);
            if (e.music_survival.hazard_type != MUSIC_HAZARD_LAVA_FLOOD)
                continue;
            ++flood_events;
            levels_seen |= 1u << e.music_survival.hazard_level;
            CHECK(e.music_survival.safe_count == MUSIC_SAFE_MAX);
            CHECK(e.music_survival.phase_duration >= 3.55 - 1.0e-9);
            CHECK(e.music_survival.phase_duration <= 4.65 + 1.0e-9);
            for (i = 0u; i < e.music_survival.safe_count; ++i) {
                const music_safe_zone *safe = &e.music_survival.safe[i];
                double flood_h = 0.8 +
                    (double)e.music_survival.hazard_level * 2.05;
                double vehicle_support_h = safe->height + 0.50;
                CHECK(safe->shelter == 0u);
                CHECK(safe->height == 1.8 + (double)safe->level * 2.25);
                if (vehicle_support_h >= flood_h) {
                    double dx = safe->x - e.vehicles[0].x;
                    double dz = safe->z - e.vehicles[0].z;
                    double refuge_distance = dsqrt(dx * dx + dz * dz);
                    ++survivable;
                    CHECK(safe->level == e.music_survival.hazard_level);
                    CHECK(refuge_distance >= 26.0 - 0.05);
                    CHECK(refuge_distance <= 43.0 + 0.05);
                } else {
                    CHECK(safe->level < e.music_survival.hazard_level);
                }
            }
            /* No accidental second winning mountain: one event, one answer. */
            CHECK(survivable == 1u);
            e.music_survival.hazard_phase = MUSIC_EVENT_ACTIVE;
            props_refresh_music_survival(&e);
            {
                uint32_t p;
                int lava_plane_seen = 0;
                double expected_top = MUSIC_ARENA_Y + 0.8 +
                    (double)e.music_survival.hazard_level * 2.05;
                for (p = 0u; p < e.prop_count; ++p) {
                    const prop_internal *prop = &e.props[p];
                    if (prop->id != UINT32_C(0x93000000)) continue;
                    CHECK(prop->type == ODWD_PROP_MUSIC_LAVA);
                    CHECK(dabs((prop->y + prop->extent_y) - expected_top) < 1.0e-12);
                    lava_plane_seen = 1;
                    break;
                }
                CHECK(lava_plane_seen);
            }
        }
    }
    CHECK(flood_events >= 100u);
    CHECK((levels_seen & ((1u << 1u) | (1u << 2u) | (1u << 3u))) ==
          ((1u << 1u) | (1u << 2u) | (1u << 3u)));
}

static void test_music_arena_and_endless_variety_contract(void) {
    uint64_t signatures[96];
    uint32_t signature_count = 0u;
    uint32_t seed;
    uint32_t pattern_mask = 0u;
    uint32_t style_mask = 0u;
    odwd_engine_internal road;
    odwd_config config;

    for (seed = 1u; seed <= 96u; ++seed) {
        odwd_engine_internal a, twin;
        music_survival_state *m;
        uint64_t signature;
        uint32_t i;
        int unique = 1;
        configure_music_engine(&a, seed * UINT32_C(3266489917));
        configure_music_engine(&twin, seed * UINT32_C(3266489917));
        m = &a.music_survival;
        CHECK(double_bits(m->half_w) == double_bits(twin.music_survival.half_w));
        CHECK(double_bits(m->half_d) == double_bits(twin.music_survival.half_d));
        CHECK(m->map_variant == twin.music_survival.map_variant);
        CHECK(m->half_w >= 64.0 && m->half_w <= 82.1);
        CHECK(m->half_d >= 54.0 && m->half_d <= 70.1);
        signature = ((uint64_t)m->map_variant << 48u) ^
                    ((uint64_t)((m->half_w - 31.0) * 10000.0) << 24u) ^
                    (uint64_t)((m->half_d - 25.0) * 10000.0);
        for (i = 0u; i < signature_count; ++i)
            if (signatures[i] == signature) unique = 0;
        if (unique) signatures[signature_count++] = signature;
    }
    /* A new run seed must materially change arena shape/dimensions, while the
     * same seed stays deterministic for replay/debugging. */
    CHECK(signature_count >= 90u);

    memset(&road, 0, sizeof(road));
    odwd_config_defaults(&config);
    config.seed = UINT32_C(0x20a60819);
    config.world_mode = ODWD_MODE_ENDLESS;
    road.config = config;
    for (seed = 0u; seed < 240u; ++seed) {
        double progress = ((double)seed + 0.25) * road.config.section_length_m;
        int64_t node = (int64_t)(progress / ROAD_NODE_SPACING);
        pattern_mask |= 1u << road_action_pattern(&road, node);
        style_mask |= 1u << road_sector_style(&road, node);
    }
    CHECK((pattern_mask & UINT32_C(0x0fff)) == UINT32_C(0x0fff));
    CHECK((style_mask & UINT32_C(0x003f)) == UINT32_C(0x003f));
}

static void test_blockdash_bot_pacing_contract(void) {
    uint32_t seed;
    for (seed = 1u; seed <= 24u; ++seed) {
        odwd_engine_internal e;
        odwd_config config;
        odwd_input input;
        uint32_t tick;
        odwd_config_defaults(&config);
        config.seed = seed;
        config.rival_count = 7u;
        config.world_mode = ODWD_MODE_SURVIVAL;
        CHECK(odwd_engine_init(&e, sizeof(e), &config) == ODWD_OK);
        CHECK(e.survival.alive_count == 8u);
        for (tick = 0u; tick < 120u * 15u; ++tick) {
            odwd_input_neutral(&input);
            input.buttons |= ODWD_BUTTON_EXPLICIT_PEDALS;
            input.brake = 0.25;
            CHECK(odwd_engine_step(&e, &input, NULL) == ODWD_OK);
            CHECK(e.survival.alive_count >= 1u);
            if (e.survival.finished) break;
        }
        /* A fresh match may punish bad play, but its seven rivals must not all
         * suicide on the opening pattern. Fifteen seconds is a hard lower
         * pacing contract across a diverse deterministic seed set. */
        CHECK(!e.survival.finished);
        CHECK(e.survival.alive_count >= 2u);
    }
}

static double run_blockdash_auto_seed(uint32_t seed, double max_seconds) {
    odwd_engine_internal e;
    odwd_config config;
    odwd_input input;
    uint32_t tick;
    uint32_t max_ticks = (uint32_t)(max_seconds * (double)ODWD_TICK_HZ);

    memset(&e, 0, sizeof(e));
    odwd_config_defaults(&config);
    config.seed = seed;
    config.rival_count = 7u;
    config.world_mode = ODWD_MODE_SURVIVAL;
    CHECK(odwd_engine_init(&e, sizeof(e), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS | ODWD_BUTTON_AUTODRIVE;
    for (tick = 0u; tick < max_ticks; ++tick) {
        CHECK(odwd_engine_step(&e, &input, NULL) == ODWD_OK);
        if (e.survival.eliminated[0]) return (double)tick / (double)ODWD_TICK_HZ;
    }
    return max_seconds;
}

static void test_blockdash_player_auto_regressions(void) {
    static const uint32_t seeds[] = {29u, 31u, 48u};
    uint32_t i;
    /* These seeds were the early target-behind/U-turn failures recovered from
     * the post-v0.6.1 WIP. Keep player AUTO separate from bot pacing. */
    for (i = 0u; i < (uint32_t)(sizeof(seeds) / sizeof(seeds[0])); ++i)
        CHECK(run_blockdash_auto_seed(seeds[i], 45.0) >= 18.0);
}

static double run_music_auto_seed(uint32_t seed, double max_seconds) {
    odwd_engine_internal e;
    odwd_config config;
    odwd_input input;
    uint32_t tick;
    uint32_t max_ticks = (uint32_t)(max_seconds * (double)ODWD_TICK_HZ);

    memset(&e, 0, sizeof(e));
    odwd_config_defaults(&config);
    config.seed = seed;
    config.rival_count = 3u;
    config.world_mode = ODWD_MODE_MUSIC_SURVIVAL;
    CHECK(odwd_engine_init(&e, sizeof(e), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS | ODWD_BUTTON_AUTODRIVE;
    for (tick = 0u; tick < max_ticks; ++tick) {
        /* Deterministic energetic envelope: exercise hazards/AUTO without a
         * host decoder or audio device in this white-box test. */
        e.music_energy = 0.88;
        e.music_pulse = 0.72;
        e.music_bass = 0.80;
        e.music_mid = 0.65;
        e.music_high = 0.50;
        CHECK(odwd_engine_step(&e, &input, NULL) == ODWD_OK);
        if (e.music_survival.finished)
            return (double)tick / (double)ODWD_TICK_HZ;
    }
    return max_seconds;
}

static void test_music_auto_recovery_regression(void) {
    uint32_t i;
    uint32_t survivors = 0u;
    for (i = 1u; i <= 12u; ++i) {
        double survived = run_music_auto_seed(i * UINT32_C(2654435761), 45.0);
        if (survived >= 45.0) ++survivors;
        else CHECK(survived >= 20.0);
    }
    /* Recovered WIP contract: most energetic deterministic seeds must survive
     * the full window; an unlucky seed still may fail, but never immediately. */
    CHECK(survivors >= 10u);
}

static void test_music_holes_boundary_and_life_contract(void) {
    odwd_engine_internal e;
    odwd_config config;
    odwd_input input;
    music_survival_state *m;
    vehicle_internal *player;
    uint32_t tick;
    int saw_airborne = 0;

    memset(&e, 0, sizeof(e));
    odwd_config_defaults(&config);
    config.seed = UINT32_C(0x60a8cafe);
    config.rival_count = 3u;
    config.world_mode = ODWD_MODE_MUSIC_SURVIVAL;
    CHECK(odwd_engine_init(&e, sizeof(e), &config) == ODWD_OK);
    m = &e.music_survival;
    player = &e.vehicles[0];
    CHECK(m->half_w >= 64.0 && m->half_w <= 82.1);
    CHECK(m->half_d >= 54.0 && m->half_d <= 70.1);

    /* An ACTIVE hole is actual missing support. The player must fall through,
     * lose life at the fall threshold, then recover onto supported ground. */
    m->hazard_type = MUSIC_HAZARD_HOLES;
    m->hazard_phase = MUSIC_EVENT_ACTIVE;
    m->phase_elapsed = 0.0;
    m->phase_duration = 20.0;
    m->hazard_count = 1u;
    m->hazards[0].x = 0.0;
    m->hazards[0].z = 0.0;
    m->hazards[0].radius = 5.5;
    player->x = player->z = 0.0;
    player->y = MUSIC_ARENA_Y + 0.58;
    player->vx = player->vy = player->vz = 0.0;
    player->airborne = 0u;
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    for (tick = 0u; tick < 360u; ++tick) {
        CHECK(odwd_engine_step(&e, &input, NULL) == ODWD_OK);
        if (player->airborne) saw_airborne = 1;
        if (player->respawns > 0u) break;
    }
    CHECK(saw_airborne);
    CHECK(player->respawns > 0u);
    CHECK(m->health <= 0.8000001);
    CHECK(music_vehicle_supported_at(&e, player->x, player->z));
    CHECK(player->y >= MUSIC_ARENA_Y + 0.50);

    /* The outer contour is a closed wall for the player, not another void.
     * Find its exact +X edge using the same authoritative predicate, hit it at
     * speed and require every resulting center point to remain in-bounds. */
    m->hazard_phase = MUSIC_EVENT_COOLDOWN;
    m->phase_elapsed = 0.0;
    m->phase_duration = 100.0;
    m->hazard_count = 0u;
    {
        double lo = 0.0;
        double hi = m->half_w * 1.5;
        uint32_t k;
        for (k = 0u; k < 64u; ++k) {
            double mid = (lo + hi) * 0.5;
            if (music_map_contains(m, mid, 0.0)) lo = mid;
            else hi = mid;
        }
        player->x = lo - 1.4;
        player->z = 0.0;
        player->y = MUSIC_ARENA_Y + 0.58;
        player->yaw = HALF_PI;
        player->vx = 24.0;
        player->vy = player->vz = 0.0;
        player->airborne = 0u;
        player->respawns = 0u;
        input.throttle = 1.0;
        for (tick = 0u; tick < 180u; ++tick) {
            CHECK(odwd_engine_step(&e, &input, NULL) == ODWD_OK);
            CHECK(music_map_contains(m, player->x, player->z));
        }
        CHECK(player->respawns == 0u);
    }

    /* Health reaching zero is an authoritative game-over event. */
    e.event_flags = 0u;
    m->health = 0.05;
    m->finished = 0u;
    music_damage_player(&e, 0.20);
    CHECK(m->health == 0.0);
    CHECK(m->finished == 1u);
    CHECK((e.event_flags & ODWD_EVENT_MUSIC_FINISH) != 0u);
}

static void test_open_world_slope_pose_contract(void) {
    odwd_engine_internal e;
    odwd_config config;
    odwd_input input;
    vehicle_internal *player;
    double best_x = 0.0, best_z = 0.0, best_gradient = 0.0;
    int32_t gx, gz;
    uint32_t tick;

    /* Pick a steep deterministic piece of the procedural terrain instead of
     * hard-coding a particular mountain that may evolve in a future release. */
    for (gz = -18; gz <= 18; ++gz) {
        for (gx = -18; gx <= 18; ++gx) {
            double x = (double)gx * 24.0;
            double z = (double)gz * 24.0;
            double dx = open_ground_height(x + 2.4, z) -
                        open_ground_height(x - 2.4, z);
            double dz = open_ground_height(x, z + 2.4) -
                        open_ground_height(x, z - 2.4);
            double g = dsqrt(dx * dx + dz * dz);
            if (g > best_gradient) {
                best_gradient = g;
                best_x = x;
                best_z = z;
            }
        }
    }
    CHECK(best_gradient > 0.35);
    memset(&e, 0, sizeof(e));
    odwd_config_defaults(&config);
    config.seed = UINT32_C(0x0da62026);
    config.rival_count = 0u;
    config.world_mode = ODWD_MODE_OPEN_FIELD;
    CHECK(odwd_engine_init(&e, sizeof(e), &config) == ODWD_OK);
    player = &e.vehicles[0];
    player->x = best_x;
    player->z = best_z;
    player->y = open_ground_height(best_x, best_z) + 0.58;
    player->vx = player->vy = player->vz = 0.0;
    player->yaw = 0.63;
    player->body_pitch = player->body_roll = 0.0;
    player->airborne = 0u;
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.brake = 0.35;
    for (tick = 0u; tick < 90u; ++tick)
        CHECK(odwd_engine_step(&e, &input, NULL) == ODWD_OK);
    CHECK(dabs(player->body_pitch) + dabs(player->body_roll) > 0.06);
}

static void test_music_global_event_telegraph_contract(void) {
    static const uint32_t global_hazards[] = {
        MUSIC_HAZARD_QUAKE,
        MUSIC_HAZARD_LAVA_FLOOD,
        MUSIC_HAZARD_WIND,
    };
    uint32_t h;
    for (h = 0u; h < (uint32_t)(sizeof(global_hazards) / sizeof(global_hazards[0])); ++h) {
        odwd_engine_internal e;
        uint32_t phase;
        configure_music_engine(&e, UINT32_C(0x6000a11e) + h * 97u);
        e.music_survival.hazard_type = global_hazards[h];
        e.music_survival.hazard_level = 2u;
        e.music_survival.hazard_count = 0u;
        for (phase = MUSIC_EVENT_WARNING; phase <= MUSIC_EVENT_ACTIVE; ++phase) {
            uint32_t i;
            int found = 0;
            e.music_survival.hazard_phase = phase;
            props_refresh_music_survival(&e);
            for (i = 0u; i < e.prop_count; ++i) {
                const prop_internal *prop = &e.props[i];
                if (prop->id != UINT32_C(0x90600000)) continue;
                CHECK(prop->type == ODWD_PROP_MUSIC_WARNING);
                CHECK(((prop->variant >> 8u) & 255u) == global_hazards[h]);
                CHECK(prop->radius > 10.0);
                CHECK(prop->collidable == 0u);
                found = 1;
                break;
            }
            CHECK(found);
        }
    }
}

static void test_music_striker_route_contract(void) {
    uint32_t seed;
    uint32_t striker_count = 0u;
    for (seed = 1u; seed <= 160u; ++seed) {
        odwd_engine_internal a, b;
        music_survival_state *m;
        vehicle_internal *va;
        vehicle_internal *vb;
        configure_music_engine(&a, seed * UINT32_C(2654435761));
        configure_music_engine(&b, seed * UINT32_C(2654435761));
        m = &a.music_survival;
        m->hazard_phase = MUSIC_EVENT_WARNING;
        if (seed & 1u) {
            uint32_t i;
            m->hazard_type = MUSIC_HAZARD_HOLES;
            m->hazard_count = 5u;
            for (i = 0u; i < m->hazard_count; ++i) {
                music_hazard_zone *h = &m->hazards[i];
                double phase = (double)i / (double)m->hazard_count * TWO_PI;
                h->x = dsin(phase) * m->half_w * 0.30;
                h->z = dcos(phase) * m->half_d * 0.30;
                h->radius = 3.2 + (double)(i & 1u) * 0.7;
            }
            b.music_survival = a.music_survival;
        } else {
            m->hazard_type = MUSIC_HAZARD_METEORS;
            m->safe_count = 3u;
            m->safe[0].x = 0.0;
            m->safe[0].z = 0.0;
            m->safe[0].radius = 5.4;
            m->safe[0].shelter = 1u;
            m->safe[1].x = m->half_w * 0.34;
            m->safe[1].z = -m->half_d * 0.18;
            m->safe[1].radius = 4.4;
            m->safe[1].shelter = 1u;
            m->safe[2].x = -m->half_w * 0.30;
            m->safe[2].z = m->half_d * 0.25;
            m->safe[2].radius = 4.1;
            m->safe[2].shelter = 1u;
            b.music_survival = a.music_survival;
        }
        music_enemy_spawn(&a, 1u, MUSIC_ENEMY_STRIKER);
        music_enemy_spawn(&b, 1u, MUSIC_ENEMY_STRIKER);
        va = &a.vehicles[1];
        vb = &b.vehicles[1];
        CHECK(double_bits(va->x) == double_bits(vb->x));
        CHECK(double_bits(va->z) == double_bits(vb->z));
        CHECK(double_bits(va->ai_target_x) == double_bits(vb->ai_target_x));
        CHECK(double_bits(va->ai_target_z) == double_bits(vb->ai_target_z));
        CHECK(va->ai_archetype == vb->ai_archetype);
        CHECK(music_map_contains(&a.music_survival, va->x, va->z));
        if (va->ai_archetype == MUSIC_ENEMY_STRIKER) {
            ++striker_count;
            CHECK(!music_map_contains(&a.music_survival,
                                      va->ai_target_x, va->ai_target_z));
            CHECK(music_straight_route_clear(&a.music_survival,
                                             va->x, va->z,
                                             va->ai_target_x,
                                             va->ai_target_z));
        }
    }
    /* The avoidance rule should still leave abundant high-speed threats; it
     * must not silently downgrade almost every STRIKER. */
    CHECK(striker_count >= 120u);
}

int main(void) {
    test_vehicle_derived_gaps();
    test_contact_resolution_no_hidden_corner();
    test_jump_contract();
    test_generation_feasibility_history_and_determinism();
    test_elimination_order_is_deterministic();
    test_bot_and_player_share_obstacle_collision();
    test_bumper_arena_impact_contract();
    test_music_flood_refuge_contract();
    test_music_auto_recovery_regression();
    test_blockdash_player_auto_regressions();
    test_music_arena_and_endless_variety_contract();
    test_music_holes_boundary_and_life_contract();
    test_open_world_slope_pose_contract();
    test_music_global_event_telegraph_contract();
    test_music_striker_route_contract();
    test_blockdash_bot_pacing_contract();
    if (failures != 0) {
        fprintf(stderr, "%d survival invariant assertion(s) failed\n", failures);
        return 1;
    }
    puts("ODWD survival invariant tests: PASS");
    return 0;
}
