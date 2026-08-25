#define _POSIX_C_SOURCE 200809L
#include "odpar/odg_host.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void sleep_ms(long ms) {
    struct timespec req;
    req.tv_sec = ms / 1000L;
    req.tv_nsec = (ms % 1000L) * 1000000L;
    (void)nanosleep(&req, NULL);
}

int main(void) {
    OdgServiceConfig config;
    OdgEngineService *service;
    OdgInputFrame input;
    OdgUiSnapshot a;
    OdgUiSnapshot b;
    OdgUiSnapshot c;
    (void)memset(&config, 0, sizeof(config));
    config.struct_size = (uint32_t)sizeof(config);
    config.abi_version = ODG_HOST_ABI_VERSION;
    config.render_width = 240u;
    config.render_height = 320u;
    assert(odg_host_abi_version() == ODG_HOST_ABI_VERSION);
    service = odg_service_create(&config);
    assert(service != NULL);
    assert(odg_service_start(service) == ODG_STATUS_OK);
    sleep_ms(45L);
    assert(odg_service_copy_ui_snapshot(service, &a) == ODG_STATUS_OK);

    (void)memset(&input, 0, sizeof(input));
    input.struct_size = (uint32_t)sizeof(input);
    input.abi_version = ODG_HOST_ABI_VERSION;
    input.sequence = 1u;
    input.move_forward_q15 = INT16_C(32767);
    input.look_pitch_q15 = INT16_C(1200);
    assert(odg_service_submit_input(service, &input) == ODG_STATUS_OK);
    sleep_ms(120L);
    assert(odg_service_copy_ui_snapshot(service, &b) == ODG_STATUS_OK);
    assert(b.sequence > a.sequence);
    assert(b.player_z > a.player_z);
    assert(b.camera_pitch > a.camera_pitch);
    assert(b.sim_p99_us < 5000u); /* baseline host canary, far below mobile budget */
    assert(b.sim_max_us >= b.sim_p99_us);
    assert(b.render_max_us >= b.render_p99_us);
    assert(b.render_width == 240u && b.render_height == 320u);

    /* No new host packet arrives: after the stale-input deadline the engine
       must neutralize continuous movement rather than walking forever. */
    sleep_ms(450L);
    assert(odg_service_copy_ui_snapshot(service, &c) == ODG_STATUS_OK);
    assert(c.player_speed < 0.05f);
    assert(c.input_age_us >= 250000u);

    assert(odg_service_set_render_extent(service, 320u, 240u) == ODG_STATUS_OK);
    sleep_ms(40L);
    assert(odg_service_copy_ui_snapshot(service, &b) == ODG_STATUS_OK);
    assert(b.render_width == 320u && b.render_height == 240u);
    odg_service_stop(service);
    odg_service_destroy(service);
    puts("engine service: OK");
    return 0;
}
