#ifndef ODWD_RENDER_H
#define ODWD_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The renderer is owned by C and writes a complete RGBA framebuffer.
 * Web and Flutter are thin presentation adapters over these calls.
 */
uint32_t od_render(void);
/* Presentation orientation only; it swaps render dimensions, never physics. */
void od_set_view_orientation(uint32_t portrait);
uint32_t od_framebuffer_ptr(void);
uint32_t od_framebuffer_width(void);
uint32_t od_framebuffer_height(void);
uint64_t od_framebuffer_hash(void);
const uint32_t *od_framebuffer_data(void);

/* Native adapter hook. The framebuffer is shared and valid until the next
 * render call; simulation state remains independently owned by the caller. */
uint32_t od_renderer_render_storage(const void *storage, uint32_t quality);
void od_renderer_set_context(uint32_t context_id);
/* Native-only regression probe: handedness, near clipping and depth ordering. */
uint32_t od_renderer_regression_probe(void);

#ifdef __cplusplus
}
#endif

#endif /* ODWD_RENDER_H */
