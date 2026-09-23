#include "vmm.h"
#include "memory.h"
#include "tlb.h"
#include "cpu.h"

#define ENTRY_COUNT 512ULL
#define PAGE_MASK 0x000ffffffffff000ULL
#define HUGE_PAGE_2M 0x080ULL
#define HUGE_PAGE_SIZE 0x200000ULL
#define PHYS_MASK 0x000ffffffffff000ULL

#define VMM_LEAF_FLAGS 0x00000000000001ffULL
/* Software-only ownership marker; bit 9 is ignored by x86 page walkers. */
#define VMM_INTERNAL_OWNED 0x0000000000000200ULL

static uint64_t *root_table;
static uint64_t root_physical;
static uint64_t active_root_physical;

static int mapping_flags_valid(uint64_t flags) {
    /* ZEROOS enforces W^X for all explicit leaf mappings. */
    if ((flags & VMM_WRITABLE) && !(flags & VMM_NO_EXECUTE))
        return 0;
    return (flags & ~(VMM_USER | VMM_WRITABLE | VMM_WRITE_THROUGH |
                      VMM_CACHE_DISABLE | VMM_NO_EXECUTE))==0;
}

static uint64_t hardware_leaf_flags(uint64_t flags) {
    /* NO_EXECUTE is a software contract when NX is unavailable; never place
     * EFER.NXE's page-table bit in hardware page tables unless CPUID proved
     * that the CPU implements it. */
    if (!cpu_has(ZEROOS_CPU_FEATURE_NX))
        flags &= ~VMM_NO_EXECUTE;
    return flags & (VMM_LEAF_FLAGS | VMM_NO_EXECUTE);
}

static int physical_page_valid(uint64_t physical_address) {
    return memory_is_usable_range(physical_address,VMM_PAGE_SIZE) &&
           memory_page_is_allocated(physical_address);
}

static inline void write_cr3(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
}

static inline void invalidate_page(uint64_t address) {
    if (tlb_invalidate_page(address)!=0)
        for (;;) __asm__ volatile ("cli; hlt");
}

static void zero_page(uint64_t *page) {
    for (uint64_t i = 0; i < ENTRY_COUNT; ++i)
        page[i] = 0;
}

static int page_table_empty(const uint64_t *table) {
    for (uint64_t i=0; i<ENTRY_COUNT; ++i)
        if (table[i] & VMM_PRESENT)
            return 0;
    return 1;
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
static int split_2m(uint64_t *pd, uint64_t index, uint64_t owner_root) {
    uint64_t old = pd[index];
    if (!(old & VMM_PRESENT) || !(old & HUGE_PAGE_2M))
        return 0;

    void *page = page_alloc();
    if (!page)
        return -1;

    uint64_t *pt = (uint64_t *)page;
    uint64_t base = old & 0x000ffffffe00000ULL;
    uint64_t flags = hardware_leaf_flags(old &
                            (VMM_PRESENT | VMM_WRITABLE | VMM_USER |
                             VMM_WRITE_THROUGH | VMM_CACHE_DISABLE |
                             0x100ULL | VMM_NO_EXECUTE));

    zero_page(pt);

    for (uint64_t i = 0; i < ENTRY_COUNT; ++i)
        pt[i] = (base + i * VMM_PAGE_SIZE) | flags;

    /*
     * The old entry is no longer a huge mapping.  Clear it before installing
     * the PT so no CPU can retain a stale translation for the old page size.
     */
    pd[index] = 0;
    if (owner_root==active_root_physical) {
        if (tlb_flush_all()!=0)
            for (;;) __asm__ volatile ("cli; hlt");
    }
    pd[index] = ((uint64_t)pt & PAGE_MASK) |
                VMM_PRESENT | VMM_WRITABLE |
                (old & VMM_USER);
    if (owner_root==active_root_physical) {
        if (tlb_flush_all()!=0)
            for (;;) __asm__ volatile ("cli; hlt");
    }

    return 0;
}

int vmm_init(void) {
    if (tlb_init()!=0)
        return -1;
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
            flags |= hardware_leaf_flags(VMM_NO_EXECUTE);
        pd[i] = i * HUGE_PAGE_SIZE | flags;
    }

    write_cr3(root_physical);
    active_root_physical=root_physical;
    return 0;
}

int vmm_map_page(uint64_t virtual_address,
                 uint64_t physical_address,
                 uint64_t flags) {
    if (!root_table || !canonical_address(virtual_address))
        return -1;
    if ((virtual_address & (VMM_PAGE_SIZE - 1)) != 0)
        return -1;
    if ((physical_address & (VMM_PAGE_SIZE - 1)) != 0 ||
        !physical_page_valid(physical_address) ||
        !mapping_flags_valid(flags))
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
        if (split_2m(pd, pd_index, root_physical) != 0)
            return -1;
    }

    uint64_t *pt = ensure_table(pd, pd_index, flags);
    if (!pt)
        return -1;

    if (pt[pt_index] & VMM_PRESENT)
        return -1;
    if (memory_page_retain(physical_address)!=0)
        return -1;

    pt[pt_index] = (physical_address & PHYS_MASK) |
                   VMM_PRESENT | VMM_INTERNAL_OWNED |
                   hardware_leaf_flags(flags);

    if (active_root_physical==root_physical)
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
        if (split_2m(pd, pd_index, root_physical) != 0)
            return -1;
    }

    uint64_t e3 = pd[pd_index];
    if (!(e3 & VMM_PRESENT))
        return -1;

    uint64_t *pt = table_from_entry(e3);
    uint64_t old_leaf=pt[pt_index];
    if (!(old_leaf & VMM_PRESENT))
        return -1;
    if ((old_leaf & VMM_INTERNAL_OWNED) &&
        memory_page_release(old_leaf & PHYS_MASK)!=0)
        return -1;

    pt[pt_index] = 0;
    if (active_root_physical==root_physical)
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
    if (page_count==0)
        return 0;
    if (page_count > (~0ULL/VMM_PAGE_SIZE) ||
        virtual_address > ~0ULL-(page_count-1ULL)*VMM_PAGE_SIZE ||
        physical_address > ~0ULL-(page_count-1ULL)*VMM_PAGE_SIZE ||
        !mapping_flags_valid(flags))
        return -1;

    for (uint64_t i=0; i<page_count; ++i) {
        if (vmm_map_page(virtual_address+i*VMM_PAGE_SIZE,
                         physical_address+i*VMM_PAGE_SIZE,flags)!=0) {
            while (i>0) {
                --i;
                vmm_unmap_page(virtual_address+i*VMM_PAGE_SIZE);
            }
            return -1;
        }
    }
    return 0;
}

int vmm_unmap_range(uint64_t virtual_address, uint64_t page_count) {
    if (page_count==0)
        return 0;
    if (page_count > (~0ULL/VMM_PAGE_SIZE) ||
        virtual_address > ~0ULL-(page_count-1ULL)*VMM_PAGE_SIZE)
        return -1;
    for (uint64_t i=0; i<page_count; ++i) {
        if (vmm_unmap_page(virtual_address+i*VMM_PAGE_SIZE)!=0)
            return -1;
    }
    return 0;
}

int vmm_protect_page(uint64_t virtual_address, uint64_t flags) {
    if (!root_table || !canonical_address(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE - 1)) != 0 ||
        !mapping_flags_valid(flags))
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
        if (split_2m(pd, pd_index, root_physical) != 0) return -1;
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

    pt[pt_index] = (pt[pt_index] & (PHYS_MASK | VMM_INTERNAL_OWNED)) |
                   VMM_PRESENT |
                   hardware_leaf_flags(flags);
    if (active_root_physical==root_physical)
        invalidate_page(virtual_address);
    return 0;
}

int vmm_map_mmio_page(uint64_t virtual_address, uint64_t physical_address,
                      uint64_t flags) {
    if (!root_table || !canonical_address(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE-1ULL)) ||
        (physical_address & (VMM_PAGE_SIZE-1ULL)) ||
        (physical_address & ~PHYS_MASK) ||
        (flags & VMM_USER) || !(flags & VMM_CACHE_DISABLE) ||
        !(flags & VMM_NO_EXECUTE) || !mapping_flags_valid(flags))
        return -1;

    uint64_t pml4_index=(virtual_address>>39)&0x1ffULL;
    uint64_t pdpt_index=(virtual_address>>30)&0x1ffULL;
    uint64_t pd_index=(virtual_address>>21)&0x1ffULL;
    uint64_t pt_index=(virtual_address>>12)&0x1ffULL;
    uint64_t *pdpt=ensure_table(root_table,pml4_index,0);
    if (!pdpt) return -1;
    uint64_t *pd=ensure_table(pdpt,pdpt_index,0);
    if (!pd || (pd[pd_index]&HUGE_PAGE_2M)) return -1;
    uint64_t *pt=ensure_table(pd,pd_index,0);
    if (!pt || (pt[pt_index]&VMM_PRESENT)) return -1;

    pt[pt_index]=(physical_address&PHYS_MASK)|VMM_PRESENT|
                 hardware_leaf_flags(flags);
    if (active_root_physical==root_physical)
        invalidate_page(virtual_address);
    return 0;
}

int vmm_unmap_mmio_page(uint64_t virtual_address) {
    if (!root_table || !canonical_address(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE-1ULL)))
        return -1;

    uint64_t pml4_index=(virtual_address>>39)&0x1ffULL;
    uint64_t pdpt_index=(virtual_address>>30)&0x1ffULL;
    uint64_t pd_index=(virtual_address>>21)&0x1ffULL;
    uint64_t pt_index=(virtual_address>>12)&0x1ffULL;
    uint64_t e1=root_table[pml4_index];
    if (!(e1&VMM_PRESENT) || (e1&HUGE_PAGE_2M)) return -1;
    uint64_t *pdpt=table_from_entry(e1);
    uint64_t e2=pdpt[pdpt_index];
    if (!(e2&VMM_PRESENT) || (e2&HUGE_PAGE_2M)) return -1;
    uint64_t *pd=table_from_entry(e2);
    uint64_t e3=pd[pd_index];
    if (!(e3&VMM_PRESENT) || (e3&HUGE_PAGE_2M)) return -1;
    uint64_t *pt=table_from_entry(e3);
    uint64_t old=pt[pt_index];
    if (!(old&VMM_PRESENT) || (old&VMM_INTERNAL_OWNED)) return -1;
    pt[pt_index]=0;

    int active=active_root_physical==root_physical;
    if (active) invalidate_page(virtual_address);
    if (page_table_empty(pt)) {
        page_free(pt);
        pd[pd_index]=0;
        if (page_table_empty(pd)) {
            page_free(pd);
            pdpt[pdpt_index]=0;
            if (page_table_empty(pdpt)) {
                page_free(pdpt);
                root_table[pml4_index]=0;
            }
        }
    }
    if (active) {
        if (tlb_flush_all()!=0)
            for (;;) __asm__ volatile ("cli; hlt");
    }
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
    space->mapped_pages = 0;
    space->max_pages = ~0ULL;

    /* Slot 0 contains the kernel's identity/direct map and is shared. */
    space->root[0] = root_table[0];
    return 0;
}

int vmm_space_destroy(struct vmm_space *space) {
    if (!space || !space->root)
        return -1;
    if (space->root_physical==active_root_physical)
        return -1;
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
                uint64_t *pt=table_from_entry(e3);
                for (uint64_t k=0; k<ENTRY_COUNT; ++k) {
                    uint64_t e4=pt[k];
                    if ((e4&VMM_PRESENT) && (e4&VMM_INTERNAL_OWNED)) {
                        if (space->mapped_pages==0 ||
                            memory_page_release(e4&PHYS_MASK)!=0)
                            return -1;
                        --space->mapped_pages;
                    }
                }
                page_free(pt);
            }
            page_free(pd);
        }
        page_free(pdpt);
    }
    page_free(space->root);
    space->root = 0;
    space->root_physical = 0;
    space->mapped_pages = 0;
    space->max_pages = 0;
    return 0;
}

int vmm_space_map_page(struct vmm_space *space, uint64_t virtual_address,
                       uint64_t physical_address, uint64_t flags) {
    if (!space || !space->root || space->mapped_pages>=space->max_pages ||
        space->mapped_pages==~0ULL || !space_canonical(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE-1)) ||
        (physical_address & (VMM_PAGE_SIZE-1)) ||
        !physical_page_valid(physical_address) ||
        !mapping_flags_valid(flags))
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
        if (split_2m(pd,pd_i,space->root_physical) != 0) return -1;
    }

    uint64_t *pt = space_ensure_table(pd,pd_i,flags);
    if (!pt || (pt[pt_i] & VMM_PRESENT)) return -1;
    if (memory_page_retain(physical_address)!=0)
        return -1;

    pt[pt_i] = (physical_address & PHYS_MASK) |
               VMM_PRESENT | VMM_INTERNAL_OWNED |
               hardware_leaf_flags(flags);
    ++space->mapped_pages;
    if (space->root_physical == active_root_physical)
        invalidate_page(virtual_address);
    return 0;
}

int vmm_space_unmap_page(struct vmm_space *space, uint64_t virtual_address) {
    uint64_t pml4_index=(virtual_address>>39)&0x1ff;
    uint64_t pdpt_index=(virtual_address>>30)&0x1ff;
    uint64_t pd_index=(virtual_address>>21)&0x1ff;
    uint64_t pt_index=(virtual_address>>12)&0x1ff;

    if (!space || !space->root || pml4_index!=VMM_USER_PML4_INDEX ||
        !space_canonical(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE-1)))
        return -1;

    uint64_t e1=space->root[pml4_index];
    if (!(e1&VMM_PRESENT)) return -1;
    uint64_t *pdpt=table_from_entry(e1);
    uint64_t e2=pdpt[pdpt_index];
    if (!(e2&VMM_PRESENT) || (e2&HUGE_PAGE_2M)) return -1;
    uint64_t *pd=table_from_entry(e2);
    uint64_t e3=pd[pd_index];
    if (!(e3&VMM_PRESENT) || (e3&HUGE_PAGE_2M)) return -1;
    uint64_t *pt=table_from_entry(e3);
    uint64_t old_leaf=pt[pt_index];
    if (!(old_leaf&VMM_PRESENT)) return -1;
    if ((old_leaf&VMM_INTERNAL_OWNED) &&
        (space->mapped_pages==0 ||
         memory_page_release(old_leaf&PHYS_MASK)!=0))
        return -1;
    if (old_leaf&VMM_INTERNAL_OWNED)
        --space->mapped_pages;

    pt[pt_index]=0;
    int active=space->root_physical==active_root_physical;
    if (active)
        invalidate_page(virtual_address);

    /* Reclaim empty private paging levels immediately. */
    if (page_table_empty(pt)) {
        page_free(pt);
        pd[pd_index]=0;
        if (page_table_empty(pd)) {
            page_free(pd);
            pdpt[pdpt_index]=0;
            if (page_table_empty(pdpt)) {
                page_free(pdpt);
                space->root[pml4_index]=0;
            }
        }
    }
    if (active) {
        if (tlb_flush_all()!=0)
            for (;;) __asm__ volatile ("cli; hlt");
    }
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

int vmm_space_set_page_limit(struct vmm_space *space, uint64_t max_pages) {
    if (!space || !space->root || max_pages==0 ||
        max_pages<space->mapped_pages)
        return -1;
    space->max_pages=max_pages;
    return 0;
}

uint64_t vmm_space_mapped_pages(const struct vmm_space *space) {
    return space && space->root ? space->mapped_pages : 0;
}

int vmm_space_is_user_range(const struct vmm_space *space,
                            uint64_t virtual_address, uint64_t length,
                            uint64_t write) {
    if (!space || !space->root || length==0 ||
        !space_canonical(virtual_address) ||
        virtual_address>~0ULL-length)
        return 0;

    uint64_t end=virtual_address+length-1ULL;
    if (!space_canonical(end))
        return 0;
    uint64_t cursor=virtual_address&~(VMM_PAGE_SIZE-1ULL);
    uint64_t last=end&~(VMM_PAGE_SIZE-1ULL);

    for (;;) {
        if (((cursor>>39)&0x1ffULL)!=VMM_USER_PML4_INDEX)
            return 0;
        uint64_t e1=space->root[VMM_USER_PML4_INDEX];
        if (!(e1&VMM_PRESENT) || !(e1&VMM_USER)) return 0;
        uint64_t *pdpt=table_from_entry(e1);
        uint64_t e2=pdpt[(cursor>>30)&0x1ffULL];
        if (!(e2&VMM_PRESENT) || !(e2&VMM_USER)) return 0;
        uint64_t *pd=table_from_entry(e2);
        uint64_t e3=pd[(cursor>>21)&0x1ffULL];
        if (!(e3&VMM_PRESENT) || !(e3&VMM_USER)) return 0;
        if (e3&HUGE_PAGE_2M) {
            if (write && !(e3&VMM_WRITABLE)) return 0;
        } else {
            uint64_t *pt=table_from_entry(e3);
            uint64_t e4=pt[(cursor>>12)&0x1ffULL];
            if (!(e4&VMM_PRESENT) || !(e4&VMM_USER) ||
                (write && !(e4&VMM_WRITABLE)))
                return 0;
        }
        if (cursor==last) break;
        cursor+=VMM_PAGE_SIZE;
    }
    return 1;
}

int vmm_space_activate(const struct vmm_space *space) {
    if (!space || !space->root || space->root_physical==0)
        return -1;
    write_cr3(space->root_physical);
    active_root_physical=space->root_physical;
    return 0;
}

int vmm_activate_kernel(void) {
    if (!root_table || root_physical==0)
        return -1;
    write_cr3(root_physical);
    active_root_physical=root_physical;
    return 0;
}
