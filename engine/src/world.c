#include "odg_internal.h"

#include <math.h>

static const OdgWorldBox k_boxes[] = {
    {{-3.60f, 0.00f, 5.20f}, {-1.90f, 1.75f, 6.60f}, UINT32_C(0x6f7f63ff)},
    {{ 2.40f, 0.00f, 7.80f}, { 4.20f, 2.80f, 9.30f}, UINT32_C(0x88745cff)},
    {{-0.85f, 0.00f,11.20f}, { 0.85f, 0.95f,12.90f}, UINT32_C(0x6b766fff)}
};

const OdgWorldBox *odg_world_boxes(uint32_t *out_count) {
    if (out_count != NULL) *out_count = (uint32_t)(sizeof(k_boxes) / sizeof(k_boxes[0]));
    return k_boxes;
}

int odg_world_player_position_valid(float x, float y, float z) {
    uint32_t count = 0u;
    const OdgWorldBox *boxes = odg_world_boxes(&count);
    const float radius = 0.34f;
    const float collider_height = 1.80f;
    uint32_t i;
    for (i = 0u; i < count; ++i) {
        const int horizontal =
            x + radius > boxes[i].min.x && x - radius < boxes[i].max.x &&
            z + radius > boxes[i].min.z && z - radius < boxes[i].max.z;
        const int vertical =
            y < boxes[i].max.y - 0.001f && y + collider_height > boxes[i].min.y + 0.001f;
        if (horizontal != 0 && vertical != 0) return 0;
    }
    return 1;
}

float odg_world_ground_height(float x, float z) {
    uint32_t count = 0u;
    const OdgWorldBox *boxes = odg_world_boxes(&count);
    const float radius = 0.30f;
    float ground = 0.0f;
    uint32_t i;
    for (i = 0u; i < count; ++i) {
        if (x + radius > boxes[i].min.x && x - radius < boxes[i].max.x &&
            z + radius > boxes[i].min.z && z - radius < boxes[i].max.z &&
            boxes[i].max.y > ground) {
            ground = boxes[i].max.y;
        }
    }
    return ground;
}


static int segment_aabb(float ax, float ay, float az,
                        float bx, float by, float bz,
                        const OdgWorldBox *box, float inflate,
                        float *out_t) {
    float tmin = 0.0f;
    float tmax = 1.0f;
    const float a[3] = {ax, ay, az};
    const float d[3] = {bx - ax, by - ay, bz - az};
    const float mn[3] = {box->min.x - inflate, box->min.y - inflate, box->min.z - inflate};
    const float mx[3] = {box->max.x + inflate, box->max.y + inflate, box->max.z + inflate};
    uint32_t axis;
    for (axis = 0u; axis < 3u; ++axis) {
        if (fabsf(d[axis]) < 0.00001f) {
            if (a[axis] < mn[axis] || a[axis] > mx[axis]) return 0;
        } else {
            float inv = 1.0f / d[axis];
            float t1 = (mn[axis] - a[axis]) * inv;
            float t2 = (mx[axis] - a[axis]) * inv;
            if (t1 > t2) {
                float tmp = t1;
                t1 = t2;
                t2 = tmp;
            }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return 0;
        }
    }
    if (out_t != NULL) *out_t = tmin;
    return 1;
}

float odg_world_camera_distance(const OdgPlayerState *player, float desired_distance) {
    uint32_t count = 0u;
    const OdgWorldBox *boxes = odg_world_boxes(&count);
    const float cp = cosf(player->camera_pitch);
    const float sp = sinf(player->camera_pitch);
    const float sy = sinf(player->camera_yaw);
    const float cy = cosf(player->camera_yaw);
    const float anchor_x = player->x;
    const float anchor_y = player->y + 1.35f;
    const float anchor_z = player->z;
    const float fx = sy * cp;
    const float fy = sp;
    const float fz = cy * cp;
    const float eye_x = anchor_x - fx * desired_distance;
    const float eye_y = anchor_y - fy * desired_distance;
    const float eye_z = anchor_z - fz * desired_distance;
    float best_t = 1.0f;
    uint32_t i;
    if (eye_y < 0.18f) {
        const float denom = anchor_y - eye_y;
        if (denom > 0.00001f) {
            const float ground_t = (anchor_y - 0.18f) / denom;
            if (ground_t >= 0.0f && ground_t < best_t) best_t = ground_t;
        }
    }
    for (i = 0u; i < count; ++i) {
        float t = 1.0f;
        if (segment_aabb(anchor_x, anchor_y, anchor_z, eye_x, eye_y, eye_z,
                         &boxes[i], 0.22f, &t) && t < best_t) {
            best_t = t;
        }
    }
    if (best_t < 1.0f) {
        float distance = desired_distance * best_t - 0.08f;
        if (distance < 0.65f) distance = 0.65f;
        return distance;
    }
    return desired_distance;
}
