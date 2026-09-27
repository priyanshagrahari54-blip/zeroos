#include "memory.h"
#include "sync.h"

#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_MMAP 6
#define MULTIBOOT_MEMORY_AVAILABLE 1

/*
 * Early physical-memory policy:
 *
 * The supported Stage 1 matrix currently manages the first 512 MiB. Memory
 * outside that range is deliberately reported as unsupported rather than
 * being handed to a caller through an unchecked bitmap. The allocator keeps
 * separate usable and allocation bitmaps: a page can only be returned to the
 * free pool when firmware marked it usable. This prevents page_free() from
 * accidentally releasing kernel, boot-info or page-table reservations.
 */
#define ZEROOS_MAX_PHYS_MEM (512ULL * 1024ULL * 1024ULL)
#define ZEROOS_MAX_PAGES (ZEROOS_MAX_PHYS_MEM / ZEROOS_PAGE_SIZE)
#define ZEROOS_BITMAP_WORDS ((ZEROOS_MAX_PAGES + 63ULL) / 64ULL)
#define ZEROOS_SUMMARY_WORDS ((ZEROOS_BITMAP_WORDS + 63ULL) / 64ULL)
#define ZEROOS_MAX_MULTIBOOT_INFO (16ULL * 1024ULL * 1024ULL)

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
static uint64_t usable_bitmap[ZEROOS_BITMAP_WORDS];
static uint64_t free_word_summary[ZEROOS_SUMMARY_WORDS];
/* A frame remains allocated until every owner/mapping reference releases it. */
static uint32_t page_references[ZEROOS_MAX_PAGES];
static uint64_t managed_pages;
static uint64_t free_pages;

/*
 * Lock order (never inverted):
 *   wait_queue::lock -> task_lock -> memory_lock
 *   process_lock / thread_lock -> memory_lock
 * memory_init() runs before interrupts and uses the same initialized lock
 * only after the bitmap has been constructed. Runtime allocation/free is
 * always serialized, including scheduler reaping and VMM teardown.
 */
static struct spinlock memory_lock;

extern char __kernel_start;
extern char __kernel_end;
extern void serial_write_public(const char *text);

static void memory_debug_u64(uint64_t value) {
    char buffer[21];
    int pos=20;
    buffer[pos]='\0';
    if (value==0) {
        serial_write_public("0");
        return;
    }
    while (value>0 && pos>0) {
        buffer[--pos]=(char)('0'+(value%10));
        value/=10;
    }
    serial_write_public(&buffer[pos]);
}

static inline void memory_debug(char marker) {
    __asm__ volatile("outb %0, $0xe9" : : "a"(marker) : "memory");
}

static void bitmap_set(uint64_t page) {
    page_bitmap[page >> 6] |= 1ULL << (page & 63);
}

static void bitmap_clear(uint64_t page) {
    page_bitmap[page >> 6] &= ~(1ULL << (page & 63));
}

static int bitmap_test(uint64_t page) {
    return (page_bitmap[page >> 6] >> (page & 63)) & 1U;
}

static void usable_set(uint64_t page) {
    usable_bitmap[page >> 6] |= 1ULL << (page & 63);
}

static void usable_clear(uint64_t page) {
    usable_bitmap[page >> 6] &= ~(1ULL << (page & 63));
}

static int usable_test(uint64_t page) {
    return (usable_bitmap[page >> 6] >> (page & 63)) & 1U;
}

static int word_has_free(uint64_t word) {
    return (usable_bitmap[word] & ~page_bitmap[word])!=0;
}

static void summary_set(uint64_t word) {
    if (word<ZEROOS_BITMAP_WORDS && word_has_free(word))
        free_word_summary[word >> 6] |= 1ULL << (word & 63);
}

static void summary_refresh(uint64_t word) {
    if (word>=ZEROOS_BITMAP_WORDS)
        return;
    if (word_has_free(word))
        summary_set(word);
    else
        free_word_summary[word >> 6] &= ~(1ULL << (word & 63));
}

static uint64_t align_up_page(uint64_t value) {
    if (value > ~0ULL-(ZEROOS_PAGE_SIZE-1ULL))
        return ~0ULL;
    return (value+ZEROOS_PAGE_SIZE-1ULL) & ~(ZEROOS_PAGE_SIZE-1ULL);
}

static void reserve_range(uint64_t start, uint64_t end) {
    if (end<=start || start>=ZEROOS_MAX_PHYS_MEM)
        return;
    if (end>ZEROOS_MAX_PHYS_MEM)
        end=ZEROOS_MAX_PHYS_MEM;

    /* Reservations must cover every page the range touches: round the start
     * down and the end up. Rounding inward (as usable ranges do) would leave a
     * sub-page boot-info block or the kernel's final partial page allocatable. */
    uint64_t first=(start & ~(ZEROOS_PAGE_SIZE-1ULL))/ZEROOS_PAGE_SIZE;
    uint64_t last=align_up_page(end)/ZEROOS_PAGE_SIZE;
    if (last>ZEROOS_MAX_PAGES)
        last=ZEROOS_MAX_PAGES;
    if (last<=first)
        return;

    for (uint64_t page=first; page<last; ++page) {
        if (usable_test(page)) {
            usable_clear(page);
            if (!bitmap_test(page)) {
                bitmap_set(page);
                if (free_pages)
                    --free_pages;
            }
            if (managed_pages)
                --managed_pages;
        }
    }

    for (uint64_t word=first>>6; word<=((last-1ULL)>>6); ++word)
        if (word<ZEROOS_BITMAP_WORDS)
            summary_refresh(word);
}

void memory_init(uint64_t multiboot_info) {
    uint8_t end_tag_found=0;

    spinlock_init(&memory_lock);
    memory_debug('a');

    for (uint64_t i=0; i<ZEROOS_BITMAP_WORDS; ++i) {
        page_bitmap[i]=~0ULL;
        usable_bitmap[i]=0;
    }
    for (uint64_t i=0; i<ZEROOS_MAX_PAGES; ++i)
        page_references[i]=0;
    for (uint64_t i=0; i<ZEROOS_SUMMARY_WORDS; ++i)
        free_word_summary[i]=0;

    managed_pages=0;
    free_pages=0;

    serial_write_public("ZEROOS: allocator bitmap address: ");
    memory_debug_u64((uint64_t)page_bitmap);
    serial_write_public("\nZEROOS: allocator bitmap bytes: ");
    memory_debug_u64((uint64_t)(sizeof(page_bitmap)+sizeof(usable_bitmap)));
    serial_write_public("\n");

    /* The Multiboot information block is identity-mapped by boot.S. */
    if (multiboot_info==0 || multiboot_info>=ZEROOS_MAX_PHYS_MEM ||
        multiboot_info>ZEROOS_MAX_MULTIBOOT_INFO) {
        memory_debug('!');
        return;
    }

    uint32_t total_size=*(uint32_t *)(uint64_t)multiboot_info;
    if (total_size<16U || total_size>0x1000000U ||
        (uint64_t)total_size>ZEROOS_MAX_PHYS_MEM-multiboot_info) {
        memory_debug('!');
        return;
    }

    uint8_t *info_base=(uint8_t *)(uint64_t)multiboot_info;
    uint8_t *cursor=info_base+8;
    uint8_t *end=info_base+total_size;

    while (cursor+sizeof(struct multiboot_tag)<=end) {
        struct multiboot_tag *tag=(struct multiboot_tag *)cursor;
        if (tag->size<sizeof(struct multiboot_tag) ||
            cursor+tag->size>end)
            break;

        if (tag->type==MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap *mmap=(struct multiboot_tag_mmap *)tag;
            if (mmap->size<sizeof(*mmap) ||
                mmap->entry_size<sizeof(struct multiboot_mmap_entry) ||
                mmap->entry_size>mmap->size-sizeof(*mmap))
                break;

            uint8_t *entry_ptr=cursor+sizeof(*mmap);
            uint8_t *entry_end=cursor+mmap->size;
            while (entry_ptr+mmap->entry_size<=entry_end) {
                struct multiboot_mmap_entry *entry=
                    (struct multiboot_mmap_entry *)entry_ptr;
                if (entry->type==MULTIBOOT_MEMORY_AVAILABLE &&
                    entry->len!=0 && entry->addr<ZEROOS_MAX_PHYS_MEM &&
                    entry->len<=~0ULL-entry->addr) {
                    uint64_t start=entry->addr;
                    uint64_t end_addr=entry->addr+entry->len;
                    if (end_addr>start) {
                        if (end_addr>ZEROOS_MAX_PHYS_MEM)
                            end_addr=ZEROOS_MAX_PHYS_MEM;
                        uint64_t first=align_up_page(start)/ZEROOS_PAGE_SIZE;
                        uint64_t last=end_addr/ZEROOS_PAGE_SIZE;
                        if (last>ZEROOS_MAX_PAGES)
                            last=ZEROOS_MAX_PAGES;
                        for (uint64_t page=first; page<last; ++page) {
                            /* Overlapping available entries are counted once. */
                            if (!usable_test(page)) {
                                usable_set(page);
                                bitmap_clear(page);
                                ++managed_pages;
                                ++free_pages;
                            }
                        }
                    }
                }
                entry_ptr+=mmap->entry_size;
            }
        }

        if (tag->type==MULTIBOOT_TAG_TYPE_END) {
            end_tag_found=1;
            break;
        }

        uint64_t advance=((uint64_t)tag->size+7ULL) & ~7ULL;
        if (advance==0 || cursor+advance<cursor || cursor+advance>end)
            break;
        cursor+=advance;
    }

    if (!end_tag_found) {
        memory_debug('!');
        /* Keep all pages reserved when the handoff is structurally invalid. */
        for (uint64_t i=0; i<ZEROOS_MAX_PAGES; ++i) {
            usable_clear(i);
            bitmap_set(i);
        }
        managed_pages=0;
        free_pages=0;
        return;
    }

    /* Never return firmware, kernel or Multiboot metadata to the allocator. */
    reserve_range(0,ZEROOS_PAGE_SIZE);
    reserve_range((uint64_t)&__kernel_start,(uint64_t)&__kernel_end);
    if (multiboot_info<=ZEROOS_MAX_PHYS_MEM-(uint64_t)total_size)
        reserve_range(multiboot_info,multiboot_info+(uint64_t)total_size);

    for (uint64_t word=0; word<ZEROOS_BITMAP_WORDS; ++word)
        summary_refresh(word);
    memory_debug('f');
}

void *page_alloc(void) {
    uint64_t flags=spin_lock_irqsave(&memory_lock);

    for (uint64_t summary_word=0; summary_word<ZEROOS_SUMMARY_WORDS;
         ++summary_word) {
        uint64_t summary=free_word_summary[summary_word];
        while (summary) {
            uint64_t bit=0;
            while (bit<64 && !(summary & (1ULL<<bit)))
                ++bit;
            if (bit>=64)
                break;

            uint64_t word=(summary_word<<6)+bit;
            if (word>=ZEROOS_BITMAP_WORDS) {
                free_word_summary[summary_word]&=~(1ULL<<bit);
                summary&=~(1ULL<<bit);
                continue;
            }

            uint64_t free_mask=usable_bitmap[word] & ~page_bitmap[word];
            if (!free_mask) {
                free_word_summary[summary_word]&=~(1ULL<<bit);
                summary&=~(1ULL<<bit);
                continue;
            }

            uint64_t page_bit=0;
            while (page_bit<64 && !(free_mask & (1ULL<<page_bit)))
                ++page_bit;
            uint64_t page=(word<<6)+page_bit;
            if (page>=ZEROOS_MAX_PAGES) {
                summary_refresh(word);
                continue;
            }

            bitmap_set(page);
            page_references[page]=1;
            if (free_pages)
                --free_pages;
            summary_refresh(word);
            spin_unlock_irqrestore(&memory_lock,flags);
            return (void *)(page*ZEROOS_PAGE_SIZE);
        }
    }

    spin_unlock_irqrestore(&memory_lock,flags);
    return (void *)0;
}

void *page_alloc_below(uint64_t physical_limit) {
    if (physical_limit==0)
        return (void *)0;
    uint64_t page_limit;
    if (physical_limit>~0ULL-(ZEROOS_PAGE_SIZE-1ULL))
        page_limit=ZEROOS_MAX_PAGES;
    else
        page_limit=(physical_limit+ZEROOS_PAGE_SIZE-1ULL)/ZEROOS_PAGE_SIZE;
    if (page_limit>ZEROOS_MAX_PAGES || physical_limit>ZEROOS_MAX_PHYS_MEM)
        page_limit=ZEROOS_MAX_PAGES;

    uint64_t flags=spin_lock_irqsave(&memory_lock);
    for (uint64_t page=0; page<page_limit; ++page) {
        if (!usable_test(page) || bitmap_test(page))
            continue;
        bitmap_set(page);
        page_references[page]=1;
        if (free_pages)
            --free_pages;
        summary_refresh(page>>6);
        spin_unlock_irqrestore(&memory_lock,flags);
        return (void *)(page*ZEROOS_PAGE_SIZE);
    }
    spin_unlock_irqrestore(&memory_lock,flags);
    return (void *)0;
}

/*
 * Physically contiguous run allocation for multi-page kernel stacks and DMA
 * rings. The run is searched top-down so contiguous requests do not fragment
 * the low region that page_alloc_below() consumers (the AP trampoline) need.
 * Each page of the run is an independent single-reference frame, so the run
 * may be released with page_free_contiguous() or page-by-page.
 */
void *page_alloc_contiguous(uint64_t count) {
    if (count==0 || count>ZEROOS_MAX_PAGES)
        return (void *)0;
    if (count==1)
        return page_alloc();

    uint64_t flags=spin_lock_irqsave(&memory_lock);
    uint64_t run=0;
    for (uint64_t page=ZEROOS_MAX_PAGES; page>0; --page) {
        uint64_t candidate=page-1ULL;
        if (!usable_test(candidate) || bitmap_test(candidate)) {
            run=0;
            continue;
        }
        if (++run<count)
            continue;
        for (uint64_t i=0; i<count; ++i) {
            bitmap_set(candidate+i);
            page_references[candidate+i]=1;
            if (free_pages)
                --free_pages;
        }
        for (uint64_t word=candidate>>6; word<=((candidate+count-1ULL)>>6);
             ++word)
            summary_refresh(word);
        spin_unlock_irqrestore(&memory_lock,flags);
        return (void *)(candidate*ZEROOS_PAGE_SIZE);
    }
    spin_unlock_irqrestore(&memory_lock,flags);
    return (void *)0;
}

void page_free_contiguous(void *address, uint64_t count) {
    uint64_t base=(uint64_t)address;
    if (!address || count==0 || (base%ZEROOS_PAGE_SIZE)!=0)
        return;
    for (uint64_t i=0; i<count; ++i)
        (void)memory_page_release(base+i*ZEROOS_PAGE_SIZE);
}

void page_free(void *address) {
    (void)memory_page_release((uint64_t)address);
}

int memory_page_retain(uint64_t physical) {
    if ((physical%ZEROOS_PAGE_SIZE)!=0 || physical>=ZEROOS_MAX_PHYS_MEM)
        return -1;

    uint64_t flags=spin_lock_irqsave(&memory_lock);
    uint64_t page=physical/ZEROOS_PAGE_SIZE;
    if (!usable_test(page) || !bitmap_test(page) ||
        page_references[page]==0 || page_references[page]==0xffffffffU) {
        spin_unlock_irqrestore(&memory_lock,flags);
        return -1;
    }
    ++page_references[page];
    spin_unlock_irqrestore(&memory_lock,flags);
    return 0;
}

int memory_page_release(uint64_t physical) {
    if ((physical%ZEROOS_PAGE_SIZE)!=0 || physical>=ZEROOS_MAX_PHYS_MEM)
        return -1;

    uint64_t flags=spin_lock_irqsave(&memory_lock);
    uint64_t page=physical/ZEROOS_PAGE_SIZE;
    /* Reserved, unallocated and already-released frames are never mutated. */
    if (!usable_test(page) || !bitmap_test(page) || page_references[page]==0) {
        spin_unlock_irqrestore(&memory_lock,flags);
        return -1;
    }

    --page_references[page];
    if (page_references[page]==0) {
        bitmap_clear(page);
        ++free_pages;
        summary_refresh(page>>6);
    }
    spin_unlock_irqrestore(&memory_lock,flags);
    return 0;
}

uint32_t memory_page_references(uint64_t physical) {
    if ((physical%ZEROOS_PAGE_SIZE)!=0 || physical>=ZEROOS_MAX_PHYS_MEM)
        return 0;
    uint64_t flags=spin_lock_irqsave(&memory_lock);
    uint32_t references=page_references[physical/ZEROOS_PAGE_SIZE];
    spin_unlock_irqrestore(&memory_lock,flags);
    return references;
}

void *page_alloc_zero(void) {
    void *page=page_alloc();
    if (!page)
        return (void *)0;
    uint64_t *words=(uint64_t *)page;
    for (uint64_t i=0; i<ZEROOS_PAGE_SIZE/sizeof(uint64_t); ++i)
        words[i]=0;
    return page;
}

int memory_is_managed_range(uint64_t address, uint64_t length) {
    if (length==0 || address>~0ULL-length)
        return 0;
    uint64_t end=address+length;
    return address<ZEROOS_MAX_PHYS_MEM && end<=ZEROOS_MAX_PHYS_MEM;
}

int memory_is_usable_range(uint64_t address, uint64_t length) {
    if (!memory_is_managed_range(address,length) ||
        (address%ZEROOS_PAGE_SIZE)!=0 ||
        (length%ZEROOS_PAGE_SIZE)!=0)
        return 0;

    uint64_t flags=spin_lock_irqsave(&memory_lock);
    uint64_t first=address/ZEROOS_PAGE_SIZE;
    uint64_t count=length/ZEROOS_PAGE_SIZE;
    int valid=1;
    for (uint64_t i=0; i<count; ++i) {
        if (!usable_test(first+i)) {
            valid=0;
            break;
        }
    }
    spin_unlock_irqrestore(&memory_lock,flags);
    return valid;
}

int memory_page_is_allocated(uint64_t address) {
    if ((address%ZEROOS_PAGE_SIZE)!=0 || address>=ZEROOS_MAX_PHYS_MEM)
        return 0;
    uint64_t flags=spin_lock_irqsave(&memory_lock);
    int allocated=usable_test(address/ZEROOS_PAGE_SIZE) &&
                  bitmap_test(address/ZEROOS_PAGE_SIZE);
    spin_unlock_irqrestore(&memory_lock,flags);
    return allocated;
}

uint64_t memory_total_pages(void) {
    uint64_t flags=spin_lock_irqsave(&memory_lock);
    uint64_t value=managed_pages;
    spin_unlock_irqrestore(&memory_lock,flags);
    return value;
}

uint64_t memory_free_pages(void) {
    uint64_t flags=spin_lock_irqsave(&memory_lock);
    uint64_t value=free_pages;
    spin_unlock_irqrestore(&memory_lock,flags);
    return value;
}

uint64_t memory_max_physical(void) {
    return ZEROOS_MAX_PHYS_MEM;
}

void memory_reserve_physical(uint64_t start, uint64_t length) {
    /* Boot-context helper: reserve_range already clamps to the managed
     * window and no-ops for addresses at/above ZEROOS_MAX_PHYS_MEM, which
     * is exactly the typical high PCI framebuffer location. */
    if (length==0 || start>~0ULL-(length-1ULL))
        return;
    reserve_range(start,start+length);
}
