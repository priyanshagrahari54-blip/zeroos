#include "vmm.h"
#include "memory.h"

#define ENTRY_COUNT 512ULL
#define PAGE_MASK 0x000ffffffffff000ULL
#define HUGE_PAGE_2M 0x080ULL
#define HUGE_PAGE_SIZE 0x200000ULL
#define PHYS_MASK 0x000ffffffffff000ULL

#define VMM_LEAF_FLAGS 0x00000000000001ffULL

static uint64_t *root_table;
static uint64_t root_physical;

static inline void write_cr3(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
}

static inline void invalidate_page(uint64_t address) {
    __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
}

static void zero_page(uint64_t *page) {
    for (uint64_t i = 0; i < ENTRY_COUNT; ++i)
        page[i] = 0;
}

static uint64_t *table_from_entry(uint64_t entry) {
    return (uint64_t *)(entry & PAGE_MASK);
}

static int canonical_address(uint64_t address) {
    uint64_t sign = (address >> 47) & 1ULL;
    uint64_t upper = address >> 48;
    return sign ? upper == 0xffffULL : upper == 0;
}

static uint64_t *ensure_table(uint64_t *parent,
                              uint64_t index,
                              uint64_t flags) {
    uint64_t entry = parent[index];

    if (entry & VMM_PRESENT) {
        if (entry & HUGE_PAGE_2M)
            return (uint64_t *)0;
        return table_from_entry(entry);
    }

    void *page = page_alloc();
    if (!page)
        return (uint64_t *)0;

    zero_page((uint64_t *)page);
    parent[index] = ((uint64_t)page & PAGE_MASK) |
                    VMM_PRESENT | VMM_WRITABLE |
                    (flags & VMM_USER);
    return (uint64_t *)page;
}

/*
 * Convert one 2 MiB PDE into a 4 KiB PT.  This keeps huge mappings as the
 * default and pays the extra 4 KiB table only when fine-grained mapping is
 * actually required.
 */
static int split_2m(uint64_t *pd, uint64_t index) {
    uint64_t old = pd[index];
    if (!(old & VMM_PRESENT) || !(old & HUGE_PAGE_2M))
        return 0;

    void *page = page_alloc();
    if (!page)
        return -1;

    uint64_t *pt = (uint64_t *)page;
    uint64_t base = old & 0x000ffffffe00000ULL;
    uint64_t flags = old & (VMM_PRESENT | VMM_WRITABLE | VMM_USER |
                            VMM_WRITE_THROUGH | VMM_CACHE_DISABLE |
                            0x100ULL | VMM_NO_EXECUTE);

    zero_page(pt);

    for (uint64_t i = 0; i < ENTRY_COUNT; ++i)
        pt[i] = (base + i * VMM_PAGE_SIZE) | flags;

    /*
     * The old entry is no longer a huge mapping.  Clear it before installing
     * the PT so no CPU can retain a stale translation for the old page size.
     */
    pd[index] = 0;
    write_cr3(root_physical);
    pd[index] = ((uint64_t)pt & PAGE_MASK) |
                VMM_PRESENT | VMM_WRITABLE |
                (old & VMM_USER);
    write_cr3(root_physical);

    return 0;
}

int vmm_init(void) {
    void *root = page_alloc();
    if (!root)
        return -1;

    root_table = (uint64_t *)root;
    root_physical = (uint64_t)root;
    zero_page(root_table);

    uint64_t *pdpt = ensure_table(root_table, 0, 0);
    if (!pdpt)
        return -1;

    uint64_t *pd = ensure_table(pdpt, 0, 0);
    if (!pd)
        return -1;

    /*
     * Keep a compact identity/direct map for the complete physical range
     * currently managed by the physical allocator.  512 MiB needs only one
     * 4 KiB page directory (256 x 2 MiB entries).
     *
     * The first 2 MiB contains the executable kernel/bootstrap area.
     * Remaining RAM is writable and non-executable.
     */
    uint64_t tracked = memory_max_physical();
    uint64_t entries = tracked / HUGE_PAGE_SIZE;
    if (entries > ENTRY_COUNT)
        entries = ENTRY_COUNT;

    for (uint64_t i = 0; i < entries; ++i) {
        uint64_t flags = VMM_PRESENT | VMM_WRITABLE | HUGE_PAGE_2M;
        if (i != 0)
            flags |= VMM_NO_EXECUTE;
        pd[i] = i * HUGE_PAGE_SIZE | flags;
    }

    write_cr3(root_physical);
    return 0;
}

int vmm_map_page(uint64_t virtual_address,
                 uint64_t physical_address,
                 uint64_t flags) {
    if (!root_table || !canonical_address(virtual_address))
        return -1;
    if ((virtual_address & (VMM_PAGE_SIZE - 1)) != 0)
        return -1;
    if ((physical_address & (VMM_PAGE_SIZE - 1)) != 0)
        return -1;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1ff;
    uint64_t pd_index = (virtual_address >> 21) & 0x1ff;
    uint64_t pt_index = (virtual_address >> 12) & 0x1ff;

    uint64_t *pdpt = ensure_table(root_table, pml4_index, flags);
    if (!pdpt)
        return -1;

    uint64_t *pd = ensure_table(pdpt, pdpt_index, flags);
    if (!pd)
        return -1;

    if (pd[pd_index] & HUGE_PAGE_2M) {
        if (split_2m(pd, pd_index) != 0)
            return -1;
    }

    uint64_t *pt = ensure_table(pd, pd_index, flags);
    if (!pt)
        return -1;

    if (pt[pt_index] & VMM_PRESENT)
        return -1;

    pt[pt_index] = (physical_address & PHYS_MASK) |
                   VMM_PRESENT | (flags & (VMM_LEAF_FLAGS | VMM_NO_EXECUTE));

    invalidate_page(virtual_address);
    return 0;
}

int vmm_unmap_page(uint64_t virtual_address) {
    if (!root_table || !canonical_address(virtual_address))
        return -1;
    if ((virtual_address & (VMM_PAGE_SIZE - 1)) != 0)
        return -1;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1ff;
    uint64_t pd_index = (virtual_address >> 21) & 0x1ff;
    uint64_t pt_index = (virtual_address >> 12) & 0x1ff;

    uint64_t e1 = root_table[pml4_index];
    if (!(e1 & VMM_PRESENT) || (e1 & HUGE_PAGE_2M))
        return -1;

    uint64_t *pdpt = table_from_entry(e1);
    uint64_t e2 = pdpt[pdpt_index];
    if (!(e2 & VMM_PRESENT) || (e2 & HUGE_PAGE_2M))
        return -1;

    uint64_t *pd = table_from_entry(e2);
    if (pd[pd_index] & HUGE_PAGE_2M) {
        if (split_2m(pd, pd_index) != 0)
            return -1;
    }

    uint64_t e3 = pd[pd_index];
    if (!(e3 & VMM_PRESENT))
        return -1;

    uint64_t *pt = table_from_entry(e3);
    if (!(pt[pt_index] & VMM_PRESENT))
        return -1;

    pt[pt_index] = 0;
    invalidate_page(virtual_address);
    return 0;
}

uint64_t vmm_translate(uint64_t virtual_address) {
    if (!root_table || !canonical_address(virtual_address))
        return 0;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1ff;
    uint64_t pd_index = (virtual_address >> 21) & 0x1ff;
    uint64_t pt_index = (virtual_address >> 12) & 0x1ff;

    uint64_t e1 = root_table[pml4_index];
    if (!(e1 & VMM_PRESENT))
        return 0;

    uint64_t *pdpt = table_from_entry(e1);
    uint64_t e2 = pdpt[pdpt_index];
    if (!(e2 & VMM_PRESENT))
        return 0;

    uint64_t *pd = table_from_entry(e2);
    uint64_t e3 = pd[pd_index];
    if (!(e3 & VMM_PRESENT))
        return 0;

    if (e3 & HUGE_PAGE_2M)
        return (e3 & 0x000ffffffe00000ULL) |
               (virtual_address & 0x1fffffULL);

    uint64_t *pt = table_from_entry(e3);
    uint64_t e4 = pt[pt_index];
    if (!(e4 & VMM_PRESENT))
        return 0;

    return (e4 & PHYS_MASK) | (virtual_address & 0xfffULL);
}

uint64_t vmm_root(void) {
    return root_physical;
}


int vmm_map_range(uint64_t virtual_address, uint64_t physical_address,
                   uint64_t page_count, uint64_t flags) {
    for (uint64_t i = 0; i < page_count; ++i) {
        if (vmm_map_page(virtual_address + i * VMM_PAGE_SIZE,
                         physical_address + i * VMM_PAGE_SIZE, flags) != 0) {
            while (i > 0) {
                --i;
                vmm_unmap_page(virtual_address + i * VMM_PAGE_SIZE);
            }
            return -1;
        }
    }
    return 0;
}

int vmm_unmap_range(uint64_t virtual_address, uint64_t page_count) {
    for (uint64_t i = 0; i < page_count; ++i) {
        if (vmm_unmap_page(virtual_address + i * VMM_PAGE_SIZE) != 0)
            return -1;
    }
    return 0;
}

int vmm_protect_page(uint64_t virtual_address, uint64_t flags) {
    if (!root_table || !canonical_address(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE - 1)) != 0)
        return -1;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1ff;
    uint64_t pd_index = (virtual_address >> 21) & 0x1ff;
    uint64_t pt_index = (virtual_address >> 12) & 0x1ff;

    uint64_t e1 = root_table[pml4_index];
    if (!(e1 & VMM_PRESENT)) return -1;
    uint64_t *pdpt = table_from_entry(e1);

    /*
     * x86-64 user accessibility is hierarchical: a leaf U/S bit is not
     * sufficient when any ancestor entry remains supervisor-only.  A page
     * protected as user therefore has to promote every paging level that
     * contains it.  This is safe here because the explicit user-space test
     * address lives in the dedicated user PML4 slot; kernel mappings remain
     * supervisor-only.
     */
    if (flags & VMM_USER)
        e1 |= VMM_USER;
    root_table[pml4_index] = e1;

    uint64_t e2 = pdpt[pdpt_index];
    if (!(e2 & VMM_PRESENT)) return -1;
    uint64_t *pd = table_from_entry(e2);

    if (flags & VMM_USER)
        e2 |= VMM_USER;
    pdpt[pdpt_index] = e2;

    if (pd[pd_index] & HUGE_PAGE_2M) {
        if (split_2m(pd, pd_index) != 0) return -1;
    }

    uint64_t e3 = pd[pd_index];
    if (!(e3 & VMM_PRESENT)) return -1;
    if (flags & VMM_USER) {
        e3 |= VMM_USER;
        pd[pd_index] = e3;
    }
    if (!(e3 & VMM_PRESENT)) return -1;
    uint64_t *pt = table_from_entry(e3);
    if (!(pt[pt_index] & VMM_PRESENT)) return -1;

    pt[pt_index] = (pt[pt_index] & PHYS_MASK) |
                   VMM_PRESENT |
                   (flags & (VMM_LEAF_FLAGS | VMM_NO_EXECUTE));
    invalidate_page(virtual_address);
    return 0;
}

int vmm_is_user_range(uint64_t virtual_address, uint64_t length, uint64_t write) {
    if (!root_table || length == 0 || !canonical_address(virtual_address))
        return 0;
    if (virtual_address + length < virtual_address)
        return 0;

    uint64_t end = virtual_address + length - 1;
    if (!canonical_address(end))
        return 0;

    uint64_t cursor = virtual_address & ~(VMM_PAGE_SIZE - 1ULL);
    uint64_t last = end & ~(VMM_PAGE_SIZE - 1ULL);

    for (;;) {
        uint64_t pml4_index = (cursor >> 39) & 0x1ff;
        uint64_t pdpt_index = (cursor >> 30) & 0x1ff;
        uint64_t pd_index = (cursor >> 21) & 0x1ff;
        uint64_t pt_index = (cursor >> 12) & 0x1ff;

        uint64_t e1 = root_table[pml4_index];
        if (!(e1 & VMM_PRESENT) || !(e1 & VMM_USER)) return 0;
        uint64_t *pdpt = table_from_entry(e1);
        uint64_t e2 = pdpt[pdpt_index];
        if (!(e2 & VMM_PRESENT) || !(e2 & VMM_USER)) return 0;
        uint64_t *pd = table_from_entry(e2);
        uint64_t e3 = pd[pd_index];
        if (!(e3 & VMM_PRESENT) || !(e3 & VMM_USER)) return 0;

        if (e3 & HUGE_PAGE_2M) {
            if (write && !(e3 & VMM_WRITABLE)) return 0;
        } else {
            uint64_t *pt = table_from_entry(e3);
            uint64_t e4 = pt[pt_index];
            if (!(e4 & VMM_PRESENT) || !(e4 & VMM_USER)) return 0;
            if (write && !(e4 & VMM_WRITABLE)) return 0;
        }

        if (cursor == last) break;
        cursor += VMM_PAGE_SIZE;
    }
    return 1;
}


/*
 * Per-address-space operations. Kernel mappings are shared from the active
 * kernel root; user mappings live in PML4 slot 254 (0x00007f... range).
 * The space root itself is independent, so CR3 switching never mutates the
 * kernel root.
 */
#define VMM_USER_PML4_INDEX 254ULL

static int space_canonical(uint64_t address) {
    return canonical_address(address);
}

static uint64_t *space_ensure_table(uint64_t *parent, uint64_t index,
                                     uint64_t flags) {
    uint64_t entry = parent[index];
    if (entry & VMM_PRESENT) {
        if (entry & HUGE_PAGE_2M)
            return (uint64_t *)0;
        return table_from_entry(entry);
    }

    void *page = page_alloc_zero();
    if (!page) return (uint64_t *)0;
    parent[index] = ((uint64_t)page & PAGE_MASK) |
                    VMM_PRESENT | VMM_WRITABLE |
                    (flags & VMM_USER);
    return (uint64_t *)page;
}

int vmm_space_create(struct vmm_space *space) {
    if (!space || !root_table) return -1;
    void *root = page_alloc_zero();
    if (!root) return -1;

    space->root = (uint64_t *)root;
    space->root_physical = (uint64_t)root;

    /* Slot 0 contains the kernel's identity/direct map and is shared. */
    space->root[0] = root_table[0];
    return 0;
}

void vmm_space_destroy(struct vmm_space *space) {
    if (!space || !space->root) return;
    /* User page-table pages are private to this space. */
    uint64_t e1 = space->root[VMM_USER_PML4_INDEX];
    if (e1 & VMM_PRESENT) {
        uint64_t *pdpt = table_from_entry(e1);
        for (uint64_t i=0; i<ENTRY_COUNT; ++i) {
            uint64_t e2 = pdpt[i];
            if (!(e2 & VMM_PRESENT) || (e2 & HUGE_PAGE_2M)) continue;
            uint64_t *pd = table_from_entry(e2);
            for (uint64_t j=0; j<ENTRY_COUNT; ++j) {
                uint64_t e3 = pd[j];
                if (!(e3 & VMM_PRESENT)) continue;
                if (e3 & HUGE_PAGE_2M) {
                    /* User huge mappings own no page-table leaf here. */
                    continue;
                }
                page_free(table_from_entry(e3));
            }
            page_free(pd);
        }
        page_free(pdpt);
    }
    page_free(space->root);
    space->root = 0;
    space->root_physical = 0;
}

int vmm_space_map_page(struct vmm_space *space, uint64_t virtual_address,
                       uint64_t physical_address, uint64_t flags) {
    if (!space || !space->root || !space_canonical(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE-1)) ||
        (physical_address & (VMM_PAGE_SIZE-1)))
        return -1;

    uint64_t pml4 = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_i = (virtual_address >> 30) & 0x1ff;
    uint64_t pd_i = (virtual_address >> 21) & 0x1ff;
    uint64_t pt_i = (virtual_address >> 12) & 0x1ff;

    /* Do not expose kernel identity mappings through the user API. */
    if (pml4 != VMM_USER_PML4_INDEX || !(flags & VMM_USER))
        return -1;

    uint64_t *pdpt = space_ensure_table(space->root,pml4,flags);
    if (!pdpt) return -1;
    uint64_t *pd = space_ensure_table(pdpt,pdpt_i,flags);
    if (!pd) return -1;

    if (pd[pd_i] & HUGE_PAGE_2M) {
        if (split_2m(pd,pd_i) != 0) return -1;
    }

    uint64_t *pt = space_ensure_table(pd,pd_i,flags);
    if (!pt || (pt[pt_i] & VMM_PRESENT)) return -1;

    pt[pt_i] = (physical_address & PHYS_MASK) |
               VMM_PRESENT | (flags & (VMM_LEAF_FLAGS|VMM_NO_EXECUTE));
    if (space->root_physical == root_physical)
        invalidate_page(virtual_address);
    return 0;
}

int vmm_space_unmap_page(struct vmm_space *space, uint64_t virtual_address) {
    if (!space || !space->root ||
        ((virtual_address >> 39) & 0x1ff) != VMM_USER_PML4_INDEX ||
        !space_canonical(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE-1)))
        return -1;

    uint64_t *pdpt=table_from_entry(space->root[VMM_USER_PML4_INDEX]);
    if (!(space->root[VMM_USER_PML4_INDEX]&VMM_PRESENT)) return -1;
    uint64_t e2=pdpt[(virtual_address>>30)&0x1ff];
    if (!(e2&VMM_PRESENT)) return -1;
    uint64_t *pd=table_from_entry(e2);
    uint64_t e3=pd[(virtual_address>>21)&0x1ff];
    if (!(e3&VMM_PRESENT)) return -1;
    if (e3&HUGE_PAGE_2M) return -1;
    uint64_t *pt=table_from_entry(e3);
    uint64_t idx=(virtual_address>>12)&0x1ff;
    if (!(pt[idx]&VMM_PRESENT)) return -1;
    pt[idx]=0;
    if (space->root_physical==root_physical) invalidate_page(virtual_address);
    return 0;
}

uint64_t vmm_space_translate(const struct vmm_space *space, uint64_t virtual_address) {
    if (!space || !space->root || !space_canonical(virtual_address)) return 0;
    uint64_t pml4=(virtual_address>>39)&0x1ff;
    uint64_t e1=space->root[pml4];
    if (!(e1&VMM_PRESENT)) return 0;
    uint64_t *pdpt=table_from_entry(e1);
    uint64_t e2=pdpt[(virtual_address>>30)&0x1ff];
    if (!(e2&VMM_PRESENT)) return 0;
    uint64_t *pd=table_from_entry(e2);
    uint64_t e3=pd[(virtual_address>>21)&0x1ff];
    if (!(e3&VMM_PRESENT)) return 0;
    if (e3&HUGE_PAGE_2M)
        return (e3&0x000ffffffe00000ULL)|(virtual_address&0x1fffffULL);
    uint64_t *pt=table_from_entry(e3);
    uint64_t e4=pt[(virtual_address>>12)&0x1ff];
    if (!(e4&VMM_PRESENT)) return 0;
    return (e4&PHYS_MASK)|(virtual_address&0xfffULL);
}

int vmm_space_activate(const struct vmm_space *space) {
    if (!space || !space->root) return -1;
    write_cr3(space->root_physical);
    return 0;
}
