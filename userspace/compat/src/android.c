#include <zeroos/compat/android.h>

static uint32_t za_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n])
        ++n;
    return n;
}

static void za_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!cap)
        return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int za_eq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a && b && a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return (!a && !b) || (a && b && a[i] == 0 && b[i] == 0);
}

static struct zandroid_app *find_app(struct zandroid_runtime *rt, const char *package_name) {
    if (!rt || !package_name || !package_name[0])
        return 0;
    for (uint32_t i = 0; i < ZANDROID_MAX_APPS; ++i) {
        if (rt->apps[i].used && za_eq(rt->apps[i].apk.package_name, package_name))
            return &rt->apps[i];
    }
    return 0;
}

void zandroid_init(struct zandroid_runtime *rt, const char *storage_root) {
    if (!rt)
        return;
    for (uint32_t i = 0; i < sizeof(*rt); ++i)
        ((uint8_t *)rt)[i] = 0;
    rt->state = ZANDROID_RT_DORMANT;
    rt->total_resident_bytes = 0;
    if (storage_root && storage_root[0]) {
        za_copy(rt->storage_root, storage_root, sizeof(rt->storage_root));
    } else {
        za_copy(rt->storage_root, "/data/android", sizeof(rt->storage_root));
    }
}

int zandroid_runtime_wake(struct zandroid_runtime *rt) {
    if (!rt)
        return ZANDROID_BADARG;
    if (rt->state == ZANDROID_RT_DORMANT || rt->state == ZANDROID_RT_SUSPENDED) {
        rt->state = ZANDROID_RT_WARM;
        rt->stats.runtime_wakeups++;
        /* Baseline runtime overhead: minimal ART resident footprint (1 MB) */
        rt->total_resident_bytes = 1024 * 1024;
    }
    return ZANDROID_OK;
}

int zandroid_runtime_suspend(struct zandroid_runtime *rt) {
    if (!rt)
        return ZANDROID_BADARG;
    if (rt->state == ZANDROID_RT_DORMANT)
        return ZANDROID_OK;

    /* Freeze all running apps to PAUSED and trim non-essential heap */
    for (uint32_t i = 0; i < ZANDROID_MAX_APPS; ++i) {
        if (rt->apps[i].used && rt->apps[i].state == ZANDROID_APP_RUNNING) {
            rt->apps[i].state = ZANDROID_APP_PAUSED;
            rt->apps[i].resident_bytes /= 2; /* half-heap trimmed under pressure */
        }
    }
    rt->state = ZANDROID_RT_SUSPENDED;
    rt->total_resident_bytes = 512 * 1024; /* trimmed runtime overhead */
    return ZANDROID_OK;
}

int zandroid_runtime_stop(struct zandroid_runtime *rt) {
    if (!rt)
        return ZANDROID_BADARG;
    for (uint32_t i = 0; i < ZANDROID_MAX_APPS; ++i) {
        if (rt->apps[i].used) {
            rt->apps[i].state = ZANDROID_APP_STOPPED;
            rt->apps[i].resident_bytes = 0;
        }
    }
    rt->state = ZANDROID_RT_DORMANT;
    rt->total_resident_bytes = 0; /* 0 resident bytes when dormant! */
    return ZANDROID_OK;
}

int zandroid_install_apk(struct zandroid_runtime *rt, const struct zandroid_apk_info *apk) {
    if (!rt || !apk || !apk->package_name[0])
        return ZANDROID_BADARG;
    if (apk->target_sdk < 21 || apk->target_sdk > 36)
        return ZANDROID_UNSUPPORTED;
    if (find_app(rt, apk->package_name))
        return ZANDROID_BADSTATE; /* already installed */

    for (uint32_t i = 0; i < ZANDROID_MAX_APPS; ++i) {
        if (!rt->apps[i].used) {
            rt->apps[i].apk = *apk;
            /* Auto-grant INTERNET if requested; runtime permissions require explicit grant */
            rt->apps[i].apk.granted_perms = apk->requested_perms & ZANDROID_PERM_INTERNET;
            rt->apps[i].state = ZANDROID_APP_INSTALLED;
            rt->apps[i].uid = 10000 + i;
            rt->apps[i].resident_bytes = 0;
            rt->apps[i].crashes = 0;
            rt->apps[i].used = 1;
            rt->stats.apps_installed++;
            return ZANDROID_OK;
        }
    }
    return ZANDROID_NOSPACE;
}

int zandroid_uninstall_app(struct zandroid_runtime *rt, const char *package_name) {
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app)
        return ZANDROID_NOTFOUND;
    if (app->state == ZANDROID_APP_RUNNING)
        return ZANDROID_BADSTATE;

    /* Cancel any active notifications from this package */
    for (uint32_t i = 0; i < ZANDROID_MAX_NOTIFICATIONS; ++i) {
        if (rt->notifications[i].active && za_eq(rt->notifications[i].package_name, package_name))
            rt->notifications[i].active = 0;
    }

    app->used = 0;
    app->state = ZANDROID_APP_NONE;
    return ZANDROID_OK;
}

int zandroid_start_app(struct zandroid_runtime *rt, const char *package_name, uint32_t initial_heap_bytes) {
    struct zandroid_app *app;
    if (!rt || !package_name)
        return ZANDROID_BADARG;
    app = find_app(rt, package_name);
    if (!app)
        return ZANDROID_NOTFOUND;
    if (app->state == ZANDROID_APP_RUNNING)
        return ZANDROID_BADSTATE;

    /* Demand-wake runtime if dormant */
    if (rt->state == ZANDROID_RT_DORMANT || rt->state == ZANDROID_RT_SUSPENDED)
        zandroid_runtime_wake(rt);

    rt->state = ZANDROID_RT_ACTIVE;
    app->state = ZANDROID_APP_RUNNING;
    app->resident_bytes = initial_heap_bytes ? initial_heap_bytes : (4 * 1024 * 1024);
    rt->total_resident_bytes += app->resident_bytes;
    rt->stats.apps_started++;
    return ZANDROID_OK;
}

int zandroid_pause_app(struct zandroid_runtime *rt, const char *package_name) {
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app)
        return ZANDROID_NOTFOUND;
    if (app->state != ZANDROID_APP_RUNNING)
        return ZANDROID_BADSTATE;
    app->state = ZANDROID_APP_PAUSED;
    return ZANDROID_OK;
}

int zandroid_stop_app(struct zandroid_runtime *rt, const char *package_name) {
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app)
        return ZANDROID_NOTFOUND;
    if (app->state != ZANDROID_APP_RUNNING && app->state != ZANDROID_APP_PAUSED)
        return ZANDROID_BADSTATE;

    if (rt->total_resident_bytes >= app->resident_bytes)
        rt->total_resident_bytes -= app->resident_bytes;
    else
        rt->total_resident_bytes = 0;

    app->resident_bytes = 0;
    app->state = ZANDROID_APP_STOPPED;
    rt->stats.apps_stopped++;

    /* If no more apps are running, transition runtime to warm/dormant */
    int any_running = 0;
    for (uint32_t i = 0; i < ZANDROID_MAX_APPS; ++i) {
        if (rt->apps[i].used && (rt->apps[i].state == ZANDROID_APP_RUNNING || rt->apps[i].state == ZANDROID_APP_PAUSED))
            any_running = 1;
    }
    if (!any_running)
        rt->state = ZANDROID_RT_WARM;

    return ZANDROID_OK;
}

int zandroid_crash_app(struct zandroid_runtime *rt, const char *package_name) {
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app)
        return ZANDROID_NOTFOUND;
    if (app->state != ZANDROID_APP_RUNNING)
        return ZANDROID_BADSTATE;

    if (rt->total_resident_bytes >= app->resident_bytes)
        rt->total_resident_bytes -= app->resident_bytes;
    app->resident_bytes = 0;
    app->state = ZANDROID_APP_STOPPED;
    app->crashes++;
    rt->stats.apps_crashed++;

    /* Fault isolation: runtime does NOT crash */
    int any_running = 0;
    for (uint32_t i = 0; i < ZANDROID_MAX_APPS; ++i) {
        if (rt->apps[i].used && rt->apps[i].state == ZANDROID_APP_RUNNING)
            any_running = 1;
    }
    if (!any_running)
        rt->state = ZANDROID_RT_WARM;

    return ZANDROID_OK;
}

int zandroid_get_app_state(const struct zandroid_runtime *rt, const char *package_name, enum zandroid_app_state *out_state) {
    if (!rt || !package_name || !out_state)
        return ZANDROID_BADARG;
    for (uint32_t i = 0; i < ZANDROID_MAX_APPS; ++i) {
        if (rt->apps[i].used && za_eq(rt->apps[i].apk.package_name, package_name)) {
            *out_state = rt->apps[i].state;
            return ZANDROID_OK;
        }
    }
    return ZANDROID_NOTFOUND;
}

int zandroid_grant_permission(struct zandroid_runtime *rt, const char *package_name, uint32_t perm_bit) {
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app)
        return ZANDROID_NOTFOUND;
    if (!(app->apk.requested_perms & perm_bit))
        return ZANDROID_BADARG; /* app did not request this in manifest */
    app->apk.granted_perms |= perm_bit;
    return ZANDROID_OK;
}

int zandroid_revoke_permission(struct zandroid_runtime *rt, const char *package_name, uint32_t perm_bit) {
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app)
        return ZANDROID_NOTFOUND;
    app->apk.granted_perms &= ~perm_bit;
    return ZANDROID_OK;
}

int zandroid_check_permission(const struct zandroid_runtime *rt, const char *package_name, uint32_t perm_bit) {
    if (!rt || !package_name)
        return ZANDROID_BADARG;
    for (uint32_t i = 0; i < ZANDROID_MAX_APPS; ++i) {
        if (rt->apps[i].used && za_eq(rt->apps[i].apk.package_name, package_name)) {
            if (rt->apps[i].apk.granted_perms & perm_bit)
                return ZANDROID_OK;
            ((struct zandroid_runtime *)rt)->stats.permissions_rejected++;
            return ZANDROID_PERMISSION_DENIED;
        }
    }
    return ZANDROID_NOTFOUND;
}

int zandroid_translate_path(struct zandroid_runtime *rt, const char *package_name,
                            const char *guest_path, char *out_path, uint32_t out_cap) {
    if (!rt || !package_name || !guest_path || !out_path || out_cap < 32)
        return ZANDROID_BADARG;
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app)
        return ZANDROID_NOTFOUND;

    /* Prevent path traversal / escape attempts */
    for (uint32_t i = 0; guest_path[i]; ++i) {
        if (guest_path[i] == '.' && guest_path[i + 1] == '.') {
            rt->stats.path_traversals_blocked++;
            return ZANDROID_UNSUPPORTED;
        }
    }

    /* Translate /sdcard/ -> <storage_root>/shared/
     * Translate /data/data/<package_name>/ -> <storage_root>/data/<package_name>/ */
    uint32_t rlen = za_len(rt->storage_root);
    if (guest_path[0] == '/' && guest_path[1] == 's' && guest_path[2] == 'd' &&
        guest_path[3] == 'c' && guest_path[4] == 'a' && guest_path[5] == 'r' &&
        guest_path[6] == 'd') {
        const char *sub = guest_path + 7;
        za_copy(out_path, rt->storage_root, out_cap);
        za_copy(out_path + rlen, "/shared", out_cap - rlen);
        uint32_t cur = za_len(out_path);
        za_copy(out_path + cur, sub, out_cap - cur);
        return ZANDROID_OK;
    }

    if (guest_path[0] == '/' && guest_path[1] == 'd' && guest_path[2] == 'a' &&
        guest_path[3] == 't' && guest_path[4] == 'a') {
        za_copy(out_path, rt->storage_root, out_cap);
        za_copy(out_path + rlen, "/data/", out_cap - rlen);
        uint32_t cur = za_len(out_path);
        za_copy(out_path + cur, package_name, out_cap - cur);
        cur = za_len(out_path);
        const char *sub = guest_path + 5;
        /* skip optional /data/<pkg> prefix if given */
        if (sub[0] == '/' && sub[1] == 'd' && sub[2] == 'a' && sub[3] == 't' && sub[4] == 'a' && sub[5] == '/')
            sub += 6;
        za_copy(out_path + cur, sub, out_cap - cur);
        return ZANDROID_OK;
    }

    rt->stats.path_traversals_blocked++;
    return ZANDROID_UNSUPPORTED;
}

int zandroid_bridge_submit_frame(struct zandroid_runtime *rt, const char *package_name,
                                 const struct zandroid_gfx_frame *frame) {
    if (!rt || !package_name || !frame)
        return ZANDROID_BADARG;
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app || app->state != ZANDROID_APP_RUNNING)
        return ZANDROID_BADSTATE;
    if (frame->width == 0 || frame->height == 0 || frame->stride < frame->width)
        return ZANDROID_BADARG;

    rt->stats.frames_rendered++;
    return ZANDROID_OK;
}

int zandroid_bridge_post_notification(struct zandroid_runtime *rt, const char *package_name,
                                      uint32_t notif_id, const char *title, const char *text, uint32_t priority) {
    if (!rt || !package_name || !title || !text)
        return ZANDROID_BADARG;
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app)
        return ZANDROID_NOTFOUND;
    /* Requires POST_NOTIFICATIONS permission */
    if (zandroid_check_permission(rt, package_name, ZANDROID_PERM_POST_NOTIFICATIONS) != ZANDROID_OK)
        return ZANDROID_PERMISSION_DENIED;

    for (uint32_t i = 0; i < ZANDROID_MAX_NOTIFICATIONS; ++i) {
        if (!rt->notifications[i].active ||
            (za_eq(rt->notifications[i].package_name, package_name) && rt->notifications[i].notification_id == notif_id)) {
            za_copy(rt->notifications[i].package_name, package_name, sizeof(rt->notifications[i].package_name));
            rt->notifications[i].notification_id = notif_id;
            za_copy(rt->notifications[i].title, title, sizeof(rt->notifications[i].title));
            za_copy(rt->notifications[i].text, text, sizeof(rt->notifications[i].text));
            rt->notifications[i].priority = priority;
            rt->notifications[i].active = 1;
            rt->stats.notifications_posted++;
            return ZANDROID_OK;
        }
    }
    return ZANDROID_NOSPACE;
}

int zandroid_bridge_cancel_notification(struct zandroid_runtime *rt, const char *package_name, uint32_t notif_id) {
    if (!rt || !package_name)
        return ZANDROID_BADARG;
    for (uint32_t i = 0; i < ZANDROID_MAX_NOTIFICATIONS; ++i) {
        if (rt->notifications[i].active &&
            za_eq(rt->notifications[i].package_name, package_name) &&
            rt->notifications[i].notification_id == notif_id) {
            rt->notifications[i].active = 0;
            return ZANDROID_OK;
        }
    }
    return ZANDROID_NOTFOUND;
}

int zandroid_bridge_inject_motion(struct zandroid_runtime *rt, const char *package_name,
                                  const struct zandroid_motion_event *motion) {
    if (!rt || !package_name || !motion)
        return ZANDROID_BADARG;
    struct zandroid_app *app = find_app(rt, package_name);
    if (!app || app->state != ZANDROID_APP_RUNNING)
        return ZANDROID_BADSTATE;
    return ZANDROID_OK;
}
