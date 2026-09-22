#ifndef ZEROOS_MEMORY_H
#define ZEROOS_MEMORY_H

#include "types.h"

#define ZEROOS_PAGE_SIZE 4096ULL

void memory_init(uint64_t multiboot_info);
void *page_alloc(void);
void *page_alloc_zero(void);
/* page_free releases one allocator/reference owner; it is not a raw bitmap clear. */
void page_free(void *address);
int memory_page_retain(uint64_t address);
int memory_page_release(uint64_t address);
uint32_t memory_page_references(uint64_t address);
int memory_is_managed_range(uint64_t address, uint64_t length);
int memory_is_usable_range(uint64_t address, uint64_t length);
int memory_page_is_allocated(uint64_t address);
uint64_t memory_total_pages(void);
uint64_t memory_free_pages(void);
uint64_t memory_max_physical(void);

#endif
