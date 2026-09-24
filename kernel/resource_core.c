#include "resource_core.h"
static int valid(uint64_t b,uint64_t n,uint8_t k,uint32_t owner){return n&&b<=~0ULL-(n-1U)&&(k==HW_RESOURCE_MMIO||k==HW_RESOURCE_IOPORT)&&owner;}
static int overlaps(uint64_t a,uint64_t an,uint64_t b,uint64_t bn){return a<=b+bn-1U&&b<=a+an-1U;}
static int insert(struct hw_resource_map*m,uint64_t b,uint64_t n,uint8_t k,uint32_t o,uint8_t reserved){if(!m||!valid(b,n,k,o)||m->count>=HW_RESOURCE_CAPACITY)return -1;for(uint32_t i=0;i<m->count;i++){struct hw_resource*e=&m->entries[i];if(e->active&&e->kind==k&&overlaps(b,n,e->base,e->length))return -2;}struct hw_resource*e=&m->entries[m->count++];e->base=b;e->length=n;e->kind=k;e->owner=o;e->reserved=reserved;e->active=1;return 0;}
int hw_resource_reserve(struct hw_resource_map*m,uint64_t b,uint64_t n,uint8_t k,uint32_t o){return insert(m,b,n,k,o,1);}
int hw_resource_claim(struct hw_resource_map*m,uint64_t b,uint64_t n,uint8_t k,uint32_t o){return insert(m,b,n,k,o,0);}
int hw_resource_release(struct hw_resource_map*m,uint64_t b,uint64_t n,uint8_t k,uint32_t o){if(!m)return -1;for(uint32_t i=0;i<m->count;i++){struct hw_resource*e=&m->entries[i];if(e->active&&e->base==b&&e->length==n&&e->kind==k&&e->owner==o&&!e->reserved){e->active=0;return 0;}}return -1;}
int hw_resource_contains(const struct hw_resource_map*m,uint64_t b,uint64_t n,uint8_t k){if(!m||!n||b>~0ULL-(n-1U))return 0;for(uint32_t i=0;i<m->count;i++){const struct hw_resource*e=&m->entries[i];if(e->active&&e->kind==k&&b>=e->base&&(b-e->base)<=e->length&&n<=e->length-(b-e->base))return 1;}return 0;}
