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

struct heap_block {
    uint64_t size;
    uint64_t magic;
    uint64_t canary;
    /* payload follows immediately after the header */
};

static uint8_t *heap_base;
static uint8_t *heap_end;
static uint64_t used_bytes;
static struct spinlock heap_lock;
static int heap_ready;

static uint64_t heap_round16(uint64_t value) {
    return (value + (ZEROOS_HEAP_ALIGN - 1)) & ~(ZEROOS_HEAP_ALIGN - 1);
}

static struct heap_block *block_at(uint8_t *address) {
    return (struct heap_block *)address;
}

static uint8_t *payload_of(struct heap_block *block) {
    return (uint8_t *)(block + 1);
}

/* Walks the block chain from the region start. Returns 0 on success. */
int heap_validate(void) {
    uint8_t *cursor = heap_base;

    while (cursor < heap_end) {
        struct heap_block *block = block_at(cursor);
        if (cursor + sizeof(*block) > heap_end) return -1;
        if (block->size < HEAP_HEADER + ZEROOS_HEAP_MIN_ALLOC) return -1;
        if ((block->size & (ZEROOS_HEAP_ALIGN - 1)) != 0) return -1;
        if (cursor + block->size > heap_end) return -1;
        if (block->magic != HEAP_MAGIC && block->magic != HEAP_FREE_MAGIC)
            return -1;
        if (block->magic == HEAP_MAGIC && block->canary != HEAP_CANARY)
            return -1;
        cursor += block->size;
    }
    return cursor == heap_end ? 0 : -1;
}

int heap_init(void) {
    uint8_t *region = 0;
    uint64_t pages = 0;
    struct heap_block *root;

    if (heap_ready)
        return 0;

    /*
     * Consume pages from the physical allocator until a strictly linear
     * window is built. The bitmap allocator hands out pages in address
     * order, so the contiguous prefix before the first gap is taken; the
     * gap page is returned. The region may be as small as 1 MiB on hosts
     * with fragmented low memory; capacity is reported at runtime.
     */
    while (pages < HEAP_REGION_PAGES) {
        uint8_t *next = (uint8_t *)page_alloc_zero();
        if (!next)
            break;
        if (pages > 0 &&
            (uint64_t)next != (uint64_t)region + pages * ZEROOS_PAGE_SIZE) {
            page_free(next);
            break;
        }
        if (!region)
            region = next;
        ++pages;
    }

    if (!region || pages < HEAP_MIN_PAGES) {
        for (uint64_t i = 0; i < pages; ++i)
            page_free(region + i * ZEROOS_PAGE_SIZE);
        return -1;
    }

    heap_base = region;
    heap_end = region + pages * ZEROOS_PAGE_SIZE;
    used_bytes = 0;
    spinlock_init(&heap_lock);

    root = block_at(heap_base);
    root->size = (uint64_t)(heap_end - heap_base);
    root->magic = HEAP_FREE_MAGIC;
    root->canary = 0;

    heap_ready = 1;
    return 0;
}

void *kmalloc(uint64_t size) {
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
    uint8_t *payload = (uint8_t *)pointer;
    uint8_t *next_address;
    struct heap_block *block;
    struct heap_block *next;
    uint64_t payload_size;
    uint64_t flags;
    int result = -1;

    if (!heap_ready || !pointer)
        return -1;
    if (payload < payload_of(block_at(heap_base)) || payload >= heap_end)
        return -1;
    if ((uint64_t)(payload - heap_base) % ZEROOS_HEAP_ALIGN != 0)
        return -1;

    block = (struct heap_block *)(payload - HEAP_HEADER);

    flags = spin_lock_irqsave(&heap_lock);

    if (block->magic == HEAP_MAGIC) {
        if (block->canary != HEAP_CANARY) {
            spin_unlock_irqrestore(&heap_lock, flags);
            serial_write_public("ZEROOS PANIC: heap canary corrupted (buffer overflow).\n");
            for (;;) __asm__ volatile ("cli; hlt");
        }
        payload_size = block->size - HEAP_HEADER;

        /* Forward coalescing with the physically following block. */
        next_address = (uint8_t *)block + block->size;
        if (next_address < heap_end) {
            next = block_at(next_address);
            if (next->magic == HEAP_FREE_MAGIC)
                block->size += next->size;
        }

        block->magic = HEAP_FREE_MAGIC;
        block->canary = 0;
        used_bytes -= payload_size;
        result = 0;
    }

    spin_unlock_irqrestore(&heap_lock, flags);
    return result;
}

uint64_t heap_used_bytes(void) { return used_bytes; }
uint64_t heap_capacity_bytes(void) {
    return heap_ready ? (uint64_t)(heap_end - heap_base) - HEAP_HEADER : 0;
}
