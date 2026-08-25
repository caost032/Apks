#include "odg_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t clamp_render_extent(uint32_t value, uint32_t fallback, uint32_t lo, uint32_t hi) {
    if (value == 0u) value = fallback;
    if (value < lo) value = lo;
    if (value > hi) value = hi;
    return value;
}

static float input_q15(int16_t value) {
    return odg_clampf((float)value / 32767.0f, -1.0f, 1.0f);
}

static OdgConsumedInput consume_input(OdgEngineService *service) {
    OdgConsumedInput out;
    (void)memset(&out, 0, sizeof(out));
    (void)pthread_mutex_lock(&service->input_mu);
    out.move_x = service->input.move_x;
    out.move_forward = service->input.move_forward;
    out.look_yaw_delta = service->input.look_yaw_accum;
    out.look_pitch_delta = service->input.look_pitch_accum;
    out.buttons_pressed = service->input.buttons_pressed;
    out.buttons_held = service->input.buttons_held;
    out.submitted_ns = service->input.submitted_ns;
    out.sequence = service->input.sequence;
    service->input.look_yaw_accum = 0.0f;
    service->input.look_pitch_accum = 0.0f;
    service->input.buttons_pressed = 0u;
    (void)pthread_mutex_unlock(&service->input_mu);
    return out;
}

static void publish_snapshot(OdgEngineService *service, uint64_t now_ns, const OdgConsumedInput *input) {
    OdgUiSnapshot ui;
    OdgRenderSnapshot render;
    const OdgPlayerState *p = &service->engine.player;
    (void)memset(&ui, 0, sizeof(ui));
    ui.struct_size = (uint32_t)sizeof(ui);
    ui.abi_version = ODG_HOST_ABI_VERSION;
    ui.sequence = service->engine.simulation_step;
    ui.simulation_step = service->engine.simulation_step;
    ui.published_ns = now_ns;
    ui.player_x = p->x;
    ui.player_y = p->y;
    ui.player_z = p->z;
    ui.player_speed = hypotf(p->vx, p->vz);
    ui.player_facing_yaw = p->facing_yaw;
    ui.camera_yaw = p->camera_yaw;
    ui.camera_pitch = p->camera_pitch;
    ui.camera_distance = p->camera_distance;
    ui.grounded = p->grounded;
    ui.overload_count = service->engine.overload_count;
    if (input != NULL && input->submitted_ns != 0u && now_ns >= input->submitted_ns) {
        uint64_t age_us = (now_ns - input->submitted_ns) / UINT64_C(1000);
        ui.input_age_us = age_us > UINT32_MAX ? UINT32_MAX : (uint32_t)age_us;
    }
    (void)pthread_mutex_lock(&service->render_cfg_mu);
    ui.render_width = service->requested_render_width;
    ui.render_height = service->requested_render_height;
    (void)pthread_mutex_unlock(&service->render_cfg_mu);
    odg_engine_make_render_snapshot(&service->engine, ui.sequence, &render);
    (void)pthread_mutex_lock(&service->snapshot_mu);
    service->ui_snapshot = ui;
    service->render_snapshot = render;
    (void)pthread_mutex_unlock(&service->snapshot_mu);
}

static void *simulation_main(void *arg) {
    OdgEngineService *service = (OdgEngineService *)arg;
    uint64_t next_ns = odg_monotonic_ns() + ODG_SIM_STEP_NS;
    const float dt = 1.0f / (float)ODG_SIM_HZ;
    while (!atomic_load_explicit(&service->stop_requested, memory_order_acquire)) {
        uint64_t now;
        uint64_t begin_ns;
        uint64_t end_ns;
        uint64_t elapsed_us;
        OdgConsumedInput input;
        odg_sleep_until_ns(next_ns);
        now = odg_monotonic_ns();
        if (now > next_ns + ODG_SIM_STEP_NS * UINT64_C(2)) {
            service->engine.overload_count += 1u;
            next_ns = now + ODG_SIM_STEP_NS;
        } else {
            next_ns += ODG_SIM_STEP_NS;
        }
        input = consume_input(service);
        if (input.submitted_ns != 0u && now > input.submitted_ns &&
            now - input.submitted_ns > ODG_INPUT_STALE_NS) {
            /* A suspended/stalled host must never leave continuous movement,
               camera deltas, or held actions latched indefinitely. */
            input.move_x = 0.0f;
            input.move_forward = 0.0f;
            input.look_yaw_delta = 0.0f;
            input.look_pitch_delta = 0.0f;
            input.buttons_pressed = 0u;
            input.buttons_held = 0u;
        }
        begin_ns = odg_monotonic_ns();
        odg_engine_step(&service->engine, &input, dt);
        end_ns = odg_monotonic_ns();
        publish_snapshot(service, end_ns, &input);
        elapsed_us = end_ns >= begin_ns ? (end_ns - begin_ns) / UINT64_C(1000) : 0u;
        {
            const uint32_t sample_us = elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
            (void)pthread_mutex_lock(&service->perf_mu);
            odg_perf_push(&service->sim_perf, sample_us);
            if (sample_us > service->sim_max_us) service->sim_max_us = sample_us;
            if (sample_us > ODG_SIM_SPIKE_BUDGET_US) service->sim_spikes_over_5ms += 1u;
            (void)pthread_mutex_unlock(&service->perf_mu);
        }
    }
    return NULL;
}

static void *render_main(void *arg) {
    OdgEngineService *service = (OdgEngineService *)arg;
    uint64_t next_ns = odg_monotonic_ns() + ODG_SIM_STEP_NS;
    uint64_t last_sequence = UINT64_MAX;
    while (!atomic_load_explicit(&service->stop_requested, memory_order_acquire)) {
        OdgRenderSnapshot snapshot;
        uint32_t requested_w;
        uint32_t requested_h;
        uint64_t now_ns;
        uint64_t begin_ns;
        uint64_t end_ns;
        uint64_t elapsed_us;
        odg_sleep_until_ns(next_ns);
        now_ns = odg_monotonic_ns();
        if (now_ns > next_ns + ODG_SIM_STEP_NS * UINT64_C(2)) {
            /* Rendering has no debt: after a stall, resume from the newest
               snapshot instead of spinning through missed presentation slots. */
            next_ns = now_ns + ODG_SIM_STEP_NS;
        } else {
            next_ns += ODG_SIM_STEP_NS;
        }
        (void)pthread_mutex_lock(&service->snapshot_mu);
        snapshot = service->render_snapshot;
        (void)pthread_mutex_unlock(&service->snapshot_mu);
        (void)pthread_mutex_lock(&service->render_cfg_mu);
        requested_w = service->requested_render_width;
        requested_h = service->requested_render_height;
        (void)pthread_mutex_unlock(&service->render_cfg_mu);
        if (requested_w != service->raster.width || requested_h != service->raster.height) {
            if (!odg_raster_resize(&service->raster, requested_w, requested_h)) {
                (void)pthread_mutex_lock(&service->render_cfg_mu);
                service->requested_render_width = service->raster.width;
                service->requested_render_height = service->raster.height;
                (void)pthread_mutex_unlock(&service->render_cfg_mu);
            }
        }
        if (snapshot.sequence == last_sequence) continue;
        last_sequence = snapshot.sequence;
        begin_ns = odg_monotonic_ns();
        odg_raster_render(&service->raster, &snapshot);
        odg_android_present(service, &service->raster);
        end_ns = odg_monotonic_ns();
        elapsed_us = end_ns >= begin_ns ? (end_ns - begin_ns) / UINT64_C(1000) : 0u;
        {
            const uint32_t sample_us = elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
            (void)pthread_mutex_lock(&service->perf_mu);
            odg_perf_push(&service->render_perf, sample_us);
            if (sample_us > service->render_max_us) service->render_max_us = sample_us;
            if (sample_us > ODG_RENDER_SPIKE_BUDGET_US) service->render_spikes_over_16ms += 1u;
            (void)pthread_mutex_unlock(&service->perf_mu);
        }
    }
    return NULL;
}

OdgEngineService *odg_service_create_internal(const OdgServiceConfig *config) {
    OdgEngineService *service;
    uint32_t width;
    uint32_t height;
    if (config == NULL || config->struct_size != sizeof(*config) || config->abi_version != ODG_HOST_ABI_VERSION) {
        return NULL;
    }
    width = clamp_render_extent(config->render_width, ODG_DEFAULT_RENDER_W, ODG_RENDER_MIN_W, ODG_RENDER_MAX_W);
    height = clamp_render_extent(config->render_height, ODG_DEFAULT_RENDER_H, ODG_RENDER_MIN_H, ODG_RENDER_MAX_H);
    service = (OdgEngineService *)calloc(1u, sizeof(*service));
    if (service == NULL) return NULL;
    if (pthread_mutex_init(&service->input_mu, NULL) != 0) {
        free(service);
        return NULL;
    }
    if (pthread_mutex_init(&service->snapshot_mu, NULL) != 0) {
        (void)pthread_mutex_destroy(&service->input_mu);
        free(service);
        return NULL;
    }
    if (pthread_mutex_init(&service->perf_mu, NULL) != 0) {
        (void)pthread_mutex_destroy(&service->snapshot_mu);
        (void)pthread_mutex_destroy(&service->input_mu);
        free(service);
        return NULL;
    }
    if (pthread_mutex_init(&service->render_cfg_mu, NULL) != 0) {
        (void)pthread_mutex_destroy(&service->perf_mu);
        (void)pthread_mutex_destroy(&service->snapshot_mu);
        (void)pthread_mutex_destroy(&service->input_mu);
        free(service);
        return NULL;
    }
    if (pthread_mutex_init(&service->surface_mu, NULL) != 0) {
        (void)pthread_mutex_destroy(&service->render_cfg_mu);
        (void)pthread_mutex_destroy(&service->perf_mu);
        (void)pthread_mutex_destroy(&service->snapshot_mu);
        (void)pthread_mutex_destroy(&service->input_mu);
        free(service);
        return NULL;
    }
    if (!odg_raster_init(&service->raster, width, height)) {
        (void)pthread_mutex_destroy(&service->surface_mu);
        (void)pthread_mutex_destroy(&service->render_cfg_mu);
        (void)pthread_mutex_destroy(&service->perf_mu);
        (void)pthread_mutex_destroy(&service->snapshot_mu);
        (void)pthread_mutex_destroy(&service->input_mu);
        free(service);
        return NULL;
    }
    odg_engine_init(&service->engine);
    service->requested_render_width = width;
    service->requested_render_height = height;
    atomic_init(&service->running, false);
    atomic_init(&service->stop_requested, false);
    atomic_init(&service->ref_count, 1u);
    publish_snapshot(service, odg_monotonic_ns(), NULL);
    return service;
}

static void service_free_final(OdgEngineService *service) {
    odg_android_detach_surface(service);
    odg_raster_destroy(&service->raster);
    (void)pthread_mutex_destroy(&service->surface_mu);
    (void)pthread_mutex_destroy(&service->render_cfg_mu);
    (void)pthread_mutex_destroy(&service->perf_mu);
    (void)pthread_mutex_destroy(&service->snapshot_mu);
    (void)pthread_mutex_destroy(&service->input_mu);
    free(service);
}

void odg_service_retain_reference(OdgEngineService *service) {
    if (service == NULL) return;
    (void)atomic_fetch_add_explicit(&service->ref_count, 1u, memory_order_relaxed);
}

void odg_service_release_reference(OdgEngineService *service) {
    unsigned int previous;
    if (service == NULL) return;
    previous = atomic_fetch_sub_explicit(&service->ref_count, 1u, memory_order_acq_rel);
    if (previous == 1u) service_free_final(service);
}

void odg_service_destroy_internal(OdgEngineService *service) {
    if (service == NULL) return;
    /* The Dart/FFI owner is one reference. Android presentation may retain a
       second reference while a SurfaceProducer can issue lifecycle callbacks. */
    odg_service_stop_internal(service);
    odg_service_release_reference(service);
}

uint32_t odg_service_start_internal(OdgEngineService *service) {
    bool expected = false;
    if (service == NULL) return ODG_STATUS_INVALID_ARGUMENT;
    if (!atomic_compare_exchange_strong_explicit(&service->running, &expected, true,
                                                  memory_order_acq_rel, memory_order_acquire)) {
        return ODG_STATUS_OK;
    }
    atomic_store_explicit(&service->stop_requested, false, memory_order_release);
    if (pthread_create(&service->simulation_thread, NULL, simulation_main, service) != 0) {
        atomic_store_explicit(&service->running, false, memory_order_release);
        return ODG_STATUS_STATE;
    }
    if (pthread_create(&service->render_thread, NULL, render_main, service) != 0) {
        atomic_store_explicit(&service->stop_requested, true, memory_order_release);
        (void)pthread_join(service->simulation_thread, NULL);
        atomic_store_explicit(&service->running, false, memory_order_release);
        return ODG_STATUS_STATE;
    }
    return ODG_STATUS_OK;
}

void odg_service_stop_internal(OdgEngineService *service) {
    if (service == NULL) return;
    if (!atomic_load_explicit(&service->running, memory_order_acquire)) return;
    atomic_store_explicit(&service->stop_requested, true, memory_order_release);
    (void)pthread_join(service->simulation_thread, NULL);
    (void)pthread_join(service->render_thread, NULL);
    atomic_store_explicit(&service->running, false, memory_order_release);
}

uint32_t odg_service_submit_input_internal(OdgEngineService *service, const OdgInputFrame *frame) {
    uint64_t now;
    if (service == NULL || frame == NULL || frame->struct_size != sizeof(*frame) ||
        frame->abi_version != ODG_HOST_ABI_VERSION) {
        return ODG_STATUS_INVALID_ARGUMENT;
    }
    now = odg_monotonic_ns();
    (void)pthread_mutex_lock(&service->input_mu);
    if (frame->sequence > service->input.sequence) {
        service->input.move_x = input_q15(frame->move_x_q15);
        service->input.move_forward = input_q15(frame->move_forward_q15);
        service->input.look_yaw_accum = odg_clampf(service->input.look_yaw_accum + input_q15(frame->look_yaw_q15), -2.0f, 2.0f);
        service->input.look_pitch_accum = odg_clampf(service->input.look_pitch_accum + input_q15(frame->look_pitch_q15), -2.0f, 2.0f);
        service->input.buttons_pressed |= frame->buttons_pressed;
        service->input.buttons_held = frame->buttons_held;
        service->input.sequence = frame->sequence;
        service->input.submitted_ns = now;
    }
    (void)pthread_mutex_unlock(&service->input_mu);
    return ODG_STATUS_OK;
}

uint32_t odg_service_copy_ui_snapshot_internal(OdgEngineService *service, OdgUiSnapshot *out_snapshot) {
    OdgUiSnapshot out;
    uint64_t now;
    if (service == NULL || out_snapshot == NULL) return ODG_STATUS_INVALID_ARGUMENT;
    (void)pthread_mutex_lock(&service->snapshot_mu);
    out = service->ui_snapshot;
    (void)pthread_mutex_unlock(&service->snapshot_mu);
    {
        OdgPerfRing sim_perf;
        OdgPerfRing render_perf;
        uint32_t sim_max_us;
        uint32_t render_max_us;
        uint32_t sim_spikes;
        uint32_t render_spikes;
        (void)pthread_mutex_lock(&service->perf_mu);
        sim_perf = service->sim_perf;
        render_perf = service->render_perf;
        sim_max_us = service->sim_max_us;
        render_max_us = service->render_max_us;
        sim_spikes = service->sim_spikes_over_5ms;
        render_spikes = service->render_spikes_over_16ms;
        (void)pthread_mutex_unlock(&service->perf_mu);
        out.sim_p50_us = odg_perf_quantile(&sim_perf, 50u);
        out.sim_p95_us = odg_perf_quantile(&sim_perf, 95u);
        out.sim_p99_us = odg_perf_quantile(&sim_perf, 99u);
        out.sim_max_us = sim_max_us;
        out.sim_spikes_over_5ms = sim_spikes;
        out.render_p50_us = odg_perf_quantile(&render_perf, 50u);
        out.render_p95_us = odg_perf_quantile(&render_perf, 95u);
        out.render_p99_us = odg_perf_quantile(&render_perf, 99u);
        out.render_max_us = render_max_us;
        out.render_spikes_over_16ms = render_spikes;
    }
    now = odg_monotonic_ns();
    (void)pthread_mutex_lock(&service->input_mu);
    if (service->input.submitted_ns != 0u && now >= service->input.submitted_ns) {
        uint64_t age_us = (now - service->input.submitted_ns) / UINT64_C(1000);
        out.input_age_us = age_us > UINT32_MAX ? UINT32_MAX : (uint32_t)age_us;
    }
    (void)pthread_mutex_unlock(&service->input_mu);
    *out_snapshot = out;
    return ODG_STATUS_OK;
}

uint32_t odg_service_set_render_extent_internal(OdgEngineService *service, uint32_t width, uint32_t height) {
    if (service == NULL) return ODG_STATUS_INVALID_ARGUMENT;
    width = clamp_render_extent(width, ODG_DEFAULT_RENDER_W, ODG_RENDER_MIN_W, ODG_RENDER_MAX_W);
    height = clamp_render_extent(height, ODG_DEFAULT_RENDER_H, ODG_RENDER_MIN_H, ODG_RENDER_MAX_H);
    (void)pthread_mutex_lock(&service->render_cfg_mu);
    service->requested_render_width = width;
    service->requested_render_height = height;
    (void)pthread_mutex_unlock(&service->render_cfg_mu);
    return ODG_STATUS_OK;
}
