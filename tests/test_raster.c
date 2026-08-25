#include "odg_internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    OdgRaster raster;
    OdgEngine engine;
    OdgRenderSnapshot snap;
    uint64_t hash_a;
    uint64_t hash_b;
    (void)memset(&raster, 0, sizeof(raster));
    odg_engine_init(&engine);
    assert(odg_raster_init(&raster, 240u, 320u));
    odg_engine_make_render_snapshot(&engine, 1u, &snap);
    odg_raster_render(&raster, &snap);
    hash_a = odg_raster_hash(&raster);
    assert(hash_a != 0u);
    engine.player.camera_yaw = 0.6f;
    odg_engine_make_render_snapshot(&engine, 2u, &snap);
    odg_raster_render(&raster, &snap);
    hash_b = odg_raster_hash(&raster);
    assert(hash_b != 0u && hash_b != hash_a);
    assert(odg_raster_resize(&raster, 320u, 240u));
    assert(raster.width == 320u && raster.height == 240u);
    odg_raster_destroy(&raster);
    puts("software raster: OK");
    return 0;
}
