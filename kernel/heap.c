#include "heap.h"
#include "memory.h"
#include "sync.h"

extern void serial_write_public(const char *text);

#define HEAP_MAGIC 0x5a454f5348504152ULL /* in-use block header */
#define HEAP_FREE_MAGIC 0x4652454546524545ULL /* free block header */
#define HEAP_CANARY 0xd1d5d0d9d8d7d6d5ULL
#define HEAP_HEADER sizeof(struct heap_block)
#define HEAP_REGION_PAGES 1024ULL /* up to 4 MiB for Stage 1 */
#define HEAP_MIN_PAGES 256ULL /* 1 MiB floor */

/*
 * The header is padded to 32 bytes so that every payload (header + 1
 * word, i.e. block+32) is 16-byte aligned while block addresses remain
 * 16-byte aligned. sizeof must stay a 16-byte multiple: heap_round16
 * and the payload-alignment checks depend on it.
 */
struct heap_block {
    uint64_t size;
    uint64_t magic;
    uint64_t canary;
    uint64_t pad;
    /* payload follows immediately after the header */
};

/* Compile-time: the header must be a 16-byte multiple or payloads drift
 * off their 16-byte alignment. */
typedef char heap_header_multiple_check[
    (sizeof(struct heap_block) % ZEROOS_HEAP_ALIGN) == 0 ? 1 : -1];

static uint8_t *heap_base;
static uint8_t *heap_end;
static uint64_t used_bytes;
static struct spinlock heap_lock;
static int heap_ready;

#ifdef ZEROOS_TEST_FAULTS
static int fail_after = -1;
void heap_test_fail_after(int count) { fail_after=count; }
#endif

static uint64_t heap_round16(uint64_t value) {
    return (value + (ZEROOS_HEAP_ALIGN - 1)) & ~(ZEROOS_HEAP_ALIGN - 1);
}

static struct heap_block *block_at(uint8_t *address) {
    return (struct heap_block *)address;
}

static uint8_t *payload_of(struct heap_block *block) {
    return (uint8_t *)(block + 1);
}

static int block_valid(const uint8_t *cursor) {
    uint64_t remaining=(uint64_t)(heap_end-cursor);
    if (remaining<HEAP_HEADER) return 0;
    const struct heap_block *block=(const struct heap_block *)cursor;
    return block->size>=HEAP_HEADER+ZEROOS_HEAP_MIN_ALLOC &&
           !(block->size&(ZEROOS_HEAP_ALIGN-1)) && block->size<=remaining &&
           (block->magic==HEAP_FREE_MAGIC ||
            (block->magic==HEAP_MAGIC && block->canary==HEAP_CANARY));
}

static void heap_corruption(void) __attribute__((noreturn));
static void heap_corruption(void) {
    serial_write_public("ZEROOS PANIC: heap block-chain metadata corrupted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

/* Every iteration advances by a validated, bounded nonzero block size. */
int heap_validate(void) {
    if (!heap_ready) return -1;
    uint64_t flags=spin_lock_irqsave(&heap_lock);
    uint8_t *cursor=heap_base;
    int result=0;
    while (cursor<heap_end) {
        if (!block_valid(cursor)) { result=-1; break; }
        cursor+=block_at(cursor)->size;
    }
    if (cursor!=heap_end) result=-1;
    spin_unlock_irqrestore(&heap_lock,flags);
    return result;
}

int heap_init(void) {
    uint8_t *region = 0;
    uint64_t pages = 0;
    struct heap_block *root;

    if (heap_ready)
        return 0;

    /*
     * Find the first physically contiguous free run of the desired
     * region size (falling back to the minimum) by scanning the PMM
     * bitmap, then allocate exactly that range.
     *
     * This must not be done with page_alloc() and free-on-gap: that
     * allocator serves the lowest free page first, and scattered free
     * pages in the low-memory region (below the 1 MiB PCI/BIOS hole,
     * non-adjacent to each other) would be cycled on forever - every
     * page is a "gap" against the one-page run it replaces - while the
     * long main-RAM run is never reached and the search never ends.
     */
    uint64_t start_page = memory_find_free_run(HEAP_REGION_PAGES);
    pages = HEAP_REGION_PAGES;
    if (start_page == ~0ULL) {
        start_page = memory_find_free_run(HEAP_MIN_PAGES);
        pages = HEAP_MIN_PAGES;
    }
    if (start_page == ~0ULL)
        return -1;

    region = (uint8_t *)(start_page * ZEROOS_PAGE_SIZE);
    for (uint64_t i = 0; i < pages; ++i) {
        uint8_t *next = (uint8_t *)page_alloc_at(
            (start_page + i) * ZEROOS_PAGE_SIZE);
        if (!next) {
            for (uint64_t j = 0; j < i; ++j)
                page_free(region + j * ZEROOS_PAGE_SIZE);
            return -1;
        }
    }

    heap_base = region;
    heap_end = region + pages * ZEROOS_PAGE_SIZE;
    used_bytes = 0;
    spinlock_init(&heap_lock);

    /*
     * Report the actual region so a later fault's CR2/RIP can be
     * cross-referenced against it.
     */
    extern void serial_write_public(const char *text);
    extern void serial_write_u64_public(uint64_t value);
    serial_write_public("ZEROOS: heap region [");
    serial_write_u64_public((uint64_t)heap_base);
    serial_write_public("-");
    serial_write_u64_public((uint64_t)heap_end);
    serial_write_public("] pages=");
    serial_write_u64_public(pages);
    serial_write_public("\n");

    root = block_at(heap_base);
    root->size = (uint64_t)(heap_end - heap_base);
    root->magic = HEAP_FREE_MAGIC;
    root->canary = 0;

    heap_ready = 1;
    return 0;
}

void *kmalloc(uint64_t size) {
#ifdef ZEROOS_TEST_FAULTS
    if (fail_after == 0) return 0;
    if (fail_after > 0) --fail_after;
#endif
    uint64_t need;
    uint64_t flags;
    uint8_t *cursor;
    struct heap_block *block;
    struct heap_block *remainder;
    void *payload;

    if (!heap_ready)
        return 0;
    if (size == 0)
        size = 1;
    if (size > (uint64_t)(heap_end - heap_base))
        return 0;

    need = heap_round16(HEAP_HEADER + size);
    if (need < HEAP_HEADER + ZEROOS_HEAP_MIN_ALLOC)
        need = HEAP_HEADER + ZEROOS_HEAP_MIN_ALLOC;
    if (need > (uint64_t)(heap_end - heap_base))
        return 0;

    flags = spin_lock_irqsave(&heap_lock);

    for (cursor = heap_base; cursor < heap_end; cursor += block->size) {
        if (!block_valid(cursor)) heap_corruption();
        block = block_at(cursor);
        if (block->magic != HEAP_FREE_MAGIC || block->size < need)
            continue;

        if (block->size >= need + HEAP_HEADER + ZEROOS_HEAP_MIN_ALLOC) {
            remainder = (struct heap_block *)(payload_of(block) + (need - HEAP_HEADER));
            remainder->size = block->size - need;
            remainder->magic = HEAP_FREE_MAGIC;
            remainder->canary = 0;
            block->size = need;
        }

        block->magic = HEAP_MAGIC;
        block->canary = HEAP_CANARY;
        used_bytes += block->size - HEAP_HEADER;
        payload = payload_of(block);
        spin_unlock_irqrestore(&heap_lock, flags);
        return payload;
    }

    spin_unlock_irqrestore(&heap_lock, flags);
    return 0;
}

void *kcalloc(uint64_t count, uint64_t size) {
    uint64_t total;
    void *memory;

    if (count == 0 || size == 0)
        return 0;
    if (count > ~0ULL / size)
        return 0;
    total = count * size;
    memory = kmalloc(total);
    if (!memory)
        return 0;

    for (uint64_t i = 0; i < total; ++i)
        ((uint8_t *)memory)[i] = 0;
    return memory;
}

int kfree(void *pointer) {
    uint64_t address=(uint64_t)pointer;
    if (!heap_ready || address<(uint64_t)heap_base+HEAP_HEADER ||
        address>=(uint64_t)heap_end || (address&(ZEROOS_HEAP_ALIGN-1))) return -1;
    uint64_t flags=spin_lock_irqsave(&heap_lock);
    uint8_t *target=(uint8_t *)(address-HEAP_HEADER), *walk=heap_base;
    struct heap_block *prev=0;
    while (walk<target) {
        if (!block_valid(walk)) heap_corruption();
        prev=block_at(walk);
        walk+=prev->size;
    }
    /* Magic in a payload or an absorbed header is not a live allocation. */
    if (walk!=target) { spin_unlock_irqrestore(&heap_lock,flags); return -1; }
    if (!block_valid(walk)) heap_corruption();
    struct heap_block *block=block_at(walk);
    if (block->magic!=HEAP_MAGIC) { spin_unlock_irqrestore(&heap_lock,flags); return -1; }
    uint64_t payload_size=block->size-HEAP_HEADER;
    if (payload_size>used_bytes) heap_corruption();
    block->magic=HEAP_FREE_MAGIC;
    block->canary=0;
    if (prev && prev->magic==HEAP_FREE_MAGIC) {
        prev->size+=block->size;
        block=prev;
    }
    uint8_t *next=(uint8_t *)block+block->size;
    while (next<heap_end) {
        if (!block_valid(next)) heap_corruption();
        if (block_at(next)->magic!=HEAP_FREE_MAGIC) break;
        block->size+=block_at(next)->size;
        next=(uint8_t *)block+block->size;
    }
    used_bytes-=payload_size;
    spin_unlock_irqrestore(&heap_lock,flags);
    return 0;
}

#ifdef ZEROOS_TEST_FAULTS
int heap_test_forged_free(void) {
    uint8_t *p=kmalloc(256);
    if (!p) return -1;
    struct heap_block *fake=(struct heap_block *)(p+32);
    fake->size=64; fake->magic=HEAP_MAGIC; fake->canary=HEAP_CANARY;
    int rejected=kfree(p+64);
    int valid=heap_validate();
    int freed=kfree(p);
    return rejected==-1 && valid==0 && freed==0 ? 0 : -1;
}
#endif

uint64_t heap_used_bytes(void) { return used_bytes; }
uint64_t heap_capacity_bytes(void) {
    return heap_ready ? (uint64_t)(heap_end - heap_base) - HEAP_HEADER : 0;
}
