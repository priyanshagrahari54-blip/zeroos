#ifndef ZEROOS_DESKTOP_INPUT_H
#define ZEROOS_DESKTOP_INPUT_H

/* Event-level input routing: turns raw pointer/key events into window
 * deliveries with shell policy (pointer focus, click-to-focus + raise,
 * implicit grab while a button is held, overlay priority for launcher /
 * search surfaces). Pure policy over the window system — no kernel code,
 * no polling, fixed capacity.
 *
 * The delivery result tells the shell who consumed the event:
 *   ZD_INPUT_DROPPED   nobody (background) — shell may act (e.g. slap)
 *   ZD_INPUT_TO_WINDOW a window (window + window-local physical coords)
 *   ZD_INPUT_TO_OVERLAY the active overlay surface
 */

#include <zeroos/desktop/common.h>
#include <zeroos/desktop/window.h>

enum zd_input_result {
    ZD_INPUT_DROPPED = 0,
    ZD_INPUT_TO_WINDOW = 1,
    ZD_INPUT_TO_OVERLAY = 2
};

struct zd_input_delivery {
    enum zd_input_result result;
    zd_window_id window;    /* valid for TO_WINDOW/TO_OVERLAY */
    int32_t local_x;        /* window-local physical pixels */
    int32_t local_y;
};

struct zd_input_router_stats {
    uint64_t pointer_moves;
    uint64_t button_events;
    uint64_t key_events;
    uint64_t focus_changes;
    uint64_t overlay_deliveries;
    uint64_t dropped_events;
};

struct zd_input_router {
    struct zd_wm *wm;
    zd_window_id pointer_focus;
    zd_window_id pointer_grab;      /* button-held capture target */
    zd_window_id overlay;           /* ZD_INVALID_WINDOW = none */
    uint32_t overlay_takes_keyboard;
    int32_t pointer_x;              /* physical desktop coordinates */
    int32_t pointer_y;
    uint32_t buttons_down;          /* bit N = button N held */
    struct zd_input_router_stats stats;
};

void zd_input_router_init(struct zd_input_router *router, struct zd_wm *wm);

/* Overlay surfaces (launcher, universal search) may claim keyboard focus
 * independently of their window type. overlay=0 clears the overlay. */
int zd_input_set_overlay(struct zd_input_router *router, zd_window_id overlay,
                         uint32_t takes_keyboard);

/* Pointer motion: hit-tests under the pointer (or honors an active grab),
 * updates pointer focus and reports the delivery target. */
struct zd_input_delivery zd_input_pointer_move(struct zd_input_router *router,
                                               int32_t physical_x,
                                               int32_t physical_y);

/* Button press/release (button 1 = primary, 2 = secondary, 3 = middle).
 * Press: click-to-focus + raise + implicit grab. Release: delivers to the
 * grab (or fresh hit test) and clears it. */
struct zd_input_delivery zd_input_pointer_button(struct zd_input_router *router,
                                                 uint32_t button,
                                                 uint32_t pressed);

/* Key press/release: overlay (when it takes the keyboard) wins, else the
 * focused window of the active workspace. */
struct zd_input_delivery zd_input_key(struct zd_input_router *router,
                                      uint16_t code, uint32_t down);

/* Current pointer focus (window under the pointer with grab overrides). */
zd_window_id zd_input_pointer_focus(const struct zd_input_router *router);

#endif
