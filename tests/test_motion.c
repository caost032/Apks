#include "odg_internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static OdgConsumedInput input_forward(void) {
    OdgConsumedInput i;
    (void)memset(&i, 0, sizeof(i));
    i.move_forward = 1.0f;
    return i;
}

int main(void) {
    OdgEngine e;
    OdgConsumedInput i = input_forward();
    float yaw;
    uint32_t n;
    odg_engine_init(&e);
    yaw = e.player.camera_yaw;
    for (n = 0u; n < 60u; ++n) odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.z > 2.5f);
    assert(fabsf(e.player.x) < 0.1f);
    assert(fabsf(e.player.camera_yaw - yaw) < 0.00001f); /* joystick does not drag camera */

    odg_engine_init(&e);
    e.player.camera_yaw = ODG_PI_F * 0.5f;
    for (n = 0u; n < 40u; ++n) odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.x > 1.0f);
    assert(fabsf(e.player.z) < 0.15f);
    assert(fabsf(odg_wrap_pi(e.player.facing_yaw - ODG_PI_F * 0.5f)) < 0.15f);

    odg_engine_init(&e);
    (void)memset(&i, 0, sizeof(i));
    i.buttons_pressed = ODG_BUTTON_JUMP;
    odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.grounded == 0u);
    assert(e.player.y > 0.0f);
    i.buttons_pressed = 0u;
    for (n = 0u; n < 120u; ++n) odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.grounded == 1u);
    assert(fabsf(e.player.y) < 0.00001f);

    /* Collision has vertical extent: a grounded body cannot occupy a box,
       while feet above its top can traverse it. */
    assert(odg_world_player_position_valid(-2.75f, 0.0f, 5.9f) == 0);
    assert(odg_world_player_position_valid(-2.75f, 1.75f, 5.9f) != 0);

    /* Raised world geometry is real support. Jumping from it returns to that
       surface rather than falling through it to the global ground plane. */
    odg_engine_init(&e);
    e.player.x = -2.75f;
    e.player.z = 5.9f;
    e.player.y = 1.75f;
    e.player.grounded = 1u;
    (void)memset(&i, 0, sizeof(i));
    i.buttons_pressed = ODG_BUTTON_JUMP;
    odg_engine_step(&e, &i, 1.0f / 60.0f);
    i.buttons_pressed = 0u;
    for (n = 0u; n < 150u; ++n) odg_engine_step(&e, &i, 1.0f / 60.0f);
    assert(e.player.grounded == 1u);
    assert(fabsf(e.player.y - 1.75f) < 0.0001f);

    puts("motion canary: OK");
    return 0;
}
