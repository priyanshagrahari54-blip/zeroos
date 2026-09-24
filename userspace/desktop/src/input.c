#include <zeroos/desktop/input.h>

static struct zd_input_delivery delivery(enum zd_input_result result,
                                         zd_window_id window,
                                         const struct zd_rect *physical,
                                         int32_t x, int32_t y) {
    struct zd_input_delivery out;
    out.result = result;
    out.window = window;
    out.local_x = physical ? x - physical->x : 0;
    out.local_y = physical ? y - physical->y : 0;
    return out;
}

void zd_input_router_init(struct zd_input_router *router, struct zd_wm *wm) {
    if (!router)
        return;
    router->wm = wm;
    router->pointer_focus = ZD_INVALID_WINDOW;
    router->pointer_grab = ZD_INVALID_WINDOW;
    router->overlay = ZD_INVALID_WINDOW;
    router->overlay_takes_keyboard = 0;
    router->pointer_x = 0;
    router->pointer_y = 0;
    router->buttons_down = 0;
    router->stats = (struct zd_input_router_stats){0};
}

int zd_input_set_overlay(struct zd_input_router *router, zd_window_id overlay,
                         uint32_t takes_keyboard) {
    if (!router || !router->wm)
        return -ZD_EINVAL;
    if (overlay != ZD_INVALID_WINDOW && !zd_wm_window(router->wm, overlay))
        return -ZD_ENOENT;
    router->overlay = overlay;
    router->overlay_takes_keyboard = overlay == ZD_INVALID_WINDOW ?
                                     0 : (takes_keyboard ? 1U : 0U);
    return 0;
}

static int window_physical_rect(struct zd_wm *wm, zd_window_id id,
                                struct zd_rect *out) {
    struct zd_window *window = zd_wm_window(wm, id);
    const struct zd_monitor *monitor;
    if (!window)
        return -ZD_ENOENT;
    monitor = zd_wm_monitor(wm, window->monitor_id);
    if (!monitor)
        return -ZD_ENOENT;
    *out = zd_wm_logical_to_physical(monitor, window->logical);
    return 0;
}

static struct zd_input_delivery deliver_window(struct zd_input_router *router,
                                               zd_window_id id,
                                               int32_t x, int32_t y,
                                               uint32_t count_drop) {
    struct zd_rect physical;
    if (id == ZD_INVALID_WINDOW ||
        window_physical_rect(router->wm, id, &physical) != 0) {
        if (count_drop)
            ++router->stats.dropped_events;
        return delivery(ZD_INPUT_DROPPED, ZD_INVALID_WINDOW, 0, x, y);
    }
    return delivery(ZD_INPUT_TO_WINDOW, id, &physical, x, y);
}

struct zd_input_delivery zd_input_pointer_move(struct zd_input_router *router,
                                               int32_t physical_x,
                                               int32_t physical_y) {
    zd_window_id hit;
    if (!router || !router->wm) {
        if (router)
            ++router->stats.dropped_events;
        return delivery(ZD_INPUT_DROPPED, ZD_INVALID_WINDOW, 0, 0, 0);
    }

    ++router->stats.pointer_moves;
    router->pointer_x = physical_x;
    router->pointer_y = physical_y;

    if (router->pointer_grab != ZD_INVALID_WINDOW) {
        /* Implicit grab: the pressed window keeps every motion until the
         * button is released, even outside its bounds. */
        return deliver_window(router, router->pointer_grab,
                              physical_x, physical_y, 0);
    }

    hit = zd_wm_hit_test(router->wm, physical_x, physical_y);
    if (hit != router->pointer_focus) {
        router->pointer_focus = hit;
        ++router->stats.focus_changes;
    }
    if (hit == ZD_INVALID_WINDOW) {
        ++router->stats.dropped_events;
        return delivery(ZD_INPUT_DROPPED, ZD_INVALID_WINDOW, 0,
                        physical_x, physical_y);
    }
    return deliver_window(router, hit, physical_x, physical_y, 0);
}

struct zd_input_delivery zd_input_pointer_button(struct zd_input_router *router,
                                                 uint32_t button,
                                                 uint32_t pressed) {
    zd_window_id target;
    if (!router || !router->wm || button == 0 || button > 31) {
        if (router)
            ++router->stats.dropped_events;
        return delivery(ZD_INPUT_DROPPED, ZD_INVALID_WINDOW, 0, 0, 0);
    }

    ++router->stats.button_events;
    if (pressed)
        router->buttons_down |= (1U << button);
    else
        router->buttons_down &= ~(1U << button);

    /* Overlays claim pointer input first (launcher surface, search box). */
    if (router->overlay != ZD_INVALID_WINDOW) {
        struct zd_rect physical;
        if (window_physical_rect(router->wm, router->overlay, &physical) == 0) {
            ++router->stats.overlay_deliveries;
            return delivery(ZD_INPUT_TO_OVERLAY, router->overlay, &physical,
                            router->pointer_x, router->pointer_y);
        }
    }

    if (pressed) {
        target = zd_wm_hit_test(router->wm, router->pointer_x,
                                router->pointer_y);
        if (target != ZD_INVALID_WINDOW) {
            /* Click-to-focus + raise is window-system policy, executed
             * exactly here so every input source behaves identically. */
            (void)zd_wm_focus(router->wm, target);
            (void)zd_wm_raise(router->wm, target);
            router->pointer_focus = target;
            router->pointer_grab = target;
        }
        if (target == ZD_INVALID_WINDOW) {
            ++router->stats.dropped_events;
            return delivery(ZD_INPUT_DROPPED, ZD_INVALID_WINDOW, 0,
                            router->pointer_x, router->pointer_y);
        }
        return deliver_window(router, target, router->pointer_x,
                              router->pointer_y, 0);
    }

    /* Release: prefer the grab, fall back to a fresh hit test. */
    target = router->pointer_grab != ZD_INVALID_WINDOW ?
             router->pointer_grab :
             zd_wm_hit_test(router->wm, router->pointer_x, router->pointer_y);
    router->pointer_grab = ZD_INVALID_WINDOW;
    if (target == ZD_INVALID_WINDOW) {
        ++router->stats.dropped_events;
        return delivery(ZD_INPUT_DROPPED, ZD_INVALID_WINDOW, 0,
                        router->pointer_x, router->pointer_y);
    }
    return deliver_window(router, target, router->pointer_x,
                          router->pointer_y, 0);
}

struct zd_input_delivery zd_input_key(struct zd_input_router *router,
                                      uint16_t code, uint32_t down) {
    zd_window_id target;
    if (!router || !router->wm) {
        if (router)
            ++router->stats.dropped_events;
        return delivery(ZD_INPUT_DROPPED, ZD_INVALID_WINDOW, 0, 0, 0);
    }
    (void)code;
    (void)down;

    ++router->stats.key_events;
    if (router->overlay != ZD_INVALID_WINDOW &&
        router->overlay_takes_keyboard) {
        struct zd_rect physical;
        if (window_physical_rect(router->wm, router->overlay, &physical) == 0) {
            ++router->stats.overlay_deliveries;
            return delivery(ZD_INPUT_TO_OVERLAY, router->overlay, &physical,
                            0, 0);
        }
    }
    target = zd_wm_keyboard_target(router->wm);
    if (target == ZD_INVALID_WINDOW) {
        ++router->stats.dropped_events;
        return delivery(ZD_INPUT_DROPPED, ZD_INVALID_WINDOW, 0, 0, 0);
    }
    return delivery(ZD_INPUT_TO_WINDOW, target, 0, 0, 0);
}

zd_window_id zd_input_pointer_focus(const struct zd_input_router *router) {
    if (!router)
        return ZD_INVALID_WINDOW;
    return router->pointer_grab != ZD_INVALID_WINDOW ?
           router->pointer_grab : router->pointer_focus;
}
