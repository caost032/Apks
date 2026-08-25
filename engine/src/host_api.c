#include "odg_internal.h"

uint32_t odg_host_abi_version(void) {
    return ODG_HOST_ABI_VERSION;
}

OdgEngineService *odg_service_create(const OdgServiceConfig *config) {
    return odg_service_create_internal(config);
}

void odg_service_destroy(OdgEngineService *service) {
    odg_service_destroy_internal(service);
}

uint32_t odg_service_start(OdgEngineService *service) {
    return odg_service_start_internal(service);
}

void odg_service_stop(OdgEngineService *service) {
    odg_service_stop_internal(service);
}

uint32_t odg_service_submit_input(OdgEngineService *service, const OdgInputFrame *frame) {
    return odg_service_submit_input_internal(service, frame);
}

uint32_t odg_service_copy_ui_snapshot(OdgEngineService *service, OdgUiSnapshot *out_snapshot) {
    return odg_service_copy_ui_snapshot_internal(service, out_snapshot);
}

uint32_t odg_service_set_render_extent(OdgEngineService *service, uint32_t width, uint32_t height) {
    return odg_service_set_render_extent_internal(service, width, height);
}
