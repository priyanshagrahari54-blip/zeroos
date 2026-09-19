#ifndef ZEROOS_VMM_H
#define ZEROOS_VMM_H

#include "types.h"

#define VMM_PAGE_SIZE 4096ULL
#define VMM_PRESENT 0x001ULL
#define VMM_WRITABLE 0x002ULL
#define VMM_USER 0x004ULL
#define VMM_WRITE_THROUGH 0x008ULL
#define VMM_CACHE_DISABLE 0x010ULL
#define VMM_NO_EXECUTE (1ULL << 63)

void vmm_init(void);
int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
int vmm_unmap_page(uint64_t virtual_address);
uint64_t vmm_translate(uint64_t virtual_address);
uint64_t vmm_root(void);

#endif
