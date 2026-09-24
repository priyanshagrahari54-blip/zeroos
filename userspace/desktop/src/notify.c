#include <zeroos/desktop/notify.h>

static void expire_if_due(struct zd_notify *notify,
                          struct zd_notification *item, uint64_t now_ns);

static const uint64_t priority_ttl[ZD_NOTIFY_CRITICAL + 1U] = {
    30ULL * 1000000000ULL,     /* LOW: 30s */
    120ULL * 1000000000ULL,    /* NORMAL: 2min */
    600ULL * 1000000000ULL,    /* HIGH: 10min */
    ~0ULL                      /* CRITICAL: no expiry */
};

void zd_notify_init(struct zd_notify *notify) {
    if (!notify)
        return;
    zd_memset(notify, 0, sizeof(*notify));
    notify->next_id = 1;
    notify->max_per_window = 5;
    notify->rate_window_ns = 10000000000ULL; /* 10s */
    notify->dedupe_window_ns = 5000000000ULL; /* 5s */
    notify->global_capacity = 32;
    notify->global_tokens = 32;
    notify->global_refill_ns = 0;
}

int zd_notify_add_listener(struct zd_notify *notify, void (*on_post)(void *,
                            zd_notification_id,
                            enum zd_notify_priority),
                           void (*on_rate_limited)(void *context,
                                                   const char *app_id),
                           void *context) {
    if (!notify || !on_post)
        return -ZD_EINVAL;
    if (notify->listener_count >= ZD_NOTIFY_MAX_LISTENERS)
        return -ZD_ENOSPC;
    notify->listeners[notify->listener_count].on_post = on_post;
    notify->listeners[notify->listener_count].on_rate_limited = on_rate_limited;
    notify->listeners[notify->listener_count].context = context;
    ++notify->listener_count;
    return 0;
}

static void refill_tokens(struct zd_notify *notify, const char *app_id,
                          uint64_t now_ns) {
    uint32_t index;
    for (index = 0; index < ZD_NOTIFY_MAX_APPS; ++index) {
        if (!notify->apps[index].in_use)
            continue;
        if (!zd_str_equal(notify->apps[index].app_id, app_id))
            continue;
        if (now_ns > notify->apps[index].last_refill_ns) {
            uint64_t elapsed = now_ns - notify->apps[index].last_refill_ns;
            uint64_t tokens = elapsed / notify->rate_window_ns;
            if (tokens) {
                uint64_t total =
                    notify->apps[index].tokens + tokens;
                notify->apps[index].tokens = total > notify->apps[index].capacity ?
                    notify->apps[index].capacity : (uint32_t)total;
                notify->apps[index].last_refill_ns +=
                    tokens * notify->rate_window_ns;
            }
        }
        return;
    }
    /* First sighting: allocate a bucket. */
    for (index = 0; index < ZD_NOTIFY_MAX_APPS; ++index)
        if (!notify->apps[index].in_use) {
            notify->apps[index].in_use = 1;
            zd_str_copy(notify->apps[index].app_id,
                        sizeof(notify->apps[index].app_id), app_id);
            notify->apps[index].capacity = notify->max_per_window;
            notify->apps[index].tokens = notify->max_per_window;
            notify->apps[index].last_refill_ns = now_ns;
            return;
        }
    /* No slot: enforce with the global bucket only. */
}

static int consume_token(struct zd_notify *notify, const char *app_id,
                         uint64_t now_ns) {
    uint32_t index;
    int matched = 0;
    refill_tokens(notify, app_id, now_ns);

    if (now_ns > notify->global_refill_ns) {
        uint64_t elapsed = now_ns - notify->global_refill_ns;
        uint64_t tokens = elapsed / notify->rate_window_ns;
        if (tokens) {
            uint64_t total = notify->global_tokens + tokens;
            notify->global_tokens = total > notify->global_capacity ?
                notify->global_capacity : (uint32_t)total;
            notify->global_refill_ns += tokens * notify->rate_window_ns;
        }
    }
    for (index = 0; index < ZD_NOTIFY_MAX_APPS; ++index) {
        if (notify->apps[index].in_use &&
            zd_str_equal(notify->apps[index].app_id, app_id)) {
            matched = 1;
            if (notify->apps[index].tokens == 0)
                return -ZD_EAGAIN;
            --notify->apps[index].tokens;
            break;
        }
    }
    (void)matched;
    if (notify->global_tokens == 0)
        return -ZD_EAGAIN;
    --notify->global_tokens;
    return 0;
}

static struct zd_notification *find_notification(struct zd_notify *notify,
                                                 zd_notification_id id) {
    uint32_t index;
    if (id == ZD_NOTIFICATION_INVALID)
        return (struct zd_notification *)0;
    for (index = 0; index < ZD_NOTIFY_MAX; ++index)
        if (notify->items[index].in_use && notify->items[index].id == id)
            return &notify->items[index];
    return (struct zd_notification *)0;
}

int zd_notify_post(struct zd_notify *notify, const struct zd_notify_post *post,
                   uint64_t now_ns, zd_notification_id *out_id) {
    struct zd_notification *slot = (struct zd_notification *)0;
    struct zd_notification *existing = (struct zd_notification *)0;
    uint32_t index;
    uint32_t listener;
    const char *app_id;
    const char *category;

    if (!notify || !post || !post->app_id || !post->title)
        return -ZD_EINVAL;
    if ((int)post->priority < 0 || (int)post->priority > ZD_NOTIFY_CRITICAL)
        return -ZD_EINVAL;
    app_id = post->app_id;
    category = post->category && *post->category ? post->category : "general";

    /* Deduplication: same app + key within the window coalesces. */
    if (post->dedupe_key && *post->dedupe_key) {
        for (index = 0; index < ZD_NOTIFY_MAX; ++index) {
            struct zd_notification *item = &notify->items[index];
            if (!item->in_use || item->state == ZD_NOTIFY_STATE_DISMISSED)
                continue;
            if (zd_str_equal(item->app_id, app_id) &&
                zd_str_equal(item->dedupe_key, post->dedupe_key) &&
                now_ns >= item->posted_ns &&
                now_ns - item->posted_ns <= notify->dedupe_window_ns) {
                existing = item;
                break;
            }
        }
    }
    if (existing) {
        ++existing->dedupe_count;
        existing->posted_ns = now_ns;
        if ((int)post->priority > (int)existing->priority)
            existing->priority = post->priority;
        zd_str_copy(existing->title, sizeof(existing->title), post->title);
        zd_str_copy(existing->body, sizeof(existing->body),
                    post->body ? post->body : "");
        existing->expires_at_ns = post->ttl_ns ?
            now_ns + post->ttl_ns : priority_ttl[existing->priority];
        existing->state = ZD_NOTIFY_STATE_ACTIVE;
        ++notify->stats.deduped;
        ++notify->stats.active;
        if (out_id)
            *out_id = existing->id;
        return 0;
    }

    if (consume_token(notify, app_id, now_ns) != 0) {
        ++notify->stats.rate_limited;
        for (listener = 0; listener < notify->listener_count; ++listener)
            if (notify->listeners[listener].on_rate_limited)
                notify->listeners[listener].on_rate_limited(
                    notify->listeners[listener].context, app_id);
        return -ZD_EAGAIN;
    }

    /* Event-driven reclamation: apply pending expiries for this timestamp
     * and free dismissed/expired slots before allocating. No polling
     * reaper exists; every transition is caused by a caller event. */
    for (index = 0; index < ZD_NOTIFY_MAX; ++index)
        if (notify->items[index].in_use)
            expire_if_due(notify, &notify->items[index], now_ns);
    for (index = 0; index < ZD_NOTIFY_MAX; ++index)
        if (notify->items[index].in_use &&
            (notify->items[index].state == ZD_NOTIFY_STATE_DISMISSED ||
             notify->items[index].state == ZD_NOTIFY_STATE_EXPIRED)) {
            notify->items[index].in_use = 0;
            if (notify->count)
                --notify->count;
        }
    for (index = 0; index < ZD_NOTIFY_MAX; ++index)
        if (!notify->items[index].in_use) {
            slot = &notify->items[index];
            break;
        }
    if (!slot)
        return -ZD_ENOSPC;

    zd_memset(slot, 0, sizeof(*slot));
    slot->id = notify->next_id++;
    if (slot->id == 0)
        slot->id = notify->next_id++;
    zd_str_copy(slot->app_id, sizeof(slot->app_id), app_id);
    zd_str_copy(slot->category, sizeof(slot->category), category);
    if (post->dedupe_key)
        zd_str_copy(slot->dedupe_key, sizeof(slot->dedupe_key),
                    post->dedupe_key);
    zd_str_copy(slot->title, sizeof(slot->title), post->title);
    zd_str_copy(slot->body, sizeof(slot->body), post->body ? post->body : "");
    slot->priority = post->priority;
    slot->state = ZD_NOTIFY_STATE_ACTIVE;
    slot->posted_ns = now_ns;
    slot->expires_at_ns = post->ttl_ns ?
        now_ns + post->ttl_ns : priority_ttl[post->priority];
    slot->in_use = 1;
    ++notify->count;
    ++notify->stats.posted;
    ++notify->stats.active;

    for (listener = 0; listener < notify->listener_count; ++listener)
        if (notify->listeners[listener].on_post)
            notify->listeners[listener].on_post(
                notify->listeners[listener].context, slot->id,
                slot->priority);
    if (out_id)
        *out_id = slot->id;
    return 0;
}

int zd_notify_dismiss(struct zd_notify *notify, zd_notification_id id) {
    struct zd_notification *item = find_notification(notify, id);
    if (!notify || !item)
        return -ZD_ENOENT;
    if (item->state == ZD_NOTIFY_STATE_DISMISSED)
        return -ZD_ESTATE;
    item->state = ZD_NOTIFY_STATE_DISMISSED;
    if (notify->stats.active)
        --notify->stats.active;
    ++notify->stats.dismissed;
    return 0;
}

uint32_t zd_notify_dismiss_group(struct zd_notify *notify, const char *app_id,
                                 const char *category) {
    uint32_t index;
    uint32_t dismissed = 0;
    if (!notify || !app_id)
        return 0;
    for (index = 0; index < ZD_NOTIFY_MAX; ++index) {
        struct zd_notification *item = &notify->items[index];
        if (!item->in_use || item->state == ZD_NOTIFY_STATE_DISMISSED)
            continue;
        if (!zd_str_equal(item->app_id, app_id))
            continue;
        if (category && *category && !zd_str_equal(item->category, category))
            continue;
        item->state = ZD_NOTIFY_STATE_DISMISSED;
        if (notify->stats.active)
            --notify->stats.active;
        ++notify->stats.dismissed;
        ++dismissed;
    }
    return dismissed;
}

int zd_notify_defer(struct zd_notify *notify, zd_notification_id id,
                    uint64_t now_ns, uint64_t defer_for_ns) {
    struct zd_notification *item = find_notification(notify, id);
    if (!notify || !item)
        return -ZD_ENOENT;
    if (defer_for_ns == 0)
        return -ZD_EINVAL;
    if (item->state == ZD_NOTIFY_STATE_DISMISSED)
        return -ZD_ESTATE;
    item->state = ZD_NOTIFY_STATE_DEFERRED;
    item->deferred_until_ns = now_ns + defer_for_ns;
    ++notify->stats.deferred;
    return 0;
}

struct zd_notification *zd_notify_get(struct zd_notify *notify,
                                      zd_notification_id id) {
    return find_notification(notify, id);
}

static void expire_if_due(struct zd_notify *notify,
                          struct zd_notification *item, uint64_t now_ns) {
    if (item->state == ZD_NOTIFY_STATE_ACTIVE &&
        item->expires_at_ns != ~0ULL && now_ns >= item->expires_at_ns) {
        item->state = ZD_NOTIFY_STATE_EXPIRED;
        if (notify->stats.active)
            --notify->stats.active;
        ++notify->stats.expired;
    }
    if (item->state == ZD_NOTIFY_STATE_DEFERRED &&
        now_ns >= item->deferred_until_ns)
        item->state = ZD_NOTIFY_STATE_ACTIVE;
}

uint32_t zd_notify_visible(struct zd_notify *notify, uint64_t now_ns,
                           zd_notification_id *out_ids, uint32_t capacity) {
    uint32_t index;
    uint32_t count = 0;
    if (!notify || !out_ids || capacity == 0)
        return 0;
    for (index = 0; index < ZD_NOTIFY_MAX; ++index)
        if (notify->items[index].in_use)
            expire_if_due(notify, &notify->items[index], now_ns);

    /* Selection sort by (priority desc, posted desc) over visible items. */
    while (count < capacity) {
        struct zd_notification *best = (struct zd_notification *)0;
        uint32_t best_index = ZD_NOTIFY_MAX;
        uint32_t pick;
        for (pick = 0; pick < ZD_NOTIFY_MAX; ++pick) {
            struct zd_notification *item = &notify->items[pick];
            uint32_t already;
            if (!item->in_use ||
                (item->state != ZD_NOTIFY_STATE_ACTIVE &&
                 item->state != ZD_NOTIFY_STATE_DEFERRED))
                continue;
            if (item->state == ZD_NOTIFY_STATE_DEFERRED)
                continue; /* not yet due */
            already = 0;
            {
                uint32_t seen;
                for (seen = 0; seen < count; ++seen)
                    if (out_ids[seen] == item->id)
                        already = 1;
            }
            if (already)
                continue;
            if (!best || (int)item->priority > (int)best->priority ||
                ((int)item->priority == (int)best->priority &&
                 item->posted_ns > best->posted_ns)) {
                best = item;
                best_index = pick;
            }
        }
        if (best_index == ZD_NOTIFY_MAX)
            break;
        out_ids[count++] = best->id;
        /* Mark via temporary: use a sentinel through id set already. */
        if (count >= capacity)
            break;
        {
            /* exclude by id in next pass (handled by `already` scan) */
        }
    }
    return count;
}

uint32_t zd_notify_groups(struct zd_notify *notify, uint64_t now_ns,
                          struct zd_notify_group *out_groups,
                          uint32_t capacity) {
    uint32_t index;
    uint32_t count = 0;
    if (!notify || !out_groups || capacity == 0)
        return 0;
    for (index = 0; index < ZD_NOTIFY_MAX; ++index)
        if (notify->items[index].in_use)
            expire_if_due(notify, &notify->items[index], now_ns);

    for (index = 0; index < ZD_NOTIFY_MAX; ++index) {
        struct zd_notification *item = &notify->items[index];
        uint32_t group_index;
        struct zd_notify_group *group = (struct zd_notify_group *)0;
        if (!item->in_use || item->state != ZD_NOTIFY_STATE_ACTIVE)
            continue;
        for (group_index = 0; group_index < count; ++group_index)
            if (zd_str_equal(out_groups[group_index].app_id, item->app_id) &&
                zd_str_equal(out_groups[group_index].category,
                             item->category)) {
                group = &out_groups[group_index];
                break;
            }
        if (!group) {
            if (count >= capacity)
                continue;
            group = &out_groups[count++];
            zd_memset(group, 0, sizeof(*group));
            zd_str_copy(group->app_id, sizeof(group->app_id), item->app_id);
            zd_str_copy(group->category, sizeof(group->category),
                        item->category);
        }
        ++group->count;
        if ((int)item->priority > (int)group->highest_priority)
            group->highest_priority = item->priority;
        if (item->posted_ns > group->latest_ns) {
            group->latest_ns = item->posted_ns;
            group->newest_id = item->id;
        }
    }
    return count;
}

enum zd_notify_a11y zd_notify_a11y_policy(enum zd_notify_priority priority) {
    switch (priority) {
    case ZD_NOTIFY_CRITICAL:
        return ZD_NOTIFY_A11Y_ASSERTIVE;
    case ZD_NOTIFY_HIGH:
        return ZD_NOTIFY_A11Y_POLITE;
    default:
        return ZD_NOTIFY_A11Y_QUIET;
    }
}

const char *zd_notify_priority_name(enum zd_notify_priority priority) {
    static const char *const names[] = {"low", "normal", "high", "critical"};
    if ((int)priority < 0 || (int)priority > ZD_NOTIFY_CRITICAL)
        return "?";
    return names[priority];
}
