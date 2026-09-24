#include <zeroos/desktop/watchdog.h>

static void log_event(struct zd_watchdog *watchdog, uint32_t service,
                      enum zd_watchdog_event_kind kind,
                      enum zd_watchdog_decision decision, uint64_t timestamp_ns,
                      int32_t exit_status, uint64_t delay_ns) {
    struct zd_watchdog_log_entry *entry =
        &watchdog->log[watchdog->log_head];
    entry->service = service;
    entry->kind = kind;
    entry->decision = decision;
    entry->timestamp_ns = timestamp_ns;
    entry->exit_status = exit_status;
    entry->delay_ns = delay_ns;
    watchdog->log_head = (watchdog->log_head + 1U) % ZD_WATCHDOG_MAX_LOG;
    if (watchdog->log_count < ZD_WATCHDOG_MAX_LOG)
        ++watchdog->log_count;
}

void zd_watchdog_init(struct zd_watchdog *watchdog) {
    if (!watchdog)
        return;
    zd_memset(watchdog, 0, sizeof(*watchdog));
    watchdog->next_id = 1;
}

int zd_watchdog_set_listener(struct zd_watchdog *watchdog,
                             void (*on_decision)(void *context,
                                                 uint32_t service_id,
                                                 enum zd_watchdog_decision
                                                 decision,
                                                 uint64_t delay_ns),
                             void (*on_degraded)(void *context),
                             void *context) {
    if (!watchdog)
        return -ZD_EINVAL;
    watchdog->listener.on_decision = on_decision;
    watchdog->listener.on_degraded = on_degraded;
    watchdog->listener.context = context;
    watchdog->has_listener = 1;
    return 0;
}

int zd_watchdog_register(struct zd_watchdog *watchdog, const char *name,
                         enum zd_watchdog_policy policy, uint32_t max_restarts,
                         uint64_t health_window_ns, uint64_t backoff_base_ns,
                         uint64_t backoff_cap_ns, uint32_t *out_id) {
    struct zd_watchdog_service *slot = (struct zd_watchdog_service *)0;
    uint32_t index;

    if (!watchdog || !name || !*name || backoff_base_ns == 0)
        return -ZD_EINVAL;
    if ((int)policy < ZD_WD_NEVER || (int)policy > ZD_WD_ALWAYS)
        return -ZD_EINVAL;
    if (backoff_cap_ns && backoff_cap_ns < backoff_base_ns)
        return -ZD_EINVAL;
    for (index = 0; index < ZD_WATCHDOG_MAX_SERVICES; ++index)
        if (watchdog->services[index].in_use &&
            zd_str_equal(watchdog->services[index].name, name))
            return -ZD_EBUSY;
    for (index = 0; index < ZD_WATCHDOG_MAX_SERVICES; ++index)
        if (!watchdog->services[index].in_use) {
            slot = &watchdog->services[index];
            break;
        }
    if (!slot)
        return -ZD_ENOSPC;
    zd_memset(slot, 0, sizeof(*slot));
    if (watchdog->next_id == 0)
        watchdog->next_id = 1;
    slot->id = watchdog->next_id++;
    zd_str_copy(slot->name, sizeof(slot->name), name);
    slot->policy = policy;
    slot->max_restarts = max_restarts;
    slot->health_window_ns = health_window_ns;
    slot->backoff_base_ns = backoff_base_ns;
    slot->backoff_cap_ns = backoff_cap_ns ? backoff_cap_ns :
        backoff_base_ns * 32ULL;
    slot->in_use = 1;
    log_event(watchdog, slot->id, ZD_WD_EVENT_REGISTER, ZD_WD_CONTINUE, 0, 0, 0);
    if (out_id)
        *out_id = slot->id;
    return 0;
}

int zd_watchdog_unregister(struct zd_watchdog *watchdog, uint32_t id) {
    uint32_t index;
    if (!watchdog)
        return -ZD_EINVAL;
    for (index = 0; index < ZD_WATCHDOG_MAX_SERVICES; ++index)
        if (watchdog->services[index].in_use &&
            watchdog->services[index].id == id) {
            log_event(watchdog, id, ZD_WD_EVENT_UNREGISTER, ZD_WD_CONTINUE, 0,
                      0, 0);
            watchdog->services[index].in_use = 0;
            return 0;
        }
    return -ZD_ENOENT;
}

struct zd_watchdog_service *zd_watchdog_service(struct zd_watchdog *watchdog,
                                                uint32_t id) {
    uint32_t index;
    if (!watchdog)
        return (struct zd_watchdog_service *)0;
    for (index = 0; index < ZD_WATCHDOG_MAX_SERVICES; ++index)
        if (watchdog->services[index].in_use &&
            watchdog->services[index].id == id)
            return &watchdog->services[index];
    return (struct zd_watchdog_service *)0;
}

int zd_watchdog_set_heartbeat(struct zd_watchdog *watchdog, uint32_t id,
                              uint64_t interval_ns) {
    struct zd_watchdog_service *service = zd_watchdog_service(watchdog, id);
    if (!service)
        return -ZD_ENOENT;
    service->heartbeat_interval_ns = interval_ns;
    return 0;
}

int zd_watchdog_note_start(struct zd_watchdog *watchdog, uint32_t id,
                           uint64_t now_ns) {
    struct zd_watchdog_service *service = zd_watchdog_service(watchdog, id);
    if (!service)
        return -ZD_ENOENT;
    service->running = 1;
    service->started_ns = now_ns;
    service->current_backoff_ns = 0;
    service->last_heartbeat_ns = now_ns;
    log_event(watchdog, id, ZD_WD_EVENT_START, ZD_WD_CONTINUE, now_ns, 0, 0);
    return 0;
}

int zd_watchdog_note_heartbeat(struct zd_watchdog *watchdog, uint32_t id,
                               uint64_t now_ns) {
    struct zd_watchdog_service *service = zd_watchdog_service(watchdog, id);
    if (!service)
        return -ZD_ENOENT;
    if (!service->running)
        return -ZD_ESTATE;
    service->last_heartbeat_ns = now_ns;
    return 0;
}

static uint64_t compute_backoff(const struct zd_watchdog_service *service) {
    uint64_t backoff = service->backoff_base_ns;
    uint32_t index;
    for (index = 1; index < service->restart_count && backoff; ++index) {
        if (backoff > service->backoff_cap_ns / 2ULL)
            return service->backoff_cap_ns;
        backoff *= 2ULL;
    }
    if (service->backoff_cap_ns && backoff > service->backoff_cap_ns)
        backoff = service->backoff_cap_ns;
    return backoff;
}

static void publish_decision(struct zd_watchdog *watchdog, uint32_t id,
                             enum zd_watchdog_decision decision,
                             uint64_t delay_ns) {
    log_event(watchdog, id, ZD_WD_EVENT_DECISION, decision, 0, 0, delay_ns);
    if (watchdog->has_listener && watchdog->listener.on_decision)
        watchdog->listener.on_decision(watchdog->listener.context, id,
                                       decision, delay_ns);
}

static void mark_degraded(struct zd_watchdog *watchdog, uint64_t now_ns) {
    (void)now_ns;
    if (!watchdog->degraded) {
        watchdog->degraded = 1;
        watchdog->escalated = 1;
        if (watchdog->has_listener && watchdog->listener.on_degraded)
            watchdog->listener.on_degraded(watchdog->listener.context);
    }
}

int zd_watchdog_note_exit(struct zd_watchdog *watchdog, uint32_t id,
                          uint64_t now_ns, int32_t exit_status,
                          struct zd_watchdog_result *out_result) {
    struct zd_watchdog_service *service = zd_watchdog_service(watchdog, id);
    struct zd_watchdog_result result = {ZD_WD_CONTINUE, 0};

    if (!service)
        return -ZD_ENOENT;
    service->running = 0;
    log_event(watchdog, id, ZD_WD_EVENT_EXIT, ZD_WD_CONTINUE, now_ns,
              exit_status, 0);

    if (service->policy == ZD_WD_NEVER ||
        (service->policy == ZD_WD_ON_FAILURE && exit_status == 0)) {
        result.decision = ZD_WD_CONTINUE;
        publish_decision(watchdog, id, result.decision, 0);
        if (out_result)
            *out_result = result;
        return 0;
    }
    if (service->gave_up) {
        result.decision = ZD_WD_ESCALATE;
        mark_degraded(watchdog, now_ns);
        if (out_result)
            *out_result = result;
        return 0;
    }

    /* Health window: a service that ran long enough before failing gets a
     * fresh restart budget (window evaluated at the failure event). */
    if (service->health_window_ns && service->started_ns &&
        now_ns >= service->started_ns &&
        now_ns - service->started_ns >= service->health_window_ns)
        service->restart_count = 0;

    if (service->max_restarts == 0 || service->restart_count >=
        service->max_restarts) {
        service->gave_up = 1;
        result.decision = ZD_WD_GIVE_UP;
        ++watchdog->total_giveups;
        mark_degraded(watchdog, now_ns);
        publish_decision(watchdog, id, result.decision, 0);
        if (out_result)
            *out_result = result;
        return 0;
    }

    ++service->restart_count;
    service->current_backoff_ns = compute_backoff(service);
    service->last_restart_ns = now_ns;
    ++watchdog->total_restarts;
    result.decision = ZD_WD_RESTART;
    result.delay_ns = service->current_backoff_ns;
    publish_decision(watchdog, id, result.decision, result.delay_ns);
    if (out_result)
        *out_result = result;
    return 0;
}

int zd_watchdog_evaluate(struct zd_watchdog *watchdog, uint32_t id,
                         uint64_t now_ns, struct zd_watchdog_result *out_result) {
    struct zd_watchdog_service *service = zd_watchdog_service(watchdog, id);
    struct zd_watchdog_result result = {ZD_WD_CONTINUE, 0};
    if (!service || !out_result)
        return -ZD_EINVAL;
    if (service->running) {
        /* Heartbeat deadline expiry (caller supplies the timer event). */
        if (service->heartbeat_interval_ns &&
            now_ns > service->last_heartbeat_ns &&
            now_ns - service->last_heartbeat_ns >
                service->heartbeat_interval_ns) {
            service->running = 0;
            result.decision = ZD_WD_ESCALATE;
            mark_degraded(watchdog, now_ns);
            publish_decision(watchdog, id, result.decision, 0);
            *out_result = result;
            return 0;
        }
        result.decision = ZD_WD_CONTINUE;
        *out_result = result;
        return 0;
    }
    if (service->current_backoff_ns && !service->gave_up) {
        uint64_t due = service->last_restart_ns + service->current_backoff_ns;
        if (now_ns >= due) {
            result.decision = ZD_WD_RESTART;
            result.delay_ns = 0;
            service->current_backoff_ns = 0;
            publish_decision(watchdog, id, result.decision, 0);
        } else {
            result.decision = ZD_WD_WAIT;
            result.delay_ns = due - now_ns;
        }
        *out_result = result;
        return 0;
    }
    if (service->gave_up) {
        result.decision = ZD_WD_GIVE_UP;
        *out_result = result;
        return 0;
    }
    *out_result = result;
    return 0;
}

uint32_t zd_watchdog_restart_pending(const struct zd_watchdog *watchdog,
                                     uint32_t id, uint64_t now_ns,
                                     uint64_t *out_delay_ns) {
    const struct zd_watchdog_service *service =
        (const struct zd_watchdog_service *)
        zd_watchdog_service((struct zd_watchdog *)(uintptr_t)watchdog, id);
    if (!service || service->running || !service->current_backoff_ns ||
        service->gave_up)
        return 0;
    {
        uint64_t due = service->last_restart_ns + service->current_backoff_ns;
        if (now_ns >= due) {
            if (out_delay_ns)
                *out_delay_ns = 0;
            return 1;
        }
        if (out_delay_ns)
            *out_delay_ns = due - now_ns;
        return 2; /* pending with remaining delay */
    }
}

int zd_watchdog_session_degraded(const struct zd_watchdog *watchdog) {
    return watchdog && watchdog->degraded;
}

const char *zd_watchdog_decision_name(enum zd_watchdog_decision decision) {
    static const char *const names[] = {"continue", "restart", "give_up",
                                        "escalate", "wait"};
    if ((int)decision < 0 || (int)decision > ZD_WD_WAIT)
        return "?";
    return names[decision];
}

uint32_t zd_watchdog_log(const struct zd_watchdog *watchdog,
                         struct zd_watchdog_log_entry *out,
                         uint32_t capacity) {
    uint32_t count = 0;
    uint32_t index;
    if (!watchdog || !out || capacity == 0)
        return 0;
    /* Newest first. */
    index = watchdog->log_count;
    while (index && count < capacity) {
        uint32_t at;
        --index;
        at = (watchdog->log_head + ZD_WATCHDOG_MAX_LOG - 1U - index) %
             ZD_WATCHDOG_MAX_LOG;
        if (index < watchdog->log_count)
            out[count++] = watchdog->log[at];
    }
    return count;
}
