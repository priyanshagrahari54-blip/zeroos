#include "vmm.h"
#include "memory.h"
#include "heap.h"

#define ENTRY_COUNT 512ULL
#define PAGE_MASK 0x000ffffffffff000ULL
#define HUGE_PAGE_2M 0x080ULL
#define HUGE_PAGE_SIZE 0x200000ULL
#define PHYS_MASK 0x000ffffffffff000ULL

#define VMM_LEAF_FLAGS 0x00000000000001ffULL

/* User mappings live in PML4 slot 254 (the 0x00007f... canonical range). */
#define VMM_USER_PML4_INDEX 254ULL

static uint64_t *root_table;
static uint64_t root_physical;

static uint64_t active_root;
static uint16_t active_pcid;
static int pcid_enabled;
static int invpcid_available;
static uint32_t pcid_bitmap; /* bit N set => PCID N in use (bits 1-31) */

/*
 * Invalidate every TLB entry tagged with one PCID (INVPCID type 1 = "All").
 * Required when a PCID is returned to the allocator: a later space reusing
 * the same PCID must never see the dead space's translations, including
 * pages that may now be owned by a different process.
 */
static void invpcid_all(uint16_t pcid) {
    /*
     * 128-bit INVPCID descriptor: PCID in bits [11:0], all other bits zero
     * (type 1 = single-context invalidation; bits [63:12] must be zero or
     * the CPU raises #GP).
     *
     * The instruction bytes are emitted directly: this binutils version
     * cannot assemble INVPCID (it does not infer the m128 operand size).
     * 66 0F 38 82 08 = INVPCID %rax, (%rcx) with the type in ECX.
     */
    uint64_t operand[2] = {(uint64_t)(pcid & 0x3ffULL), 0};
    if (!invpcid_available)
        return;
    __asm__ volatile (".byte 0x66, 0x0f, 0x38, 0x82, 0x08"
                      :
                      : "a"(operand), "c"((uint32_t)1)
                      : "memory");
}

static int cpu_has_pcid(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(1));
    return (ecx & (1U << 17)) != 0;
}

static int cpu_has_invpcid(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(7), "c"(0));
    return (ebx & (1U << 10)) != 0;
}

int vmm_pcid_enabled(void) { return pcid_enabled; }

uint64_t vmm_active_root(void) { return active_root; }

uint16_t vmm_pcid_alloc(void) {
    for (uint16_t pcid = 1; pcid <= 31; ++pcid) {
        uint32_t bit = 1U << pcid;
        if (!(pcid_bitmap & bit)) {
            pcid_bitmap |= bit;
            return pcid;
        }
    }
    return 0;
}

void vmm_pcid_free(uint16_t pcid) {
    if (pcid >= 1 && pcid <= 31)
        pcid_bitmap &= ~(1U << pcid);
}

void vmm_flush_tlb(void) {
    /*
     * Without PCID, TLB entries are untagged by address space. Writing the
     * currently loaded CR3 value again invalidates the entire TLB.
     */
    __asm__ volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3"
                      : : : "rax", "memory");
}

/*
 * Load an address-space root. The PCID bits are set only when PCID is
 * enabled; callers must flush the TLB first (vmm_flush_tlb) when PCID is
 * unavailable and the root changes.
 */
void vmm_load_root(uint64_t root_physical_value, uint16_t pcid) {
    uint64_t value = root_physical_value;
    if (pcid_enabled && pcid)
        value |= (uint64_t)pcid;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
    active_root = root_physical_value;
    active_pcid = pcid;
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
 *
 * base_virtual is the 2 MiB-aligned virtual base of the PDE being split; it
 * is required to invalidate the exact 4 KiB range in the TLB. The old
 * implementation reloaded the kernel CR3, which was wrong for page tables
 * belonging to a different address space and would have switched the CPU's
 * active translation root mid-operation.
 */
static int split_2m(uint64_t *pd, uint64_t index, uint64_t base_virtual) {
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
     * Clear the PDE before installing the PT, then invalidate the 4 KiB
     * range so no CPU can retain a stale huge-page translation. invlpg is
     * correct whether or not this space is the one currently loaded in CR3.
     */
    pd[index] = 0;
    for (uint64_t i = 0; i < ENTRY_COUNT; ++i)
        invalidate_page(base_virtual + i * VMM_PAGE_SIZE);
    pd[index] = ((uint64_t)pt & PAGE_MASK) |
                VMM_PRESENT | VMM_WRITABLE |
                (old & VMM_USER);

    return 0;
}

/* NX is a required security property, not an optional mapping hint.
 * Check CPUID before touching EFER.NXE; never install NX entries with NXE
 * clear (bit 63 would be reserved and every access would fault).
 */
static int enable_nx(void) {
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                      : "a"(0x80000000U), "c"(0));
    if (a < 0x80000001U) return -1;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                      : "a"(0x80000001U), "c"(0));
    if (!(d & (1U << 20))) return -1;
    __asm__ volatile ("rdmsr" : "=a"(a), "=d"(d) : "c"(0xc0000080U));
    a |= 1U << 11;
    __asm__ volatile ("wrmsr" : : "a"(a), "d"(d), "c"(0xc0000080U)
                      : "memory");
    __asm__ volatile ("rdmsr" : "=a"(a), "=d"(d) : "c"(0xc0000080U));
    return (a & (1U << 11)) ? 0 : -1;
}

int vmm_init(void) {
    if (enable_nx() != 0) return -1;
    void *root = page_alloc();
    if (!root)
        return -1;

    /*
     * PCID must be detected before the first root load. CR4.PCIDE is set
     * by the boot code when CPUID reports the feature; the two must agree.
     */
    pcid_enabled = cpu_has_pcid();
    invpcid_available = pcid_enabled && cpu_has_invpcid();
    pcid_bitmap = 0;
    active_root = 0;
    active_pcid = 0;

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

    vmm_load_root(root_physical, 0);
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
        if (split_2m(pd, pd_index, virtual_address & ~(HUGE_PAGE_SIZE - 1ULL)) != 0)
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
        if (split_2m(pd, pd_index, virtual_address & ~(HUGE_PAGE_SIZE - 1ULL)) != 0)
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

/*
 * Per-address-space user-range validation. Walks the supplied space's root,
 * so it can be used for the space of a user process that is not the one
 * currently loaded in CR3. Unlike the kernel-root variant, this requires
 * the user bit on every intermediate entry as well: process page tables
 * are writable by the process if the kernel ever exposes a table page, so
 * the walk must not trust intermediate entries that the kernel would never
 * create without the user bit (space_ensure_table() always sets it for
 * user mappings).
 */
int vmm_space_is_user_range(const struct vmm_space *space, uint64_t virtual_address,
                            uint64_t length, uint64_t write) {
    if (!space || !space->root || length == 0 || !canonical_address(virtual_address))
        return 0;
    if (virtual_address + length < virtual_address)
        return 0;

    uint64_t end = virtual_address + length - 1;
    if (!canonical_address(end))
        return 0;

    if (((virtual_address >> 39) & 0x1ff) != VMM_USER_PML4_INDEX ||
        ((end >> 39) & 0x1ff) != VMM_USER_PML4_INDEX)
        return 0;

    uint64_t cursor = virtual_address & ~(VMM_PAGE_SIZE - 1ULL);
    uint64_t last = end & ~(VMM_PAGE_SIZE - 1ULL);

    for (;;) {
        uint64_t pdpt_index = (cursor >> 30) & 0x1ff;
        uint64_t pd_index = (cursor >> 21) & 0x1ff;
        uint64_t pt_index = (cursor >> 12) & 0x1ff;

        uint64_t e1 = space->root[VMM_USER_PML4_INDEX];
        if (!(e1 & VMM_PRESENT) || !(e1 & VMM_USER)) return 0;
        if ((e1 & HUGE_PAGE_2M) || (write && !(e1 & VMM_WRITABLE))) return 0;
        uint64_t *pdpt = table_from_entry(e1);
        uint64_t e2 = pdpt[pdpt_index];
        if (!(e2 & VMM_PRESENT) || !(e2 & VMM_USER)) return 0;
        if ((e2 & HUGE_PAGE_2M) || (write && !(e2 & VMM_WRITABLE))) return 0;
        uint64_t *pd = table_from_entry(e2);
        uint64_t e3 = pd[pd_index];
        if (!(e3 & VMM_PRESENT) || !(e3 & VMM_USER)) return 0;

        if (write && !(e3 & VMM_WRITABLE)) return 0;
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
    uint64_t e2 = pdpt[pdpt_index];
    if (!(e2 & VMM_PRESENT)) return -1;
    uint64_t *pd = table_from_entry(e2);

    if (pd[pd_index] & HUGE_PAGE_2M) {
        if (split_2m(pd, pd_index, virtual_address & ~(HUGE_PAGE_SIZE - 1ULL)) != 0) return -1;
    }

    uint64_t e3 = pd[pd_index];
    if (!(e3 & VMM_PRESENT)) return -1;
    uint64_t *pt = table_from_entry(e3);
    if (!(pt[pt_index] & VMM_PRESENT)) return -1;

    pt[pt_index] = (pt[pt_index] & PHYS_MASK) |
                   VMM_PRESENT |
                   (flags & (VMM_LEAF_FLAGS | VMM_NO_EXECUTE));
    invalidate_page(virtual_address);
    return 0;
}

/*
 * Kernel-root user-range validation. Hardware semantics: the MMU ignores
 * U/S on intermediate (non-leaf) entries, so only presence is required
 * there; the leaf must be present, user-accessible, and (for write)
 * writable. This walks the kernel root, which the kernel alone controls.
 *
 * Do NOT reuse this for process spaces: vmm_space_is_user_range() is
 * deliberately stricter (it also requires the user bit on every
 * intermediate entry) because a process's page tables can be corrupted
 * by the process itself.
 */
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
        if (!(e1 & VMM_PRESENT)) return 0;
        uint64_t *pdpt = table_from_entry(e1);
        uint64_t e2 = pdpt[pdpt_index];
        if (!(e2 & VMM_PRESENT)) return 0;
        uint64_t *pd = table_from_entry(e2);
        uint64_t e3 = pd[pd_index];
        if (!(e3 & VMM_PRESENT)) return 0;

        if (e3 & HUGE_PAGE_2M) {
            if (!(e3 & VMM_USER)) return 0;
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
    space->pcid = 0;
    space->has_pcid = 0;
    space->owned_pages = 0;
    space->owned_page_count = 0;

    /* Slot 0 contains the kernel's identity/direct map and is shared. */
    space->root[0] = root_table[0];
    return 0;
}

void vmm_space_destroy(struct vmm_space *space) {
    if (!space || !space->root) return;

    /*
     * Destruction must never run with this space loaded in CR3: the page
     * tables being freed are the active translation root.
     */
    if (active_root == space->root_physical) {
        vmm_load_root(root_physical, 0);
    }

    /*
     * Retire this space's PCID from the TLB before it can be reused by a
     * different process (see invpcid_all).
     */
    if (space->has_pcid && pcid_enabled)
        invpcid_all(space->pcid);

    /* Release every physical page this space owns (user data pages). */
    struct vmm_owned_page *cursor = space->owned_pages;
    while (cursor) {
        struct vmm_owned_page *next = cursor->next;
        page_free((void *)cursor->physical);
        kfree(cursor);
        cursor = next;
    }
    space->owned_pages = 0;
    space->owned_page_count = 0;

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
    space->has_pcid = 0;
    space->pcid = 0;
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

    /*
     * W^X policy for user mappings: a user page must never be both
     * writable and executable. (flags & VMM_WRITABLE) together with the
     * absence of VMM_NO_EXECUTE would be writable+executable.
     */
    if ((flags & VMM_WRITABLE) && !(flags & VMM_NO_EXECUTE))
        return -1;

    if (vmm_space_own_page(space, physical_address) != 0)
        return -1;

    uint64_t *pdpt = space_ensure_table(space->root,pml4,flags);
    if (!pdpt) {
        vmm_space_release_page(space, physical_address);
        return -1;
    }
    uint64_t *pd = space_ensure_table(pdpt,pdpt_i,flags);
    if (!pd) {
        vmm_space_release_page(space, physical_address);
        return -1;
    }

    if (pd[pd_i] & HUGE_PAGE_2M) {
        if (split_2m(pd,pd_i,virtual_address & ~(HUGE_PAGE_SIZE - 1ULL)) != 0) {
            vmm_space_release_page(space, physical_address);
            return -1;
        }
    }

    uint64_t *pt = space_ensure_table(pd,pd_i,flags);
    if (!pt || (pt[pt_i] & VMM_PRESENT)) {
        vmm_space_release_page(space, physical_address);
        return -1;
    }

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
    uint64_t physical=pt[idx]&PHYS_MASK;
    pt[idx]=0;
    if (space->root_physical==root_physical) invalidate_page(virtual_address);
    vmm_space_release_page(space, physical);
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
    if (!vmm_pcid_enabled() && active_root != space->root_physical)
        vmm_flush_tlb();
    vmm_load_root(space->root_physical, space->has_pcid ? space->pcid : 0);
    return 0;
}

/*
 * Physical-page ownership inside one address space. The descriptors are
 * heap allocations so spaces of unbounded page counts stay supported.
 */
int vmm_space_own_page(struct vmm_space *space, uint64_t physical) {
    struct vmm_owned_page *cursor;

    if (!space || !space->root) return -1;
    if ((physical & (VMM_PAGE_SIZE - 1)) != 0) return -1;

    for (cursor = space->owned_pages; cursor; cursor = cursor->next)
        if (cursor->physical == physical)
            return -1; /* already owned by this space */

    cursor = (struct vmm_owned_page *)kmalloc(sizeof(*cursor));
    if (!cursor) return -1;
    cursor->physical = physical;
    cursor->next = space->owned_pages;
    space->owned_pages = cursor;
    ++space->owned_page_count;
    return 0;
}

int vmm_space_release_page(struct vmm_space *space, uint64_t physical) {
    struct vmm_owned_page **cursor;

    if (!space || !space->root) return -1;
    for (cursor = &space->owned_pages; *cursor; cursor = &(*cursor)->next) {
        if ((*cursor)->physical == physical) {
            *cursor = (*cursor)->next;
            kfree(*cursor);
            --space->owned_page_count;
            return 0;
        }
    }
    return -1;
}
