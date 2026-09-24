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
    info.title = "router window";
    info.logical_rect.x = x;
    info.logical_rect.y = y;
    info.logical_rect.w = w;
    info.logical_rect.h = h;
    info.monitor_id = 1;
    info.resizable = 1;
    info.role = ZD_ROLE_WINDOW;
    return info;
}

static void test_move_and_focus(void) {
    struct zd_input_router router;
    struct zd_window_create_info info;
    zd_window_id first, second;
    struct zd_input_delivery got;

    setup();
    zd_input_router_init(&router, &wm);
    ZD_CHECK_EQ(router.pointer_focus, ZD_INVALID_WINDOW);
    ZD_CHECK_EQ(zd_input_pointer_focus(&router), ZD_INVALID_WINDOW);

    info = make_info(client_a, 10, 10, 200, 100);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &first));
    info = make_info(client_b, 300, 10, 200, 100);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &second));

    /* Motion over empty background: dropped, no focus. */
    got = zd_input_pointer_move(&router, 1000, 800);
    ZD_CHECK_EQ(got.result, ZD_INPUT_DROPPED);
    ZD_CHECK_EQ(router.pointer_focus, ZD_INVALID_WINDOW);

    /* Motion over the first window: focused with local coordinates. */
    got = zd_input_pointer_move(&router, 50, 40);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_WINDOW);
    ZD_CHECK_EQ(got.window, first);
    ZD_CHECK_EQ(got.local_x, 40);
    ZD_CHECK_EQ(got.local_y, 30);
    ZD_CHECK_EQ(router.pointer_focus, first);

    /* Motion over the second window: pointer focus follows (no click). */
    got = zd_input_pointer_move(&router, 310, 20);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_WINDOW);
    ZD_CHECK_EQ(got.window, second);
    ZD_CHECK_EQ(router.pointer_focus, second);
    ZD_CHECK(router.stats.focus_changes >= 2);
    ZD_CHECK(router.stats.pointer_moves >= 3);
    ZD_CHECK_EQ(router.stats.dropped_events, 1);
}

static void test_click_grab_and_raise(void) {
    struct zd_input_router router;
    struct zd_window_create_info info;
    zd_window_id first, second;
    struct zd_input_delivery got;
    zd_window_id order[ZD_MAX_WINDOWS];

    setup();
    zd_input_router_init(&router, &wm);
    info = make_info(client_a, 10, 10, 200, 100);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &first));
    info = make_info(client_b, 300, 10, 200, 100);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &second));

    /* Focus starts on `second` (created last); click `first` raises it. */
    ZD_CHECK_EQ(zd_wm_keyboard_target(&wm), second);
    (void)zd_input_pointer_move(&router, 50, 40);
    got = zd_input_pointer_button(&router, 1, 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_WINDOW);
    ZD_CHECK_EQ(got.window, first);
    ZD_CHECK_EQ(zd_wm_keyboard_target(&wm), first);
    ZD_CHECK(zd_wm_is_above(&wm, first, second));
    ZD_CHECK_EQ(zd_input_pointer_focus(&router), first);

    /* Implicit grab: motion outside the window still delivers to it. */
    ZD_CHECK_EQ(router.pointer_grab, first);
    got = zd_input_pointer_move(&router, 1500, 900);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_WINDOW);
    ZD_CHECK_EQ(got.window, first);
    ZD_CHECK_EQ(router.pointer_grab, first);

    /* Release clears the grab; motion over `second` re-targets. */
    got = zd_input_pointer_button(&router, 1, 0);
    ZD_CHECK_EQ(got.window, first);
    ZD_CHECK_EQ(router.pointer_grab, ZD_INVALID_WINDOW);
    got = zd_input_pointer_move(&router, 310, 20);
    ZD_CHECK_EQ(got.window, second);

    /* Click on background: dropped, no grab. */
    (void)zd_input_pointer_move(&router, 1000, 800);
    got = zd_input_pointer_button(&router, 1, 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_DROPPED);
    ZD_CHECK_EQ(router.pointer_grab, ZD_INVALID_WINDOW);
    got = zd_input_pointer_button(&router, 1, 0);
    ZD_CHECK_EQ(got.result, ZD_INPUT_DROPPED);

    /* Stacking order sanity: first is above second after the click. */
    ZD_CHECK(zd_wm_stacking_order(&wm, order, ZD_MAX_WINDOWS) >= 2);
    ZD_CHECK_EQ(zd_wm_is_above(&wm, first, second), 1);
}

static void test_overlay_priority(void) {
    struct zd_input_router router;
    struct zd_window_create_info info;
    zd_window_id app, launcher;
    struct zd_input_delivery got;

    setup();
    zd_input_router_init(&router, &wm);
    info = make_info(client_a, 10, 10, 400, 300);
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &app));
    info = make_info(client_b, 400, 400, 300, 200);
    info.title = "launcher";
    ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &launcher));

    /* Keys follow window focus while no overlay is active. */
    got = zd_input_key(&router, 'a', 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_WINDOW);

    /* Overlay without keyboard claim does not steal keys. */
    ZD_CHECK_OK(zd_input_set_overlay(&router, launcher, 0));
    got = zd_input_key(&router, 'a', 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_WINDOW);

    /* Overlay with keyboard claim wins keys. */
    ZD_CHECK_OK(zd_input_set_overlay(&router, launcher, 1));
    got = zd_input_key(&router, 'a', 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_OVERLAY);
    ZD_CHECK_EQ(got.window, launcher);

    /* Pointer over the app still reaches the overlay first. */
    (void)zd_input_pointer_move(&router, 50, 50);
    got = zd_input_pointer_button(&router, 1, 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_OVERLAY);
    ZD_CHECK_EQ(got.window, launcher);
    ZD_CHECK(router.stats.overlay_deliveries >= 2);

    /* Invalid overlay id rejected; clearing restores normal routing. */
    ZD_CHECK_ERR(zd_input_set_overlay(&router, 9999, 1), ZD_ENOENT);
    ZD_CHECK_OK(zd_input_set_overlay(&router, ZD_INVALID_WINDOW, 1));
    got = zd_input_key(&router, 'a', 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_WINDOW);
    got = zd_input_pointer_button(&router, 1, 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_TO_WINDOW);
    ZD_CHECK_EQ(got.window, app);
}

static void test_no_wm_guards(void) {
    struct zd_input_router router;
    struct zd_input_delivery got;

    zd_input_router_init(&router, 0);
    got = zd_input_pointer_move(&router, 1, 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_DROPPED);
    got = zd_input_pointer_button(&router, 1, 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_DROPPED);
    got = zd_input_key(&router, 'a', 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_DROPPED);
    ZD_CHECK_EQ(zd_input_set_overlay(&router, 1, 1), -ZD_EINVAL);
    ZD_CHECK_EQ(zd_input_pointer_focus(&router), ZD_INVALID_WINDOW);
    ZD_CHECK_EQ(router.stats.dropped_events, 3);
    /* Button index out of range is dropped, not a crash. */
    zd_input_router_init(&router, 0);
    got = zd_input_pointer_button(&router, 0, 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_DROPPED);
}

static void test_key_without_focus(void) {
    struct zd_input_router router;
    struct zd_input_delivery got;

    setup();
    zd_input_router_init(&router, &wm);
    got = zd_input_key(&router, 0x100, 1);
    ZD_CHECK_EQ(got.result, ZD_INPUT_DROPPED);
    ZD_CHECK_EQ(router.stats.dropped_events, 1);
    ZD_CHECK_EQ(router.stats.key_events, 1);
}

void zd_test_input_suite(void) {
    zd_test_current = "input";
    test_move_and_focus();
    test_click_grab_and_raise();
    test_overlay_priority();
    test_no_wm_guards();
    test_key_without_focus();
    zd_test_current = "main";
}
