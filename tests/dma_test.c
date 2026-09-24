#include <assert.h>
#include "../kernel/dma.h"
static int maps,unmaps;static int map(void*c,uint64_t p,uint64_t n,uint8_t d,uint64_t*b){(void)c;(void)n;(void)d;maps++;*b=p+0x1000;return 0;}static int unmap(void*c,uint64_t b,uint64_t n,uint8_t d){(void)c;(void)b;(void)n;(void)d;unmaps++;return 0;}
int main(void){struct dma_owner o;struct dma_mapping m;assert(dma_owner_init(&o,7,0,map,unmap)==0);assert(dma_map(&o,0x2000,4096,DMA_BIDIRECTIONAL,&m)==0);assert(dma_owner_destroy(&o)==-2);struct dma_mapping stale=m;assert(dma_unmap(&o,&m)==0&&unmaps==1);assert(dma_unmap(&o,&stale)==-1);assert(dma_owner_destroy(&o)==0);assert(dma_map(&o,0x2000,0,DMA_TO_DEVICE,&m)==-1);return 0;}
