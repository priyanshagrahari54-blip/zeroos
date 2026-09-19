#include "vmm.h"
#include "memory.h"

#define ENTRY_COUNT 512ULL
#define PAGE_MASK 0x000ffffffffff000ULL
#define HUGE_PAGE_2M 0x080ULL
#define PHYS_MASK 0x000ffffffffff000ULL

static uint64_t *root_table;
static uint64_t root_physical;

static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value & PAGE_MASK;
}

static inline void write_cr3(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
}

static void zero_page(uint64_t *page) {
    for (uint64_t i = 0; i < ENTRY_COUNT; ++i) page[i] = 0;
}

static uint64_t *table_from_entry(uint64_t entry) {
    return (uint64_t *)(entry & PAGE_MASK);
}

static int canonical_address(uint64_t address) {
    uint64_t upper = address >> 48;
    return upper == 0 || upper == 0xffff;
}

static uint64_t *ensure_table(uint64_t *parent, uint64_t index, uint64_t flags) {
    uint64_t entry = parent[index];

    if (entry & VMM_PRESENT) {
        if (entry & HUGE_PAGE_2M) return (uint64_t *)0;
        return table_from_entry(entry);
    }

    void *page = page_alloc();
    if (!page) return (uint64_t *)0;

    zero_page((uint64_t *)page);
    parent[index] = ((uint64_t)page & PAGE_MASK) |
                    VMM_PRESENT | VMM_WRITABLE | (flags & VMM_USER);
    return (uint64_t *)page;
}

void vmm_init(void) {
    void *page = page_alloc();
    if (!page) return;

    root_table = (uint64_t *)page;
    root_physical = (uint64_t)page;
    zero_page(root_table);

    uint64_t *pdpt = ensure_table(root_table, 0, 0);
    if (!pdpt) return;

    uint64_t *pd = ensure_table(pdpt, 0, 0);
    if (!pd) return;

    /* Preserve the early kernel's real 2 MiB identity mapping. */
    pd[0] = VMM_PRESENT | VMM_WRITABLE | HUGE_PAGE_2M;

    write_cr3(root_physical);
}

int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags) {
    if (!canonical_address(virtual_address)) return -1;
    if ((virtual_address & (VMM_PAGE_SIZE - 1)) != 0) return -1;
    if ((physical_address & (VMM_PAGE_SIZE - 1)) != 0) return -1;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1ff;
    uint64_t pd_index = (virtual_address >> 21) & 0x1ff;
    uint64_t pt_index = (virtual_address >> 12) & 0x1ff;

    uint64_t *pdpt = ensure_table(root_table, pml4_index, flags);
    if (!pdpt) return -1;

    uint64_t *pd = ensure_table(pdpt, pdpt_index, flags);
    if (!pd) return -1;

    uint64_t *pt = ensure_table(pd, pd_index, flags);
    if (!pt) return -1;

    if (pt[pt_index] & VMM_PRESENT) return -1;

    pt[pt_index] = (physical_address & PHYS_MASK) |
                    VMM_PRESENT | (flags & 0xfffULL);

    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return 0;
}

int vmm_unmap_page(uint64_t virtual_address) {
    if (!canonical_address(virtual_address)) return -1;
    if ((virtual_address & (VMM_PAGE_SIZE - 1)) != 0) return -1;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1ff;
    uint64_t pd_index = (virtual_address >> 21) & 0x1ff;
    uint64_t pt_index = (virtual_address >> 12) & 0x1ff;

    uint64_t e1 = root_table[pml4_index];
    if (!(e1 & VMM_PRESENT) || (e1 & HUGE_PAGE_2M)) return -1;

    uint64_t *pdpt = table_from_entry(e1);
    uint64_t e2 = pdpt[pdpt_index];
    if (!(e2 & VMM_PRESENT) || (e2 & HUGE_PAGE_2M)) return -1;

    uint64_t *pd = table_from_entry(e2);
    uint64_t e3 = pd[pd_index];
    if (!(e3 & VMM_PRESENT) || (e3 & HUGE_PAGE_2M)) return -1;

    uint64_t *pt = table_from_entry(e3);
    if (!(pt[pt_index] & VMM_PRESENT)) return -1;

    pt[pt_index] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return 0;
}

uint64_t vmm_translate(uint64_t virtual_address) {
    if (!canonical_address(virtual_address)) return 0;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1ff;
    uint64_t pd_index = (virtual_address >> 21) & 0x1ff;
    uint64_t pt_index = (virtual_address >> 12) & 0x1ff;
    uint64_t offset = virtual_address & 0xfffULL;

    uint64_t e1 = root_table[pml4_index];
    if (!(e1 & VMM_PRESENT)) return 0;

    uint64_t *pdpt = table_from_entry(e1);
    uint64_t e2 = pdpt[pdpt_index];
    if (!(e2 & VMM_PRESENT)) return 0;

    uint64_t *pd = table_from_entry(e2);
    uint64_t e3 = pd[pd_index];
    if (!(e3 & VMM_PRESENT)) return 0;

    if (e3 & HUGE_PAGE_2M)
        return (e3 & 0x000fffffffe00000ULL) | (virtual_address & 0x1fffffULL);

    uint64_t *pt = table_from_entry(e3);
    uint64_t e4 = pt[pt_index];
    if (!(e4 & VMM_PRESENT)) return 0;

    return (e4 & PHYS_MASK) | offset;
}

uint64_t vmm_root(void) {
    return root_physical;
}
