#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

static struct zd_wm wm;
static struct zd_monitor monitor0;
static zd_client_id client_a;
static zd_client_id client_b;

static void setup(void) {
    zd_wm_init(&wm);
    memset(&monitor0, 0, sizeof(monitor0));
    monitor0.id = 1;
    monitor0.bounds.x = 0;
    monitor0.bounds.y = 0;
    monitor0.bounds.w = 1920;
    monitor0.bounds.h = 1080;
    monitor0.scale_percent = 100;
    monitor0.primary = 1;
    monitor0.enabled = 1;
    strcpy(monitor0.name, "eDP-1");
    ZD_CHECK_OK(zd_wm_add_monitor(&wm, &monitor0));
    client_a = zd_wm_register_client(&wm);
    client_b = zd_wm_register_client(&wm);
    ZD_CHECK(client_a != 0 && client_b != 0 && client_a != client_b);
}

static struct zd_window_create_info make_info(zd_client_id client,
                                              int32_t x, int32_t y,
                                              int32_t w, int32_t h) {
    struct zd_window_create_info info;
    memset(&info, 0, sizeof(info));
    info.client = client;
    info.title = "test window";
    info.logical_rect.x = x;
    info.logical_rect.y = y;
    info.logical_rect.w = w;
    info.logical_rect.h = h;
    info.monitor_id = 1;
    info.resizable = 1;
    info.role = ZD_ROLE_WINDOW;
    return info;
}

static void test_monitor_and_dpi(void) {
    struct zd_monitor hidpi;
    struct zd_rect logical = {10, 20, 100, 50};
    struct zd_rect physical;
    setup();
    /* Duplicate monitor id rejected. */
    ZD_CHECK_ERR(zd_wm_add_monitor(&wm, &monitor0), ZD_EBUSY);
    /* Invalid monitor rejected. */
    hidpi = monitor0;
    hidpi.id = 2;
    hidpi.scale_percent = 0;
    ZD_CHECK_ERR(zd_wm_add_monitor(&wm, &hidpi), ZD_EINVAL);
    hidpi.scale_percent = 150;
    hidpi.primary = 0;
    hidpi.name[0] = 0;
    strcpy(hidpi.name, "HDMI-1");
    hidpi.bounds.w = 3840;
    hidpi.bounds.h = 2160;
    ZD_CHECK_OK(zd_wm_add_monitor(&wm, &hidpi));
    ZD_CHECK_EQ(wm.monitor_count, 2U);
    /* Exactly one primary. */
    ZD_CHECK(zd_wm_monitor(&wm, 1)->primary);
    ZD_CHECK(!zd_wm_monitor(&wm, 2)->primary);

    physical = zd_wm_logical_to_physical(zd_wm_monitor(&wm, 2), logical);
    ZD_CHECK_EQ(physical.x, 15);
    ZD_CHECK_EQ(physical.y, 30);
    ZD_CHECK_EQ(physical.w, 150);
    ZD_CHECK_EQ(physical.h, 75);
    ZD_CHECK_EQ(zd_wm_unscale_x(zd_wm_monitor(&wm, 2), 15), 10);
    /* Workarea: primary reserves the 40-unit ZERO Bar strip. */
    {
        struct zd_rect workarea = zd_wm_workarea(zd_wm_monitor(&wm, 1));
        ZD_CHECK_EQ(workarea.w, 1920);
        ZD_CHECK_EQ(workarea.h, 1040);
    }
}

static void test_create_destroy_focus(void) {
    struct zd_window_create_info info;
    zd_window_id first = ZD_INVALID_WINDOW;
    zd_window_id second = ZD_INVALID_WINDOW;
    setup();
    info = make_info(client_a, 10, 10, 400, 300);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &first));
    info = make_info(client_b, 50, 50, 400, 300);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &second));
    ZD_CHECK_EQ(wm.window_count, 2U);
    /* Second window has focus; first does not. */
    ZD_CHECK_EQ(zd_wm_keyboard_target(&wm), second);
    ZD_CHECK(!zd_wm_window(&wm, first)->keyboard_focused);
    /* Focus back to first. */
    ZD_CHECK_OK(zd_wm_focus(&wm, first));
    ZD_CHECK_EQ(zd_wm_keyboard_target(&wm), first);
    ZD_CHECK(zd_wm_is_above(&wm, second, first) ||
             zd_wm_is_above(&wm, first, second));
    /* Raise first: first now above second. */
    ZD_CHECK_OK(zd_wm_raise(&wm, first));
    ZD_CHECK(zd_wm_is_above(&wm, first, second));
    ZD_CHECK(zd_wm_is_above(&wm, first, first) == 0);
    /* Lower first: back to bottom. */
    ZD_CHECK_OK(zd_wm_lower(&wm, first));
    ZD_CHECK(zd_wm_is_above(&wm, second, first));
    /* Destroy focused window: focus falls back. */
    ZD_CHECK_OK(zd_wm_focus(&wm, first));
    ZD_CHECK_OK(zd_wm_destroy_window(&wm, first));
    ZD_CHECK_EQ(wm.window_count, 1U);
    ZD_CHECK_EQ(zd_wm_keyboard_target(&wm), second);
    ZD_CHECK_ERR(zd_wm_destroy_window(&wm, first), ZD_ENOENT);
    ZD_CHECK_ERR(zd_wm_destroy_window(&wm, 9999), ZD_ENOENT);
}

static void test_geometry_and_snapping(void) {
    struct zd_window_create_info info;
    zd_window_id id = ZD_INVALID_WINDOW;
    struct zd_rect geometry;
    setup();
    info = make_info(client_a, 10, 10, 400, 300);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &id));

    /* Move/resize clamps into the monitor. */
    ZD_CHECK_OK(zd_wm_move_resize(&wm, id, (struct zd_rect){-500, -500,
                                                             400, 300}));
    ZD_CHECK_EQ(zd_wm_window(&wm, id)->logical.x, 0);
    ZD_CHECK_EQ(zd_wm_window(&wm, id)->logical.y, 0);

    /* Snapping geometries cover the workarea without overlap leaks. */
    geometry = zd_wm_snap_geometry(zd_wm_monitor(&wm, 1), ZD_SNAP_LEFT);
    ZD_CHECK_EQ(geometry.x, 0);
    ZD_CHECK_EQ(geometry.w, 960);
    ZD_CHECK_EQ(geometry.h, 1040);
    ZD_CHECK_OK(zd_wm_snap(&wm, id, ZD_SNAP_LEFT));
    ZD_CHECK_EQ(zd_wm_window(&wm, id)->snap, ZD_SNAP_LEFT);
    ZD_CHECK_EQ(zd_wm_window(&wm, id)->logical.w, 960);

    geometry = zd_wm_snap_geometry(zd_wm_monitor(&wm, 1), ZD_SNAP_RIGHT);
    ZD_CHECK_EQ(geometry.x, 960);
    geometry = zd_wm_snap_geometry(zd_wm_monitor(&wm, 1), ZD_SNAP_TOP_RIGHT);
    ZD_CHECK_EQ(geometry.x, 960);
    ZD_CHECK_EQ(geometry.y, 0);
    ZD_CHECK_EQ(geometry.w, 960);
    ZD_CHECK_EQ(geometry.h, 520);
    geometry = zd_wm_snap_geometry(zd_wm_monitor(&wm, 1), ZD_SNAP_FULLSCREEN);
    ZD_CHECK_EQ(geometry.w, 1920);
    ZD_CHECK_EQ(geometry.h, 1040);
    ZD_CHECK_OK(zd_wm_snap(&wm, id, ZD_SNAP_FULLSCREEN));
    ZD_CHECK_EQ(zd_wm_window(&wm, id)->state, ZD_WINDOW_MAXIMIZED);

    /* Restore from maximize. */
    ZD_CHECK_OK(zd_wm_restore(&wm, id));
    ZD_CHECK_EQ(zd_wm_window(&wm, id)->state, ZD_WINDOW_NORMAL);

    /* Size limits enforced. */
    info = make_info(client_a, 0, 0, 400, 300);
    info.min_width = 200;
    info.min_height = 150;
    info.max_width = 500;
    info.max_height = 400;
    {
        zd_window_id limited = ZD_INVALID_WINDOW;
        ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &limited));
        /* Sub-minimum geometry is clamped upward to the declared minimum. */
        ZD_CHECK_OK(zd_wm_move_resize(&wm, limited,
                                      (struct zd_rect){0, 0, 50, 50}));
        ZD_CHECK_EQ(zd_wm_window(&wm, limited)->logical.w, 200);
        ZD_CHECK_EQ(zd_wm_window(&wm, limited)->logical.h, 150);
        /* Super-maximum geometry is clamped down to the declared maximum. */
        ZD_CHECK_OK(zd_wm_move_resize(&wm, limited,
                                      (struct zd_rect){0, 0, 9999, 9999}));
        ZD_CHECK_EQ(zd_wm_window(&wm, limited)->logical.w, 500);
        ZD_CHECK_EQ(zd_wm_window(&wm, limited)->logical.h, 400);
    }

    /* Non-resizable windows reject size changes but allow moves. */
    info = make_info(client_b, 0, 0, 300, 200);
    info.resizable = 0;
    {
        zd_window_id fixed = ZD_INVALID_WINDOW;
        ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &fixed));
        ZD_CHECK_ERR(zd_wm_move_resize(&wm, fixed,
                                       (struct zd_rect){0, 0, 600, 600}),
                     ZD_EPERM);
        ZD_CHECK_OK(zd_wm_move_resize(&wm, fixed,
                                      (struct zd_rect){80, 90, 300, 200}));
        ZD_CHECK_EQ(zd_wm_window(&wm, fixed)->logical.x, 80);
    }
}

static void test_workspaces(void) {
    struct zd_window_create_info info;
    zd_window_id on_ws0 = ZD_INVALID_WINDOW;
    setup();
    wm.workspace_count = 3;
    info = make_info(client_a, 10, 10, 300, 200);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &on_ws0));
    ZD_CHECK_EQ(zd_wm_window(&wm, on_ws0)->workspace, 0U);

    ZD_CHECK_OK(zd_wm_set_workspace(&wm, on_ws0, 2));
    /* Window on an inactive workspace is not routed. */
    ZD_CHECK_EQ(zd_wm_hit_test(&wm, 20, 20), ZD_INVALID_WINDOW);
    ZD_CHECK_EQ(zd_wm_keyboard_target(&wm), ZD_INVALID_WINDOW);
    /* Switching to its workspace restores routing. */
    ZD_CHECK_OK(zd_wm_switch_workspace(&wm, 2));
    ZD_CHECK_EQ(zd_wm_hit_test(&wm, 20, 20), on_ws0);
    ZD_CHECK_EQ(zd_wm_keyboard_target(&wm), on_ws0);
    /* Invalid workspace rejected. */
    ZD_CHECK_ERR(zd_wm_switch_workspace(&wm, 9), ZD_EINVAL);
    ZD_CHECK_ERR(zd_wm_set_workspace(&wm, on_ws0, 9), ZD_EINVAL);
    /* Switching away reassigns focus to nothing (only window). */
    ZD_CHECK_OK(zd_wm_switch_workspace(&wm, 0));
    ZD_CHECK_EQ(zd_wm_keyboard_target(&wm), ZD_INVALID_WINDOW);
    ZD_CHECK_EQ(wm.stats.workspace_switches, 2U);
}

static void test_input_routing(void) {
    struct zd_window_create_info info;
    zd_window_id under = ZD_INVALID_WINDOW;
    zd_window_id over = ZD_INVALID_WINDOW;
    setup();
    info = make_info(client_a, 100, 100, 400, 300);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &under));
    info = make_info(client_b, 200, 150, 400, 300);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &over));
    /* Overlap region hits the topmost (over). */
    ZD_CHECK_EQ(zd_wm_hit_test(&wm, 250, 200), over);
    /* Region only in `under` hits under. */
    ZD_CHECK_EQ(zd_wm_hit_test(&wm, 120, 250), under);
    /* Region outside both: shell. */
    ZD_CHECK_EQ(zd_wm_hit_test(&wm, 10, 1070), ZD_INVALID_WINDOW);
    /* Minimized top window falls through. */
    ZD_CHECK_OK(zd_wm_minimize(&wm, over));
    ZD_CHECK_EQ(zd_wm_hit_test(&wm, 250, 200), under);
    ZD_CHECK_OK(zd_wm_restore(&wm, over));
    ZD_CHECK_EQ(zd_wm_hit_test(&wm, 250, 200), over);
}

static void test_buffer_lifetime(void) {
    struct zd_window_create_info info;
    zd_window_id id = ZD_INVALID_WINDOW;
    zd_buffer_generation generation = 0;
    setup();
    info = make_info(client_a, 0, 0, 200, 200);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &id));
    ZD_CHECK_EQ(zd_wm_buffer_state(&wm, id, &generation), ZD_BUFFER_FREE);
    ZD_CHECK(generation > 0);
    /* Queue with the wrong generation rejected. */
    ZD_CHECK_ERR(zd_wm_queue_buffer(&wm, id, generation + 1), ZD_EINVAL);
    ZD_CHECK_EQ(zd_wm_buffer_state(&wm, id, 0), ZD_BUFFER_FREE);
    ZD_CHECK_OK(zd_wm_queue_buffer(&wm, id, generation));
    ZD_CHECK_EQ(zd_wm_buffer_state(&wm, id, 0), ZD_BUFFER_QUEUED);
    /* Double queue is idempotent (still queued). */
    ZD_CHECK_OK(zd_wm_queue_buffer(&wm, id, generation));
    ZD_CHECK_OK(zd_wm_release_buffer(&wm, id, generation));
    ZD_CHECK_EQ(zd_wm_buffer_state(&wm, id, 0), ZD_BUFFER_FREE);
    /* Release when already free -> ESTATE. */
    ZD_CHECK_ERR(zd_wm_release_buffer(&wm, id, generation), ZD_ESTATE);
    /* Generation 0 never valid. */
    ZD_CHECK_ERR(zd_wm_queue_buffer(&wm, id, 0), ZD_EINVAL);
    ZD_CHECK(wm.stats.buffer_rejects >= 3U);
}

static uint32_t crash_listener_destroyed;

static void on_window_destroyed_crash(void *context, zd_window_id id) {
    (void)context;
    (void)id;
    ++crash_listener_destroyed;
}

static void test_client_crash_isolation(void) {
    struct zd_window_create_info info;
    struct zd_wm_listener listener;
    zd_window_id owned[4];
    zd_window_id stranger = ZD_INVALID_WINDOW;
    uint32_t index;
    setup();
    crash_listener_destroyed = 0;
    memset(&listener, 0, sizeof(listener));
    listener.on_window_destroyed = on_window_destroyed_crash;
    ZD_CHECK_OK(zd_wm_add_listener(&wm, &listener));

    for (index = 0; index < 4; ++index) {
        info = make_info(client_a, (int32_t)(10 + index * 30), 10, 200, 150);
        ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &owned[index]));
    }
    info = make_info(client_b, 400, 400, 200, 150);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &stranger));
    ZD_CHECK_EQ(zd_wm_windows_for_client(&wm, client_a, 0, 0), 4U);
    /* Focus one of A's windows, then crash A. */
    ZD_CHECK_OK(zd_wm_focus(&wm, owned[2]));
    ZD_CHECK_EQ(zd_wm_client_crashed(&wm, client_a), 4U);
    ZD_CHECK_EQ(crash_listener_destroyed, 4U);
    ZD_CHECK_EQ(wm.window_count, 1U);
    ZD_CHECK_EQ(zd_wm_windows_for_client(&wm, client_a, 0, 0), 0U);
    /* Focus reassigns to the surviving window. */
    ZD_CHECK_EQ(zd_wm_keyboard_target(&wm), stranger);
    /* Stale buffer ops on destroyed ids fail. */
    ZD_CHECK_EQ(zd_wm_buffer_state(&wm, owned[0], 0), ZD_BUFFER_INVALID);
    ZD_CHECK_ERR(zd_wm_queue_buffer(&wm, owned[0], 1), ZD_ENOENT);
    ZD_CHECK_EQ(wm.stats.client_crashes_handled, 1U);
    /* Crashing an unknown client is a no-op, not a fault. */
    ZD_CHECK_EQ(zd_wm_client_crashed(&wm, 9999), 0U);
}

static void test_capacity_limits(void) {
    struct zd_window_create_info info;
    uint32_t index;
    zd_window_id id;
    uint32_t created = 0;
    setup();
    info = make_info(client_a, 0, 0, 100, 100);
    for (index = 0; index < ZD_MAX_WINDOWS + 5U; ++index) {
        if (zd_wm_create_window(&wm, &info, &id) == 0)
            ++created;
        else
            break;
    }
    ZD_CHECK_EQ(created, ZD_MAX_WINDOWS);
    ZD_CHECK_ERR(zd_wm_create_window(&wm, &info, &id), ZD_ENOSPC);
    /* Bad inputs rejected. */
    info.client = 0;
    ZD_CHECK_ERR(zd_wm_create_window(&wm, &info, &id), ZD_EINVAL);
    info.client = client_a;
    info.logical_rect.w = 0;
    ZD_CHECK_ERR(zd_wm_create_window(&wm, &info, &id), ZD_EINVAL);
}

void zd_test_window_suite(void) {
    printf(" suite: window system\n");
    ZD_RUN(test_monitor_and_dpi);
    ZD_RUN(test_create_destroy_focus);
    ZD_RUN(test_geometry_and_snapping);
    ZD_RUN(test_workspaces);
    ZD_RUN(test_input_routing);
    ZD_RUN(test_buffer_lifetime);
    ZD_RUN(test_client_crash_isolation);
    ZD_RUN(test_capacity_limits);
}
