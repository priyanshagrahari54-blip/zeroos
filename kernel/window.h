#ifndef ZEROOS_WINDOW_H
#define ZEROOS_WINDOW_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_WINDOW_MAX_WINDOWS 64U
#define ZEROOS_WINDOW_MAX_TITLE 64U

enum zeroos_window_state {
    ZEROOS_WINDOW_STOPPED = 0,
    ZEROOS_WINDOW_DORMANT,
    ZEROOS_WINDOW_WARM,
    ZEROOS_WINDOW_ACTIVE,
    ZEROOS_WINDOW_THROTTLED,
    ZEROOS_WINDOW_SUSPENDED,
    ZEROOS_WINDOW_MINIMIZED,
    ZEROOS_WINDOW_MAXIMIZED,
    ZEROOS_WINDOW_CLOSED
};

enum zeroos_window_type {
    ZEROOS_WINDOW_TYPE_NORMAL = 0,
    ZEROOS_WINDOW_TYPE_DIALOG,
    ZEROOS_WINDOW_TYPE_POPUP,
    ZEROOS_WINDOW_TYPE_TOOLTIP,
    ZEROOS_WINDOW_TYPE_DESKTOP
};

struct zeroos_window {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_window_state state;
    enum zeroos_window_type type;
    char title[ZEROOS_WINDOW_MAX_TITLE];
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t surface_id;
    uint64_t layer_id;
    uint64_t owner_task_id;
    uint8_t focused;
    uint8_t visible;
    uint32_t z_order;
    struct spinlock lock;
    struct wait_queue event_waiters;
    uint64_t event_count;
};

int window_system_init(void);
int window_create(enum zeroos_window_type type, const char *title,
                  int32_t x, int32_t y, uint32_t width, uint32_t height,
                  uint64_t owner_task_id, uint64_t *window_id_out);
int window_set_position(uint64_t window_id, int32_t x, int32_t y);
int window_set_size(uint64_t window_id, uint32_t width, uint32_t height);
int window_set_state(uint64_t window_id, enum zeroos_window_state state);
int window_set_focus(uint64_t window_id, uint8_t focused);
int window_destroy(uint64_t window_id);
struct zeroos_window *window_lookup(uint64_t window_id);
int window_debug_validate(void);

#endif
