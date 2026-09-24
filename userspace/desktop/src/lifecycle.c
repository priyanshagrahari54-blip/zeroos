#include <zeroos/desktop/lifecycle.h>

/* Legal transition table indexed by [state][event]; entries are the next
 * state or -1 when the event is illegal in that state. */
static const int8_t lifecycle_table[ZD_LIFECYCLE_STATE_COUNT]
                                   [ZD_LIFECYCLE_EVENT_COUNT] = {
    /* STOPPED */
    {
        ZD_LIFECYCLE_DORMANT,  /* START */
        -1,                    /* CONTROLLERS_UP */
        -1,                    /* ACTIVATE */
        -1,                    /* USER_IDLE */
        -1,                    /* USER_ACTIVITY */
        -1,                    /* PRESSURE */
        -1,                    /* PRESSURE_RELEASED */
        -1,                    /* SUSPEND */
        -1,                    /* RESUME */
        -1,                    /* SERVICE_FAILURE */
        -1                     /* STOP */
    },
    /* DORMANT */
    {
        -1,
        ZD_LIFECYCLE_WARM,     /* CONTROLLERS_UP */
        -1,
        -1,
        -1,
        -1,
        -1,
        ZD_LIFECYCLE_SUSPENDED,/* SUSPEND from DORMANT allowed */
        -1,
        -1,
        ZD_LIFECYCLE_STOPPED   /* STOP */
    },
    /* WARM */
    {
        -1,
        -1,
        ZD_LIFECYCLE_ACTIVE,   /* ACTIVATE */
        ZD_LIFECYCLE_DORMANT,  /* USER_IDLE: drop to dormant controllers */
        -1,
        ZD_LIFECYCLE_THROTTLED,/* PRESSURE while warming */
        -1,
        ZD_LIFECYCLE_SUSPENDED,
        -1,
        ZD_LIFECYCLE_DORMANT,  /* SERVICE_FAILURE isolates to dormant */
        ZD_LIFECYCLE_DORMANT
    },
    /* ACTIVE */
    {
        -1,
        -1,
        -1,
        ZD_LIFECYCLE_WARM,     /* USER_IDLE */
        -1,                    /* USER_ACTIVITY already active */
        ZD_LIFECYCLE_THROTTLED,/* PRESSURE */
        -1,
        ZD_LIFECYCLE_SUSPENDED,
        -1,
        ZD_LIFECYCLE_THROTTLED,/* SERVICE_FAILURE degrades */
        -1                     /* STOP requires leaving ACTIVE first */
    },
    /* THROTTLED */
    {
        -1,
        -1,
        -1,
        ZD_LIFECYCLE_WARM,
        ZD_LIFECYCLE_ACTIVE,   /* USER_ACTIVITY lifts throttle */
        -1,                    /* already throttled */
        ZD_LIFECYCLE_ACTIVE,   /* PRESSURE_RELEASED */
        ZD_LIFECYCLE_SUSPENDED,
        ZD_LIFECYCLE_ACTIVE,   /* RESUME from suspend-like throttle */
        -1,                    /* failure already degraded */
        ZD_LIFECYCLE_DORMANT
    },
    /* SUSPENDED */
    {
        -1,
        -1,
        -1,
        -1,
        ZD_LIFECYCLE_THROTTLED,/* USER_ACTIVITY resumes conservatively */
        ZD_LIFECYCLE_THROTTLED,/* PRESSURE while suspended stays throttled */
        -1,
        -1,
        ZD_LIFECYCLE_THROTTLED,/* RESUME */
        -1,
        ZD_LIFECYCLE_DORMANT
    }
};

static const char *const state_names[ZD_LIFECYCLE_STATE_COUNT] = {
    "STOPPED", "DORMANT", "WARM", "ACTIVE", "THROTTLED", "SUSPENDED"
};

static const char *const event_names[ZD_LIFECYCLE_EVENT_COUNT] = {
    "START", "CONTROLLERS_UP", "ACTIVATE", "USER_IDLE", "USER_ACTIVITY",
    "PRESSURE", "PRESSURE_RELEASED", "SUSPEND", "RESUME", "SERVICE_FAILURE",
    "STOP"
};

void zd_lifecycle_init(struct zd_lifecycle *lifecycle) {
    if (!lifecycle)
        return;
    zd_memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->state = ZD_LIFECYCLE_STOPPED;
}

int zd_lifecycle_state(const struct zd_lifecycle *lifecycle) {
    return lifecycle ? (int)lifecycle->state : -ZD_EINVAL;
}

int zd_lifecycle_add_listener(struct zd_lifecycle *lifecycle,
                              zd_lifecycle_listener callback, void *context) {
    if (!lifecycle || !callback)
        return -ZD_EINVAL;
    if (lifecycle->listener_count >= ZD_LIFECYCLE_MAX_LISTENERS)
        return -ZD_ENOSPC;
    lifecycle->listeners[lifecycle->listener_count].callback = callback;
    lifecycle->listeners[lifecycle->listener_count].context = context;
    ++lifecycle->listener_count;
    return 0;
}

int zd_lifecycle_dispatch(struct zd_lifecycle *lifecycle,
                          enum zd_lifecycle_event event, uint64_t now_ns) {
    enum zd_lifecycle_state previous;
    enum zd_lifecycle_state next;
    uint32_t index;
    uint32_t slot;
    uint32_t listener;
    (void)now_ns;

    if (!lifecycle || (int)event >= ZD_LIFECYCLE_EVENT_COUNT || event < 0)
        return -ZD_EINVAL;
    previous = lifecycle->state;
    next = (enum zd_lifecycle_state)lifecycle_table[previous][event];
    if ((int)next < 0) {
        ++lifecycle->illegal_event_count;
        return -ZD_EINVAL;
    }
    if (next == previous)
        return 0;

    lifecycle->state = next;
    ++lifecycle->transition_count;
    slot = lifecycle->history_head;
    lifecycle->history[slot].sequence = lifecycle->transition_count;
    lifecycle->history[slot].from = previous;
    lifecycle->history[slot].next = next;
    lifecycle->history[slot].cause = event;
    lifecycle->history_head = (slot + 1U) % ZD_LIFECYCLE_MAX_HISTORY;
    if (lifecycle->history_count < ZD_LIFECYCLE_MAX_HISTORY)
        ++lifecycle->history_count;

    for (listener = 0; listener < lifecycle->listener_count; ++listener)
        lifecycle->listeners[listener].callback(
            lifecycle->listeners[listener].context, previous, next, event);
    (void)index;
    return 0;
}

int zd_lifecycle_allows_heavy_work(const struct zd_lifecycle *lifecycle) {
    if (!lifecycle)
        return 0;
    return lifecycle->state == ZD_LIFECYCLE_ACTIVE;
}

const char *zd_lifecycle_state_name(enum zd_lifecycle_state state) {
    if ((int)state < 0 || (int)state >= ZD_LIFECYCLE_STATE_COUNT)
        return "?";
    return state_names[state];
}

const char *zd_lifecycle_event_name(enum zd_lifecycle_event event) {
    if ((int)event < 0 || (int)event >= ZD_LIFECYCLE_EVENT_COUNT)
        return "?";
    return event_names[event];
}
