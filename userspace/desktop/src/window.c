#include <zeroos/desktop/window.h>

/* Internal helpers --------------------------------------------------- */

static struct zd_monitor *monitor_lookup_mut(struct zd_wm *wm, uint32_t id) {
    uint32_t index;
    for (index = 0; index < wm->monitor_count; ++index)
        if (wm->monitors[index].id == id && wm->monitors[index].enabled)
            return &wm->monitors[index];
    return (struct zd_monitor *)0;
}

static const struct zd_monitor *monitor_lookup(const struct zd_wm *wm,
                                               uint32_t id) {
    uint32_t index;
    for (index = 0; index < wm->monitor_count; ++index)
        if (wm->monitors[index].id == id && wm->monitors[index].enabled)
            return &wm->monitors[index];
    return (const struct zd_monitor *)0;
}

static struct zd_monitor *primary_monitor(struct zd_wm *wm) {
    uint32_t index;
    for (index = 0; index < wm->monitor_count; ++index)
        if (wm->monitors[index].enabled && wm->monitors[index].primary)
            return &wm->monitors[index];
    return wm->monitor_count ? &wm->monitors[0] :
           (struct zd_monitor *)0;
}

static const struct zd_monitor *monitor_for_window(const struct zd_wm *wm,
                                                   const struct zd_window *window) {
    const struct zd_monitor *monitor = monitor_lookup(wm, window->monitor_id);
    if (monitor)
        return monitor;
    return wm->monitor_count ?
           (wm->monitors[0].primary ? &wm->monitors[0] :
            (const struct zd_monitor *)0) :
           (const struct zd_monitor *)0;
}

static struct zd_window *window_at(struct zd_wm *wm, zd_window_id id) {
    uint32_t index;
    if (!wm || id == ZD_INVALID_WINDOW)
        return (struct zd_window *)0;
    for (index = 0; index < ZD_MAX_WINDOWS; ++index)
        if (wm->windows[index].in_use && wm->windows[index].id == id)
            return &wm->windows[index];
    return (struct zd_window *)0;
}

static struct zd_rect clamp_to_monitor(const struct zd_monitor *monitor,
                                       struct zd_rect rect) {
    struct zd_rect workarea = zd_wm_workarea(monitor);
    if (rect.w > workarea.w)
        rect.w = workarea.w;
    if (rect.h > workarea.h)
        rect.h = workarea.h;
    if (rect.x < workarea.x)
        rect.x = workarea.x;
    if (rect.y < workarea.y)
        rect.y = workarea.y;
    if (rect.x + rect.w > workarea.x + workarea.w)
        rect.x = workarea.x + workarea.w - rect.w;
    if (rect.y + rect.h > workarea.y + workarea.h)
        rect.y = workarea.y + workarea.h - rect.h;
    return rect;
}

static void apply_size_limits(struct zd_window *window) {
    if (window->min_width > 0 && window->logical.w < window->min_width)
        window->logical.w = window->min_width;
    if (window->min_height > 0 && window->logical.h < window->min_height)
        window->logical.h = window->min_height;
    if (window->max_width > 0 && window->logical.w > window->max_width)
        window->logical.w = window->max_width;
    if (window->max_height > 0 && window->logical.h > window->max_height)
        window->logical.h = window->max_height;
}

static void stacking_remove(struct zd_wm *wm, zd_window_id id) {
    uint32_t index;
    for (index = 0; index < wm->stacking_count; ++index) {
        if (wm->stacking[index] == id) {
            uint32_t inner;
            for (inner = index; inner + 1U < wm->stacking_count; ++inner)
                wm->stacking[inner] = wm->stacking[inner + 1U];
            --wm->stacking_count;
            return;
        }
    }
}

static void focus_stack_remove(struct zd_wm *wm, zd_window_id id) {
    uint32_t index;
    for (index = 0; index < wm->focus_count; ++index) {
        if (wm->focus_stack[index] == id) {
            uint32_t inner;
            for (inner = index; inner + 1U < wm->focus_count; ++inner)
                wm->focus_stack[inner] = wm->focus_stack[inner + 1U];
            --wm->focus_count;
            return;
        }
    }
}

static void focus_stack_push(struct zd_wm *wm, zd_window_id id) {
    uint32_t index;
    focus_stack_remove(wm, id);
    if (wm->focus_count >= ZD_MAX_WINDOWS)
        return;
    for (index = wm->focus_count; index > 0; --index)
        wm->focus_stack[index] = wm->focus_stack[index - 1U];
    wm->focus_stack[0] = id;
    ++wm->focus_count;
}

static void emit_window_event(struct zd_wm *wm, zd_window_id id, uint32_t kind) {
    uint32_t index;
    for (index = 0; index < wm->listener_count; ++index) {
        const struct zd_wm_listener *listener = &wm->listeners[index];
        switch (kind) {
        case 0:
            if (listener->on_window_created)
                listener->on_window_created(listener->context, id);
            break;
        case 1:
            if (listener->on_window_destroyed)
                listener->on_window_destroyed(listener->context, id);
            break;
        case 2:
            if (listener->on_window_geometry)
                listener->on_window_geometry(listener->context, id);
            break;
        case 3:
            if (listener->on_window_state)
                listener->on_window_state(listener->context, id);
            break;
        case 4:
            if (listener->on_focus_changed)
                listener->on_focus_changed(listener->context, id);
            break;
        case 5:
            if (listener->on_stacking_changed)
                listener->on_stacking_changed(listener->context);
            break;
        default:
            break;
        }
    }
}

static void emit_workspace_event(struct zd_wm *wm, uint32_t workspace) {
    uint32_t index;
    for (index = 0; index < wm->listener_count; ++index)
        if (wm->listeners[index].on_workspace_changed)
            wm->listeners[index].on_workspace_changed(
                wm->listeners[index].context, workspace);
}

static int window_visible_on_workspace(const struct zd_window *window,
                                       uint32_t workspace) {
    return window->mapped && window->state != ZD_WINDOW_MINIMIZED &&
           window->workspace == workspace;
}

/* Lifecycle ----------------------------------------------------------- */

void zd_wm_init(struct zd_wm *wm) {
    if (!wm)
        return;
    zd_memset(wm, 0, sizeof(*wm));
    wm->next_id = 1;
    wm->next_client = 1;
    wm->workspace_count = 1;
    wm->active_workspace = 0;
}

int zd_wm_add_monitor(struct zd_wm *wm, const struct zd_monitor *monitor) {
    struct zd_monitor *slot;
    if (!wm || !monitor || monitor->enabled == 0 ||
        zd_rect_empty(monitor->bounds) ||
        monitor->scale_percent < 50 || monitor->scale_percent > 400 ||
        wm->monitor_count >= ZD_MAX_MONITORS)
        return -ZD_EINVAL;
    if (zd_wm_monitor(wm, monitor->id))
        return -ZD_EBUSY;
    slot = &wm->monitors[wm->monitor_count];
    *slot = *monitor;
    if (wm->monitor_count == 0)
        slot->primary = 1;
    else if (slot->primary) {
        /* Exactly one primary: demote the others. */
        uint32_t index;
        for (index = 0; index < wm->monitor_count; ++index)
            wm->monitors[index].primary = 0;
    }
    ++wm->monitor_count;
    return 0;
}

const struct zd_monitor *zd_wm_monitor(const struct zd_wm *wm, uint32_t id) {
    if (!wm)
        return (const struct zd_monitor *)0;
    return monitor_lookup(wm, id);
}

int zd_wm_add_listener(struct zd_wm *wm, const struct zd_wm_listener *listener) {
    if (!wm || !listener)
        return -ZD_EINVAL;
    if (wm->listener_count >= ZD_MAX_WM_LISTENERS)
        return -ZD_ENOSPC;
    wm->listeners[wm->listener_count] = *listener;
    ++wm->listener_count;
    return 0;
}

zd_client_id zd_wm_register_client(struct zd_wm *wm) {
    if (!wm)
        return 0;
    if (wm->next_client == 0)
        wm->next_client = 1;
    return wm->next_client++;
}

int zd_wm_create_window(struct zd_wm *wm,
                        const struct zd_window_create_info *info,
                        zd_window_id *out_id) {
    struct zd_window *window = (struct zd_window *)0;
    uint32_t index;
    struct zd_monitor *monitor;
    zd_window_id id;

    if (!wm || !info || info->client == 0 || zd_rect_empty(info->logical_rect))
        return -ZD_EINVAL;
    if (wm->window_count >= ZD_MAX_WINDOWS)
        return -ZD_ENOSPC;
    if (info->min_width < 0 || info->min_height < 0 ||
        info->max_width < 0 || info->max_height < 0)
        return -ZD_EINVAL;
    if (info->max_width && info->min_width > info->max_width)
        return -ZD_EINVAL;
    if (info->max_height && info->min_height > info->max_height)
        return -ZD_EINVAL;
    if (wm->monitor_count == 0)
        return -ZD_ESTATE;

    monitor = monitor_lookup_mut(wm, info->monitor_id);
    if (!monitor)
        monitor = primary_monitor(wm);
    if (!monitor)
        return -ZD_ENODEV;

    for (index = 0; index < ZD_MAX_WINDOWS; ++index) {
        if (!wm->windows[index].in_use) {
            window = &wm->windows[index];
            break;
        }
    }
    if (!window)
        return -ZD_ENOSPC;
    if (wm->next_id == 0)
        wm->next_id = 1;
    id = wm->next_id++;

    zd_memset(window, 0, sizeof(*window));
    window->id = id;
    window->client = info->client;
    window->monitor_id = monitor->id;
    window->workspace = wm->active_workspace;
    window->logical = info->logical_rect;
    window->min_width = info->min_width;
    window->min_height = info->min_height;
    window->max_width = info->max_width;
    window->max_height = info->max_height;
    window->resizable = info->resizable ? 1U : 0U;
    window->state = ZD_WINDOW_NORMAL;
    window->mapped = 1;
    window->buffer_generation = 1;
    window->buffer_state = ZD_BUFFER_FREE;
    window->role = info->role;
    window->in_use = 1;
    zd_str_copy(window->title, sizeof(window->title),
                info->title ? info->title : "untitled");
    zd_str_copy(window->a11y_label, sizeof(window->a11y_label),
                info->a11y_label ? info->a11y_label : window->title);
    apply_size_limits(window);
    window->logical = clamp_to_monitor(monitor, window->logical);

    if (wm->stacking_count < ZD_MAX_WINDOWS)
        wm->stacking[wm->stacking_count++] = id;
    ++wm->window_count;
    ++wm->stats.windows_created;

    /* New windows take keyboard focus (explicit policy). */
    (void)zd_wm_focus(wm, id);
    emit_window_event(wm, id, 0);
    emit_window_event(wm, id, 5);
    if (out_id)
        *out_id = id;
    return 0;
}

static void reassign_focus(struct zd_wm *wm) {
    zd_window_id previous = ZD_INVALID_WINDOW;
    uint32_t index;

    /* Iterate slots (not window_count): destroys leave holes, and the
     * focused window may live past the first window_count entries. */
    for (index = 0; index < ZD_MAX_WINDOWS; ++index) {
        struct zd_window *window = &wm->windows[index];
        if (window->in_use && window->keyboard_focused) {
            previous = window->id;
            window->keyboard_focused = 0;
        }
    }
    /* Pick the top of the focus stack still visible on the workspace. */
    while (wm->focus_count > 0) {
        struct zd_window *candidate = window_at(wm, wm->focus_stack[0]);
        if (candidate &&
            window_visible_on_workspace(candidate, wm->active_workspace)) {
            candidate->keyboard_focused = 1;
            ++wm->stats.focus_changes;
            emit_window_event(wm, candidate->id, 4);
            return;
        }
        focus_stack_remove(wm, wm->focus_stack[0]);
    }
    if (previous != ZD_INVALID_WINDOW)
        emit_window_event(wm, ZD_INVALID_WINDOW, 4);
}

static void destroy_window_at(struct zd_wm *wm, struct zd_window *window,
                              uint32_t reassign) {
    zd_window_id id = window->id;
    uint32_t was_focused = window->keyboard_focused;

    stacking_remove(wm, id);
    focus_stack_remove(wm, id);
    if (wm->pointer_focus == id)
        wm->pointer_focus = ZD_INVALID_WINDOW;
    window->in_use = 0;
    window->buffer_state = ZD_BUFFER_INVALID;
    --wm->window_count;
    ++wm->stats.windows_destroyed;
    emit_window_event(wm, id, 1);
    emit_window_event(wm, id, 5);
    if (was_focused && reassign)
        reassign_focus(wm);
}

int zd_wm_destroy_window(struct zd_wm *wm, zd_window_id id) {
    struct zd_window *window;
    if (!wm)
        return -ZD_EINVAL;
    window = window_at(wm, id);
    if (!window)
        return -ZD_ENOENT;
    destroy_window_at(wm, window, 1);
    return 0;
}

struct zd_window *zd_wm_window(struct zd_wm *wm, zd_window_id id) {
    return window_at(wm, id);
}

const struct zd_window *zd_wm_window_const(const struct zd_wm *wm,
                                           zd_window_id id) {
    if (!wm || id == ZD_INVALID_WINDOW)
        return (const struct zd_window *)0;
    return window_at((struct zd_wm *)wm, id);
}

int zd_wm_set_title(struct zd_wm *wm, zd_window_id id, const char *title) {
    struct zd_window *window = window_at(wm, id);
    if (!window || !title)
        return -ZD_EINVAL;
    zd_str_copy(window->title, sizeof(window->title), title);
    return 0;
}

int zd_wm_move_resize(struct zd_wm *wm, zd_window_id id,
                      struct zd_rect logical) {
    struct zd_window *window = window_at(wm, id);
    const struct zd_monitor *monitor;
    struct zd_rect previous;

    if (!window || zd_rect_empty(logical))
        return -ZD_EINVAL;
    if (!window->resizable &&
        (logical.w != window->logical.w || logical.h != window->logical.h))
        return -ZD_EPERM;
    monitor = monitor_for_window(wm, window);
    if (!monitor)
        return -ZD_ESTATE;
    previous = window->logical;
    window->logical = logical;
    apply_size_limits(window);
    if (!window->resizable) {
        window->logical.w = previous.w;
        window->logical.h = previous.h;
    }
    window->logical = clamp_to_monitor(monitor, window->logical);
    window->snap = ZD_SNAP_NONE;
    if (window->state == ZD_WINDOW_MAXIMIZED)
        window->state = ZD_WINDOW_NORMAL;
    emit_window_event(wm, id, 2);
    return 0;
}

static void set_window_state(struct zd_wm *wm, struct zd_window *window,
                             enum zd_window_state state) {
    if (window->state == state)
        return;
    window->state = state;
    if (state == ZD_WINDOW_MINIMIZED && window->keyboard_focused)
        reassign_focus(wm);
    emit_window_event(wm, window->id, 3);
}

int zd_wm_minimize(struct zd_wm *wm, zd_window_id id) {
    struct zd_window *window = window_at(wm, id);
    if (!window)
        return -ZD_ENOENT;
    set_window_state(wm, window, ZD_WINDOW_MINIMIZED);
    return 0;
}

int zd_wm_maximize(struct zd_wm *wm, zd_window_id id) {
    struct zd_window *window = window_at(wm, id);
    const struct zd_monitor *monitor;
    if (!window)
        return -ZD_ENOENT;
    monitor = monitor_for_window(wm, window);
    if (!monitor)
        return -ZD_ESTATE;
    window->logical = zd_wm_workarea(monitor);
    window->snap = ZD_SNAP_NONE;
    set_window_state(wm, window, ZD_WINDOW_MAXIMIZED);
    emit_window_event(wm, id, 2);
    return 0;
}

int zd_wm_restore(struct zd_wm *wm, zd_window_id id) {
    struct zd_window *window = window_at(wm, id);
    const struct zd_monitor *monitor;
    if (!window)
        return -ZD_ENOENT;
    if (window->state == ZD_WINDOW_NORMAL)
        return 0;
    monitor = monitor_for_window(wm, window);
    if (monitor)
        window->logical = clamp_to_monitor(monitor, window->logical);
    set_window_state(wm, window, ZD_WINDOW_NORMAL);
    emit_window_event(wm, id, 2);
    return 0;
}

struct zd_rect zd_wm_snap_geometry(const struct zd_monitor *monitor,
                                   enum zd_snap_region region) {
    struct zd_rect workarea;
    struct zd_rect out;
    if (!monitor || (int)region < 0 || (int)region >= ZD_SNAP_COUNT) {
        struct zd_rect empty = {0, 0, 0, 0};
        return empty;
    }
    workarea = zd_wm_workarea(monitor);
    out = workarea;
    switch (region) {
    case ZD_SNAP_LEFT:
        out.w = workarea.w / 2;
        break;
    case ZD_SNAP_RIGHT:
        out.x = workarea.x + workarea.w - workarea.w / 2;
        out.w = workarea.w / 2;
        break;
    case ZD_SNAP_TOP_LEFT:
        out.w = workarea.w / 2;
        out.h = workarea.h / 2;
        break;
    case ZD_SNAP_TOP_RIGHT:
        out.x = workarea.x + workarea.w - workarea.w / 2;
        out.w = workarea.w / 2;
        out.h = workarea.h / 2;
        break;
    case ZD_SNAP_BOTTOM_LEFT:
        out.y = workarea.y + workarea.h - workarea.h / 2;
        out.w = workarea.w / 2;
        out.h = workarea.h / 2;
        break;
    case ZD_SNAP_BOTTOM_RIGHT:
        out.x = workarea.x + workarea.w - workarea.w / 2;
        out.w = workarea.w / 2;
        out.y = workarea.y + workarea.h - workarea.h / 2;
        out.h = workarea.h / 2;
        break;
    case ZD_SNAP_FULLSCREEN:
    case ZD_SNAP_NONE:
    default:
        break;
    }
    return out;
}

int zd_wm_snap(struct zd_wm *wm, zd_window_id id, enum zd_snap_region region) {
    struct zd_window *window = window_at(wm, id);
    const struct zd_monitor *monitor;
    if (!window || (int)region < 0 || (int)region >= ZD_SNAP_COUNT)
        return -ZD_EINVAL;
    if (region == ZD_SNAP_NONE) {
        window->snap = ZD_SNAP_NONE;
        return 0;
    }
    monitor = monitor_for_window(wm, window);
    if (!monitor)
        return -ZD_ESTATE;
    window->logical = zd_wm_snap_geometry(monitor, region);
    window->snap = region;
    if (window->state == ZD_WINDOW_MINIMIZED)
        window->state = ZD_WINDOW_NORMAL;
    if (region == ZD_SNAP_FULLSCREEN)
        window->state = ZD_WINDOW_MAXIMIZED;
    else if (window->state == ZD_WINDOW_MAXIMIZED)
        window->state = ZD_WINDOW_NORMAL;
    emit_window_event(wm, id, 2);
    emit_window_event(wm, id, 3);
    return 0;
}

int zd_wm_raise(struct zd_wm *wm, zd_window_id id) {
    struct zd_window *window = window_at(wm, id);
    uint32_t index;
    if (!window)
        return -ZD_ENOENT;
    stacking_remove(wm, id);
    if (wm->stacking_count >= ZD_MAX_WINDOWS)
        return -ZD_ENOSPC;
    wm->stacking[wm->stacking_count++] = id;
    /* Keep focus stack consistent with user intent: raised = most recent. */
    focus_stack_push(wm, id);
    (void)index;
    emit_window_event(wm, id, 5);
    return 0;
}

int zd_wm_lower(struct zd_wm *wm, zd_window_id id) {
    struct zd_window *window = window_at(wm, id);
    uint32_t index;
    if (!window)
        return -ZD_ENOENT;
    stacking_remove(wm, id);
    if (wm->stacking_count >= ZD_MAX_WINDOWS)
        return -ZD_ENOSPC;
    for (index = wm->stacking_count; index > 0; --index)
        wm->stacking[index] = wm->stacking[index - 1U];
    wm->stacking[0] = id;
    ++wm->stacking_count;
    emit_window_event(wm, id, 5);
    return 0;
}

int zd_wm_focus(struct zd_wm *wm, zd_window_id id) {
    struct zd_window *window = window_at(wm, id);
    struct zd_window *previous = (struct zd_window *)0;
    uint32_t index;

    if (!window)
        return -ZD_ENOENT;
    if (!window_visible_on_workspace(window, wm->active_workspace))
        return -ZD_ESTATE;
    for (index = 0; index < ZD_MAX_WINDOWS; ++index)
        if (wm->windows[index].in_use && wm->windows[index].keyboard_focused) {
            previous = &wm->windows[index];
            break;
        }
    if (previous == window)
        return 0;
    if (previous)
        previous->keyboard_focused = 0;
    window->keyboard_focused = 1;
    focus_stack_push(wm, id);
    ++wm->stats.focus_changes;
    if (previous)
        emit_window_event(wm, previous->id, 4);
    emit_window_event(wm, id, 4);
    return 0;
}

int zd_wm_set_workspace(struct zd_wm *wm, zd_window_id id, uint32_t workspace) {
    struct zd_window *window = window_at(wm, id);
    if (!window)
        return -ZD_ENOENT;
    if (workspace >= wm->workspace_count || workspace >= ZD_MAX_WORKSPACES)
        return -ZD_EINVAL;
    if (window->workspace == workspace)
        return 0;
    window->workspace = workspace;
    if (workspace != wm->active_workspace && window->keyboard_focused)
        reassign_focus(wm);
    emit_window_event(wm, id, 3);
    return 0;
}

int zd_wm_switch_workspace(struct zd_wm *wm, uint32_t workspace) {
    zd_window_id topmost = ZD_INVALID_WINDOW;
    uint32_t index;
    if (!wm)
        return -ZD_EINVAL;
    if (workspace >= wm->workspace_count || workspace >= ZD_MAX_WORKSPACES)
        return -ZD_EINVAL;
    if (workspace == wm->active_workspace)
        return 0;
    wm->active_workspace = workspace;
    ++wm->stats.workspace_switches;
    /* Restore keyboard focus to the topmost visible window of the newly
     * active workspace (explicit shell policy; no focus history yet). */
    index = wm->stacking_count;
    while (index > 0) {
        struct zd_window *window;
        --index;
        window = window_at(wm, wm->stacking[index]);
        if (window && window_visible_on_workspace(window, workspace)) {
            topmost = window->id;
            break;
        }
    }
    if (topmost != ZD_INVALID_WINDOW)
        (void)zd_wm_focus(wm, topmost);
    else
        reassign_focus(wm);
    emit_workspace_event(wm, workspace);
    emit_window_event(wm, ZD_INVALID_WINDOW, 5);
    return 0;
}

int zd_wm_assign_monitor(struct zd_wm *wm, zd_window_id id,
                         uint32_t monitor_id) {
    struct zd_window *window = window_at(wm, id);
    struct zd_monitor *monitor;
    if (!window)
        return -ZD_ENOENT;
    monitor = monitor_lookup_mut(wm, monitor_id);
    if (!monitor)
        return -ZD_ENOENT;
    window->monitor_id = monitor->id;
    window->logical = clamp_to_monitor(monitor, window->logical);
    emit_window_event(wm, id, 2);
    return 0;
}

zd_window_id zd_wm_hit_test(const struct zd_wm *wm, int32_t physical_x,
                            int32_t physical_y) {
    uint32_t index;
    if (!wm)
        return ZD_INVALID_WINDOW;
    /* Topmost first. */
    index = wm->stacking_count;
    while (index > 0) {
        const struct zd_window *window;
        const struct zd_monitor *monitor;
        struct zd_rect physical;
        --index;
        window = zd_wm_window_const(wm, wm->stacking[index]);
        if (!window || !window_visible_on_workspace(window, wm->active_workspace))
            continue;
        monitor = monitor_lookup(wm, window->monitor_id);
        if (!monitor)
            continue;
        physical = zd_wm_logical_to_physical(monitor, window->logical);
        if (zd_rect_contains(physical, physical_x, physical_y))
            return window->id;
    }
    return ZD_INVALID_WINDOW;
}

zd_window_id zd_wm_keyboard_target(const struct zd_wm *wm) {
    uint32_t index;
    if (!wm)
        return ZD_INVALID_WINDOW;
    for (index = 0; index < ZD_MAX_WINDOWS; ++index) {
        const struct zd_window *window = &wm->windows[index];
        if (window->in_use && window->keyboard_focused &&
            window_visible_on_workspace(window, wm->active_workspace))
            return window->id;
    }
    return ZD_INVALID_WINDOW;
}

uint32_t zd_wm_stacking_order(const struct zd_wm *wm, zd_window_id *out,
                              uint32_t capacity) {
    uint32_t count;
    uint32_t index;
    if (!wm)
        return 0;
    count = wm->stacking_count < capacity ? wm->stacking_count : capacity;
    for (index = 0; index < count; ++index)
        out[index] = wm->stacking[index];
    return count;
}

int32_t zd_wm_scale_x(const struct zd_monitor *monitor, int32_t logical_x) {
    if (!monitor || monitor->scale_percent == 100)
        return logical_x;
    return (int32_t)(((int64_t)logical_x * monitor->scale_percent) / 100);
}

int32_t zd_wm_scale_y(const struct zd_monitor *monitor, int32_t logical_y) {
    return zd_wm_scale_x(monitor, logical_y);
}

int32_t zd_wm_unscale_x(const struct zd_monitor *monitor, int32_t physical_x) {
    if (!monitor || monitor->scale_percent == 100)
        return physical_x;
    return (int32_t)(((int64_t)physical_x * 100) / monitor->scale_percent);
}

int32_t zd_wm_unscale_y(const struct zd_monitor *monitor, int32_t physical_y) {
    return zd_wm_unscale_x(monitor, physical_y);
}

struct zd_rect zd_wm_logical_to_physical(const struct zd_monitor *monitor,
                                         struct zd_rect logical) {
    struct zd_rect out;
    if (!monitor) {
        return logical;
    }
    out.x = zd_wm_scale_x(monitor, logical.x);
    out.y = zd_wm_scale_y(monitor, logical.y);
    out.w = zd_wm_scale_x(monitor, logical.w);
    out.h = zd_wm_scale_y(monitor, logical.h);
    return out;
}

struct zd_rect zd_wm_workarea(const struct zd_monitor *monitor) {
    /* Reserved shell strip: ZERO Bar occupies the bottom 40 logical units
     * of the primary display. Non-primary monitors use full bounds. */
    struct zd_rect workarea;
    if (!monitor) {
        struct zd_rect empty = {0, 0, 0, 0};
        return empty;
    }
    workarea = monitor->bounds;
    if (monitor->primary && workarea.h > 80)
        workarea.h -= 40;
    return workarea;
}

int zd_wm_queue_buffer(struct zd_wm *wm, zd_window_id id,
                       zd_buffer_generation generation) {
    struct zd_window *window = window_at(wm, id);
    if (!window)
        return -ZD_ENOENT;
    if (generation == 0 || generation != window->buffer_generation) {
        ++wm->stats.buffer_rejects;
        return -ZD_EINVAL;
    }
    if (window->buffer_state == ZD_BUFFER_INVALID) {
        ++wm->stats.buffer_rejects;
        return -ZD_ESTATE;
    }
    window->buffer_state = ZD_BUFFER_QUEUED;
    return 0;
}

int zd_wm_release_buffer(struct zd_wm *wm, zd_window_id id,
                         zd_buffer_generation generation) {
    struct zd_window *window = window_at(wm, id);
    if (!window)
        return -ZD_ENOENT;
    if (generation != window->buffer_generation ||
        window->buffer_state == ZD_BUFFER_INVALID) {
        ++wm->stats.buffer_rejects;
        return -ZD_EINVAL;
    }
    if (window->buffer_state == ZD_BUFFER_FREE) {
        ++wm->stats.buffer_rejects;
        return -ZD_ESTATE;
    }
    window->buffer_state = ZD_BUFFER_FREE;
    return 0;
}

enum zd_buffer_state zd_wm_buffer_state(const struct zd_wm *wm,
                                        zd_window_id id,
                                        zd_buffer_generation *generation) {
    const struct zd_window *window = zd_wm_window_const(wm, id);
    if (!window)
        return ZD_BUFFER_INVALID;
    if (generation)
        *generation = window->buffer_generation;
    return window->buffer_state;
}

uint32_t zd_wm_windows_for_client(const struct zd_wm *wm, zd_client_id client,
                                  zd_window_id *out, uint32_t capacity) {
    uint32_t count = 0;
    uint32_t index;
    if (!wm || client == 0)
        return 0;
    for (index = 0; index < ZD_MAX_WINDOWS; ++index)
        if (wm->windows[index].in_use && wm->windows[index].client == client) {
            /* capacity limits writes only; the count is always complete so
             * a NULL/0 query still reports the true total. */
            if (out && count < capacity)
                out[count] = wm->windows[index].id;
            ++count;
        }
    return count;
}

uint32_t zd_wm_client_crashed(struct zd_wm *wm, zd_client_id client) {
    uint32_t destroyed = 0;
    uint32_t index;
    if (!wm || client == 0)
        return 0;
    /* Invalidate buffers first so no in-flight release is accepted while
     * the windows are being torn down. */
    for (index = 0; index < ZD_MAX_WINDOWS; ++index) {
        struct zd_window *window = &wm->windows[index];
        if (window->in_use && window->client == client) {
            ++window->buffer_generation; /* invalidate stale generations */
            window->buffer_state = ZD_BUFFER_INVALID;
        }
    }
    for (index = 0; index < ZD_MAX_WINDOWS; ++index) {
        struct zd_window *window = &wm->windows[index];
        if (window->in_use && window->client == client) {
            destroy_window_at(wm, window, 0);
            ++destroyed;
        }
    }
    if (destroyed) {
        ++wm->stats.client_crashes_handled;
        reassign_focus(wm);
    }
    return destroyed;
}

int zd_wm_is_above(const struct zd_wm *wm, zd_window_id upper,
                   zd_window_id lower) {
    uint32_t index;
    int found_upper = 0;
    int found_lower = 0;
    if (!wm)
        return 0;
    for (index = 0; index < wm->stacking_count; ++index) {
        if (wm->stacking[index] == upper)
            found_upper = 1;
        if (wm->stacking[index] == lower)
            found_lower = 1;
    }
    if (!found_upper || !found_lower)
        return 0;
    /* Stacking order is bottom -> top: the id encountered first is the
     * lower window, so meeting `lower` before `upper` means `upper` sits
     * above it. */
    for (index = 0; index < wm->stacking_count; ++index) {
        if (wm->stacking[index] == upper)
            return 0;
        if (wm->stacking[index] == lower)
            return 1;
    }
    return 0;
}
