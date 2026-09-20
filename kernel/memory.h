#ifndef ZEROOS_MEMORY_H
#define ZEROOS_MEMORY_H

#include "types.h"

#define ZEROOS_PAGE_SIZE 4096ULL

void memory_init(uint64_t multiboot_info);
void *page_alloc(void);
void *page_alloc_at(uint64_t address);
/* IRQ-safe: serializes the bitmap search against allocator/reservation updates. */
uint64_t memory_find_free_run(uint64_t need_pages);
void *page_alloc_zero(void);
void page_free(void *address);
int memory_is_managed_range(uint64_t address, uint64_t length);
int memory_page_is_allocated(uint64_t address);
uint64_t memory_total_pages(void);
uint64_t memory_free_pages(void);
uint64_t memory_max_physical(void);

/* Exclusive mapping claim. Reserved/free pages and duplicate claims fail. */
int memory_claim_page(uint64_t address);
void memory_unclaim_page(uint64_t address);
#ifdef ZEROOS_TEST_FAULTS
void memory_test_fail_after(int count);
#endif
#endif
