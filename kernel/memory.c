#include "memory.h"

#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_MMAP 6
#define MULTIBOOT_MEMORY_AVAILABLE 1

/*
 * Early physical-memory policy:
 * - track the first 512 MiB, which is enough for the current low-memory
 *   kernel/page-table bootstrap;
 * - keep metadata compact;
 * - use a summary bitmap so allocation does not scan every physical page.
 *
 * The public allocator API returns physical addresses.  Virtual mapping is
 * handled by the VMM.
 */
#define ZEROOS_MAX_PHYS_MEM (512ULL * 1024ULL * 1024ULL)
#define ZEROOS_MAX_PAGES (ZEROOS_MAX_PHYS_MEM / ZEROOS_PAGE_SIZE)
#define ZEROOS_BITMAP_WORDS ((ZEROOS_MAX_PAGES + 63ULL) / 64ULL)
#define ZEROOS_SUMMARY_WORDS ((ZEROOS_BITMAP_WORDS + 63ULL) / 64ULL)

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

static uint64_t page_bitmap[ZEROOS_BITMAP_WORDS];
static uint64_t free_word_summary[ZEROOS_SUMMARY_WORDS];
static uint64_t managed_pages;
static uint64_t free_pages;

extern char __kernel_start;
extern char __kernel_end;

static void bitmap_set(uint64_t page) {
    page_bitmap[page >> 6] |= 1ULL << (page & 63);
}

static void bitmap_clear(uint64_t page) {
    page_bitmap[page >> 6] &= ~(1ULL << (page & 63));
}

static int bitmap_test(uint64_t page) {
    return (page_bitmap[page >> 6] >> (page & 63)) & 1U;
}

static void summary_set(uint64_t word) {
    free_word_summary[word >> 6] |= 1ULL << (word & 63);
}

static void summary_clear_if_full(uint64_t word) {
    if (page_bitmap[word] == ~0ULL)
        free_word_summary[word >> 6] &= ~(1ULL << (word & 63));
}

static void summary_set_if_free(uint64_t word) {
    if (page_bitmap[word] != ~0ULL)
        summary_set(word);
}

static void reserve_range(uint64_t start, uint64_t end) {
    if (end <= start || start >= ZEROOS_MAX_PHYS_MEM) return;
    if (end > ZEROOS_MAX_PHYS_MEM) end = ZEROOS_MAX_PHYS_MEM;

    uint64_t first = (start + ZEROOS_PAGE_SIZE - 1) / ZEROOS_PAGE_SIZE;
    uint64_t last = end / ZEROOS_PAGE_SIZE;
    if (last > ZEROOS_MAX_PAGES) last = ZEROOS_MAX_PAGES;

    for (uint64_t page = first; page < last; ++page) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            --free_pages;
        }
    }

    for (uint64_t word = first >> 6; word <= ((last ? last - 1 : 0) >> 6);
         ++word) {
        if (word < ZEROOS_BITMAP_WORDS)
            summary_clear_if_full(word);
    }
}

void memory_init(uint64_t multiboot_info) {
    /*
     * Start fully reserved.  We then release only firmware-reported
     * available ranges.  This is safer than assuming RAM is contiguous.
     */
    for (uint64_t i = 0; i < ZEROOS_BITMAP_WORDS; ++i)
        page_bitmap[i] = ~0ULL;

    for (uint64_t i = 0; i < ZEROOS_SUMMARY_WORDS; ++i)
        free_word_summary[i] = 0;

    managed_pages = 0;
    free_pages = 0;

    uint32_t total_size = *(uint32_t *)(uint64_t)multiboot_info;
    uint8_t *cursor = (uint8_t *)(uint64_t)(multiboot_info + 8);
    uint8_t *end = cursor + total_size;

    while (cursor < end) {
        struct multiboot_tag *tag = (struct multiboot_tag *)cursor;

        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap *mmap =
                (struct multiboot_tag_mmap *)tag;
            uint8_t *entry_ptr = cursor + sizeof(*mmap);
            uint8_t *entry_end = cursor + mmap->size;

            while (entry_ptr + mmap->entry_size <= entry_end) {
                struct multiboot_mmap_entry *entry =
                    (struct multiboot_mmap_entry *)entry_ptr;

                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    uint64_t start = entry->addr;
                    uint64_t end_addr = entry->addr + entry->len;

                    if (start < ZEROOS_MAX_PHYS_MEM) {
                        if (end_addr > ZEROOS_MAX_PHYS_MEM)
                            end_addr = ZEROOS_MAX_PHYS_MEM;

                        uint64_t first =
                            (start + ZEROOS_PAGE_SIZE - 1) / ZEROOS_PAGE_SIZE;
                        uint64_t last = end_addr / ZEROOS_PAGE_SIZE;

                        if (last > ZEROOS_MAX_PAGES)
                            last = ZEROOS_MAX_PAGES;

                        for (uint64_t page = first; page < last; ++page) {
                            if (bitmap_test(page)) {
                                bitmap_clear(page);
                                ++free_pages;
                                summary_set(page >> 6);
                            }
                        }
                    }
                }

                entry_ptr += mmap->entry_size;
            }
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_END)
            break;

        cursor += (tag->size + 7U) & ~7U;
    }

    reserve_range(0, ZEROOS_PAGE_SIZE);
    reserve_range((uint64_t)&__kernel_start,
                  (uint64_t)&__kernel_end);
    reserve_range(multiboot_info, multiboot_info + total_size);

    /*
     * managed_pages is the number of pages ZEROOS actually accepted from
     * the firmware map, not the artificial 512 MiB tracking ceiling.
     */
    managed_pages = free_pages;

    /* Reserved pages are included in the physical span, so reconstruct the
       managed count from the accepted map below rather than free_pages. */
    for (uint64_t word = 0; word < ZEROOS_BITMAP_WORDS; ++word) {
        uint64_t used = page_bitmap[word];
        for (uint64_t bit = 0; bit < 64; ++bit) {
            uint64_t page = (word << 6) + bit;
            if (page >= ZEROOS_MAX_PAGES) break;
            if (!((used >> bit) & 1ULL))
                ++managed_pages;
        }
    }
}

void *page_alloc(void) {
    for (uint64_t summary_word = 0;
         summary_word < ZEROOS_SUMMARY_WORDS;
         ++summary_word) {
        uint64_t summary = free_word_summary[summary_word];
        if (!summary) continue;

        for (uint64_t bit = 0; bit < 64; ++bit) {
            if (!(summary & (1ULL << bit))) continue;

            uint64_t word = (summary_word << 6) + bit;
            if (word >= ZEROOS_BITMAP_WORDS) return (void *)0;

            uint64_t free_mask = ~page_bitmap[word];
            if (!free_mask) {
                free_word_summary[summary_word] &= ~(1ULL << bit);
                continue;
            }

            uint64_t page_bit = 0;
            while (page_bit < 64 && !(free_mask & (1ULL << page_bit)))
                ++page_bit;

            uint64_t page = (word << 6) + page_bit;
            if (page >= ZEROOS_MAX_PAGES) return (void *)0;

            bitmap_set(page);
            --free_pages;
            summary_clear_if_full(word);

            return (void *)(page * ZEROOS_PAGE_SIZE);
        }
    }

    return (void *)0;
}

void page_free(void *address) {
    uint64_t physical = (uint64_t)address;

    if ((physical % ZEROOS_PAGE_SIZE) != 0 ||
        physical >= ZEROOS_MAX_PHYS_MEM)
        return;

    uint64_t page = physical / ZEROOS_PAGE_SIZE;
    if (!bitmap_test(page)) return;

    bitmap_clear(page);
    ++free_pages;
    summary_set_if_free(page >> 6);
}

uint64_t memory_total_pages(void) { return managed_pages; }
uint64_t memory_free_pages(void) { return free_pages; }
uint64_t memory_max_physical(void) { return ZEROOS_MAX_PHYS_MEM; }
