#ifndef ZEROOS_DMA_H
#define ZEROOS_DMA_H
#include "types.h"
#define DMA_MAX_MAPPINGS 64
enum dma_direction { DMA_TO_DEVICE=1,DMA_FROM_DEVICE=2,DMA_BIDIRECTIONAL=3 };
struct dma_mapping { uint64_t bus_address,physical_address,length; uint32_t generation,owner_id; uint8_t direction,active; };
typedef int (*dma_backend_map_fn)(void *context,uint64_t physical,uint64_t length,uint8_t direction,uint64_t *bus);
typedef int (*dma_backend_unmap_fn)(void *context,uint64_t bus,uint64_t length,uint8_t direction);
struct dma_owner { uint32_t id,next_generation; uint8_t alive; void *context; dma_backend_map_fn map; dma_backend_unmap_fn unmap; struct dma_mapping mappings[DMA_MAX_MAPPINGS]; }; /* dma_map returns a caller-owned token copy, not an internal slot pointer. */
int dma_owner_init(struct dma_owner *owner,uint32_t id,void *context,dma_backend_map_fn map,dma_backend_unmap_fn unmap);
int dma_map(struct dma_owner *owner,uint64_t physical,uint64_t length,uint8_t direction,struct dma_mapping *mapping);
int dma_unmap(struct dma_owner *owner,struct dma_mapping *mapping);
int dma_owner_destroy(struct dma_owner *owner);
#endif
