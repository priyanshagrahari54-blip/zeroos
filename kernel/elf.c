#include "elf.h"
#include "memory.h"
#include "process.h"
#include "user.h"
#include "vmm.h"
#include "sync.h"
#include "syscall.h"

struct elf_load_segment {
    struct zeroos_elf64_phdr header;
    uint64_t page_start;
    uint64_t page_end;
};

struct elf_loader_workspace {
    struct spinlock lock;
    uint8_t initialized;
    struct elf_load_segment segments[ZEROOS_ELF_MAX_PROGRAM_HEADERS];
    uint8_t mapped[ZEROOS_ELF_MAX_TOTAL_PAGES/8U];
};

static struct elf_loader_workspace elf_workspace;

static int range_end(uint64_t start, uint64_t length, uint64_t *end_out) {
    if (length>~0ULL-start)
        return -1;
    *end_out=start+length;
    return 0;
}

static int canonical_address(uint64_t address) {
    uint64_t sign=(address>>47)&1ULL;
    uint64_t upper=address>>48;
    return sign ? upper==0xffffULL : upper==0;
}

static int align_down_page(uint64_t value, uint64_t *result) {
    *result=value&~(VMM_PAGE_SIZE-1ULL);
    return 0;
}

static int align_up_page(uint64_t value, uint64_t *result) {
    if (value>~0ULL-(VMM_PAGE_SIZE-1ULL))
        return -1;
    *result=(value+VMM_PAGE_SIZE-1ULL)&~(VMM_PAGE_SIZE-1ULL);
    return 0;
}

static int user_page(uint64_t address) {
    return canonical_address(address) &&
           ((address>>39)&0x1ffULL)==254ULL;
}

static void mapped_set(uint8_t *bitmap, uint64_t index) {
    bitmap[index>>3] |= (uint8_t)(1U<<(index&7U));
}

static int mapped_test(const uint8_t *bitmap, uint64_t index) {
    return (bitmap[index>>3]&(uint8_t)(1U<<(index&7U)))!=0;
}

int elf_system_init(void) {
    spinlock_init(&elf_workspace.lock);
    elf_workspace.initialized=1;
    for (uint32_t i=0; i<sizeof(elf_workspace.mapped); ++i)
        elf_workspace.mapped[i]=0;
    return 0;
}

/*
 * Parse once and return page-aligned, load-bias-adjusted segments. All
 * arithmetic is checked before a page table or physical page is touched.
 */
static int elf_collect(const void *image, uint64_t image_size,
                       struct elf_load_segment *segments,
                       uint32_t *segment_count_out,
                       uint64_t *load_bias_out, uint64_t *entry_out,
                       uint64_t *program_header_out, uint64_t *lowest_out,
                       uint64_t *highest_out, uint64_t *total_pages_out) {
    const struct zeroos_elf64_ehdr *header;
    uint64_t phdr_bytes;
    uint64_t phdr_end;
    uint64_t minimum_page=~0ULL;
    uint64_t maximum_page=0;
    uint32_t segment_count=0;
    uint64_t total_pages=0;
    uint64_t program_header_address=0;
    uint8_t executable=0;

    if (!image || image_size<sizeof(*header))
        return -ZEROOS_EINVAL;
    header=(const struct zeroos_elf64_ehdr *)image;
    if (header->ident[0]!=0x7f || header->ident[1]!='E' ||
        header->ident[2]!='L' || header->ident[3]!='F' ||
        header->ident[4]!=2 || header->ident[5]!=1 ||
        header->ident[6]!=1 ||
        (header->type!=ZEROOS_ELF_ET_EXEC &&
         header->type!=ZEROOS_ELF_ET_DYN) ||
        header->machine!=ZEROOS_ELF_EM_X86_64 || header->version!=1U ||
        header->ehsize!=sizeof(*header) ||
        header->phentsize!=sizeof(struct zeroos_elf64_phdr) ||
        header->phnum==0 || header->phnum>ZEROOS_ELF_MAX_PROGRAM_HEADERS)
        return -ZEROOS_EINVAL;
    if (header->phoff>image_size ||
        (uint64_t)header->phnum*header->phentsize>
            image_size-header->phoff)
        return -ZEROOS_EINVAL;
    phdr_bytes=(uint64_t)header->phnum*header->phentsize;
    phdr_end=header->phoff+phdr_bytes;
    (void)phdr_end;

    for (uint32_t i=0; i<header->phnum; ++i) {
        const struct zeroos_elf64_phdr *program=(const struct zeroos_elf64_phdr *)
            ((const uint8_t *)image+header->phoff+
             (uint64_t)i*header->phentsize);
        uint64_t memory_end;
        uint64_t page_end;
        uint64_t page_start;
        uint64_t page_count;

        if (program->type==ZEROOS_ELF_PT_INTERP)
            return -ZEROOS_EINVAL; /* No implicit dynamic loader policy in v1. */
        if (program->type!=ZEROOS_ELF_PT_LOAD)
            continue;
        if (segment_count>=ZEROOS_ELF_MAX_PROGRAM_HEADERS ||
            program->memory_size==0 || program->file_size>program->memory_size ||
            program->offset>image_size ||
            program->file_size>image_size-program->offset ||
            ((program->flags&ZEROOS_ELF_PF_W) &&
             (program->flags&ZEROOS_ELF_PF_X)))
            return -ZEROOS_EINVAL;
        if (program->alignment>1ULL &&
            ((program->alignment&(program->alignment-1ULL))!=0 ||
             ((program->virtual_address-program->offset)&
              (program->alignment-1ULL))!=0))
            return -ZEROOS_EINVAL;
        if (range_end(program->virtual_address,program->memory_size,
                      &memory_end)!=0 ||
            align_down_page(program->virtual_address,&page_start)!=0 ||
            align_up_page(memory_end,&page_end)!=0 ||
            page_end<=page_start)
            return -ZEROOS_EINVAL;
        page_count=(page_end-page_start)/VMM_PAGE_SIZE;
        if (page_count==0 || total_pages>ZEROOS_ELF_MAX_TOTAL_PAGES-page_count)
            return -ZEROOS_EINVAL;
        total_pages+=page_count;
        if (page_start<minimum_page)
            minimum_page=page_start;
        if (page_end>maximum_page)
            maximum_page=page_end;
        if (program->flags&ZEROOS_ELF_PF_X)
            executable=1;
        segments[segment_count].header=*program;
        segments[segment_count].page_start=page_start;
        segments[segment_count].page_end=page_end;
        ++segment_count;
    }

    if (!segment_count || !executable || minimum_page==~0ULL)
        return -ZEROOS_EINVAL;
    uint64_t load_bias=0;
    if (header->type==ZEROOS_ELF_ET_DYN) {
        if (minimum_page>ZEROOS_USER_BASE)
            return -ZEROOS_EINVAL;
        load_bias=ZEROOS_USER_BASE-minimum_page;
    }

    uint64_t adjusted_low=~0ULL;
    uint64_t adjusted_high=0;
    uint64_t entry;
    if (range_end(header->entry,load_bias,&entry)!=0)
        return -ZEROOS_EINVAL;
    /* The expression above computes entry + bias without accepting wrap.
     * It is intentionally separate from segment validation for ET_EXEC. */
    for (uint32_t i=0; i<segment_count; ++i) {
        uint64_t start,end;
        struct elf_load_segment *segment=&segments[i];
        if (range_end(segment->page_start,load_bias,&start)!=0 ||
            range_end(segment->page_end,load_bias,&end)!=0 ||
            end<=start || !user_page(start) || !user_page(end-1ULL))
            return -ZEROOS_EINVAL;
        segment->page_start=start;
        segment->page_end=end;
        if (start<adjusted_low)
            adjusted_low=start;
        if (end>adjusted_high)
            adjusted_high=end;
        for (uint32_t j=0; j<i; ++j) {
            if (segment->page_start<segments[j].page_end &&
                segments[j].page_start<segment->page_end)
                return -ZEROOS_EINVAL;
        }
        if (segment->header.flags&ZEROOS_ELF_PF_X) {
            uint64_t segment_start;
            uint64_t segment_end;
            int entry_in_segment=0;
            if (range_end(segment->header.virtual_address,load_bias,
                          &segment_start)==0 &&
                range_end(segment_start,segment->header.memory_size,
                          &segment_end)==0 &&
                entry>=segment_start && entry<segment_end)
                entry_in_segment=1;
            if (entry_in_segment)
                executable=2;
        }
    }
    if (executable!=2 || !canonical_address(entry))
        return -ZEROOS_EINVAL;

    /* Publish AT_PHDR only when the table is actually covered by a readable
     * PT_LOAD. This keeps the auxiliary vector truthful for both ET_EXEC and
     * ET_DYN images; callers must treat zero as the no-PHDR contract. */
    if (phdr_end>=header->phoff) {
        for (uint32_t i=0; i<segment_count; ++i) {
            const struct zeroos_elf64_phdr *program=&segments[i].header;
            uint64_t file_end=program->offset+program->file_size;
            uint64_t table_offset;
            uint64_t table_address;
            uint64_t table_end;
            if (!(program->flags&ZEROOS_ELF_PF_R) ||
                header->phoff<program->offset || phdr_end>file_end)
                continue;
            table_offset=header->phoff-program->offset;
            if (range_end(program->virtual_address,load_bias,
                          &table_address)!=0 ||
                range_end(table_address,table_offset,&table_address)!=0 ||
                range_end(table_address,phdr_bytes,&table_end)!=0 ||
                table_address<segments[i].page_start ||
                table_end>segments[i].page_end ||
                !canonical_address(table_address))
                continue;
            program_header_address=table_address;
            break;
        }
    }

    *segment_count_out=segment_count;
    *load_bias_out=load_bias;
    *entry_out=entry;
    *program_header_out=program_header_address;
    *lowest_out=adjusted_low;
    *highest_out=adjusted_high;
    *total_pages_out=total_pages;
    return 0;
}

static void elf_unmap_mapped(struct process *process,
                             const struct elf_load_segment *segments,
                             uint32_t segment_count,
                             const uint8_t *mapped) {
    uint64_t index=0;
    for (uint32_t i=0; i<segment_count; ++i) {
        for (uint64_t address=segments[i].page_start;
             address<segments[i].page_end;
             address+=VMM_PAGE_SIZE,++index) {
            if (mapped_test(mapped,index))
                (void)process_address_space_unmap_page(process,address);
        }
    }
}

int elf_load_image(struct process *process, const void *image,
                   uint64_t image_size,
                   struct zeroos_elf_load_result *result) {
    struct elf_load_segment *segments=elf_workspace.segments;
    uint8_t *mapped=elf_workspace.mapped;
    uint32_t segment_count=0;
    uint64_t lock_flags;
    uint64_t load_bias=0;
    uint64_t entry=0;
    uint64_t program_header_address=0;
    uint64_t lowest=0;
    uint64_t highest=0;
    uint64_t total_pages=0;
    uint64_t mapped_index=0;

    if (!elf_workspace.initialized || !process || !image || !result)
        return -ZEROOS_EINVAL;
    lock_flags=spin_lock_irqsave(&elf_workspace.lock);
    for (uint32_t i=0; i<sizeof(elf_workspace.mapped); ++i)
        mapped[i]=0;
    if (elf_collect(image,image_size,segments,&segment_count,&load_bias,
                    &entry,&program_header_address,&lowest,&highest,
                    &total_pages)!=0) {
        spin_unlock_irqrestore(&elf_workspace.lock,lock_flags);
        return -ZEROOS_EINVAL;
    }

    for (uint32_t i=0; i<segment_count; ++i) {
        const struct zeroos_elf64_phdr *program=&segments[i].header;
        uint64_t segment_start;
        if (range_end(program->virtual_address,load_bias,&segment_start)!=0)
            goto fail;
        uint64_t flags=VMM_USER;
        if (program->flags&ZEROOS_ELF_PF_W)
            flags|=VMM_WRITABLE|VMM_NO_EXECUTE;
        else if (!(program->flags&ZEROOS_ELF_PF_X))
            flags|=VMM_NO_EXECUTE;

        for (uint64_t address=segments[i].page_start;
             address<segments[i].page_end;
             address+=VMM_PAGE_SIZE) {
            void *page=page_alloc_zero();
            uint64_t physical;
            uint64_t segment_start;
            uint64_t page_end;
            uint64_t copy_begin;
            uint64_t copy_end;
            if (!page || mapped_index>=total_pages) {
                if (page) page_free(page);
                goto fail;
            }
            if (process_address_space_map_page(process,address,
                                                (uint64_t)page,flags)!=0) {
                page_free(page);
                goto fail;
            }
            page_free(page);
            mapped_set(mapped,mapped_index++);

            physical=vmm_space_translate(&process->address_space,address);
            if (!physical ||
                range_end(program->virtual_address,load_bias,
                          &segment_start)!=0 ||
                range_end(address,VMM_PAGE_SIZE,&page_end)!=0)
                goto fail;
            if (page_end>segment_start) {
                copy_begin=address>=segment_start ? address-segment_start : 0;
                copy_end=page_end-segment_start;
                if (copy_end>program->file_size)
                    copy_end=program->file_size;
                if (copy_end>copy_begin) {
                    uint64_t image_offset=program->offset+copy_begin;
                    uint64_t destination_offset=(segment_start+
                                                  copy_begin)-address;
                    uint8_t *destination=(uint8_t *)(uint64_t)physical+
                                          destination_offset;
                    const uint8_t *source=(const uint8_t *)image+image_offset;
                    for (uint64_t j=0; j<copy_end-copy_begin; ++j)
                        destination[j]=source[j];
                }
            }
        }
    }

    result->entry=entry;
    result->load_bias=load_bias;
    result->program_header_address=program_header_address;
    result->lowest_address=lowest;
    result->highest_address=highest;
    result->mapped_pages=total_pages;
    spin_unlock_irqrestore(&elf_workspace.lock,lock_flags);
    return 0;

fail:
    elf_unmap_mapped(process,segments,segment_count,mapped);
    spin_unlock_irqrestore(&elf_workspace.lock,lock_flags);
    return -ZEROOS_ENOMEM;
}

int elf_unload_image(struct process *process, const void *image,
                     uint64_t image_size) {
    struct elf_load_segment *segments=elf_workspace.segments;
    uint64_t lock_flags;
    uint32_t segment_count=0;
    uint64_t load_bias=0;
    uint64_t entry=0;
    uint64_t program_header_address=0;
    uint64_t lowest=0;
    uint64_t highest=0;
    uint64_t total_pages=0;
    if (!elf_workspace.initialized || !process)
        return -ZEROOS_EINVAL;
    lock_flags=spin_lock_irqsave(&elf_workspace.lock);
    if (elf_collect(image,image_size,segments,&segment_count,&load_bias,
                    &entry,&program_header_address,&lowest,&highest,
                    &total_pages)!=0) {
        spin_unlock_irqrestore(&elf_workspace.lock,lock_flags);
        return -ZEROOS_EINVAL;
    }
    for (uint32_t i=0; i<segment_count; ++i)
        for (uint64_t address=segments[i].page_start;
             address<segments[i].page_end;
             address+=VMM_PAGE_SIZE)
            if (process_address_space_unmap_page(process,address)!=0) {
                spin_unlock_irqrestore(&elf_workspace.lock,lock_flags);
                return -ZEROOS_EBUSY;
            }
    spin_unlock_irqrestore(&elf_workspace.lock,lock_flags);
    return 0;
}

int elf_debug_validate(void) {
    if (sizeof(struct zeroos_elf64_ehdr)!=64U ||
        sizeof(struct zeroos_elf64_phdr)!=56U ||
        VMM_PAGE_SIZE==0 || ZEROOS_ELF_MAX_PROGRAM_HEADERS==0 ||
        ZEROOS_ELF_MAX_TOTAL_PAGES==0)
        return -1;
    return 0;
}
