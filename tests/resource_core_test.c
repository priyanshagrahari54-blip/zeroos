#include <assert.h>
#include <string.h>
#include "../kernel/resource_core.h"

static void test_null_and_invalid_arguments(void) {
    struct hw_resource_map m;
    hw_resource_map_init(&m);

    /* Every entry point tolerates a NULL map instead of faulting. */
    hw_resource_map_init(0);
    assert(hw_resource_reserve(0, 0x1000, 0x1000, HW_RESOURCE_MMIO, 1) == -1);
    assert(hw_resource_claim(0, 0x1000, 0x1000, HW_RESOURCE_MMIO, 1) == -1);
    assert(hw_resource_release(0, 0x1000, 0x1000, HW_RESOURCE_MMIO, 1) == -1);
    assert(!hw_resource_contains(0, 0x1000, 0x100, HW_RESOURCE_MMIO));

    /* Zero length is never a valid range. */
    assert(hw_resource_claim(&m, 0x1000, 0, HW_RESOURCE_MMIO, 1) == -1);
    assert(!hw_resource_contains(&m, 0x1000, 0, HW_RESOURCE_MMIO));

    /* Owner 0 is reserved for "unowned" and cannot hold a resource. */
    assert(hw_resource_claim(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO, 0) == -1);

    /* Unknown resource kinds are rejected. */
    assert(hw_resource_claim(&m, 0x1000, 0x1000, 0, 1) == -1);
    assert(hw_resource_claim(&m, 0x1000, 0x1000, 3, 1) == -1);
    assert(hw_resource_claim(&m, 0x1000, 0x1000, 0xFF, 1) == -1);

    assert(m.count == 0);
}

static void test_range_overflow(void) {
    struct hw_resource_map m;
    hw_resource_map_init(&m);

    /* base + length - 1 must not wrap the address space. */
    assert(hw_resource_claim(&m, ~0ULL, 2, HW_RESOURCE_IOPORT, 9) == -1);
    assert(hw_resource_claim(&m, ~0ULL, 1, HW_RESOURCE_IOPORT, 9) == 0);
    assert(hw_resource_claim(&m, 0xFFFFFFFFFFFFF000ULL, 0x2000,
                             HW_RESOURCE_MMIO, 1) == -1);
    /* The largest representable range is accepted. */
    assert(hw_resource_claim(&m, 0, ~0ULL, HW_RESOURCE_MMIO, 2) == 0);
    assert(m.count == 2);

    /* contains() applies the same overflow guard. */
    assert(!hw_resource_contains(&m, ~0ULL, 2, HW_RESOURCE_MMIO));
}

static void test_kind_isolation(void) {
    struct hw_resource_map m;
    hw_resource_map_init(&m);

    /* The same address range may be claimed once per kind: MMIO and I/O
     * port space are independent. */
    assert(hw_resource_claim(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO, 1) == 0);
    assert(hw_resource_claim(&m, 0x1000, 0x1000, HW_RESOURCE_IOPORT, 2) == 0);
    assert(m.count == 2);

    /* contains() is kind-scoped too. */
    assert(hw_resource_contains(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO));
    assert(hw_resource_contains(&m, 0x1000, 0x1000, HW_RESOURCE_IOPORT));
    assert(!hw_resource_contains(&m, 0x1000, 0x1000, 3));

    /* A second MMIO claim over the same range still conflicts. */
    assert(hw_resource_claim(&m, 0x1800, 0x100, HW_RESOURCE_MMIO, 3) == -2);
}

static void test_overlap_semantics(void) {
    struct hw_resource_map m;
    hw_resource_map_init(&m);

    /* Inclusive-endpoint overlap: the ranges [0x1000,0x1fff] and
     * [0x2000,0x2fff] are adjacent, not overlapping. */
    assert(hw_resource_claim(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO, 1) == 0);
    assert(hw_resource_claim(&m, 0x2000, 0x1000, HW_RESOURCE_MMIO, 2) == 0);
    assert(m.count == 2);

    /* Touching the first byte of an existing range conflicts. */
    assert(hw_resource_claim(&m, 0x1FFF, 0x10, HW_RESOURCE_MMIO, 3) == -2);
    /* A range ending exactly where another begins does not conflict. */
    assert(hw_resource_claim(&m, 0x0FFF, 0x1, HW_RESOURCE_MMIO, 3) == 0);

    /* Fully enclosing an existing range conflicts. */
    assert(hw_resource_claim(&m, 0x0, 0x10000, HW_RESOURCE_MMIO, 4) == -2);

    /* A rejected claim must not consume a slot or bump the count. */
    assert(m.count == 3);
    assert(!hw_resource_contains(&m, 0x0, 0x10000, HW_RESOURCE_MMIO));
}

static void test_contains_boundaries(void) {
    struct hw_resource_map m;
    hw_resource_map_init(&m);

    assert(hw_resource_claim(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO, 1) == 0);

    /* Whole range */
    assert(hw_resource_contains(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO));
    /* Interior */
    assert(hw_resource_contains(&m, 0x1800, 0x100, HW_RESOURCE_MMIO));
    /* Single byte at either end */
    assert(hw_resource_contains(&m, 0x1000, 1, HW_RESOURCE_MMIO));
    assert(hw_resource_contains(&m, 0x1FFF, 1, HW_RESOURCE_MMIO));

    /* Extending one byte past the end is not contained. */
    assert(!hw_resource_contains(&m, 0x1FFF, 2, HW_RESOURCE_MMIO));
    assert(!hw_resource_contains(&m, 0x1F00, 0x200, HW_RESOURCE_MMIO));
    /* Starting one byte before the base is not contained. */
    assert(!hw_resource_contains(&m, 0x0FFF, 2, HW_RESOURCE_MMIO));
    /* Disjoint range */
    assert(!hw_resource_contains(&m, 0x2000, 0x100, HW_RESOURCE_MMIO));
    assert(!hw_resource_contains(&m, 0x500, 0x100, HW_RESOURCE_MMIO));
}

static void test_reserve_is_permanent(void) {
    struct hw_resource_map m;
    hw_resource_map_init(&m);

    /* A reservation is not releasable: release only matches claimed
     * (non-reserved) entries with the identical owner. */
    assert(hw_resource_reserve(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO, 1) == 0);
    assert(hw_resource_release(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO, 1) == -1);
    assert(m.count == 1);

    /* A claim over a reserved range still conflicts. */
    assert(hw_resource_claim(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO, 2) == -2);

    /* A claim may only be released by its own owner. */
    assert(hw_resource_claim(&m, 0x3000, 0x1000, HW_RESOURCE_MMIO, 2) == 0);
    assert(hw_resource_release(&m, 0x3000, 0x1000, HW_RESOURCE_MMIO, 1) == -1);
    assert(hw_resource_release(&m, 0x3000, 0x1000, HW_RESOURCE_MMIO, 2) == 0);
    assert(m.count == 1);

    /* Release is exact: a subset of a claim is not a match. */
    assert(hw_resource_claim(&m, 0x3000, 0x1000, HW_RESOURCE_MMIO, 2) == 0);
    assert(hw_resource_release(&m, 0x3000, 0x800, HW_RESOURCE_MMIO, 2) == -1);
    assert(hw_resource_release(&m, 0x3000, 0x1000, HW_RESOURCE_IOPORT, 2) == -1);
    assert(hw_resource_release(&m, 0x3000, 0x1000, HW_RESOURCE_MMIO, 2) == 0);

    /* Releasing twice fails. */
    assert(hw_resource_release(&m, 0x3000, 0x1000, HW_RESOURCE_MMIO, 2) == -1);
}

static void test_capacity_and_slot_reuse(void) {
    struct hw_resource_map full;
    struct hw_resource_map m;

    hw_resource_map_init(&full);
    for (uint32_t i = 0; i < HW_RESOURCE_CAPACITY; i++)
        assert(hw_resource_claim(&full, 0x10000U + (uint64_t)i * 0x1000U, 0x100,
                                 HW_RESOURCE_MMIO, i + 1) == 0);
    assert(full.count == HW_RESOURCE_CAPACITY);

    /* Table full: a non-conflicting claim is rejected with -3. */
    assert(hw_resource_claim(&full, 0x900000, 0x100, HW_RESOURCE_MMIO, 500) == -3);

    /* Freeing a slot makes it reusable. */
    assert(hw_resource_release(&full, 0x10000U + 10U * 0x1000U, 0x100,
                               HW_RESOURCE_MMIO, 11) == 0);
    assert(hw_resource_claim(&full, 0x900000, 0x100, HW_RESOURCE_MMIO, 500) == 0);
    assert(full.count == HW_RESOURCE_CAPACITY);

    /* Init zeroes the map, so a reused object starts empty. */
    hw_resource_map_init(&m);
    assert(hw_resource_claim(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO, 1) == 0);
    hw_resource_map_init(&m);
    assert(m.count == 0);
    assert(!hw_resource_contains(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO));
    assert(hw_resource_claim(&m, 0x1000, 0x1000, HW_RESOURCE_MMIO, 2) == 0);
    assert(m.count == 1);
}

int main(void) {
    test_null_and_invalid_arguments();
    test_range_overflow();
    test_kind_isolation();
    test_overlap_semantics();
    test_contains_boundaries();
    test_reserve_is_permanent();
    test_capacity_and_slot_reuse();
    return 0;
}
