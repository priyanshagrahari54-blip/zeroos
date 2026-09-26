#ifndef ZEROOS_GRAPHICS_H
#define ZEROOS_GRAPHICS_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_GRAPHICS_MAX_CONTEXTS 32U
#define ZEROOS_GRAPHICS_MAX_BUFFERS 64U
#define ZEROOS_GRAPHICS_MAX_COMMANDS 256U

enum zeroos_graphics_state {
    ZEROOS_GRAPHICS_STOPPED = 0,
    ZEROOS_GRAPHICS_DORMANT,
    ZEROOS_GRAPHICS_WARM,
    ZEROOS_GRAPHICS_ACTIVE,
    ZEROOS_GRAPHICS_THROTTLED,
    ZEROOS_GRAPHICS_SUSPENDED
};

enum zeroos_graphics_buffer_type {
    ZEROOS_GFX_BUFFER_FRAMEBUFFER = 0,
    ZEROOS_GFX_BUFFER_TEXTURE,
    ZEROOS_GFX_BUFFER_VERTEX,
    ZEROOS_GFX_BUFFER_COMMAND
};

struct zeroos_graphics_buffer {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_graphics_buffer_type type;
    uint64_t size;
    uint64_t phys;
    void *virt;
    uint64_t owner_task_id;
    struct spinlock lock;
};

struct zeroos_graphics_context {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_graphics_state state;
    uint64_t owner_task_id;
    uint64_t device_id;
    struct spinlock lock;
    struct wait_queue fence_waiters;
    uint64_t submitted_commands;
    uint64_t completed_commands;
    uint64_t gpu_memory_used;
    uint64_t gpu_memory_budget;
};

struct zeroos_graphics_command {
    uint64_t context_id;
    uint64_t buffer_id;
    uint32_t opcode;
    uint32_t size;
    uint8_t valid;
};

int graphics_system_init(void);
int graphics_context_create(uint64_t device_id, uint64_t owner_task_id, uint64_t *ctx_id_out);
int graphics_buffer_alloc(uint64_t ctx_id, enum zeroos_graphics_buffer_type type,
                          uint64_t size, uint64_t owner_task_id, uint64_t *buffer_id_out);
int graphics_buffer_free(uint64_t buffer_id);
int graphics_submit(uint64_t ctx_id, struct zeroos_graphics_command *cmds, uint32_t count);
int graphics_context_set_state(uint64_t ctx_id, enum zeroos_graphics_state state);
int graphics_context_destroy(uint64_t ctx_id);
int graphics_debug_validate(void);

#endif
