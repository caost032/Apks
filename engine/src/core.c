#define _POSIX_C_SOURCE 200809L
#include "odg_internal.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>

uint64_t odg_monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return UINT64_C(0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

void odg_sleep_until_ns(uint64_t deadline_ns) {
    for (;;) {
        uint64_t now = odg_monotonic_ns();
        if (now == 0u || now >= deadline_ns) return;
        uint64_t remain = deadline_ns - now;
        struct timespec req;
        req.tv_sec = (time_t)(remain / UINT64_C(1000000000));
        req.tv_nsec = (long)(remain % UINT64_C(1000000000));
        if (nanosleep(&req, &req) == 0) return;
        if (errno != EINTR) return;
    }
}

float odg_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float odg_wrap_pi(float v) {
    const float two_pi = 2.0f * ODG_PI_F;
    while (v > ODG_PI_F) v -= two_pi;
    while (v < -ODG_PI_F) v += two_pi;
    return v;
}

float odg_approach(float current, float target, float max_delta) {
    float delta = target - current;
    if (delta > max_delta) return current + max_delta;
    if (delta < -max_delta) return current - max_delta;
    return target;
}

float odg_angle_approach(float current, float target, float max_delta) {
    float delta = odg_wrap_pi(target - current);
    if (delta > max_delta) delta = max_delta;
    if (delta < -max_delta) delta = -max_delta;
    return odg_wrap_pi(current + delta);
}

void odg_perf_push(OdgPerfRing *ring, uint32_t sample_us) {
    if (ring == NULL) return;
    ring->samples[ring->cursor] = sample_us;
    ring->cursor = (ring->cursor + 1u) % ODG_PERF_RING_CAP;
    if (ring->count < ODG_PERF_RING_CAP) ring->count += 1u;
}

static void sort_u32(uint32_t *values, uint32_t count) {
    uint32_t i;
    for (i = 1u; i < count; ++i) {
        uint32_t v = values[i];
        uint32_t j = i;
        while (j > 0u && values[j - 1u] > v) {
            values[j] = values[j - 1u];
            --j;
        }
        values[j] = v;
    }
}

uint32_t odg_perf_quantile(const OdgPerfRing *ring, uint32_t percentile) {
    uint32_t tmp[ODG_PERF_RING_CAP];
    uint32_t count;
    uint32_t i;
    uint32_t index;
    if (ring == NULL || ring->count == 0u) return 0u;
    count = ring->count;
    for (i = 0u; i < count; ++i) tmp[i] = ring->samples[i];
    sort_u32(tmp, count);
    if (percentile > 100u) percentile = 100u;
    index = (percentile * (count - 1u) + 50u) / 100u;
    return tmp[index];
}
