#include "elf.h"
#include "memory.h"
#include "vmm.h"

static int elf_loader_elf_loader_add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > ~0ULL - a) return 1;
    *out = a + b;
    return 0;
}

int elf64_load_image(const void *data, uint64_t size,
                     struct vmm_space *space, struct elf_image *image) {
    const uint8_t *bytes=(const uint8_t *)data;

    if (!data || !space || !space->root || !image ||
        elf64_validate_image(data,size,image)!=0)
        return -1;

    uint64_t total_pages = 0;

    /*
     * Bound total mapping work before touching the allocator. A malformed
     * executable must not turn the loader into an effectively unbounded
     * page-allocation loop.
     */
    for (uint16_t si=0; si<image->segment_count; ++si) {
        const struct elf_load_segment *seg=&image->segments[si];
        uint64_t end, first, last, pages;
        if (add_overflow_u64(seg->virtual_address,seg->memory_size,&end))
            return -1;
        first=seg->virtual_address & ~(VMM_PAGE_SIZE-1ULL);
        if (end > ~0ULL - (VMM_PAGE_SIZE-1ULL))
            return -1;
        last=(end + VMM_PAGE_SIZE-1ULL) & ~(VMM_PAGE_SIZE-1ULL);
        pages=(last-first)/VMM_PAGE_SIZE;
        if (pages > ZEROOS_ELF_MAX_LOAD_PAGES-total_pages)
            return -1;
        total_pages += pages;
    }

    /*
     * Preflight every destination page before allocating anything. This
     * guarantees rollback cannot accidentally destroy an existing mapping.
     */
    for (uint16_t si=0; si<image->segment_count; ++si) {
        const struct elf_load_segment *seg=&image->segments[si];
        uint64_t end;
        if (add_overflow_u64(seg->virtual_address,seg->memory_size,&end))
            return -1;
        uint64_t first=seg->virtual_address & ~(VMM_PAGE_SIZE-1ULL);
        uint64_t last=(end + VMM_PAGE_SIZE-1ULL) & ~(VMM_PAGE_SIZE-1ULL);
        for (uint64_t va=first; va<last; va+=VMM_PAGE_SIZE)
            if (vmm_space_is_mapped(space,va))
                return -1;
    }

    for (uint16_t si=0; si<image->segment_count; ++si) {
        const struct elf_load_segment *seg=&image->segments[si];
        uint64_t end;
        if (add_overflow_u64(seg->virtual_address,seg->memory_size,&end))
            goto fail;
        uint64_t first=seg->virtual_address & ~(VMM_PAGE_SIZE-1ULL);
        uint64_t last=(end + VMM_PAGE_SIZE-1ULL) & ~(VMM_PAGE_SIZE-1ULL);

        for (uint64_t va=first; va<last; va+=VMM_PAGE_SIZE) {
            void *page=page_alloc_zero();
            if (!page) goto fail;

            uint64_t page_seg_start = va < seg->virtual_address
                                    ? 0 : va-seg->virtual_address;
            uint64_t page_prefix = va < seg->virtual_address
                                 ? seg->virtual_address-va : 0;
            if (page_seg_start < seg->file_size) {
                uint64_t copy_len=seg->file_size-page_seg_start;
                if (copy_len > VMM_PAGE_SIZE-page_prefix)
                    copy_len=VMM_PAGE_SIZE-page_prefix;
                for (uint64_t i=0;i<copy_len;++i)
                    ((uint8_t *)page)[page_prefix+i]=bytes[seg->file_offset+page_seg_start+i];
            }

            uint64_t flags=VMM_USER;
            if (seg->flags & ZEROOS_PF_W)
                flags |= VMM_WRITABLE | VMM_NO_EXECUTE;
            else if (!(seg->flags & ZEROOS_PF_X))
                flags |= VMM_NO_EXECUTE;

            if (vmm_space_map_page(space,va,(uint64_t)page,flags)!=0) {
                page_free(page);
                goto fail;
            }
        }
    }
    return 0;

fail:
    /*
     * Only pages in the image's validated footprint are eligible here;
     * preflight proved that all of them were unmapped on entry.
     */
    for (uint16_t si=0; si<image->segment_count; ++si) {
        const struct elf_load_segment *seg=&image->segments[si];
        uint64_t end;
        if (add_overflow_u64(seg->virtual_address,seg->memory_size,&end))
            continue;
        uint64_t first=seg->virtual_address & ~(VMM_PAGE_SIZE-1ULL);
        uint64_t last=(end + VMM_PAGE_SIZE-1ULL) & ~(VMM_PAGE_SIZE-1ULL);
        for (uint64_t va=first; va<last; va+=VMM_PAGE_SIZE)
            if (vmm_space_is_mapped(space,va))
                (void)vmm_space_unmap_page(space,va);
    }
    return -1;
}
