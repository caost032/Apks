#include "odg_internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    OdgPerfRing ring;
    uint32_t i;
    (void)memset(&ring, 0, sizeof(ring));
    for (i = 1u; i <= 100u; ++i) odg_perf_push(&ring, i);
    assert(odg_perf_quantile(&ring, 50u) >= 49u && odg_perf_quantile(&ring, 50u) <= 51u);
    assert(odg_perf_quantile(&ring, 95u) >= 94u && odg_perf_quantile(&ring, 95u) <= 96u);
    assert(odg_perf_quantile(&ring, 99u) >= 98u);
    puts("perf ring: OK");
    return 0;
}
