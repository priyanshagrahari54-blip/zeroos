#include "memory.h"

#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_MMAP 6
#define MULTIBOOT_MEMORY_AVAILABLE 1
#define ZEROOS_MAX_PHYS_MEM (512ULL * 1024ULL * 1024ULL)
#define ZEROOS_MAX_PAGES (ZEROOS_MAX_PHYS_MEM / ZEROOS_PAGE_SIZE)
#define ZEROOS_BITMAP_BYTES ((ZEROOS_MAX_PAGES + 7) / 8)

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
};

static uint8_t page_bitmap[ZEROOS_BITMAP_BYTES];
static uint64_t total_pages;
static uint64_t free_pages;

extern char __kernel_start;
extern char __kernel_end;

static void bitmap_set(uint64_t page) {
    page_bitmap[page >> 3] |= (uint8_t)(1U << (page & 7));
}

static void bitmap_clear(uint64_t page) {
    page_bitmap[page >> 3] &= (uint8_t)~(1U << (page & 7));
}

static int bitmap_test(uint64_t page) {
    return (page_bitmap[page >> 3] >> (page & 7)) & 1U;
}

static void reserve_range(uint64_t start, uint64_t end) {
    if (end <= start || start >= ZEROOS_MAX_PHYS_MEM) return;
    if (end > ZEROOS_MAX_PHYS_MEM) end = ZEROOS_MAX_PHYS_MEM;

    uint64_t first = start / ZEROOS_PAGE_SIZE;
    uint64_t last = (end + ZEROOS_PAGE_SIZE - 1) / ZEROOS_PAGE_SIZE;
    if (last > ZEROOS_MAX_PAGES) last = ZEROOS_MAX_PAGES;

    for (uint64_t page = first; page < last; ++page) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            if (free_pages > 0) --free_pages;
        }
    }
}

void memory_init(uint64_t multiboot_info) {
    for (uint64_t i = 0; i < ZEROOS_BITMAP_BYTES; ++i) page_bitmap[i] = 0xff;

    total_pages = ZEROOS_MAX_PAGES;
    free_pages = 0;

    uint32_t total_size = *(uint32_t *)(uint64_t)multiboot_info;
    uint8_t *cursor = (uint8_t *)(uint64_t)(multiboot_info + 8);
    uint8_t *end = cursor + total_size;

    while (cursor < end) {
        struct multiboot_tag *tag = (struct multiboot_tag *)cursor;

        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap *mmap = (struct multiboot_tag_mmap *)tag;
            uint8_t *entry_ptr = cursor + sizeof(*mmap);
            uint8_t *entry_end = cursor + mmap->size;

            while (entry_ptr + mmap->entry_size <= entry_end) {
                struct multiboot_mmap_entry *entry =
                    (struct multiboot_mmap_entry *)entry_ptr;

                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    uint64_t start = entry->addr;
                    uint64_t end_addr = entry->addr + entry->len;
                    if (start < ZEROOS_MAX_PHYS_MEM) {
                        if (end_addr > ZEROOS_MAX_PHYS_MEM) end_addr = ZEROOS_MAX_PHYS_MEM;
                        uint64_t first = (start + ZEROOS_PAGE_SIZE - 1) / ZEROOS_PAGE_SIZE;
                        uint64_t last = end_addr / ZEROOS_PAGE_SIZE;
                        for (uint64_t page = first; page < last; ++page) {
                            if (page < ZEROOS_MAX_PAGES && bitmap_test(page)) {
                                bitmap_clear(page);
                                ++free_pages;
                            }
                        }
                    }
                }
                entry_ptr += mmap->entry_size;
            }
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_END) break;
        cursor += (tag->size + 7U) & ~7U;
    }

    reserve_range(0, ZEROOS_PAGE_SIZE);
    reserve_range((uint64_t)(uint64_t)&__kernel_start,
                  (uint64_t)(uint64_t)&__kernel_end);
    reserve_range(multiboot_info, multiboot_info + total_size);
}

void *page_alloc(void) {
    for (uint64_t page = 0; page < ZEROOS_MAX_PAGES; ++page) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            --free_pages;
            return (void *)(uint64_t)(page * ZEROOS_PAGE_SIZE);
        }
    }
    return (void *)0;
}

void page_free(void *address) {
    uint64_t physical = (uint64_t)address;
    if ((physical % ZEROOS_PAGE_SIZE) != 0 || physical >= ZEROOS_MAX_PHYS_MEM) return;
    uint64_t page = physical / ZEROOS_PAGE_SIZE;
    if (bitmap_test(page)) {
        bitmap_clear(page);
        ++free_pages;
    }
}

uint64_t memory_total_pages(void) { return total_pages; }
uint64_t memory_free_pages(void) { return free_pages; }
