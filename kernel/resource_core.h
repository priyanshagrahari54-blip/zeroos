#ifndef ZEROOS_RESOURCE_CORE_H
#define ZEROOS_RESOURCE_CORE_H
#include "types.h"
#define HW_RESOURCE_CAPACITY 128
#define HW_RESOURCE_MMIO 1
#define HW_RESOURCE_IOPORT 2
struct hw_resource { uint64_t base,length; uint32_t owner; uint8_t kind,active,reserved; };
/* Callers must serialize access (normally with an IRQ-safe kernel lock). */
struct hw_resource_map { struct hw_resource entries[HW_RESOURCE_CAPACITY]; uint32_t count; };
void hw_resource_map_init(struct hw_resource_map *map);
int hw_resource_reserve(struct hw_resource_map *map,uint64_t base,uint64_t length,uint8_t kind,uint32_t owner);
int hw_resource_claim(struct hw_resource_map *map,uint64_t base,uint64_t length,uint8_t kind,uint32_t owner);
int hw_resource_release(struct hw_resource_map *map,uint64_t base,uint64_t length,uint8_t kind,uint32_t owner);
int hw_resource_contains(const struct hw_resource_map *map,uint64_t base,uint64_t length,uint8_t kind);
#endif
