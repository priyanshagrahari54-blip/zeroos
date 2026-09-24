#ifndef ZEROOS_COMPOSITOR_H
#define ZEROOS_COMPOSITOR_H

#include "types.h"
#include "sync.h"
#include "wait.h"
#include "display.h"

#define ZEROOS_COMPOSITOR_MAX_LAYERS 64U
#define ZEROOS_COMPOSITOR_MAX_SURFACES 64U

enum zeroos_compositor_state {
    ZEROOS_COMPOSITOR_STOPPED = 0,
    ZEROOS_COMPOSITOR_DORMANT,
    ZEROOS_COMPOSITOR_WARM,
    ZEROOS_COMPOSITOR_ACTIVE,
    ZEROOS_COMPOSITOR_THROTTLED,
    ZEROOS_COMPOSITOR_SUSPENDED
};

enum zeroos_layer_type {
    ZEROOS_LAYER_BACKGROUND = 0,
    ZEROOS_LAYER_WINDOW,
    ZEROOS_LAYER_OVERLAY,
    ZEROOS_LAYER_CURSOR
};

struct zeroos_surface {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint64_t buffer_id; /* graphics buffer */
    uint64_t owner_task_id;
    uint8_t dirty;
    struct spinlock lock;
};

struct zeroos_layer {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_layer_type type;
    uint64_t surface_id;
    int32_t x;
    int32_t y;
    uint32_t z_order;
    uint8_t visible;
    float opacity;
    struct spinlock lock;
};

struct zeroos_compositor {
    struct spinlock lock;
    enum zeroos_compositor_state state;
    uint64_t display_device_id;
    struct zeroos_surface surfaces[ZEROOS_COMPOSITOR_MAX_SURFACES];
    struct zeroos_layer layers[ZEROOS_COMPOSITOR_MAX_LAYERS];
    uint64_t frame_count;
    uint64_t vsync_count;
    struct wait_queue frame_waiters;
};

int compositor_system_init(void);
int compositor_set_display(uint64_t display_device_id);
int compositor_surface_create(uint32_t width, uint32_t height, uint32_t stride,
                              uint64_t buffer_id, uint64_t owner_task_id,
                              uint64_t *surface_id_out);
int compositor_surface_destroy(uint64_t surface_id);
int compositor_surface_mark_dirty(uint64_t surface_id);
int compositor_layer_create(enum zeroos_layer_type type, uint64_t surface_id,
                            int32_t x, int32_t y, uint32_t z_order, uint64_t *layer_id_out);
int compositor_layer_set_position(uint64_t layer_id, int32_t x, int32_t y);
int compositor_layer_set_visibility(uint64_t layer_id, uint8_t visible);
int compositor_layer_destroy(uint64_t layer_id);
int compositor_composite_frame(void);
int compositor_set_state(enum zeroos_compositor_state state);
int compositor_debug_validate(void);

#endif
