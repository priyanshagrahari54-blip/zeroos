#include "window.h"
#include "compositor.h"

static struct spinlock window_lock;
static struct zeroos_window windows[ZEROOS_WINDOW_MAX_WINDOWS];

int window_system_init(void) {
    spinlock_init(&window_lock);
    for (uint32_t i=0;i<ZEROOS_WINDOW_MAX_WINDOWS;++i) {
        windows[i].used=0;
        windows[i].generation=0;
        windows[i].state=ZEROOS_WINDOW_STOPPED;
        spinlock_init(&windows[i].lock);
        wait_queue_init(&windows[i].event_waiters);
    }
    return 0;
}

int window_create(enum zeroos_window_type type, const char *title,
                  int32_t x, int32_t y, uint32_t width, uint32_t height,
                  uint64_t owner_task_id, uint64_t *window_id_out) {
    if (!title || !window_id_out || width==0 || height==0 || owner_task_id==0) return -1;
    if (width>8192 || height>8192) return -1;
    uint64_t flags=spin_lock_irqsave(&window_lock);
    for (uint32_t i=0;i<ZEROOS_WINDOW_MAX_WINDOWS;++i) {
        if (windows[i].used) continue;
        if (windows[i].generation==0xffffffffU) continue;
        windows[i].generation++;
        if (windows[i].generation==0) continue;
        windows[i].used=1;
        windows[i].type=type;
        windows[i].state=ZEROOS_WINDOW_DORMANT;
        windows[i].x=x;
        windows[i].y=y;
        windows[i].width=width;
        windows[i].height=height;
        windows[i].owner_task_id=owner_task_id;
        windows[i].focused=0;
        windows[i].visible=1;
        windows[i].z_order=i;
        windows[i].event_count=0;
        uint32_t n=0;
        while (n<ZEROOS_WINDOW_MAX_TITLE-1 && title[n]) { windows[i].title[n]=title[n]; n++; }
        windows[i].title[n]=0;
        windows[i].id = ((uint64_t)windows[i].generation<<16) | (uint64_t)(i+1);
        *window_id_out=windows[i].id;
        spin_unlock_irqrestore(&window_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&window_lock,flags);
    return -1;
}

int window_set_position(uint64_t window_id, int32_t x, int32_t y) {
    uint64_t flags=spin_lock_irqsave(&window_lock);
    uint32_t slot=(uint32_t)(window_id & 0xffffULL);
    uint32_t gen=(uint32_t)(window_id>>16);
    if (slot==0 || slot>ZEROOS_WINDOW_MAX_WINDOWS || gen==0) { spin_unlock_irqrestore(&window_lock,flags); return -1; }
    struct zeroos_window *w=&windows[slot-1];
    if (!w->used || w->generation!=gen) { spin_unlock_irqrestore(&window_lock,flags); return -1; }
    uint64_t wflags=spin_lock_irqsave(&w->lock);
    w->x=x; w->y=y;
    spin_unlock_irqrestore(&w->lock,wflags);
    spin_unlock_irqrestore(&window_lock,flags);
    return 0;
}

int window_set_size(uint64_t window_id, uint32_t width, uint32_t height) {
    if (width==0 || height==0 || width>8192 || height>8192) return -1;
    uint64_t flags=spin_lock_irqsave(&window_lock);
    uint32_t slot=(uint32_t)(window_id & 0xffffULL);
    uint32_t gen=(uint32_t)(window_id>>16);
    if (slot==0 || slot>ZEROOS_WINDOW_MAX_WINDOWS || gen==0) { spin_unlock_irqrestore(&window_lock,flags); return -1; }
    struct zeroos_window *w=&windows[slot-1];
    if (!w->used || w->generation!=gen) { spin_unlock_irqrestore(&window_lock,flags); return -1; }
    uint64_t wflags=spin_lock_irqsave(&w->lock);
    w->width=width; w->height=height;
    spin_unlock_irqrestore(&w->lock,wflags);
    spin_unlock_irqrestore(&window_lock,flags);
    return 0;
}

int window_set_state(uint64_t window_id, enum zeroos_window_state state) {
    uint64_t flags=spin_lock_irqsave(&window_lock);
    uint32_t slot=(uint32_t)(window_id & 0xffffULL);
    uint32_t gen=(uint32_t)(window_id>>16);
    if (slot==0 || slot>ZEROOS_WINDOW_MAX_WINDOWS || gen==0) { spin_unlock_irqrestore(&window_lock,flags); return -1; }
    struct zeroos_window *w=&windows[slot-1];
    if (!w->used || w->generation!=gen) { spin_unlock_irqrestore(&window_lock,flags); return -1; }
    uint64_t wflags=spin_lock_irqsave(&w->lock);
    if (state==ZEROOS_WINDOW_CLOSED) w->used=0;
    w->state=state;
    spin_unlock_irqrestore(&w->lock,wflags);
    spin_unlock_irqrestore(&window_lock,flags);
    return 0;
}

int window_set_focus(uint64_t window_id, uint8_t focused) {
    uint64_t flags=spin_lock_irqsave(&window_lock);
    for (uint32_t i=0;i<ZEROOS_WINDOW_MAX_WINDOWS;++i) {
        if (!windows[i].used) continue;
        uint64_t wflags=spin_lock_irqsave(&windows[i].lock);
        if (windows[i].id==window_id) windows[i].focused=focused;
        else if (focused) windows[i].focused=0;
        spin_unlock_irqrestore(&windows[i].lock,wflags);
    }
    spin_unlock_irqrestore(&window_lock,flags);
    return 0;
}

int window_destroy(uint64_t window_id) {
    return window_set_state(window_id, ZEROOS_WINDOW_CLOSED);
}

struct zeroos_window *window_lookup(uint64_t window_id) {
    uint64_t flags=spin_lock_irqsave(&window_lock);
    uint32_t slot=(uint32_t)(window_id & 0xffffULL);
    uint32_t gen=(uint32_t)(window_id>>16);
    struct zeroos_window *w=0;
    if (slot && slot<=ZEROOS_WINDOW_MAX_WINDOWS && gen) {
        struct zeroos_window *win=&windows[slot-1];
        if (win->used && win->generation==gen) w=win;
    }
    spin_unlock_irqrestore(&window_lock,flags);
    return w;
}

int window_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&window_lock);
    for (uint32_t i=0;i<ZEROOS_WINDOW_MAX_WINDOWS;++i) if (windows[i].used && (windows[i].width==0 || windows[i].height==0)) { spin_unlock_irqrestore(&window_lock,flags); return -1; }
    spin_unlock_irqrestore(&window_lock,flags);
    return 0;
}
