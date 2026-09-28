#include <assert.h>
#include <string.h>
#include "../kernel/dma.h"

static int maps = 0;
static int unmaps = 0;
static int fail_map = 0;
static int fail_unmap = 0;
static uint64_t force_bus = 0;

static int test_map_cb(void *ctx, uint64_t phys, uint64_t len, uint8_t dir, uint64_t *bus) {
    (void)ctx;
    (void)len;
    (void)dir;
    maps++;
    if (fail_map)
        return -1;
    if (force_bus)
        *bus = force_bus;
    else
        *bus = phys + 0x1000;
    return 0;
}

static int test_unmap_cb(void *ctx, uint64_t bus, uint64_t len, uint8_t dir) {
    (void)ctx;
    (void)bus;
    (void)len;
    (void)dir;
    unmaps++;
    return fail_unmap ? -1 : 0;
}

static void test_dma_init(void) {
    struct dma_owner o;
    assert(dma_owner_init(0, 1, 0, test_map_cb, test_unmap_cb) == -1);
    assert(dma_owner_init(&o, 0, 0, test_map_cb, test_unmap_cb) == -1);
    assert(dma_owner_init(&o, 1, 0, 0, test_unmap_cb) == -1);
    assert(dma_owner_init(&o, 1, 0, test_map_cb, 0) == -1);

    assert(dma_owner_init(&o, 1, 0, test_map_cb, test_unmap_cb) == 0);
    assert(o.id == 1);
    assert(o.alive == 1);
    assert(o.next_generation == 1);
}

static void test_dma_map_bounds(void) {
    struct dma_owner o;
    struct dma_mapping m;

    assert(dma_owner_init(&o, 7, 0, test_map_cb, test_unmap_cb) == 0);

    /* NULL owner or output */
    assert(dma_map(0, 0x2000, 4096, DMA_BIDIRECTIONAL, &m) == -1);
    assert(dma_map(&o, 0x2000, 4096, DMA_BIDIRECTIONAL, 0) == -1);

    /* Length 0 */
    assert(dma_map(&o, 0x2000, 0, DMA_BIDIRECTIONAL, &m) == -1);

    /* Physical address overflow: p + n wraps */
    assert(dma_map(&o, ~0ULL - 10, 20, DMA_BIDIRECTIONAL, &m) == -1);

    /* Invalid directions */
    assert(dma_map(&o, 0x2000, 4096, 0, &m) == -1);
    assert(dma_map(&o, 0x2000, 4096, 4, &m) == -1);

    /* Normal mapping */
    assert(dma_map(&o, 0x2000, 4096, DMA_BIDIRECTIONAL, &m) == 0);
    assert(m.active == 1);
    assert(m.bus_address == 0x3000);
    assert(m.physical_address == 0x2000);
    assert(m.length == 4096);
    assert(m.direction == DMA_BIDIRECTIONAL);
    assert(m.owner_id == 7);

    /* Destroy fails while mapping active */
    assert(dma_owner_destroy(&o) == -2);

    /* Unmap */
    struct dma_mapping stale = m;
    assert(dma_unmap(&o, &m) == 0);
    assert(m.active == 0);

    /* Stale unmap */
    assert(dma_unmap(&o, &stale) == -1);

    /* Now destroy succeeds */
    assert(dma_owner_destroy(&o) == 0);

    /* Operations on destroyed owner fail */
    assert(dma_map(&o, 0x2000, 4096, DMA_BIDIRECTIONAL, &m) == -1);
    assert(dma_owner_destroy(&o) == -1);
}

static void test_dma_capacity(void) {
    struct dma_owner o;
    struct dma_mapping mappings[DMA_MAX_MAPPINGS];
    struct dma_mapping extra;

    assert(dma_owner_init(&o, 10, 0, test_map_cb, test_unmap_cb) == 0);

    for (uint32_t i = 0; i < DMA_MAX_MAPPINGS; i++) {
        assert(dma_map(&o, 0x1000U + (uint64_t)i * 0x1000U, 4096,
                       DMA_TO_DEVICE, &mappings[i]) == 0);
    }

    /* 65th mapping exceeds capacity */
    assert(dma_map(&o, 0x900000, 4096, DMA_TO_DEVICE, &extra) == -2);

    /* Unmap one slot */
    assert(dma_unmap(&o, &mappings[5]) == 0);

    /* Now mapping succeeds into the freed slot */
    assert(dma_map(&o, 0x900000, 4096, DMA_TO_DEVICE, &extra) == 0);

    /* Unmap all and clean up */
    assert(dma_unmap(&o, &extra) == 0);
    for (uint32_t i = 0; i < DMA_MAX_MAPPINGS; i++) {
        if (i != 5)
            assert(dma_unmap(&o, &mappings[i]) == 0);
    }
    assert(dma_owner_destroy(&o) == 0);
}

static void test_dma_backend_failures(void) {
    struct dma_owner o;
    struct dma_mapping m;

    assert(dma_owner_init(&o, 20, 0, test_map_cb, test_unmap_cb) == 0);

    /* Map backend failure */
    fail_map = 1;
    assert(dma_map(&o, 0x1000, 4096, DMA_TO_DEVICE, &m) == -3);
    fail_map = 0;

    /* Bus address overflow from backend */
    force_bus = ~0ULL - 100;
    assert(dma_map(&o, 0x1000, 4096, DMA_TO_DEVICE, &m) == -3);
    force_bus = 0;

    /* Map succeeds */
    assert(dma_map(&o, 0x1000, 4096, DMA_FROM_DEVICE, &m) == 0);

    /* Unmap backend failure */
    fail_unmap = 1;
    assert(dma_unmap(&o, &m) == -2);
    fail_unmap = 0;

    /* Successful unmap */
    assert(dma_unmap(&o, &m) == 0);
    assert(dma_owner_destroy(&o) == 0);
}

int main(void) {
    test_dma_init();
    test_dma_map_bounds();
    test_dma_capacity();
    test_dma_backend_failures();
    return 0;
}
