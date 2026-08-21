#include "game_internal.h"

#if __STDC_HOSTED__
#include <stdlib.h>
#endif

void *odg_memset(void *dst, int value, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = (uint8_t)value;
    return dst;
}

void *odg_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

/* Freestanding wasm builds may lower structure operations to these symbols. */
void *memset(void *dst, int value, size_t n) { return odg_memset(dst, value, n); }
void *memcpy(void *dst, const void *src, size_t n) { return odg_memcpy(dst, src, n); }
void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        size_t i; for (i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
        size_t i = n; while (i != 0u) { --i; d[i] = s[i]; }
    }
    return dst;
}


/* Runtime allocation is an engine service rather than a direct libc dependency.
 * Native/Android builds use the host allocator. Freestanding WASM uses a deterministic
 * bump arena rooted at the linker-provided heap base and grows linear memory only when
 * necessary. Released blocks are reclaimed as a group on odg_init/reset; reserve paths
 * grow geometrically, so a running world does not accumulate one block per tick. */
#if __STDC_HOSTED__
void *odg_mem_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
void odg_mem_free(void *ptr) { free(ptr); }
void odg_mem_heap_reset(void) { }
#else
extern unsigned char __heap_base;

typedef struct odg_heap_block_header {
    size_t size;
} odg_heap_block_header;

static uintptr_t g_odg_heap_cursor = 0u;

static uintptr_t odg_align_up_uintptr(uintptr_t value, uintptr_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static int odg_wasm_ensure_memory(uintptr_t end_address) {
#if defined(__wasm__)
    const uintptr_t page_size = UINT32_C(65536);
    uintptr_t pages = (uintptr_t)__builtin_wasm_memory_size(0);
    uintptr_t bytes = pages * page_size;
    if (end_address > bytes) {
        uintptr_t missing = end_address - bytes;
        uintptr_t grow = (missing + page_size - 1u) / page_size;
        size_t previous = __builtin_wasm_memory_grow(0, (size_t)grow);
        if (previous == (size_t)-1) return 0;
    }
    return 1;
#else
    (void)end_address;
    return 0;
#endif
}

void odg_mem_heap_reset(void) {
    uintptr_t alignment = (uintptr_t)sizeof(uint64_t);
    g_odg_heap_cursor = odg_align_up_uintptr((uintptr_t)&__heap_base, alignment);
}

void *odg_mem_realloc(void *ptr, size_t size) {
    uintptr_t alignment = (uintptr_t)sizeof(uint64_t);
    uintptr_t header_addr;
    uintptr_t payload_addr;
    uintptr_t end_addr;
    odg_heap_block_header *header;
    size_t old_size = 0u;
    void *result;
    if (size == 0u) return NULL;
    if (ptr != NULL) {
        header = (odg_heap_block_header *)((uint8_t *)ptr - sizeof(odg_heap_block_header));
        old_size = header->size;
        if (old_size >= size) return ptr;
    }
    if (g_odg_heap_cursor == 0u) odg_mem_heap_reset();
    header_addr = odg_align_up_uintptr(g_odg_heap_cursor, alignment);
    payload_addr = header_addr + sizeof(odg_heap_block_header);
    if (payload_addr > UINTPTR_MAX - (uintptr_t)size) return NULL;
    end_addr = odg_align_up_uintptr(payload_addr + (uintptr_t)size, alignment);
    if (!odg_wasm_ensure_memory(end_addr)) return NULL;
    header = (odg_heap_block_header *)header_addr;
    header->size = size;
    result = (void *)payload_addr;
    if (ptr != NULL && old_size != 0u) odg_memcpy(result, ptr, old_size);
    g_odg_heap_cursor = end_addr;
    return result;
}

void odg_mem_free(void *ptr) {
    /* Individual frees are intentionally a no-op in the freestanding arena. The engine
     * releases every dynamic store before odg_mem_heap_reset() at the next odg_init. */
    (void)ptr;
}
#endif
