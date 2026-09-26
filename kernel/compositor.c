#include "compositor.h"

static struct zeroos_compositor compositor;

int compositor_system_init(void) {
    spinlock_init(&compositor.lock);
    compositor.state=ZEROOS_COMPOSITOR_STOPPED;
    compositor.display_device_id=0;
    compositor.frame_count=0;
    compositor.vsync_count=0;
    wait_queue_init(&compositor.frame_waiters);
    for (uint32_t i=0;i<ZEROOS_COMPOSITOR_MAX_SURFACES;++i) {
        compositor.surfaces[i].used=0;
        compositor.surfaces[i].generation=0;
        spinlock_init(&compositor.surfaces[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_COMPOSITOR_MAX_LAYERS;++i) {
        compositor.layers[i].used=0;
        compositor.layers[i].generation=0;
        spinlock_init(&compositor.layers[i].lock);
    }
    return 0;
}

int compositor_set_display(uint64_t display_device_id) {
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    compositor.display_device_id=display_device_id;
    compositor.state=ZEROOS_COMPOSITOR_DORMANT;
    spin_unlock_irqrestore(&compositor.lock,flags);
    return 0;
}

int compositor_surface_create(uint32_t width, uint32_t height, uint32_t stride,
                              uint64_t buffer_id, uint64_t owner_task_id,
                              uint64_t *surface_id_out) {
    if (!surface_id_out || width==0 || height==0) return -1;
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    for (uint32_t i=0;i<ZEROOS_COMPOSITOR_MAX_SURFACES;++i) {
        if (compositor.surfaces[i].used) continue;
        if (compositor.surfaces[i].generation==0xffffffffU) continue;
        compositor.surfaces[i].generation++;
        if (compositor.surfaces[i].generation==0) continue;
        compositor.surfaces[i].used=1;
        compositor.surfaces[i].width=width;
        compositor.surfaces[i].height=height;
        compositor.surfaces[i].stride=stride ? stride : width*4;
        compositor.surfaces[i].buffer_id=buffer_id;
        compositor.surfaces[i].owner_task_id=owner_task_id;
        compositor.surfaces[i].dirty=1;
        compositor.surfaces[i].id = ((uint64_t)compositor.surfaces[i].generation<<16) | (uint64_t)(i+1);
        *surface_id_out=compositor.surfaces[i].id;
        spin_unlock_irqrestore(&compositor.lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&compositor.lock,flags);
    return -1;
}

int compositor_surface_destroy(uint64_t surface_id) {
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    uint32_t slot=(uint32_t)(surface_id & 0xffffULL);
    uint32_t gen=(uint32_t)(surface_id>>16);
    if (slot==0 || slot>ZEROOS_COMPOSITOR_MAX_SURFACES || gen==0) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    struct zeroos_surface *s=&compositor.surfaces[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->used=0;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&compositor.lock,flags);
    return 0;
}

int compositor_surface_mark_dirty(uint64_t surface_id) {
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    uint32_t slot=(uint32_t)(surface_id & 0xffffULL);
    uint32_t gen=(uint32_t)(surface_id>>16);
    if (slot==0 || slot>ZEROOS_COMPOSITOR_MAX_SURFACES || gen==0) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    struct zeroos_surface *s=&compositor.surfaces[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->dirty=1;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&compositor.lock,flags);
    return 0;
}

int compositor_layer_create(enum zeroos_layer_type type, uint64_t surface_id,
                            int32_t x, int32_t y, uint32_t z_order, uint64_t *layer_id_out) {
    if (!layer_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    for (uint32_t i=0;i<ZEROOS_COMPOSITOR_MAX_LAYERS;++i) {
        if (compositor.layers[i].used) continue;
        if (compositor.layers[i].generation==0xffffffffU) continue;
        compositor.layers[i].generation++;
        if (compositor.layers[i].generation==0) continue;
        compositor.layers[i].used=1;
        compositor.layers[i].type=type;
        compositor.layers[i].surface_id=surface_id;
        compositor.layers[i].x=x;
        compositor.layers[i].y=y;
        compositor.layers[i].z_order=z_order;
        compositor.layers[i].visible=1;
        compositor.layers[i].opacity=1.0f;
        compositor.layers[i].id = ((uint64_t)compositor.layers[i].generation<<16) | (uint64_t)(i+1);
        *layer_id_out=compositor.layers[i].id;
        spin_unlock_irqrestore(&compositor.lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&compositor.lock,flags);
    return -1;
}

int compositor_layer_set_position(uint64_t layer_id, int32_t x, int32_t y) {
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    uint32_t slot=(uint32_t)(layer_id & 0xffffULL);
    uint32_t gen=(uint32_t)(layer_id>>16);
    if (slot==0 || slot>ZEROOS_COMPOSITOR_MAX_LAYERS || gen==0) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    struct zeroos_layer *l=&compositor.layers[slot-1];
    if (!l->used || l->generation!=gen) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    uint64_t lflags=spin_lock_irqsave(&l->lock);
    l->x=x; l->y=y;
    spin_unlock_irqrestore(&l->lock,lflags);
    spin_unlock_irqrestore(&compositor.lock,flags);
    return 0;
}

int compositor_layer_set_visibility(uint64_t layer_id, uint8_t visible) {
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    uint32_t slot=(uint32_t)(layer_id & 0xffffULL);
    uint32_t gen=(uint32_t)(layer_id>>16);
    if (slot==0 || slot>ZEROOS_COMPOSITOR_MAX_LAYERS || gen==0) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    struct zeroos_layer *l=&compositor.layers[slot-1];
    if (!l->used || l->generation!=gen) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    uint64_t lflags=spin_lock_irqsave(&l->lock);
    l->visible=visible;
    spin_unlock_irqrestore(&l->lock,lflags);
    spin_unlock_irqrestore(&compositor.lock,flags);
    return 0;
}

int compositor_layer_destroy(uint64_t layer_id) {
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    uint32_t slot=(uint32_t)(layer_id & 0xffffULL);
    uint32_t gen=(uint32_t)(layer_id>>16);
    if (slot==0 || slot>ZEROOS_COMPOSITOR_MAX_LAYERS || gen==0) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    struct zeroos_layer *l=&compositor.layers[slot-1];
    if (!l->used || l->generation!=gen) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    uint64_t lflags=spin_lock_irqsave(&l->lock);
    l->used=0;
    spin_unlock_irqrestore(&l->lock,lflags);
    spin_unlock_irqrestore(&compositor.lock,flags);
    return 0;
}

int compositor_composite_frame(void) {
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    if (compositor.state!=ZEROOS_COMPOSITOR_ACTIVE) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    compositor.frame_count++;
    /* Simplified: iterate layers sorted by z_order, blit dirty surfaces */
    spin_unlock_irqrestore(&compositor.lock,flags);
    (void)wait_queue_wake_all(&compositor.frame_waiters);
    return 0;
}

int compositor_set_state(enum zeroos_compositor_state state) {
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    compositor.state=state;
    spin_unlock_irqrestore(&compositor.lock,flags);
    return 0;
}

int compositor_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&compositor.lock);
    for (uint32_t i=0;i<ZEROOS_COMPOSITOR_MAX_SURFACES;++i) if (compositor.surfaces[i].used && compositor.surfaces[i].width==0) { spin_unlock_irqrestore(&compositor.lock,flags); return -1; }
    spin_unlock_irqrestore(&compositor.lock,flags);
    return 0;
}
