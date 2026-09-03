#include "odwd_core.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) do {                                                   \
    if (!(condition)) {                                                        \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures;                                                            \
    }                                                                          \
} while (0)

static double t_abs(double x) { return x < 0.0 ? -x : x; }
static double t_sqrt(double x) {
    double g = x > 1.0 ? x * 0.5 : 1.0;
    int i;
    if (x <= 0.0) return 0.0;
    for (i = 0; i < 18; ++i) g = 0.5 * (g + x / g);
    return g;
}

static void road_follow_input(const odwd_storage *storage, odwd_input *input) {
    odwd_vehicle_snapshot vehicle;
    odwd_camera_snapshot camera;
    odwd_road_node node;
    uint32_t i, count;
    double fx, fz, flen, rx, rz, tx, tz, tlen;
    odwd_input_neutral(input);
    CHECK(odwd_engine_read_vehicle(storage->bytes, 0u, &vehicle) == ODWD_OK);
    CHECK(odwd_engine_read_camera(storage->bytes, &camera) == ODWD_OK);
    count = odwd_engine_road_node_count(storage->bytes);
    memset(&node, 0, sizeof(node));
    for (i = 0; i < count; ++i) {
        CHECK(odwd_engine_read_road_node(storage->bytes, i, &node) == ODWD_OK);
        if (node.progress_m >= vehicle.road_progress_m + 32.0) break;
    }
    fx = camera.target_x - camera.position_x;
    fz = camera.target_z - camera.position_z;
    flen = t_sqrt(fx * fx + fz * fz);
    if (flen < 0.001) { fx = 0.0; fz = 1.0; flen = 1.0; }
    fx /= flen;
    fz /= flen;
    rx = fz;
    rz = -fx;
    tx = node.center_x - vehicle.position_x;
    tz = node.center_z - vehicle.position_z;
    tlen = t_sqrt(tx * tx + tz * tz);
    if (tlen < 0.001) { tx = 0.0; tz = 1.0; tlen = 1.0; }
    tx /= tlen;
    tz /= tlen;
    input->joystick_x = tx * rx + tz * rz;
    input->joystick_y = tx * fx + tz * fz;
}

static void point_follow_input(const odwd_storage *storage,
                               double target_x, double target_z,
                               odwd_input *input) {
    odwd_vehicle_snapshot vehicle;
    odwd_camera_snapshot camera;
    double fx, fz, flen, rx, rz, tx, tz, tlen;
    odwd_input_neutral(input);
    CHECK(odwd_engine_read_vehicle(storage->bytes, 0u, &vehicle) == ODWD_OK);
    CHECK(odwd_engine_read_camera(storage->bytes, &camera) == ODWD_OK);
    fx = camera.target_x - camera.position_x;
    fz = camera.target_z - camera.position_z;
    flen = t_sqrt(fx * fx + fz * fz);
    if (flen < 0.001) { fx = 0.0; fz = 1.0; flen = 1.0; }
    fx /= flen;
    fz /= flen;
    rx = fz;
    rz = -fx;
    tx = target_x - vehicle.position_x;
    tz = target_z - vehicle.position_z;
    tlen = t_sqrt(tx * tx + tz * tz);
    if (tlen < 0.001) { tx = 0.0; tz = 1.0; tlen = 1.0; }
    tx /= tlen;
    tz /= tlen;
    input->joystick_x = tx * rx + tz * rz;
    input->joystick_y = tx * fx + tz * fz;
}

typedef struct test_route_classification {
    int alternate;
    double alternate_width;
} test_route_classification;

static test_route_classification classify_public_route(
    const odwd_storage *storage, const odwd_vehicle_snapshot *vehicle) {
    test_route_classification result;
    double best_main = 1.0e300;
    double best_alt = 1.0e300;
    double best_alt_width = 0.0;
    uint32_t index;
    result.alternate = 0;
    result.alternate_width = 0.0;
    for (index = 0u; index + 1u < odwd_engine_road_node_count(storage->bytes);
         ++index) {
        odwd_road_node a, b;
        uint32_t alternate;
        CHECK(odwd_engine_read_road_node(storage->bytes, index, &a) == ODWD_OK);
        CHECK(odwd_engine_read_road_node(storage->bytes, index + 1u, &b) == ODWD_OK);
        for (alternate = 0u; alternate <= 1u; ++alternate) {
            double ax, az, bx, bz, dx, dz, len2, t, cx, cz;
            double inv, lateral, progress, error, width = 0.0;
            if (alternate &&
                (!(a.flags & ODWD_ROAD_ALT_ROUTE) ||
                 !(b.flags & ODWD_ROAD_ALT_ROUTE))) continue;
            ax = alternate ? a.alternate_x : a.center_x;
            az = alternate ? a.alternate_z : a.center_z;
            bx = alternate ? b.alternate_x : b.center_x;
            bz = alternate ? b.alternate_z : b.center_z;
            dx = bx - ax;
            dz = bz - az;
            len2 = dx * dx + dz * dz;
            if (len2 < 1.0e-9) continue;
            t = ((vehicle->position_x - ax) * dx +
                 (vehicle->position_z - az) * dz) / len2;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            if (alternate) {
                double max_width = a.alternate_half_width_m >
                                   b.alternate_half_width_m ?
                                   a.alternate_half_width_m :
                                   b.alternate_half_width_m;
                width = a.alternate_half_width_m +
                        (b.alternate_half_width_m -
                         a.alternate_half_width_m) * t;
                if (max_width <= 1.12 || width < 1.12) continue;
            }
            cx = ax + dx * t;
            cz = az + dz * t;
            inv = 1.0 / t_sqrt(len2);
            lateral = (vehicle->position_x - cx) * (dz * inv) +
                      (vehicle->position_z - cz) * (-dx * inv);
            progress = a.progress_m + (b.progress_m - a.progress_m) * t;
            error = t_abs(progress - vehicle->road_progress_m) +
                    t_abs(lateral - vehicle->road_lateral_m);
            if (alternate) {
                if (error < best_alt) {
                    best_alt = error;
                    best_alt_width = width;
                }
            } else if (error < best_main) {
                best_main = error;
            }
        }
    }
    if (best_alt + 1.0e-6 < best_main) {
        result.alternate = 1;
        result.alternate_width = best_alt_width;
    }
    return result;
}

static void test_abi_and_validation(void) {
    odwd_storage storage;
    odwd_config config;
    odwd_frame frame;
    odwd_config_defaults(&config);
    CHECK(config.abi_version == ODWD_ABI_VERSION);
    CHECK(odwd_engine_required_bytes() <= ODWD_ENGINE_STORAGE_BYTES);
    CHECK(odwd_engine_required_alignment() <= _Alignof(odwd_storage));
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_read_frame(storage.bytes, &frame) == ODWD_OK);
    CHECK(frame.tick == 0u);
    CHECK(frame.vehicle_count == config.rival_count + 1u);
    CHECK(frame.player_place >= 1u && frame.player_place <= frame.vehicle_count);
    config.abi_version += 1u;
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_E_ABI);
}

static void test_repeatability(void) {
    odwd_storage a, b;
    odwd_config config;
    odwd_input input;
    odwd_frame fa, fb;
    unsigned tick;
    odwd_config_defaults(&config);
    config.seed = 932032u;
    config.rival_count = 4u;
    CHECK(odwd_engine_init(a.bytes, sizeof(a.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_init(b.bytes, sizeof(b.bytes), &config) == ODWD_OK);
    for (tick = 0; tick < 6000u; ++tick) {
        odwd_input_neutral(&input);
        input.joystick_y = tick < 5400u ? 1.0 : 0.55;
        input.joystick_x = ((tick / 233u) & 1u) ? 0.24 : -0.17;
        if (tick > 900u && tick < 970u) input.look_dx = 0.13;
        if (tick == 4200u) input.buttons |= ODWD_BUTTON_HANDBRAKE;
        CHECK(odwd_engine_step(a.bytes, &input, &fa) == ODWD_OK);
        CHECK(odwd_engine_step(b.bytes, &input, &fb) == ODWD_OK);
        CHECK(fa.deterministic_state_hash == fb.deterministic_state_hash);
    }
    CHECK(odwd_engine_state_hash(a.bytes) == odwd_engine_state_hash(b.bytes));
}

static void test_camera_relative_drive_and_drift(void) {
    odwd_storage storage;
    odwd_config config;
    odwd_input input;
    odwd_vehicle_snapshot start, now;
    odwd_camera_snapshot camera;
    double max_drift = 0.0, max_speed = 0.0;
    unsigned tick;
    odwd_config_defaults(&config);
    config.rival_count = 0u;
    config.seed = 77119u;
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &start) == ODWD_OK);

    /* Rotate the right-side camera gesture, then push joystick straight ahead. */
    for (tick = 0; tick < 34u; ++tick) {
        odwd_input_neutral(&input);
        input.look_dx = 0.72;
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    }
    CHECK(odwd_engine_read_camera(storage.bytes, &camera) == ODWD_OK);
    CHECK(t_abs(camera.forward_yaw_rad) > 0.50);
    for (tick = 0; tick < 260u; ++tick) {
        odwd_input_neutral(&input);
        input.joystick_y = 1.0;
        input.look_dx = 0.001; /* keep the chosen world view briefly */
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
        CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &now) == ODWD_OK);
    }
    CHECK(now.position_x > start.position_x + 4.0);

    /* Fresh run: follow the road to speed, then demand a sharp automatic drift. */
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    for (tick = 0; tick < 1500u; ++tick) {
        road_follow_input(&storage, &input);
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
        CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &now) == ODWD_OK);
        if (now.speed_mps > max_speed) max_speed = now.speed_mps;
    }
    for (tick = 0; tick < 280u; ++tick) {
        odwd_input_neutral(&input);
        input.joystick_x = 1.0;
        input.joystick_y = 0.08;
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
        CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &now) == ODWD_OK);
        if (now.drift_intensity > max_drift) max_drift = now.drift_intensity;
    }
    CHECK(max_speed > 12.0);
    CHECK(max_drift > 0.08);
}

static void test_stream_checkpoint_respawn_and_branches(void) {
    odwd_storage storage;
    odwd_config config;
    odwd_input input;
    odwd_frame frame;
    odwd_road_node first, node;
    odwd_vehicle_snapshot vehicle;
    unsigned tick, i;
    int branch_seen = 0;
    odwd_config_defaults(&config);
    config.rival_count = 0u;
    config.seed = 0xace102u;
    config.checkpoint_spacing_m = 240.0;
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_read_road_node(storage.bytes, 0u, &first) == ODWD_OK);
    for (i = 0; i < odwd_engine_road_node_count(storage.bytes); ++i) {
        CHECK(odwd_engine_read_road_node(storage.bytes, i, &node) == ODWD_OK);
        if (node.flags & ODWD_ROAD_ALT_ROUTE) branch_seen = 1;
    }
    CHECK(branch_seen);
    for (tick = 0; tick < 18000u; ++tick) {
        road_follow_input(&storage, &input);
        CHECK(odwd_engine_step(storage.bytes, &input, &frame) == ODWD_OK);
    }
    CHECK(odwd_engine_read_road_node(storage.bytes, 0u, &node) == ODWD_OK);
    CHECK(node.global_node_index > first.global_node_index);
    CHECK(frame.endless_progress_m > 500.0);
    CHECK(frame.last_checkpoint_m >= 240.0);
    {
    uint32_t previous_respawns;
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &vehicle) == ODWD_OK);
    previous_respawns = vehicle.respawn_count;
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_RESPAWN;
    CHECK(odwd_engine_step(storage.bytes, &input, &frame) == ODWD_OK);
    CHECK(frame.event_flags & ODWD_EVENT_PLAYER_RESPAWN);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &vehicle) == ODWD_OK);
    CHECK(vehicle.respawn_count == previous_respawns + 1u);
    CHECK(vehicle.road_progress_m >= frame.last_checkpoint_m - 2.0);
    }
}

static void test_ai_rubber_band_has_no_pose_teleport(void) {
    odwd_storage storage;
    odwd_config config;
    odwd_input input;
    odwd_vehicle_snapshot before, after;
    unsigned tick;
    double max_continuous_step = 0.0;
    odwd_config_defaults(&config);
    config.rival_count = 3u;
    config.seed = 4007u;
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 1u, &before) == ODWD_OK);
    for (tick = 0; tick < 8000u; ++tick) {
        double dx, dz, distance;
        odwd_input_neutral(&input);
        input.joystick_y = tick > 1200u ? 1.0 : 0.0; /* let rival become far ahead, then chase */
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
        CHECK(odwd_engine_read_vehicle(storage.bytes, 1u, &after) == ODWD_OK);
        dx = after.position_x - before.position_x;
        dz = after.position_z - before.position_z;
        distance = t_sqrt(dx * dx + dz * dz);
        if (after.respawn_count == before.respawn_count && distance > max_continuous_step)
            max_continuous_step = distance;
        before = after;
    }
    /* Acceleration-based rubber band remains a continuous 120 Hz trajectory. */
    CHECK(max_continuous_step < 3.2);
    {
        odwd_vehicle_snapshot player;
        uint32_t index;
        CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &player) == ODWD_OK);
        for (index = 1u; index <= config.rival_count; ++index) {
            CHECK(odwd_engine_read_vehicle(storage.bytes, index, &after) == ODWD_OK);
            CHECK(t_abs(after.road_progress_m - player.road_progress_m) < 1000.0);
        }
    }
}

static void test_v3_controls_world_and_music(void) {
    odwd_storage normal, boosted, open, stream_open, grip_car, drift_car, camera_car;
    odwd_config config;
    odwd_input input;
    odwd_vehicle_snapshot normal_car, boost_car, before_brake, open_car;
    odwd_frame frame;
    odwd_world_prop_snapshot prop;
    odwd_world_prop_snapshot props_before[ODWD_MAX_WORLD_PROPS + 1u];
    odwd_pickup_snapshot pickup;
    odwd_road_node node;
    float pulse[512];
    unsigned tick, index;
    double grip_slip = 0.0, handbrake_slip = 0.0;
    int tree_seen = 0, shrub_seen = 0, branch_surface_seen = 0;
    int house_seen = 0, flower_seen = 0, bird_seen = 0, ramp_seen = 0;
    int trampoline_seen = 0, goal_seen = 0, barrier_seen = 0, ball_seen = 0;
    int bumper_floor_seen = 0, bumper_pylons = 0;

    odwd_config_defaults(&config);
    config.rival_count = 0u;
    config.seed = UINT32_C(0x20da032);
    CHECK(odwd_engine_init(normal.bytes, sizeof(normal.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_init(boosted.bytes, sizeof(boosted.bytes), &config) == ODWD_OK);

    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.throttle = 1.0;
    for (tick = 0u; tick < 300u; ++tick) {
        CHECK(odwd_engine_step(normal.bytes, &input, NULL) == ODWD_OK);
        input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS | ODWD_BUTTON_TURBO;
        CHECK(odwd_engine_step(boosted.bytes, &input, NULL) == ODWD_OK);
        input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    }
    CHECK(odwd_engine_read_vehicle(normal.bytes, 0u, &normal_car) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(boosted.bytes, 0u, &boost_car) == ODWD_OK);
    CHECK(boost_car.speed_mps > normal_car.speed_mps + 4.0);
    CHECK(boost_car.turbo_01 < normal_car.turbo_01);
    CHECK(boost_car.turbo_01 >= 0.0 && boost_car.turbo_01 <= 1.0);
    CHECK(boost_car.turbo_active == 1u);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    CHECK(odwd_engine_step(boosted.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(boosted.bytes, 0u, &boost_car) == ODWD_OK);
    CHECK(boost_car.turbo_active == 0u);

    before_brake = normal_car;
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.brake = 1.0;
    for (tick = 0u; tick < 100u; ++tick)
        CHECK(odwd_engine_step(normal.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(normal.bytes, 0u, &normal_car) == ODWD_OK);
    CHECK(normal_car.speed_mps < before_brake.speed_mps * 0.55);

    /* Headlights are a core-owned toggle edge, not a momentary UI level. */
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS | ODWD_BUTTON_HEADLIGHTS;
    CHECK(odwd_engine_step(normal.bytes, &input, &frame) == ODWD_OK);
    CHECK(frame.headlights_on == 1u);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    CHECK(odwd_engine_step(normal.bytes, &input, &frame) == ODWD_OK);
    CHECK(frame.headlights_on == 1u);
    input.buttons |= ODWD_BUTTON_HEADLIGHTS;
    CHECK(odwd_engine_step(normal.bytes, &input, &frame) == ODWD_OK);
    CHECK(frame.headlights_on == 0u);

    for (index = 0u; index < odwd_engine_road_node_count(normal.bytes); ++index) {
        double dx, dy, dz, separation;
        CHECK(odwd_engine_read_road_node(normal.bytes, index, &node) == ODWD_OK);
        if (!(node.flags & ODWD_ROAD_ALT_ROUTE) ||
            node.alternate_half_width_m <= 0.42) continue;
        dx = node.alternate_x - node.center_x;
        dy = node.alternate_y - node.center_y;
        dz = node.alternate_z - node.center_z;
        separation = t_sqrt(dx * dx + dy * dy + dz * dz);
        CHECK(separation >= node.half_width_m +
                            node.alternate_half_width_m + 0.39);
        CHECK(t_abs(node.alternate_tangent_x) +
              t_abs(node.alternate_tangent_z) > 0.5);
        branch_surface_seen = 1;
    }
    CHECK(branch_surface_seen);
    CHECK(odwd_engine_world_prop_count(normal.bytes) > 8u);
    for (index = 0u; index < odwd_engine_world_prop_count(normal.bytes); ++index) {
        CHECK(odwd_engine_read_world_prop(normal.bytes, index, &prop) == ODWD_OK);
        if (prop.type == ODWD_PROP_TREE) tree_seen = 1;
        if (prop.type == ODWD_PROP_SHRUB) shrub_seen = 1;
    }
    CHECK(tree_seen && shrub_seen);
    CHECK(odwd_engine_pickup_count(normal.bytes) == ODWD_MAX_TURBO_PICKUPS);
    CHECK(odwd_engine_read_pickup(normal.bytes, 0u, &pickup) == ODWD_OK);
    CHECK(pickup.type == ODWD_PICKUP_TURBO && pickup.amount_01 > 0.0);

    /* The sparse pickup is a physical world object and can be collected. */
    odwd_config_defaults(&config);
    config.rival_count = 0u;
    config.seed = UINT32_C(0x20da032);
    CHECK(odwd_engine_init(normal.bytes, sizeof(normal.bytes), &config) == ODWD_OK);
    {
        int pickup_collected = 0;
        for (tick = 0u; tick < 6500u && !pickup_collected; ++tick) {
            CHECK(odwd_engine_read_pickup(normal.bytes, 0u, &pickup) == ODWD_OK);
            point_follow_input(&normal, pickup.position_x, pickup.position_z,
                               &input);
            CHECK(odwd_engine_step(normal.bytes, &input, &frame) == ODWD_OK);
            if ((frame.event_flags & ODWD_EVENT_TURBO_PICKUP) != 0u)
                pickup_collected = 1;
        }
        CHECK(pickup_collected);
        CHECK(odwd_engine_read_vehicle(normal.bytes, 0u, &normal_car) == ODWD_OK);
        CHECK(normal_car.turbo_01 > 0.34);
    }

    memset(pulse, 0, sizeof(pulse));
    CHECK(odwd_engine_submit_music_pcm(normal.bytes, pulse, 512u, 1u,
                                       48000u, 0.0) == ODWD_OK);
    for (index = 0u; index < 512u; ++index)
        pulse[index] = (index & 1u) ? 0.92f : -0.92f;
    CHECK(odwd_engine_submit_music_pcm(normal.bytes, pulse, 512u, 1u,
                                       48000u, 0.011) == ODWD_OK);
    odwd_input_neutral(&input);
    CHECK(odwd_engine_step(normal.bytes, &input, &frame) == ODWD_OK);
    CHECK(frame.music_energy_01 > 0.5);
    CHECK(frame.music_beat_01 > 0.1);
    CHECK((frame.event_flags & ODWD_EVENT_MUSIC_BEAT) != 0u);

    odwd_config_defaults(&config);
    config.rival_count = 0u;
    config.seed = UINT32_C(0x0f13d032);
    config.world_mode = ODWD_MODE_OPEN_FIELD;
    CHECK(odwd_engine_init(open.bytes, sizeof(open.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_world_prop_count(open.bytes) > 30u);
    CHECK(odwd_engine_world_prop_count(open.bytes) <=
          ODWD_MAX_WORLD_PROPS + 1u);
    for (index = 0u; index < odwd_engine_world_prop_count(open.bytes); ++index) {
        CHECK(odwd_engine_read_world_prop(open.bytes, index, &prop) == ODWD_OK);
        if (prop.type == ODWD_PROP_HOUSE) house_seen = 1;
        if (prop.type == ODWD_PROP_FLOWER) flower_seen = 1;
        if (prop.type == ODWD_PROP_BIRD_GROUND ||
            prop.type == ODWD_PROP_BIRD_FLYING) bird_seen = 1;
        if (prop.type == ODWD_PROP_RAMP ||
            prop.type == ODWD_PROP_RAMP_LARGE) ramp_seen = 1;
        if (prop.type == ODWD_PROP_TRAMPOLINE) trampoline_seen = 1;
        if (prop.type == ODWD_PROP_GOAL) goal_seen = 1;
        if (prop.type == ODWD_PROP_BARRIER) barrier_seen = 1;
        if (prop.type == ODWD_PROP_BALL) ball_seen = 1;
        if (prop.type == ODWD_PROP_BUMPER_FLOOR) {
            CHECK(prop.prop_id == UINT32_C(0x743fffff));
            CHECK(prop.collidable == 0u);
            CHECK(prop.radius_m > 45.0);
            bumper_floor_seen = 1;
        }
        if ((prop.prop_id & UINT32_C(0xfff00000)) == UINT32_C(0x74400000) &&
            prop.type == ODWD_PROP_SCULPTURE) ++bumper_pylons;
    }
    CHECK(house_seen && flower_seen && bird_seen && ramp_seen);
    CHECK(trampoline_seen && goal_seen && barrier_seen && ball_seen);
    CHECK(bumper_floor_seen && bumper_pylons >= 8);
    CHECK(t_abs(odwd_engine_ground_height(open.bytes, 10.0, 10.0) -
                odwd_engine_ground_height(open.bytes, 10.2, 10.2)) < 0.2);
    CHECK(odwd_engine_ground_height(open.bytes, 72.0, -98.5) >
          odwd_engine_base_ground_height(open.bytes, 72.0, -98.5) + 0.7);

    /* Crossing a 34 m procedural cell must not pop any prop that is still
     * inside the visible 200 m overlap. The expanded 15x15 ring streams only
     * beyond the rendered ground horizon. */
    CHECK(odwd_engine_init(stream_open.bytes, sizeof(stream_open.bytes),
                           &config) == ODWD_OK);
    {
        uint32_t before_count = odwd_engine_world_prop_count(stream_open.bytes);
        uint32_t preserved = 0u;
        odwd_vehicle_snapshot streamed_car;
        CHECK(before_count <= ODWD_MAX_WORLD_PROPS + 1u);
        for (index = 0u; index < before_count; ++index)
            CHECK(odwd_engine_read_world_prop(stream_open.bytes, index,
                                               &props_before[index]) == ODWD_OK);
        odwd_input_neutral(&input);
        input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
        input.throttle = 1.0;
        for (tick = 0u; tick < 1400u; ++tick) {
            CHECK(odwd_engine_step(stream_open.bytes, &input, NULL) == ODWD_OK);
            CHECK(odwd_engine_read_vehicle(stream_open.bytes, 0u,
                                           &streamed_car) == ODWD_OK);
            if (streamed_car.position_x > 42.0) break;
        }
        CHECK(streamed_car.position_x > 42.0);
        for (index = 0u; index < before_count; ++index) {
            double dx = props_before[index].position_x - streamed_car.position_x;
            double dz = props_before[index].position_z - streamed_car.position_z;
            uint32_t after_index;
            int found = 0;
            if (dx * dx + dz * dz > 200.0 * 200.0) continue;
            for (after_index = 0u;
                 after_index < odwd_engine_world_prop_count(stream_open.bytes);
                 ++after_index) {
                CHECK(odwd_engine_read_world_prop(stream_open.bytes,
                                                   after_index, &prop) == ODWD_OK);
                if (prop.prop_id != props_before[index].prop_id) continue;
                if (prop.type != ODWD_PROP_BIRD_FLYING &&
                    prop.type != ODWD_PROP_BIRD_GROUND &&
                    prop.type != ODWD_PROP_BALL) {
                    CHECK(t_abs(prop.position_x - props_before[index].position_x) < 1.0e-9);
                    CHECK(t_abs(prop.position_y - props_before[index].position_y) < 1.0e-9);
                    CHECK(t_abs(prop.position_z - props_before[index].position_z) < 1.0e-9);
                }
                found = 1;
                ++preserved;
                break;
            }
            CHECK(found);
        }
        CHECK(preserved > 40u);
    }
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.joystick_x = 1.0;
    input.throttle = 1.0;
    for (tick = 0u; tick < 700u; ++tick)
        CHECK(odwd_engine_step(open.bytes, &input, &frame) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(open.bytes, 0u, &open_car) == ODWD_OK);
    CHECK(frame.world_mode == ODWD_MODE_OPEN_FIELD);
    CHECK(open_car.traveled_distance_m > 20.0);
    CHECK(t_abs(open_car.position_x) > 4.0);

    /* The deterministic sky reaches full inverted night without a host clock. */
    odwd_input_neutral(&input);
    for (tick = 0u; tick < 120u * 106u; ++tick)
        CHECK(odwd_engine_step(open.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_frame(open.bytes, &frame) == ODWD_OK);
    CHECK(frame.night_amount_01 > 0.95);

    CHECK(odwd_engine_init(grip_car.bytes, sizeof(grip_car.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_init(drift_car.bytes, sizeof(drift_car.bytes), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.throttle = 1.0;
    for (tick = 0u; tick < 700u; ++tick) {
        CHECK(odwd_engine_step(grip_car.bytes, &input, NULL) == ODWD_OK);
        CHECK(odwd_engine_step(drift_car.bytes, &input, NULL) == ODWD_OK);
    }
    input.joystick_x = 0.82;
    input.throttle = 0.78;
    for (tick = 0u; tick < 180u; ++tick) {
        odwd_vehicle_snapshot a, b;
        input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
        CHECK(odwd_engine_step(grip_car.bytes, &input, NULL) == ODWD_OK);
        input.buttons |= ODWD_BUTTON_HANDBRAKE;
        CHECK(odwd_engine_step(drift_car.bytes, &input, NULL) == ODWD_OK);
        CHECK(odwd_engine_read_vehicle(grip_car.bytes, 0u, &a) == ODWD_OK);
        CHECK(odwd_engine_read_vehicle(drift_car.bytes, 0u, &b) == ODWD_OK);
        if (t_abs(a.slip_angle_rad) > grip_slip)
            grip_slip = t_abs(a.slip_angle_rad);
        if (t_abs(b.slip_angle_rad) > handbrake_slip)
            handbrake_slip = t_abs(b.slip_angle_rad);
    }
    /* At ~110-120 km/h a normal turn must stay in a grip regime instead of
     * entering a free drift; DRIFT remains an intentionally separate state. */
    CHECK(grip_slip < 0.10);
    CHECK(handbrake_slip > 0.30);
    CHECK(handbrake_slip > grip_slip * 4.0);

    /* Explicit web/native pedals are vehicle-relative: looking 90 degrees
     * away cannot steer or drift a straight accelerating car. */
    CHECK(odwd_engine_init(camera_car.bytes, sizeof(camera_car.bytes), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.look_dx = 0.18;
    input.camera_mode = ODWD_CAMERA_CINEMATIC;
    for (tick = 0u; tick < 90u; ++tick)
        CHECK(odwd_engine_step(camera_car.bytes, &input, NULL) == ODWD_OK);
    input.look_dx = 0.0;
    input.throttle = 1.0;
    {
        odwd_vehicle_snapshot before;
        odwd_camera_snapshot camera;
        double max_steer = 0.0;
        double max_drift = 0.0;
        CHECK(odwd_engine_read_vehicle(camera_car.bytes, 0u, &before) == ODWD_OK);
        CHECK(odwd_engine_read_camera(camera_car.bytes, &camera) == ODWD_OK);
        CHECK(camera.camera_mode == ODWD_CAMERA_CINEMATIC);
        CHECK(t_abs(camera.forward_yaw_rad - before.heading_rad) > 0.65);
        for (tick = 0u; tick < 240u; ++tick) {
            odwd_vehicle_snapshot now;
            CHECK(odwd_engine_step(camera_car.bytes, &input, NULL) == ODWD_OK);
            CHECK(odwd_engine_read_vehicle(camera_car.bytes, 0u, &now) == ODWD_OK);
            if (t_abs(now.steering_rad) > max_steer) max_steer = t_abs(now.steering_rad);
            if (now.drift_intensity > max_drift) max_drift = now.drift_intensity;
            CHECK(t_abs(now.heading_rad - before.heading_rad) < 0.025);
        }
        CHECK(max_steer < 0.001);
        CHECK(max_drift < 0.001);
    }
}

static void test_action_sector_jump_and_landing(void) {
    odwd_storage storage;
    odwd_config config;
    odwd_input input;
    odwd_frame frame;
    odwd_vehicle_snapshot car;
    unsigned tick;
    int jumped = 0;
    int landed = 0;
    double max_air_time = 0.0;
    odwd_config_defaults(&config);
    config.rival_count = 0u;
    config.seed = UINT32_C(0x574c4431);
    config.section_length_m = 1536.0;
    config.checkpoint_spacing_m = 480.0;
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    for (tick = 0u; tick < 34000u && !landed; ++tick) {
        road_follow_input(&storage, &input);
        CHECK(odwd_engine_step(storage.bytes, &input, &frame) == ODWD_OK);
        CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
        if ((frame.event_flags & ODWD_EVENT_JUMP) != 0u) jumped = 1;
        if (car.air_time_s > max_air_time) max_air_time = car.air_time_s;
        if (jumped && (frame.event_flags & ODWD_EVENT_LAND) != 0u) landed = 1;
    }
    CHECK(jumped);
    CHECK(landed);
    CHECK(max_air_time > 0.20);
    CHECK(car.airborne == 0u);
}

static void test_branch_route_transition_is_ground_continuous(void) {
    odwd_storage storage;
    odwd_config config;
    odwd_input input;
    odwd_frame frame;
    odwd_vehicle_snapshot previous, now;
    odwd_road_node node;
    double branch_start = -1.0;
    double branch_end = -1.0;
    double max_abs_dy = 0.0;
    uint32_t initial_respawns;
    unsigned tick;
    uint32_t index;
    int branch_finished = 0;
    int previous_alt = 0;
    int alt_seen = 0;
    int jump_seen = 0;
    int airborne_seen = 0;

    odwd_config_defaults(&config);
    config.seed = 18u;
    config.rival_count = 0u;
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    for (index = 0u; index < odwd_engine_road_node_count(storage.bytes); ++index) {
        CHECK(odwd_engine_read_road_node(storage.bytes, index, &node) == ODWD_OK);
        if (node.flags & ODWD_ROAD_ALT_ROUTE) {
            if (branch_start < 0.0) branch_start = node.progress_m;
            if (!branch_finished) {
                branch_end = node.progress_m;
                CHECK((node.flags & (ODWD_ROAD_RAMP | ODWD_ROAD_GAP)) == 0u);
            }
        } else if (branch_start >= 0.0) {
            branch_finished = 1;
        }
    }
    CHECK(branch_start >= 0.0 && branch_end > branch_start);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &previous) == ODWD_OK);
    initial_respawns = previous.respawn_count;

    for (tick = 0u; tick < 12000u; ++tick) {
        double target_x = 0.0;
        double target_z = 0.0;
        int target_found = 0;
        int near_branch;
        CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &previous) == ODWD_OK);
        near_branch = previous.road_progress_m >= branch_start - 72.0 &&
                      previous.road_progress_m <= branch_end + 20.0;
        for (index = 0u; index < odwd_engine_road_node_count(storage.bytes); ++index) {
            double blend;
            CHECK(odwd_engine_read_road_node(storage.bytes, index, &node) == ODWD_OK);
            if (node.progress_m < previous.road_progress_m + 36.0) continue;
            target_x = node.center_x;
            target_z = node.center_z;
            if (near_branch && (node.flags & ODWD_ROAD_ALT_ROUTE)) {
                blend = node.alternate_half_width_m < 1.12 ? 0.72 : 1.28;
                target_x += (node.alternate_x - node.center_x) * blend;
                target_z += (node.alternate_z - node.center_z) * blend;
            }
            target_found = 1;
            break;
        }
        CHECK(target_found);
        if (near_branch && previous.speed_mps > 8.5) {
            odwd_input_neutral(&input);
            input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
            input.brake = 1.0;
        } else {
            point_follow_input(&storage, target_x, target_z, &input);
            input.joystick_x *= near_branch ? 0.48 : 0.82;
            input.joystick_y *= near_branch ? 0.48 : 0.82;
        }
        CHECK(odwd_engine_step(storage.bytes, &input, &frame) == ODWD_OK);
        CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &now) == ODWD_OK);
        if (now.road_progress_m >= branch_start - 36.0 &&
            now.road_progress_m <= branch_end + 24.0) {
            test_route_classification route =
                classify_public_route(&storage, &now);
            double dy = now.position_y - previous.position_y;
            if (t_abs(dy) > max_abs_dy) max_abs_dy = t_abs(dy);
            if (frame.event_flags & ODWD_EVENT_JUMP) jump_seen = 1;
            if (now.airborne) airborne_seen = 1;
            if (route.alternate) {
                CHECK(route.alternate_width >= 1.12);
                if (!previous_alt) {
                    CHECK(t_abs(dy) <= 0.10);
                    CHECK(t_abs(dy - now.velocity_y /
                                (double)ODWD_TICK_HZ) <= 0.03);
                }
                alt_seen = 1;
            }
            previous_alt = route.alternate;
        }
        if (now.road_progress_m > branch_end + 24.0 && alt_seen) break;
    }
    CHECK(alt_seen);
    CHECK(max_abs_dy <= 0.10);
    CHECK(!jump_seen && !airborne_seen);
    CHECK(now.respawn_count == initial_respawns);
}

static void test_v4_survival_mode(void) {
    odwd_storage a, b, solo;
    odwd_config config;
    odwd_input input;
    odwd_frame fa, fb;
    odwd_vehicle_snapshot before, after;
    odwd_world_prop_snapshot prop;
    uint32_t i;
    unsigned tick;
    uint32_t jump_events = 0u;
    int platform_seen = 0;
    int obstacle_seen = 0;

    odwd_config_defaults(&config);
    config.seed = UINT32_C(0x5a17cb02); /* sector-0 WALL_GAP after SLALOM removal */
    config.rival_count = 7u;
    config.world_mode = ODWD_MODE_SURVIVAL;
    CHECK(odwd_engine_init(a.bytes, sizeof(a.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_init(b.bytes, sizeof(b.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_read_frame(a.bytes, &fa) == ODWD_OK);
    CHECK(fa.world_mode == ODWD_MODE_SURVIVAL);
    CHECK(fa.survival_alive_count == 8u);
    CHECK(fa.survival_player_eliminated == 0u);
    CHECK(fa.survival_final_place == 0u);
    CHECK(odwd_engine_pickup_count(a.bytes) == ODWD_MAX_TURBO_PICKUPS);
    for (i = 0u; i < odwd_engine_pickup_count(a.bytes); ++i) {
        odwd_pickup_snapshot pickup;
        CHECK(odwd_engine_read_pickup(a.bytes, i, &pickup) == ODWD_OK);
        CHECK(pickup.active == 0u);
    }
    for (i = 0u; i < odwd_engine_world_prop_count(a.bytes); ++i) {
        CHECK(odwd_engine_read_world_prop(a.bytes, i, &prop) == ODWD_OK);
        if (prop.type == ODWD_PROP_SURVIVAL_PLATFORM) {
            CHECK(prop.extent_x_m > 2.76);
            CHECK(prop.extent_z_m >= 2.0);
            platform_seen = 1;
        }
        if (prop.type >= ODWD_PROP_SURVIVAL_WALL &&
            prop.type <= ODWD_PROP_SURVIVAL_GATE) {
            CHECK(prop.extent_x_m > 0.0 && prop.extent_z_m > 0.0);
            obstacle_seen = 1;
        }
    }
    CHECK(platform_seen);
    CHECK(obstacle_seen);

    /* Same seed + inputs produces the same sector motion, bot decisions and
     * collision results. */
    for (tick = 0u; tick < 1800u; ++tick) {
        odwd_input_neutral(&input);
        input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
        input.brake = 0.25;
        CHECK(odwd_engine_step(a.bytes, &input, &fa) == ODWD_OK);
        CHECK(odwd_engine_step(b.bytes, &input, &fb) == ODWD_OK);
        CHECK(fa.deterministic_state_hash == fb.deterministic_state_hash);
        CHECK(fa.survival_sector_family == fb.survival_sector_family);
        CHECK(fa.survival_alive_count == fb.survival_alive_count);
    }

    /* Manual jump is mode-local, grounded-only and cannot retrigger in air. */
    config.rival_count = 0u;
    config.seed = UINT32_C(0x7710babe);
    CHECK(odwd_engine_init(solo.bytes, sizeof(solo.bytes), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS | ODWD_BUTTON_JUMP;
    for (tick = 0u; tick < 45u; ++tick) {
        CHECK(odwd_engine_step(solo.bytes, &input, &fa) == ODWD_OK);
        if (fa.event_flags & ODWD_EVENT_JUMP) ++jump_events;
    }
    CHECK(jump_events == 1u);
    CHECK(odwd_engine_read_vehicle(solo.bytes, 0u, &after) == ODWD_OK);
    CHECK(after.airborne == 1u);
    CHECK(after.velocity_y < 8.9);

    /* A survival respawn request is ignored; leaving the arena eliminates the
     * vehicle permanently and the sole survivor result is deterministically 1st. */
    CHECK(odwd_engine_init(solo.bytes, sizeof(solo.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(solo.bytes, 0u, &before) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS | ODWD_BUTTON_REVERSE;
    input.reverse = 1.0;
    for (tick = 0u; tick < 4200u; ++tick) {
        CHECK(odwd_engine_step(solo.bytes, &input, &fa) == ODWD_OK);
        if (fa.survival_player_eliminated) break;
    }
    CHECK(tick < 4200u);
    CHECK(fa.survival_player_eliminated == 1u);
    CHECK(fa.survival_final_place == 1u);
    CHECK(fa.survival_alive_count == 0u);
    CHECK(fa.event_flags & ODWD_EVENT_SURVIVAL_ELIMINATED);
    CHECK(odwd_engine_read_vehicle(solo.bytes, 0u, &after) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_RESPAWN;
    CHECK(odwd_engine_step(solo.bytes, &input, &fa) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(solo.bytes, 0u, &after) == ODWD_OK);
    CHECK(after.respawn_count == before.respawn_count);
    CHECK(fa.survival_player_eliminated == 1u);

    /* Jump/reverse bits remain isolated: ENDLESS does not gain manual jump. */
    odwd_config_defaults(&config);
    config.rival_count = 0u;
    config.world_mode = ODWD_MODE_ENDLESS;
    CHECK(odwd_engine_init(solo.bytes, sizeof(solo.bytes), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS | ODWD_BUTTON_JUMP;
    for (tick = 0u; tick < 30u; ++tick)
        CHECK(odwd_engine_step(solo.bytes, &input, &fa) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(solo.bytes, 0u, &after) == ODWD_OK);
    CHECK(after.airborne == 0u);
}

static void test_open_football_public_activity(void) {
    static const uint32_t seeds[4] = {
        UINT32_C(932032), UINT32_C(1), UINT32_C(424242), UINT32_C(20260819)
    };
    uint32_t seed_index;
    for (seed_index = 0u; seed_index < 4u; ++seed_index) {
        odwd_storage storage, twin;
        odwd_config config;
        odwd_input input;
        odwd_frame frame, twin_frame;
        unsigned tick;
        uint32_t index;
        uint32_t score_left;
        uint32_t score_right;
        uint32_t previous_left;
        uint32_t previous_right;
        uint32_t goal_events = 0u;
        uint32_t hit_events = 0u;
        int entered = 0;
        int entry_flag_seen = 0;
        int final_inside = 0;
        int ball_roll_seen = 0;
        double max_excess = 0.0;

        odwd_config_defaults(&config);
        config.seed = seeds[seed_index];
        config.rival_count = 5u;
        config.world_mode = ODWD_MODE_OPEN_FIELD;
        CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
        if (seed_index == 0u)
            CHECK(odwd_engine_init(twin.bytes, sizeof(twin.bytes), &config) == ODWD_OK);

        for (tick = 0u; tick < 1200u && !entered; ++tick) {
            point_follow_input(&storage, 160.0, 112.0, &input);
            CHECK(odwd_engine_step(storage.bytes, &input, &frame) == ODWD_OK);
            if (seed_index == 0u) {
                CHECK(odwd_engine_step(twin.bytes, &input, &twin_frame) == ODWD_OK);
                CHECK(frame.deterministic_state_hash ==
                      twin_frame.deterministic_state_hash);
            }
            if (frame.activity_zone == ODWD_ACTIVITY_FOOTBALL) {
                entered = 1;
                entry_flag_seen =
                    (frame.event_flags & ODWD_EVENT_ACTIVITY_ZONE) != 0u;
            }
        }
        CHECK(entered && tick <= 1200u);
        CHECK(entry_flag_seen);
        for (index = 1u; index <= config.rival_count; ++index) {
            odwd_vehicle_snapshot rival;
            CHECK(odwd_engine_read_vehicle(storage.bytes, index, &rival) == ODWD_OK);
            CHECK(t_abs(rival.position_x - 160.0) <= 54.0);
            CHECK(t_abs(rival.position_z - 92.0) <= 35.0);
        }

        odwd_input_neutral(&input);
        input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
        input.brake = 1.0;
        for (tick = 0u; tick < 1200u; ++tick) {
            odwd_vehicle_snapshot player;
            CHECK(odwd_engine_step(storage.bytes, &input, &frame) == ODWD_OK);
            if (seed_index == 0u) {
                CHECK(odwd_engine_step(twin.bytes, &input, &twin_frame) == ODWD_OK);
                CHECK(frame.deterministic_state_hash ==
                      twin_frame.deterministic_state_hash);
            }
            CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &player) == ODWD_OK);
            if (player.speed_mps < 0.35 &&
                frame.activity_zone == ODWD_ACTIVITY_FOOTBALL) break;
        }
        CHECK(frame.activity_zone == ODWD_ACTIVITY_FOOTBALL);
        score_left = frame.football_score_left;
        score_right = frame.football_score_right;
        previous_left = score_left;
        previous_right = score_right;

        for (tick = 0u; tick < 14400u; ++tick) {
            CHECK(odwd_engine_step(storage.bytes, &input, &frame) == ODWD_OK);
            CHECK(frame.activity_zone == ODWD_ACTIVITY_FOOTBALL);
            CHECK(frame.football_score_left >= previous_left);
            CHECK(frame.football_score_right >= previous_right);
            if (frame.event_flags & ODWD_EVENT_GOAL) ++goal_events;
            if (frame.event_flags & ODWD_EVENT_BALL_HIT) ++hit_events;
            if ((tick % 30u) == 0u ||
                (frame.event_flags & ODWD_EVENT_BALL_HIT) != 0u) {
                odwd_world_prop_snapshot ball_prop;
                uint32_t ball_index = odwd_engine_world_prop_count(storage.bytes) - 1u;
                CHECK(odwd_engine_read_world_prop(storage.bytes, ball_index,
                                                  &ball_prop) == ODWD_OK);
                CHECK(ball_prop.type == ODWD_PROP_BALL);
                if (t_abs(ball_prop.rotation_rad) > 0.01) {
                    double axis_norm = t_sqrt(ball_prop.extent_x_m * ball_prop.extent_x_m +
                                              ball_prop.extent_z_m * ball_prop.extent_z_m);
                    CHECK(axis_norm > 0.90 && axis_norm < 1.10);
                    ball_roll_seen = 1;
                }
            }
            previous_left = frame.football_score_left;
            previous_right = frame.football_score_right;
            if (seed_index == 0u) {
                CHECK(odwd_engine_step(twin.bytes, &input, &twin_frame) == ODWD_OK);
                CHECK(frame.deterministic_state_hash ==
                      twin_frame.deterministic_state_hash);
                CHECK(frame.event_flags == twin_frame.event_flags);
                CHECK(frame.football_score_left ==
                      twin_frame.football_score_left);
                CHECK(frame.football_score_right ==
                      twin_frame.football_score_right);
            }
            for (index = 1u; index <= config.rival_count; ++index) {
                odwd_vehicle_snapshot rival;
                double excess_x;
                double excess_z;
                double excess;
                CHECK(odwd_engine_read_vehicle(storage.bytes, index,
                                               &rival) == ODWD_OK);
                excess_x = t_abs(rival.position_x - 160.0) - 54.0;
                excess_z = t_abs(rival.position_z - 92.0) - 35.0;
                excess = excess_x > excess_z ? excess_x : excess_z;
                if (excess > max_excess) max_excess = excess;
            }
        }
        CHECK(goal_events >= 1u);
        CHECK(hit_events >= 1u);
        CHECK(ball_roll_seen);
        CHECK(goal_events ==
              (frame.football_score_left - score_left) +
              (frame.football_score_right - score_right));
        CHECK(max_excess <= 5.0);
        for (index = 1u; index <= config.rival_count; ++index) {
            odwd_vehicle_snapshot rival;
            CHECK(odwd_engine_read_vehicle(storage.bytes, index, &rival) == ODWD_OK);
            if (t_abs(rival.position_x - 160.0) <= 54.0 &&
                t_abs(rival.position_z - 92.0) <= 35.0)
                ++final_inside;
        }
        CHECK(final_inside >= 4);
    }
}

static void test_mobile_control_response_contract(void) {
    odwd_storage storage;
    odwd_config config;
    odwd_input input;
    odwd_vehicle_snapshot car;
    double speed_before_brake;
    unsigned tick;

    odwd_config_defaults(&config);
    config.seed = UINT32_C(0x20a60819);
    config.rival_count = 0u;
    config.world_mode = ODWD_MODE_OPEN_FIELD;
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);

    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.throttle = 1.0;
    input.joystick_x = 1.0;
    CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
    /* A digital steering down event must move the physical rack on the very
     * first 120 Hz step; there is no heading-target prefilter anymore. */
    CHECK(car.steering_rad > 0.045);
    for (tick = 1u; tick < 24u; ++tick)
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
    CHECK(car.steering_rad > 0.48);
    CHECK(car.speed_mps > 1.55);

    /* Brake and opposite steering are simultaneous independent channels, not
     * mutually-exclusive UI actions. Both must take effect in the same burst. */
    for (tick = 0u; tick < 180u; ++tick) {
        input.joystick_x = 0.0;
        input.throttle = 1.0;
        input.brake = 0.0;
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    }
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
    speed_before_brake = car.speed_mps;
    input.throttle = 0.0;
    input.brake = 1.0;
    input.joystick_x = -1.0;
    for (tick = 0u; tick < 24u; ++tick)
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
    CHECK(car.steering_rad < -0.45);
    CHECK(car.speed_mps < speed_before_brake - 1.4);

    /* A mastered drift is not a punishment-only state. Sustained intentional
     * slip regenerates a bounded amount of turbo, giving the player a reason
     * to learn entry/hold/countersteer instead of avoiding DRIFT entirely. */
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.throttle = 1.0;
    for (tick = 0u; tick < 620u; ++tick)
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
    {
        double turbo_before = car.turbo_01;
        double max_drift = 0.0;
        input.joystick_x = 0.78;
        input.throttle = 0.72;
        input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS | ODWD_BUTTON_HANDBRAKE;
        for (tick = 0u; tick < 300u; ++tick) {
            CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
            CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
            if (car.drift_intensity > max_drift) max_drift = car.drift_intensity;
        }
        CHECK(max_drift > 0.20);
        CHECK(car.turbo_01 > turbo_before + 0.035);
        CHECK(car.turbo_01 <= 1.0);
    }
}

static void test_camera_rigs_and_idle_showcase_contract(void) {
    odwd_storage storage;
    odwd_config config;
    odwd_input input;
    odwd_camera_snapshot camera_a, camera_b;
    odwd_vehicle_snapshot car;
    uint32_t mode;
    unsigned tick;

    odwd_config_defaults(&config);
    config.seed = UINT32_C(0x0ca6e7a7);
    config.rival_count = 0u;
    config.world_mode = ODWD_MODE_OPEN_FIELD;

    /* Mode selection itself is immediate. Camera smoothing applies to pose,
     * never to the requested mode identity. */
    for (mode = 0u; mode < ODWD_CAMERA_MODE_COUNT; ++mode) {
        CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
        odwd_input_neutral(&input);
        input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
        input.camera_mode = mode;
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
        CHECK(odwd_engine_read_camera(storage.bytes, &camera_a) == ODWD_OK);
        CHECK(camera_a.camera_mode == mode);
    }

    /* Camera 7 in the 1-based UI is the close roof-follow rig. It must stay
     * behind/above the car with a forward-looking pitch, never a ground shot. */
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.camera_mode = ODWD_CAMERA_ROOF;
    input.throttle = 0.62;
    for (tick = 0u; tick < 240u; ++tick)
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_camera(storage.bytes, &camera_a) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
    {
        double dx = camera_a.target_x - camera_a.position_x;
        double dz = camera_a.target_z - camera_a.position_z;
        double horizontal = t_sqrt(dx * dx + dz * dz);
        double vertical = t_abs(camera_a.target_y - camera_a.position_y);
        CHECK(camera_a.camera_mode == ODWD_CAMERA_ROOF);
        CHECK(camera_a.pitch_rad > 0.18 && camera_a.pitch_rad < 0.58);
        CHECK(horizontal > 3.2);
        CHECK(vertical / horizontal < 0.70);
        CHECK(camera_a.target_y > car.position_y + 0.55);
    }

    /* DIRECTOR (camera 8 in 1-based UI, formerly the broken final mode) must
     * frame the car, not collapse into a near-vertical ground shot. */
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.camera_mode = ODWD_CAMERA_DIRECTOR;
    input.throttle = 0.58;
    for (tick = 0u; tick < 240u; ++tick)
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_camera(storage.bytes, &camera_a) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
    {
        double dx = camera_a.target_x - camera_a.position_x;
        double dz = camera_a.target_z - camera_a.position_z;
        double horizontal = t_sqrt(dx * dx + dz * dz);
        double vertical = t_abs(camera_a.target_y - camera_a.position_y);
        CHECK(camera_a.pitch_rad > 0.10 && camera_a.pitch_rad < 0.78);
        CHECK(camera_a.target_y > car.position_y + 0.42);
        CHECK(horizontal > 3.0);
        CHECK(vertical / horizontal < 1.10);
    }

    /* Raw look input changes view heading in the first tick; no slow camera
     * prefilter is allowed between a finger drag and the authoritative rig. */
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    CHECK(odwd_engine_read_camera(storage.bytes, &camera_a) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS | ODWD_BUTTON_CAMERA_HOLD;
    input.look_dx = 0.80;
    CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_camera(storage.bytes, &camera_b) == ODWD_OK);
    CHECK(t_abs(camera_b.forward_yaw_rad - camera_a.forward_yaw_rad) > 0.42);

    /* When genuinely parked, the chase rig becomes a moving showcase. The car
     * remains still while the camera changes viewpoint over several shots. */
    CHECK(odwd_engine_init(storage.bytes, sizeof(storage.bytes), &config) == ODWD_OK);
    odwd_input_neutral(&input);
    input.buttons = ODWD_BUTTON_EXPLICIT_PEDALS;
    input.camera_mode = ODWD_CAMERA_CHASE;
    for (tick = 0u; tick < 420u; ++tick)
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_camera(storage.bytes, &camera_a) == ODWD_OK);
    CHECK(odwd_engine_read_vehicle(storage.bytes, 0u, &car) == ODWD_OK);
    CHECK(car.speed_mps < 0.05);
    for (tick = 0u; tick < 480u; ++tick)
        CHECK(odwd_engine_step(storage.bytes, &input, NULL) == ODWD_OK);
    CHECK(odwd_engine_read_camera(storage.bytes, &camera_b) == ODWD_OK);
    CHECK(t_abs(camera_b.position_x - camera_a.position_x) +
          t_abs(camera_b.position_z - camera_a.position_z) > 1.2);
}

int main(void) {
    test_abi_and_validation();
    test_repeatability();
    test_camera_relative_drive_and_drift();
    test_mobile_control_response_contract();
    test_camera_rigs_and_idle_showcase_contract();
    test_stream_checkpoint_respawn_and_branches();
    test_ai_rubber_band_has_no_pose_teleport();
    test_v3_controls_world_and_music();
    test_action_sector_jump_and_landing();
    test_branch_route_transition_is_ground_continuous();
    test_v4_survival_mode();
    test_open_football_public_activity();
    if (failures) {
        fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return 1;
    }
    puts("ODWD core tests: PASS");
    return 0;
}
