#ifndef ZEROOS_DESKTOP_LIFECYCLE_H
#define ZEROOS_DESKTOP_LIFECYCLE_H

/* Desktop session lifecycle.
 *
 * STOPPED -> DORMANT -> WARM -> ACTIVE -> THROTTLED -> SUSPENDED with
 * documented reverse edges. Transitions are table-driven; illegal events are
 * rejected with -ZD_EINVAL instead of silently changing state. Only tiny
 * controllers remain warm; heavy engines are demand-activated by the session
 * services that observe the lifecycle listener. */

#include <zeroos/desktop/common.h>

enum zd_lifecycle_state {
    ZD_LIFECYCLE_STOPPED = 0,
    ZD_LIFECYCLE_DORMANT = 1,
    ZD_LIFECYCLE_WARM = 2,
    ZD_LIFECYCLE_ACTIVE = 3,
    ZD_LIFECYCLE_THROTTLED = 4,
    ZD_LIFECYCLE_SUSPENDED = 5,
    ZD_LIFECYCLE_STATE_COUNT = 6
};

enum zd_lifecycle_event {
    ZD_LIFECYCLE_START = 0,
    ZD_LIFECYCLE_CONTROLLERS_UP = 1,
    ZD_LIFECYCLE_ACTIVATE = 2,
    ZD_LIFECYCLE_USER_IDLE = 3,
    ZD_LIFECYCLE_USER_ACTIVITY = 4,
    ZD_LIFECYCLE_PRESSURE = 5,
    ZD_LIFECYCLE_PRESSURE_RELEASED = 6,
    ZD_LIFECYCLE_SUSPEND = 7,
    ZD_LIFECYCLE_RESUME = 8,
    ZD_LIFECYCLE_SERVICE_FAILURE = 9,
    ZD_LIFECYCLE_STOP = 10,
    ZD_LIFECYCLE_EVENT_COUNT = 11
};

typedef void (*zd_lifecycle_listener)(void *context,
                                      enum zd_lifecycle_state previous,
                                      enum zd_lifecycle_state next,
                                      enum zd_lifecycle_event cause);

#define ZD_LIFECYCLE_MAX_LISTENERS 4
#define ZD_LIFECYCLE_MAX_HISTORY 16

struct zd_lifecycle {
    enum zd_lifecycle_state state;
    uint64_t transition_count;
    uint32_t illegal_event_count;
    struct {
        uint64_t sequence;
        enum zd_lifecycle_state from;
        enum zd_lifecycle_state next;
        enum zd_lifecycle_event cause;
    } history[ZD_LIFECYCLE_MAX_HISTORY];
    uint32_t history_head;
    uint32_t history_count;
    uint32_t listener_count;
    struct {
        zd_lifecycle_listener callback;
        void *context;
    } listeners[ZD_LIFECYCLE_MAX_LISTENERS];
};

void zd_lifecycle_init(struct zd_lifecycle *lifecycle);
int zd_lifecycle_state(const struct zd_lifecycle *lifecycle);
int zd_lifecycle_add_listener(struct zd_lifecycle *lifecycle,
                              zd_lifecycle_listener callback, void *context);
/* Returns 0 on a performed transition, -ZD_EINVAL on an illegal event. */
int zd_lifecycle_dispatch(struct zd_lifecycle *lifecycle,
                          enum zd_lifecycle_event event, uint64_t now_ns);
/* Heavy engines (search indexing, AI, background sync) may only run while
 * the session is ACTIVE or THROTTLED-with-explicit-override. */
int zd_lifecycle_allows_heavy_work(const struct zd_lifecycle *lifecycle);
const char *zd_lifecycle_state_name(enum zd_lifecycle_state state);
const char *zd_lifecycle_event_name(enum zd_lifecycle_event event);

#endif
