#ifndef ZEROOS_ELF_H
#define ZEROOS_ELF_H

#include "types.h"

struct process;

#define ZEROOS_ELF_ET_EXEC 2U
#define ZEROOS_ELF_ET_DYN  3U
#define ZEROOS_ELF_EM_X86_64 62U
#define ZEROOS_ELF_PT_LOAD 1U
#define ZEROOS_ELF_PT_INTERP 3U
#define ZEROOS_ELF_PF_X 1U
#define ZEROOS_ELF_PF_W 2U
#define ZEROOS_ELF_PF_R 4U
#define ZEROOS_ELF_MAX_PROGRAM_HEADERS 32U
#define ZEROOS_ELF_MAX_TOTAL_PAGES 4096ULL

struct __attribute__((packed)) zeroos_elf64_ehdr {
    uint8_t ident[16];
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
};

struct __attribute__((packed)) zeroos_elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
};

struct zeroos_elf_load_result {
    uint64_t entry;
    uint64_t load_bias;
    uint64_t lowest_address;
    uint64_t highest_address;
    uint64_t mapped_pages;
};

/* Validate and map PT_LOAD segments into a process-owned user address space.
 * The input remains kernel-owned; no pointer from the image is retained. */
int elf_system_init(void);
int elf_load_image(struct process *process, const void *image,
                   uint64_t image_size,
                   struct zeroos_elf_load_result *result);
/* Roll back all PT_LOAD mappings described by image. Missing pages are
 * tolerated so this is safe after a partially completed load. */
int elf_unload_image(struct process *process, const void *image,
                     uint64_t image_size);
int elf_debug_validate(void);

#endif
