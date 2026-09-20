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

int vmm_init(void);
int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
int vmm_map_range(uint64_t virtual_address, uint64_t physical_address,
                  uint64_t page_count, uint64_t flags);
int vmm_unmap_page(uint64_t virtual_address);
int vmm_unmap_range(uint64_t virtual_address, uint64_t page_count);
int vmm_protect_page(uint64_t virtual_address, uint64_t flags);
int vmm_is_user_range(uint64_t virtual_address, uint64_t length, uint64_t write);
uint64_t vmm_translate(uint64_t virtual_address);
uint64_t vmm_root(void);
uint64_t vmm_kernel_page_flags(uint64_t address);

/*
 * CR3 / TLB isolation.
 *
 * When PCID is available (CPUID.1:ECX[17], enabled in CR4 by the boot
 * code), every address space is loaded with its own 16-bit PCID so TLB
 * entries never leak across address spaces. Without PCID the loader
 * performs a full TLB flush (write current CR3, then load the new root)
 * because untagged TLB entries survive CR3 changes.
 */
int vmm_pcid_enabled(void);
int vmm_invpcid_enabled(void);
void vmm_load_root(uint64_t root_physical, uint16_t pcid);
void vmm_flush_tlb(void);
uint64_t vmm_active_root(void);
uint16_t vmm_pcid_alloc(void);
unsigned vmm_pcid_in_use(void);
void vmm_pcid_free(uint16_t pcid);

struct vmm_owned_page {
    uint64_t physical;
    uint64_t virtual_address;
    uint8_t executable;
    struct vmm_owned_page *next;
};

struct vmm_space {
    uint64_t root_physical;
    uint64_t *root;
    uint16_t pcid;
    uint8_t  has_pcid;
    /*
     * Physical pages owned by this address space (user code/data/stack).
     * Ownership is exclusive: a page owned by one space cannot be mapped
     * into another, and destruction releases exactly this space's pages.
     */
    struct vmm_owned_page *owned_pages;
    uint64_t owned_page_count;
};

/* Create requires a zero-initialized object; destroy returns it to empty. */
int vmm_space_create(struct vmm_space *space);
void vmm_space_destroy(struct vmm_space *space);
int vmm_space_map_page(struct vmm_space *space, uint64_t virtual_address,
                       uint64_t physical_address, uint64_t flags);
int vmm_space_unmap_page(struct vmm_space *space, uint64_t virtual_address);
uint64_t vmm_space_translate(const struct vmm_space *space, uint64_t virtual_address);
int vmm_space_activate(const struct vmm_space *space);
int vmm_space_is_user_range(const struct vmm_space *space, uint64_t virtual_address,
                            uint64_t length, uint64_t write);
int vmm_space_own_page(struct vmm_space *space, uint64_t physical);
int vmm_space_release_page(struct vmm_space *space, uint64_t physical);

#endif
