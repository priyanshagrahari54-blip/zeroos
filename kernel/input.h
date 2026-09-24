#ifndef ZEROOS_INPUT_H
#define ZEROOS_INPUT_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_INPUT_MAX_DEVICES 16U
#define ZEROOS_INPUT_MAX_EVENTS 256U

enum zeroos_input_device_type {
    ZEROOS_INPUT_TYPE_UNKNOWN = 0,
    ZEROOS_INPUT_TYPE_KEYBOARD,
    ZEROOS_INPUT_TYPE_MOUSE,
    ZEROOS_INPUT_TYPE_TOUCHPAD,
    ZEROOS_INPUT_TYPE_TOUCHSCREEN,
    ZEROOS_INPUT_TYPE_JOYSTICK
};

enum zeroos_input_event_type {
    ZEROOS_INPUT_EV_KEY = 1,
    ZEROOS_INPUT_EV_REL = 2,
    ZEROOS_INPUT_EV_ABS = 3,
    ZEROOS_INPUT_EV_SYN = 0
};

enum zeroos_input_state {
    ZEROOS_INPUT_STOPPED = 0,
    ZEROOS_INPUT_DORMANT,
    ZEROOS_INPUT_WARM,
    ZEROOS_INPUT_ACTIVE,
    ZEROOS_INPUT_THROTTLED,
    ZEROOS_INPUT_SUSPENDED
};

struct zeroos_input_event {
    uint64_t timestamp;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct zeroos_input_device {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_input_state state;
    enum zeroos_input_device_type type;
    char name[32];
    struct spinlock lock;
    struct wait_queue event_waiters;
    struct zeroos_input_event ring[ZEROOS_INPUT_MAX_EVENTS];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint64_t event_count;
    uint64_t dropped_count;
    uint8_t exclusive_owner; /* task id bound? simplified */
};

int input_system_init(void);
int input_device_register(enum zeroos_input_device_type type, const char *name, uint64_t *device_id_out);
int input_device_push_event(uint64_t device_id, struct zeroos_input_event *ev);
int input_device_read_events(uint64_t device_id, struct zeroos_input_event *buf, uint32_t cap,
                              uint32_t *read_out, uint64_t timeout_ticks);
int input_device_set_state(uint64_t device_id, enum zeroos_input_state state);
struct zeroos_input_device *input_device_lookup(uint64_t device_id);
int input_debug_validate(void);

#endif
