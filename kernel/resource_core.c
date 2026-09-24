#include "resource_core.h"

void hw_resource_map_init(struct hw_resource_map *map) {
    if (!map)
        return;
    uint8_t *bytes = (uint8_t *)map;
    for (uint32_t i = 0; i < sizeof(*map); ++i)
        bytes[i] = 0;
}

static int valid_range(uint64_t base, uint64_t length, uint8_t kind,
                       uint32_t owner) {
    if (length == 0 || base > ~0ULL - (length - 1U) || owner == 0)
        return 0;
    return kind == HW_RESOURCE_MMIO || kind == HW_RESOURCE_IOPORT;
}

static int ranges_overlap(uint64_t a, uint64_t a_length,
                          uint64_t b, uint64_t b_length) {
    /* Endpoints are inclusive; valid_range() has already excluded overflow. */
    uint64_t a_end = a + a_length - 1U;
    uint64_t b_end = b + b_length - 1U;
    return a <= b_end && b <= a_end;
}

static int insert_resource(struct hw_resource_map *map, uint64_t base,
                           uint64_t length, uint8_t kind, uint32_t owner,
                           uint8_t reserved) {
    if (!map || !valid_range(base, length, kind, owner))
        return -1;

    uint32_t free_slot = HW_RESOURCE_CAPACITY;
    for (uint32_t i = 0; i < HW_RESOURCE_CAPACITY; ++i) {
        struct hw_resource *entry = &map->entries[i];
        if (!entry->active) {
            if (free_slot == HW_RESOURCE_CAPACITY)
                free_slot = i;
            continue;
        }
        if (entry->kind == kind &&
            ranges_overlap(base, length, entry->base, entry->length))
            return -2;
    }
    if (free_slot == HW_RESOURCE_CAPACITY)
        return -3;

    struct hw_resource *entry = &map->entries[free_slot];
    entry->base = base;
    entry->length = length;
    entry->kind = kind;
    entry->owner = owner;
    entry->reserved = reserved;
    entry->active = 1;
    map->count++;
    return 0;
}

int hw_resource_reserve(struct hw_resource_map *map, uint64_t base,
                        uint64_t length, uint8_t kind, uint32_t owner) {
    return insert_resource(map, base, length, kind, owner, 1);
}

int hw_resource_claim(struct hw_resource_map *map, uint64_t base,
                      uint64_t length, uint8_t kind, uint32_t owner) {
    return insert_resource(map, base, length, kind, owner, 0);
}

int hw_resource_release(struct hw_resource_map *map, uint64_t base,
                        uint64_t length, uint8_t kind, uint32_t owner) {
    if (!map)
        return -1;
    for (uint32_t i = 0; i < HW_RESOURCE_CAPACITY; ++i) {
        struct hw_resource *entry = &map->entries[i];
        if (entry->active && !entry->reserved && entry->base == base &&
            entry->length == length && entry->kind == kind &&
            entry->owner == owner) {
            entry->active = 0;
            entry->owner = 0;
            map->count--;
            return 0;
        }
    }
    return -1;
}

int hw_resource_contains(const struct hw_resource_map *map, uint64_t base,
                         uint64_t length, uint8_t kind) {
    if (!map || length == 0 || base > ~0ULL - (length - 1U))
        return 0;
    for (uint32_t i = 0; i < HW_RESOURCE_CAPACITY; ++i) {
        const struct hw_resource *entry = &map->entries[i];
        if (!entry->active || entry->kind != kind || base < entry->base)
            continue;
        uint64_t offset = base - entry->base;
        if (offset <= entry->length && length <= entry->length - offset)
            return 1;
    }
    return 0;
}
