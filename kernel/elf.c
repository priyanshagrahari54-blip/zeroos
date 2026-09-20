#include "elf.h"
#include "memory.h"

static int add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > ~0ULL - a)
        return 1;
    *out = a + b;
    return 0;
}

static int range_inside(uint64_t offset, uint64_t length, uint64_t size) {
    uint64_t end;
    return !add_overflow_u64(offset, length, &end) && end <= size;
}

static int is_power_of_two(uint64_t value) {
    return value && (value & (value - 1ULL)) == 0;
}

static int range_end(uint64_t start, uint64_t length, uint64_t *end) {
    return !add_overflow_u64(start, length, end);
}

int elf64_validate_image(const void *data, uint64_t size,
                         struct elf_image *out) {
    const uint8_t *bytes=(const uint8_t *)data;
    const struct elf64_ehdr *eh;
    uint64_t last_end=0;
    int have_exec=0;

    if (!data || !out || size < sizeof(struct elf64_ehdr))
        return -1;

    eh=(const struct elf64_ehdr *)bytes;
    if (eh->ident[0]!=0x7f || eh->ident[1]!='E' ||
        eh->ident[2]!='L' || eh->ident[3]!='F')
        return -1;
    if (eh->ident[4]!=ZEROOS_ELF64_CLASS ||
        eh->ident[5]!=ZEROOS_ELF64_DATA_LSB ||
        eh->ident[6]!=ZEROOS_ELF_VERSION_CURRENT)
        return -1;
    if (eh->type!=ZEROOS_ELF_TYPE_EXEC ||
        eh->machine!=ZEROOS_ELF_MACHINE_X86_64 ||
        eh->version!=ZEROOS_ELF_VERSION_CURRENT)
        return -1;
    if (eh->ehsize!=sizeof(struct elf64_ehdr) ||
        eh->phentsize!=sizeof(struct elf64_phdr) ||
        eh->phnum==0 || eh->phnum>ZEROOS_ELF_MAX_LOAD_SEGMENTS)
        return -1;
    if (!range_inside(eh->phoff,
                      (uint64_t)eh->phnum * eh->phentsize, size))
        return -1;

    out->entry=eh->entry;
    out->segment_count=0;

    for (uint16_t i=0; i<eh->phnum; ++i) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)(bytes + eh->phoff +
                                        (uint64_t)i * eh->phentsize);
        uint64_t vend, fend;

        if (ph->type!=ZEROOS_PT_LOAD)
            continue;
        if (ph->memsz==0 || ph->filesz>ph->memsz)
            return -1;
        if (ph->flags & ~(ZEROOS_PF_R | ZEROOS_PF_W | ZEROOS_PF_X))
            return -1;
        /* ZEROOS never loads a writable executable segment; callers must
         * express code/data separation explicitly at page granularity. */
        if ((ph->flags & (ZEROOS_PF_W | ZEROOS_PF_X)) ==
            (ZEROOS_PF_W | ZEROOS_PF_X))
            return -1;
        if (!range_inside(ph->offset,ph->filesz,size))
            return -1;
        if (!range_end(ph->vaddr,ph->memsz,&vend))
            return -1;
        if (!range_end(ph->offset,ph->filesz,&fend))
            return -1;
        (void)fend;

        if (ph->align>1) {
            if (!is_power_of_two(ph->align) ||
                (ph->vaddr % ph->align)!=(ph->offset % ph->align))
                return -1;
        }
        if ((ph->vaddr & 0xfffULL) != (ph->offset & 0xfffULL))
            return -1;

        /* Stage 1 user ABI reserves slot 254 and rejects low/canonical kernel VAs. */
        if (ph->vaddr < 0x00007f0000000000ULL ||
            vend > 0x0000800000000000ULL)
            return -1;

        /* Load segments must not overlap in virtual memory or page footprint. */
        for (uint16_t j=0; j<out->segment_count; ++j) {
            uint64_t other_end;
            if (!range_end(out->segments[j].virtual_address,
                           out->segments[j].memory_size,&other_end))
                return -1;
            if (ph->vaddr < other_end &&
                out->segments[j].virtual_address < vend)
                return -1;
            uint64_t a0 = ph->vaddr & ~(VMM_PAGE_SIZE - 1ULL);
            uint64_t a1 = (vend + VMM_PAGE_SIZE - 1ULL) & ~(VMM_PAGE_SIZE - 1ULL);
            uint64_t b0 = out->segments[j].virtual_address & ~(VMM_PAGE_SIZE - 1ULL);
            uint64_t b1 = (other_end + VMM_PAGE_SIZE - 1ULL) & ~(VMM_PAGE_SIZE - 1ULL);
            if (a0 < b1 && b0 < a1)
                return -1;
        }

        if (out->segment_count>=ZEROOS_ELF_MAX_LOAD_SEGMENTS)
            return -1;
        out->segments[out->segment_count].file_offset=ph->offset;
        out->segments[out->segment_count].virtual_address=ph->vaddr;
        out->segments[out->segment_count].file_size=ph->filesz;
        out->segments[out->segment_count].memory_size=ph->memsz;
        out->segments[out->segment_count].flags=ph->flags;
        if (ph->flags & ZEROOS_PF_X)
            have_exec=1;
        ++out->segment_count;
        (void)last_end;
    }

    if (!out->segment_count || !have_exec)
        return -1;

    /* Entry must land inside an executable PT_LOAD, never in BSS-only bytes. */
    for (uint16_t i=0; i<out->segment_count; ++i) {
        uint64_t end;
        if (!(out->segments[i].flags & ZEROOS_PF_X))
            continue;
        if (!range_end(out->segments[i].virtual_address,
                       out->segments[i].file_size,&end))
            return -1;
        if (out->entry>=out->segments[i].virtual_address &&
            out->entry<end)
            return 0;
    }
    return -1;
}
