#include "odwd_render.h"
#include "odwd_simple.h"
#include "odwd_core.h"

#include <inttypes.h>
#include <stdio.h>

static int failures;

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                     \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures;                                                         \
    }                                                                       \
} while (0)

int main(void) {
    uint64_t first_hash;
    uint64_t moving_hash;
    uint32_t regression_probe;
    unsigned frame;
    CHECK(od_init(UINT32_C(0x574c4431), 1u) == 0);
    CHECK(od_render() != 0u);
    CHECK(od_framebuffer_width() == 640u);
    CHECK(od_framebuffer_height() == 360u);
    first_hash = od_framebuffer_hash();
    CHECK(first_hash != UINT64_C(0));
    regression_probe = od_renderer_regression_probe();
    if (regression_probe != 15u)
        fprintf(stderr, "renderer probe mask=%u\n", regression_probe);
    CHECK(regression_probe == 15u);
    CHECK(od_render() != 0u);
    for (frame = 0u; frame < 120u; ++frame) {
        od_set_input(frame < 70u ? 0.12f : -0.28f, 1.0f,
                     frame > 72u && frame < 82u ? 0.04f : 0.0f,
                     0.0f, frame > 72u && frame < 90u, 0);
        CHECK(od_advance(1.0 / 60.0) == 2u);
        CHECK(od_render() != 0u);
    }
    moving_hash = od_framebuffer_hash();
    CHECK(moving_hash != first_hash);
    CHECK(od_speed_kph() > 5.0);
    CHECK(od_racer_count() == 6u);
    CHECK(od_sector_index() >= 1u);
    CHECK(od_surface_grip() >= 0.0 && od_surface_grip() <= 1.0);
    od_set_quality(0u);
    CHECK(od_render() != 0u);
    CHECK(od_framebuffer_width() == 480u);
    CHECK(od_framebuffer_height() == 270u);
    od_set_quality(2u);
    CHECK(od_render() != 0u);
    CHECK(od_framebuffer_width() == 800u);
    CHECK(od_framebuffer_height() == 450u);
    od_set_view_orientation(1u);
    CHECK(od_render() != 0u);
    CHECK(od_framebuffer_width() == 450u);
    CHECK(od_framebuffer_height() == 800u);
    od_set_view_orientation(0u);
    od_reset_checkpoint();
    CHECK(od_advance(1.0 / 60.0) == 2u);
    CHECK((od_get_event_flags() & ODWD_EVENT_PLAYER_RESPAWN) != 0u);
    if (failures != 0) {
        fprintf(stderr, "%d render assertion(s) failed\n", failures);
        return 1;
    }
    printf("ODWD renderer tests: PASS hash=%" PRIx64 "\n", moving_hash);
    return 0;
}
