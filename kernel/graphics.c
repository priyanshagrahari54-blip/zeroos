#include "graphics.h"
#include "memory.h"

static struct spinlock graphics_lock;
static struct zeroos_graphics_context contexts[ZEROOS_GRAPHICS_MAX_CONTEXTS];
static struct zeroos_graphics_buffer buffers[ZEROOS_GRAPHICS_MAX_BUFFERS];

int graphics_system_init(void) {
    spinlock_init(&graphics_lock);
    for (uint32_t i=0;i<ZEROOS_GRAPHICS_MAX_CONTEXTS;++i) {
        contexts[i].used=0;
        contexts[i].generation=0;
        contexts[i].state=ZEROOS_GRAPHICS_STOPPED;
        spinlock_init(&contexts[i].lock);
        wait_queue_init(&contexts[i].fence_waiters);
    }
    for (uint32_t i=0;i<ZEROOS_GRAPHICS_MAX_BUFFERS;++i) {
        buffers[i].used=0;
        buffers[i].generation=0;
        spinlock_init(&buffers[i].lock);
    }
    return 0;
}

int graphics_context_create(uint64_t device_id, uint64_t owner_task_id, uint64_t *ctx_id_out) {
    if (!ctx_id_out || owner_task_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&graphics_lock);
    for (uint32_t i=0;i<ZEROOS_GRAPHICS_MAX_CONTEXTS;++i) {
        if (contexts[i].used) continue;
        if (contexts[i].generation==0xffffffffU) continue;
        contexts[i].generation++;
        if (contexts[i].generation==0) continue;
        contexts[i].used=1;
        contexts[i].state=ZEROOS_GRAPHICS_DORMANT;
        contexts[i].owner_task_id=owner_task_id;
        contexts[i].device_id=device_id;
        contexts[i].submitted_commands=0;
        contexts[i].completed_commands=0;
        contexts[i].gpu_memory_used=0;
        contexts[i].gpu_memory_budget=128*1024*1024;
        contexts[i].id = ((uint64_t)contexts[i].generation<<16) | (uint64_t)(i+1);
        *ctx_id_out=contexts[i].id;
        spin_unlock_irqrestore(&graphics_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&graphics_lock,flags);
    return -1;
}

int graphics_buffer_alloc(uint64_t ctx_id, enum zeroos_graphics_buffer_type type,
                          uint64_t size, uint64_t owner_task_id, uint64_t *buffer_id_out) {
    if (!buffer_id_out || size==0 || size>64*1024*1024) return -1;
    uint64_t flags=spin_lock_irqsave(&graphics_lock);
    uint32_t cslot=(uint32_t)(ctx_id & 0xffffULL);
    uint32_t cgen=(uint32_t)(ctx_id>>16);
    if (cslot==0 || cslot>ZEROOS_GRAPHICS_MAX_CONTEXTS || cgen==0) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    struct zeroos_graphics_context *ctx=&contexts[cslot-1];
    if (!ctx->used || ctx->generation!=cgen) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    if (ctx->gpu_memory_used+size > ctx->gpu_memory_budget) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    for (uint32_t i=0;i<ZEROOS_GRAPHICS_MAX_BUFFERS;++i) {
        if (buffers[i].used) continue;
        if (buffers[i].generation==0xffffffffU) continue;
        buffers[i].generation++;
        if (buffers[i].generation==0) continue;
        void *pages=0;
        uint64_t needed_pages=(size+4095)/4096;
        /* Simplified single page alloc for now */
        if (needed_pages>1) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
        pages=page_alloc();
        if (!pages) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
        buffers[i].used=1;
        buffers[i].type=type;
        buffers[i].size=size;
        buffers[i].virt=pages;
        buffers[i].phys=0; /* would translate */
        buffers[i].owner_task_id=owner_task_id;
        buffers[i].id = ((uint64_t)buffers[i].generation<<16) | (uint64_t)(i+1);
        ctx->gpu_memory_used+=size;
        *buffer_id_out=buffers[i].id;
        spin_unlock_irqrestore(&graphics_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&graphics_lock,flags);
    return -1;
}

int graphics_buffer_free(uint64_t buffer_id) {
    uint64_t flags=spin_lock_irqsave(&graphics_lock);
    uint32_t slot=(uint32_t)(buffer_id & 0xffffULL);
    uint32_t gen=(uint32_t)(buffer_id>>16);
    if (slot==0 || slot>ZEROOS_GRAPHICS_MAX_BUFFERS || gen==0) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    struct zeroos_graphics_buffer *b=&buffers[slot-1];
    if (!b->used || b->generation!=gen) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    uint64_t bflags=spin_lock_irqsave(&b->lock);
    if (b->virt) page_free(b->virt);
    b->used=0;
    b->virt=0;
    b->phys=0;
    spin_unlock_irqrestore(&b->lock,bflags);
    spin_unlock_irqrestore(&graphics_lock,flags);
    return 0;
}

int graphics_submit(uint64_t ctx_id, struct zeroos_graphics_command *cmds, uint32_t count) {
    if (!cmds || count==0 || count>ZEROOS_GRAPHICS_MAX_COMMANDS) return -1;
    uint64_t flags=spin_lock_irqsave(&graphics_lock);
    uint32_t cslot=(uint32_t)(ctx_id & 0xffffULL);
    uint32_t cgen=(uint32_t)(ctx_id>>16);
    if (cslot==0 || cslot>ZEROOS_GRAPHICS_MAX_CONTEXTS || cgen==0) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    struct zeroos_graphics_context *ctx=&contexts[cslot-1];
    if (!ctx->used || ctx->generation!=cgen) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    uint64_t cflags=spin_lock_irqsave(&ctx->lock);
    for (uint32_t i=0;i<count;++i) {
        if (!cmds[i].valid) { spin_unlock_irqrestore(&ctx->lock,cflags); spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    }
    ctx->submitted_commands+=count;
    ctx->completed_commands+=count; /* synchronous completion for stub */
    spin_unlock_irqrestore(&ctx->lock,cflags);
    (void)wait_queue_wake_all(&ctx->fence_waiters);
    spin_unlock_irqrestore(&graphics_lock,flags);
    return 0;
}

int graphics_context_set_state(uint64_t ctx_id, enum zeroos_graphics_state state) {
    uint64_t flags=spin_lock_irqsave(&graphics_lock);
    uint32_t slot=(uint32_t)(ctx_id & 0xffffULL);
    uint32_t gen=(uint32_t)(ctx_id>>16);
    if (slot==0 || slot>ZEROOS_GRAPHICS_MAX_CONTEXTS || gen==0) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    struct zeroos_graphics_context *ctx=&contexts[slot-1];
    if (!ctx->used || ctx->generation!=gen) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    uint64_t cflags=spin_lock_irqsave(&ctx->lock);
    ctx->state=state;
    spin_unlock_irqrestore(&ctx->lock,cflags);
    spin_unlock_irqrestore(&graphics_lock,flags);
    return 0;
}

int graphics_context_destroy(uint64_t ctx_id) {
    uint64_t flags=spin_lock_irqsave(&graphics_lock);
    uint32_t slot=(uint32_t)(ctx_id & 0xffffULL);
    uint32_t gen=(uint32_t)(ctx_id>>16);
    if (slot==0 || slot>ZEROOS_GRAPHICS_MAX_CONTEXTS || gen==0) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    struct zeroos_graphics_context *ctx=&contexts[slot-1];
    if (!ctx->used || ctx->generation!=gen) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    uint64_t cflags=spin_lock_irqsave(&ctx->lock);
    ctx->used=0;
    ctx->state=ZEROOS_GRAPHICS_STOPPED;
    (void)wait_queue_wake_all(&ctx->fence_waiters);
    spin_unlock_irqrestore(&ctx->lock,cflags);
    spin_unlock_irqrestore(&graphics_lock,flags);
    return 0;
}

int graphics_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&graphics_lock);
    for (uint32_t i=0;i<ZEROOS_GRAPHICS_MAX_CONTEXTS;++i) if (contexts[i].used && contexts[i].gpu_memory_used>contexts[i].gpu_memory_budget) { spin_unlock_irqrestore(&graphics_lock,flags); return -1; }
    spin_unlock_irqrestore(&graphics_lock,flags);
    return 0;
}
