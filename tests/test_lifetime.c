#define _POSIX_C_SOURCE 200809L
#include "odg_internal.h"

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
    unsigned int iteration;
    for (iteration = 0u; iteration < 16u; ++iteration) {
        OdgServiceConfig config;
        OdgEngineService *service;
        OdgUiSnapshot before_destroy;
        OdgUiSnapshot retained_snapshot;

        (void)memset(&config, 0, sizeof(config));
        config.struct_size = (uint32_t)sizeof(config);
        config.abi_version = ODG_HOST_ABI_VERSION;
        config.render_width = 128u;
        config.render_height = 128u;

        service = odg_service_create_internal(&config);
        assert(service != NULL);
        assert(atomic_load_explicit(&service->ref_count, memory_order_acquire) == 1u);

        /* Model Android SurfaceProducer ownership: its callback lifetime retains
           the service independently from the Dart/FFI owner. */
        odg_service_retain_reference(service);
        assert(atomic_load_explicit(&service->ref_count, memory_order_acquire) == 2u);
        assert(odg_service_start_internal(service) == ODG_STATUS_OK);
        sleep_ms(35L);
        assert(odg_service_copy_ui_snapshot_internal(service, &before_destroy) == ODG_STATUS_OK);

        /* Releasing the Dart owner must stop workers, but cannot free memory while
           Android still owns its retained callback reference. */
        odg_service_destroy_internal(service);
        assert(atomic_load_explicit(&service->ref_count, memory_order_acquire) == 1u);
        assert(!atomic_load_explicit(&service->running, memory_order_acquire));
        assert(odg_service_copy_ui_snapshot_internal(service, &retained_snapshot) == ODG_STATUS_OK);
        assert(retained_snapshot.sequence >= before_destroy.sequence);

        /* Final Android release is the only operation allowed after this point. */
        odg_service_release_reference(service);
    }
    puts("service retained lifetime: OK");
    return 0;
}
