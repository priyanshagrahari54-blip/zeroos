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
#define VMM_MMIO_BASE 0xffff800000000000ULL
#define VMM_MMIO_PML4_INDEX ((VMM_MMIO_BASE >> 39) & 0x1ffULL)

int vmm_init(void);
int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
int vmm_map_range(uint64_t virtual_address, uint64_t physical_address,
                  uint64_t page_count, uint64_t flags);
int vmm_unmap_page(uint64_t virtual_address);
int vmm_unmap_range(uint64_t virtual_address, uint64_t page_count);
int vmm_protect_page(uint64_t virtual_address, uint64_t flags);
int vmm_map_mmio_page(uint64_t virtual_address, uint64_t physical_address,
                      uint64_t flags);
int vmm_unmap_mmio_page(uint64_t virtual_address);
int vmm_is_user_range(uint64_t virtual_address, uint64_t length, uint64_t write);
uint64_t vmm_translate(uint64_t virtual_address);
uint64_t vmm_root(void);

struct vmm_space {
    uint64_t root_physical;
    uint64_t *root;
    uint64_t mapped_pages;
    uint64_t max_pages;
};

int vmm_space_create(struct vmm_space *space);
int vmm_space_destroy(struct vmm_space *space);
int vmm_space_map_page(struct vmm_space *space, uint64_t virtual_address,
                       uint64_t physical_address, uint64_t flags);
int vmm_space_unmap_page(struct vmm_space *space, uint64_t virtual_address);
uint64_t vmm_space_translate(const struct vmm_space *space, uint64_t virtual_address);
int vmm_space_set_page_limit(struct vmm_space *space, uint64_t max_pages);
uint64_t vmm_space_mapped_pages(const struct vmm_space *space);
int vmm_space_is_user_range(const struct vmm_space *space,
                            uint64_t virtual_address, uint64_t length,
                            uint64_t write);
int vmm_space_is_executable(const struct vmm_space *space,
                            uint64_t virtual_address, uint64_t length);
int vmm_space_activate(const struct vmm_space *space);
int vmm_activate_kernel(void);

#endif
