#include "odg_internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static OdgConsumedInput zero_input(void) {
    OdgConsumedInput i;
    (void)memset(&i, 0, sizeof(i));
    return i;
}

int main(void) {
    OdgEngine e;
    OdgConsumedInput i = zero_input();
    float before;
    float yaw_before;
    uint32_t n;
    odg_engine_init(&e);

    before = e.player.camera_pitch;
    i.look_pitch_delta = 0.10f;
    odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.camera_pitch > before); /* positive means LOOK UP */

    i = zero_input();
    before = e.player.camera_pitch;
    odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(fabsf(e.player.camera_pitch - before) < 0.00001f); /* no auto-return */

    i.look_pitch_delta = 2.0f;
    for (n = 0u; n < 10u; ++n) odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.camera_pitch <= 1.1301f);

    i = zero_input();
    yaw_before = e.player.camera_yaw;
    i.look_yaw_delta = 0.10f;
    odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.camera_yaw > yaw_before);

    /* A high upward pitch orbits the chase eye downward. Ground is part of
       camera collision authority, so the eye may not tunnel below y=0.18. */
    odg_engine_init(&e);
    e.player.camera_pitch = 1.0f;
    e.player.camera_distance = odg_world_camera_distance(&e.player, 4.2f);
    assert(e.player.camera_distance < 4.2f);
    assert(1.35f - sinf(e.player.camera_pitch) * e.player.camera_distance >= 0.17f);

    /* Camera collision: put the player in front of the first fixed obstacle and look away
       so the desired chase eye would cross the obstacle. The resolver must shorten it. */
    e.player.x = -2.75f;
    e.player.z = 7.15f;
    e.player.y = 0.0f;
    e.player.camera_yaw = 0.0f;
    e.player.camera_pitch = 0.0f;
    {
        const float collision_limit = odg_world_camera_distance(&e.player, 4.2f);
        assert(collision_limit < 4.2f);
        assert(collision_limit >= 0.65f);
        e.player.camera_distance = 4.2f;
        i = zero_input();
        odg_engine_step(&e, &i, 1.0f / 60.0f);
        assert(e.player.camera_distance <= collision_limit + 0.0001f);
    }

    /* Once collision clears, distance must recover continuously rather than
       snapping from the shortened chase distance back to 4.2 m. */
    e.player.x = 0.0f;
    e.player.z = 0.0f;
    e.player.camera_yaw = 0.0f;
    e.player.camera_pitch = 0.0f;
    e.player.camera_distance = 1.0f;
    e.player.camera_distance_velocity = 0.0f;
    i = zero_input();
    odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.camera_distance > 1.0f);
    assert(e.player.camera_distance < 4.2f);
    for (n = 0u; n < 90u; ++n) odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.camera_distance > 4.15f);
    assert(e.player.camera_distance <= 4.2f);

    puts("camera canary: OK");
    return 0;
}
