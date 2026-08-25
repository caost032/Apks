#include "odg_internal.h"

#include <math.h>
#include <string.h>

static float camera_recover_critically_damped(float current, float target, float *velocity, float dt) {
    const float smooth_time = 0.20f;
    const float omega = 2.0f / smooth_time;
    float x;
    float decay;
    float change;
    float temp;
    float next;
    if (velocity == NULL || dt <= 0.0f) return current;
    x = omega * dt;
    decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    change = current - target;
    temp = (*velocity + omega * change) * dt;
    *velocity = (*velocity - omega * temp) * decay;
    next = target + (change + temp) * decay;
    if (next > target) {
        next = target;
        *velocity = 0.0f;
    }
    return next;
}

void odg_engine_init(OdgEngine *engine) {
    if (engine == NULL) return;
    (void)memset(engine, 0, sizeof(*engine));
    engine->player.x = 0.0f;
    engine->player.y = 0.0f;
    engine->player.z = 0.0f;
    engine->player.facing_yaw = 0.0f;
    engine->player.camera_yaw = 0.0f;
    engine->player.camera_pitch = -0.10f;
    engine->player.camera_distance = 4.20f;
    engine->player.grounded = 1u;
}

static void move_axis_with_collision(OdgPlayerState *p, float dx, float dz) {
    float nx = p->x + dx;
    float nz = p->z + dz;
    if (odg_world_player_position_valid(nx, p->y, p->z)) p->x = nx;
    else p->vx = 0.0f;
    if (odg_world_player_position_valid(p->x, p->y, nz)) p->z = nz;
    else p->vz = 0.0f;
}

void odg_engine_step(OdgEngine *engine, const OdgConsumedInput *input, float dt) {
    OdgPlayerState *p;
    float mx;
    float mf;
    float mag;
    float sy;
    float cy;
    float desired_x;
    float desired_z;
    float desired_vx;
    float desired_vz;
    float accel;
    float old_x;
    float old_z;
    float traveled;
    const float max_speed = 4.8f;
    const float look_radians_per_normalized = 2.8f;
    if (engine == NULL || input == NULL || dt <= 0.0f) return;
    p = &engine->player;

    p->camera_yaw = odg_wrap_pi(p->camera_yaw + input->look_yaw_delta * look_radians_per_normalized);
    p->camera_pitch = odg_clampf(p->camera_pitch + input->look_pitch_delta * look_radians_per_normalized,
                                -1.22f, 1.13f);

    mx = odg_clampf(input->move_x, -1.0f, 1.0f);
    mf = odg_clampf(input->move_forward, -1.0f, 1.0f);
    mag = sqrtf(mx * mx + mf * mf);
    if (mag > 1.0f) {
        mx /= mag;
        mf /= mag;
        mag = 1.0f;
    }
    sy = sinf(p->camera_yaw);
    cy = cosf(p->camera_yaw);
    desired_x = cy * mx + sy * mf;
    desired_z = -sy * mx + cy * mf;
    desired_vx = desired_x * max_speed;
    desired_vz = desired_z * max_speed;
    accel = mag > 0.02f ? 18.0f : 24.0f;
    p->vx = odg_approach(p->vx, desired_vx, accel * dt);
    p->vz = odg_approach(p->vz, desired_vz, accel * dt);

    if ((input->buttons_pressed & ODG_BUTTON_JUMP) != 0u && p->grounded != 0u) {
        p->vy = 6.2f;
        p->grounded = 0u;
    }

    old_x = p->x;
    old_z = p->z;
    move_axis_with_collision(p, p->vx * dt, p->vz * dt);
    {
        const float support_y = odg_world_ground_height(p->x, p->z);
        if (p->grounded != 0u && p->y > support_y + 0.02f) {
            /* Walking off a raised support starts a real fall; grounded is not
               a magic state that holds the player in mid-air. */
            p->grounded = 0u;
        }
        if (p->grounded == 0u) p->vy -= 16.5f * dt;
        p->y += p->vy * dt;
        if (p->vy <= 0.0f && p->y <= support_y) {
            p->y = support_y;
            p->vy = 0.0f;
            p->grounded = 1u;
        }
    }

    traveled = hypotf(p->x - old_x, p->z - old_z);
    if (p->grounded != 0u) p->gait_distance += traveled;
    if (hypotf(p->vx, p->vz) > 0.18f) {
        float target_facing = atan2f(p->vx, p->vz);
        p->facing_yaw = odg_angle_approach(p->facing_yaw, target_facing, 9.42f * dt);
    }
    {
        const float camera_limit = odg_world_camera_distance(p, 4.20f);
        if (camera_limit < p->camera_distance) {
            /* Collision is a hard visibility constraint: never leave the eye
               behind solid geometry. Recovery after the obstacle clears is
               deliberately continuous and critically damped. */
            p->camera_distance = camera_limit;
            if (p->camera_distance_velocity > 0.0f) p->camera_distance_velocity = 0.0f;
        } else {
            p->camera_distance = camera_recover_critically_damped(
                p->camera_distance, camera_limit, &p->camera_distance_velocity, dt);
        }
    }
    engine->simulation_step += 1u;
}

void odg_engine_make_render_snapshot(const OdgEngine *engine, uint64_t sequence, OdgRenderSnapshot *out) {
    if (engine == NULL || out == NULL) return;
    out->sequence = sequence;
    out->simulation_step = engine->simulation_step;
    out->player = engine->player;
}
