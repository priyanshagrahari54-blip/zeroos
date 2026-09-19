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

void vmm_init(void) {
    void *root = page_alloc();
    if (!root)
        return;

    root_table = (uint64_t *)root;
    root_physical = (uint64_t)root;
    zero_page(root_table);

    uint64_t *pdpt = ensure_table(root_table, 0, 0);
    if (!pdpt)
        return;

    uint64_t *pd = ensure_table(pdpt, 0, 0);
    if (!pd)
        return;

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
