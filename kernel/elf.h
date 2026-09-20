#ifndef ZEROOS_ELF_H
#define ZEROOS_ELF_H

#include "types.h"

#define ZEROOS_ELF64_CLASS 2
#define ZEROOS_ELF64_DATA_LSB 1
#define ZEROOS_ELF_VERSION_CURRENT 1
#define ZEROOS_ELF_TYPE_EXEC 2
#define ZEROOS_ELF_MACHINE_X86_64 62

#define ZEROOS_PT_LOAD 1U
#define ZEROOS_PF_X 1U
#define ZEROOS_PF_W 2U
#define ZEROOS_PF_R 4U

struct elf64_ehdr {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

struct elf_load_segment {
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint32_t flags;
};

#define ZEROOS_ELF_MAX_LOAD_SEGMENTS 16U

struct elf_image {
    uint64_t entry;
    uint16_t segment_count;
    struct elf_load_segment segments[ZEROOS_ELF_MAX_LOAD_SEGMENTS];
};

/*
 * Validation only: no mappings or allocations occur. The caller owns image
 * bytes and must keep them immutable until loading is complete.
 */
int elf64_validate_image(const void *data, uint64_t size,
                         struct elf_image *out);

#endif
