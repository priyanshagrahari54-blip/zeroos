#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

/* End-to-end desktop session scenario wiring every module together the
 * way the shell services will: lifecycle + governor + window manager +
 * compositor + search + settings + notifications + a11y + i18n +
 * watchdog, including a client crash, a service restart, pressure, and
 * suspend/resume. */

static struct zd_wm wm;
static struct zd_compositor compositor;
static struct zd_lifecycle lifecycle;
static struct zd_governor governor;
static struct zd_search search;
static struct zd_settings settings;
static struct zd_notify notify;
static struct zd_a11y a11y;
static struct zd_i18n i18n;
static struct zd_watchdog watchdog;
static struct zd_monitor monitor0;
static uint32_t scene_fb[800 * 600];
static uint32_t app_fb[128 * 128];
static zd_client_id client_shell;
static zd_client_id client_app;
static int session_wd_degraded;

static const uint32_t *session_pixels(void *context, zd_window_id id,
                                      int32_t *width, int32_t *height,
                                      int32_t *stride_px) {
    (void)context;
    (void)id;
    *width = 128;
    *height = 128;
    *stride_px = 128;
    return app_fb;
}

static void session_on_degraded(void *context) {
    (void)context;
    ++session_wd_degraded;
}

static uint32_t settings_search_available(void *context) {
    (void)context;
    return 1;
}

static int settings_search_query(void *context,
                                 const struct zd_intent *intent,
                                 struct zd_search_result *results,
                                 uint32_t capacity,
                                 volatile uint32_t *cancel_token,
                                 uint32_t query_generation) {
    struct zd_settings *store = (struct zd_settings *)context;
    const char *keys[8];
    uint32_t count;
    uint32_t index;
    (void)intent;
    (void)cancel_token;
    (void)query_generation;
    if (!store || capacity == 0)
        return -ZD_EINVAL;
    count = zd_settings_search(store, intent->token_count ?
                               intent->tokens[0] : "", keys, 8);
    if (count > capacity)
        count = capacity;
    for (index = 0; index < count; ++index) {
        memset(&results[index], 0, sizeof(results[index]));
        results[index].document_id = zd_hash64(keys[index]);
        results[index].kind = ZD_SEARCH_SETTING;
        snprintf(results[index].label, sizeof(results[index].label), "%s",
                 keys[index]);
        results[index].available = 1;
    }
    return (int)count;
}

static void session_setup(void) {
    struct zd_window_create_info info;
    struct zd_frame_policy policy;
    struct zd_capabilities caps;
    struct zd_setting_def def;
    struct zd_search_provider settings_provider;
    struct zd_a11y_profile profile;
    uint32_t index;
    uint32_t wd_id = 0;

    memset(&lifecycle, 0, sizeof(lifecycle));
    zd_lifecycle_init(&lifecycle);

    memset(&caps, 0, sizeof(caps));
    caps.cpu_count = 4;
    caps.ram_mb = 8192;
    caps.gpu_tier = 2;
    caps.display_width = 800;
    caps.display_height = 600;
    caps.refresh_mhz = 60000;
    caps.hardware_accel = 1;
    zd_governor_init(&governor, &caps);

    zd_wm_init(&wm);
    memset(&monitor0, 0, sizeof(monitor0));
    monitor0.id = 1;
    monitor0.bounds.w = 800;
    monitor0.bounds.h = 600;
    monitor0.scale_percent = 100;
    monitor0.primary = 1;
    monitor0.enabled = 1;
    strcpy(monitor0.name, "session");
    ZD_CHECK_OK(zd_wm_add_monitor(&wm, &monitor0));
    client_shell = zd_wm_register_client(&wm);
    client_app = zd_wm_register_client(&wm);

    memset(&info, 0, sizeof(info));
    info.client = client_shell;
    info.title = "ZERO Shell";
    info.a11y_label = "ZERO Shell";
    info.role = ZD_ROLE_APPLICATION;
    info.logical_rect.x = 0;
    info.logical_rect.y = 0;
    info.logical_rect.w = 800;
    info.logical_rect.h = 560;
    info.monitor_id = 1;
    info.resizable = 1;
    {
        zd_window_id shell_window = ZD_INVALID_WINDOW;
        ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &shell_window));
    }

    memset(&info, 0, sizeof(info));
    info.client = client_app;
    info.title = "Editor";
    info.logical_rect.x = 100;
    info.logical_rect.y = 100;
    info.logical_rect.w = 128;
    info.logical_rect.h = 128;
    info.monitor_id = 1;
    info.resizable = 1;
    info.role = ZD_ROLE_WINDOW;
    {
        zd_window_id app_window = ZD_INVALID_WINDOW;
        ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &app_window));
        ZD_CHECK_OK(zd_wm_focus(&wm, app_window));
    }

    memset(&policy, 0, sizeof(policy));
    policy.refresh_mhz = 60000;
    zd_compositor_init(&compositor, &wm, &policy);
    ZD_CHECK_OK(zd_compositor_set_pixel_source(&compositor, session_pixels,
                                               0));
    for (index = 0; index < 128U * 128U; ++index)
        app_fb[index] = 0xFF44AA44U;

    zd_settings_init(&settings);
    memset(&def, 0, sizeof(def));
    def.key = "ui.scale_percent";
    def.type = ZD_SETTING_INT;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 100;
    def.min_value = 100;
    def.max_value = 300;
    def.description = "interface scale";
    ZD_CHECK_OK(zd_settings_register(&settings, &def));
    memset(&def, 0, sizeof(def));
    def.key = "ui.reduced_motion";
    def.type = ZD_SETTING_BOOL;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 0;
    def.description = "accessibility motion";
    ZD_CHECK_OK(zd_settings_register(&settings, &def));

    zd_search_init(&search);
    memset(&settings_provider, 0, sizeof(settings_provider));
    settings_provider.name = "settings-live";
    settings_provider.kind_mask = 1U << ZD_SEARCH_SETTING;
    settings_provider.priority = 7;
    settings_provider.available = settings_search_available;
    settings_provider.query = settings_search_query;
    settings_provider.context = &settings;
    ZD_CHECK_OK(zd_search_add_provider(&search, &settings_provider));
    ZD_CHECK_OK(zd_search_index_upsert(&search, ZD_INDEX_APP, "editor",
                                       "Editor"));

    zd_notify_init(&notify);
    zd_a11y_init(&a11y);
    memset(&profile, 0, sizeof(profile));
    profile.screen_reader_enabled = 1;
    zd_a11y_set_profile(&a11y, &profile);
    {
        uint32_t node = 0;
        ZD_CHECK_OK(zd_a11y_create(&a11y, ZD_A11Y_ROOT, ZD_ROLE_BAR,
                                   "ZERO Bar", 0, 0, &node));
        ZD_CHECK_OK(zd_a11y_create(&a11y, node, ZD_ROLE_BUTTON, "Launcher",
                                   ZD_A11Y_FOCUSABLE, 0, &node));
        ZD_CHECK_OK(zd_a11y_create(&a11y, node, ZD_ROLE_BUTTON, "Search",
                                   ZD_A11Y_FOCUSABLE, 0, &node));
    }

    zd_i18n_init(&i18n, ZD_LOCALE_HI);
    ZD_CHECK(zd_i18n_load_shell_catalog(&i18n) > 0U);

    zd_watchdog_init(&watchdog);
    ZD_CHECK_OK(zd_watchdog_set_listener(&watchdog, 0, session_on_degraded,
                                         0));
    ZD_CHECK_OK(zd_watchdog_register(&watchdog, "search", ZD_WD_ON_FAILURE, 2,
                                     0, 1000000ULL, 8000000ULL, &wd_id));
}

static void test_session_lifecycle_to_active(void) {
    session_setup();
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_START, 0));
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_CONTROLLERS_UP,
                                      1));
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_ACTIVATE, 2));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_ACTIVE);
    ZD_CHECK_EQ(governor.tier, ZD_TIER_RICH);
    ZD_CHECK(zd_lifecycle_allows_heavy_work(&lifecycle));
}

static void test_session_present_cycle(void) {
    struct zd_bitmap target;
    zd_window_id focused;
    target.pixels = scene_fb;
    target.width = 800;
    target.height = 600;
    target.stride_px = 800;
    memset(scene_fb, 0x11, sizeof(scene_fb));

    zd_compositor_sync_scene(&compositor);
    ZD_CHECK(compositor.node_count >= 2U);
    focused = zd_wm_keyboard_target(&wm);
    ZD_CHECK(focused != ZD_INVALID_WINDOW);

    /* App renders a frame: queue buffer, damage, present. */
    {
        zd_buffer_generation generation = 0;
        ZD_CHECK_EQ(zd_wm_buffer_state(&wm, focused, &generation),
                    ZD_BUFFER_FREE);
        ZD_CHECK_OK(zd_wm_queue_buffer(&wm, focused, generation));
        ZD_CHECK_OK(zd_compositor_damage_full(&compositor, focused));
        ZD_CHECK_OK(zd_compositor_begin_frame(&compositor, 1000000ULL));
        ZD_CHECK(zd_compositor_present(&compositor, target, 1000000ULL) > 0);
        ZD_CHECK_OK(zd_wm_release_buffer(&wm, focused, generation));
    }
    /* Idle frame: nothing to do, no pixels written. */
    ZD_CHECK_EQ(zd_compositor_present(&compositor, target, 2000000ULL), 0);
    ZD_CHECK_EQ(compositor.stats.frames_presented, 1U);
}

static void test_session_search_across_providers(void) {
    struct zd_search_result results[16];
    uint32_t count = 0;
    /* Settings provider returns the live schema key. */
    ZD_CHECK_OK(zd_search_query(&search, "set: scale", results, 16, &count));
    ZD_CHECK(count >= 1U);
    ZD_CHECK_EQ(results[0].kind, ZD_SEARCH_SETTING);
    /* Index provider returns installed apps. */
    ZD_CHECK_OK(zd_search_query(&search, "app: editor", results, 16, &count));
    ZD_CHECK(count >= 1U);
    ZD_CHECK(strcmp(results[0].label, "Editor") == 0);
    /* Unknown query: empty but successful (UI shows the empty state). */
    ZD_CHECK_OK(zd_search_query(&search, "zzz-not-a-thing", results, 16,
                                &count));
    ZD_CHECK_EQ(count, 0U);
    /* Indexer pauses under pressure without breaking queries. */
    zd_search_index_set_paused(&search, 1);
    ZD_CHECK_OK(zd_search_query(&search, "editor", results, 16, &count));
    ZD_CHECK(count >= 1U);
    zd_search_index_set_paused(&search, 0);
}

static void test_session_notification_flow(void) {
    struct zd_notify_post post;
    zd_notification_id id = 0;
    memset(&post, 0, sizeof(post));
    post.app_id = "search";
    post.category = "index";
    post.title = "Index ready";
    post.body = "3 apps indexed";
    post.priority = ZD_NOTIFY_HIGH;
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 5000000ULL, &id));
    /* Accessibility maps HIGH to a polite announcement. */
    ZD_CHECK_EQ(zd_notify_a11y_policy(post.priority), ZD_NOTIFY_A11Y_POLITE);
    ZD_CHECK_OK(zd_a11y_announce(&a11y, ZD_A11Y_ROOT, post.title,
                                 ZD_A11Y_URGENCY_NORMAL));
    {
        struct zd_a11y_announcement announcement;
        ZD_CHECK_OK(zd_a11y_next_announcement(&a11y, &announcement));
        ZD_CHECK(strcmp(announcement.text, "Index ready") == 0);
    }
    /* Hindi shell label available for the notification center. */
    ZD_CHECK(strcmp(zd_i18n_text(&i18n, "notify.center_title"),
                    "Notification center") != 0);
}

static void test_session_client_crash_recovery(void) {
    zd_window_id doomed[2];
    uint32_t count;
    zd_client_id victim = client_app;
    /* App has one window; create a second owned by the same client. */
    {
        struct zd_window_create_info info;
        memset(&info, 0, sizeof(info));
        info.client = victim;
        info.title = "dialog";
        info.logical_rect.x = 300;
        info.logical_rect.y = 300;
        info.logical_rect.w = 100;
        info.logical_rect.h = 100;
        info.monitor_id = 1;
        info.resizable = 1;
        ZD_CHECK_OK(zd_wm_create_window(&wm, &info, &doomed[1]));
    }
    count = zd_wm_windows_for_client(&wm, victim, doomed, 2);
    ZD_CHECK(count >= 1U);

    /* Crash: compositor drops resources, WM destroys windows, focus
     * falls back to the shell window, watchdog schedules a restart. */
    zd_compositor_drop_client(&compositor, victim);
    ZD_CHECK(zd_wm_client_crashed(&wm, victim) >= 1U);
    zd_compositor_sync_scene(&compositor);
    {
        zd_window_id remaining = zd_wm_keyboard_target(&wm);
        const struct zd_window *window = zd_wm_window_const(&wm, remaining);
        ZD_CHECK(window != 0);
        if (window)
            ZD_CHECK_EQ(window->client, client_shell);
    }
    {
        struct zd_watchdog_result result;
        uint32_t wd_id = watchdog.services[0].id;
        ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, wd_id, 10000000ULL));
        ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, wd_id, 10000010ULL, 1,
                                          &result));
        ZD_CHECK_EQ(result.decision, ZD_WD_RESTART);
        ZD_CHECK(result.delay_ns > 0);
        /* Backoff due: restart decision fires (event-driven). */
        ZD_CHECK_OK(zd_watchdog_evaluate(&watchdog, wd_id,
                                         10000010ULL + result.delay_ns,
                                         &result));
        ZD_CHECK_EQ(result.decision, ZD_WD_RESTART);
        ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, wd_id,
                                           11000000ULL));
    }
    /* Kernel-side invariant: the session objects survived. */
    ZD_CHECK(zd_lifecycle_state(&lifecycle) != 0); /* still a valid state */
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_ACTIVE);
}

static void test_session_pressure_and_suspend(void) {
    /* Pressure ladder engages; optional work (indexer) pauses first. */
    ZD_CHECK_OK(zd_governor_set_pressure(&governor, ZD_PRESSURE_HIGH, 0));
    ZD_CHECK((governor.active_actions & ZD_GOVERNOR_PAUSE_OPTIONAL) != 0);
    ZD_CHECK((governor.active_actions & ZD_GOVERNOR_RECLAIM_CACHES) != 0);
    ZD_CHECK((governor.active_actions & ZD_GOVERNOR_RECOVERY) == 0);
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_PRESSURE, 0));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_THROTTLED);
    ZD_CHECK(!zd_lifecycle_allows_heavy_work(&lifecycle));

    /* Suspend then resume back through the documented chain. */
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_SUSPEND, 0));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_SUSPENDED);
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_RESUME, 0));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_THROTTLED);
    ZD_CHECK_OK(zd_governor_set_pressure(&governor, ZD_PRESSURE_NONE, 0));
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle,
                                      ZD_LIFECYCLE_PRESSURE_RELEASED, 0));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_ACTIVE);
    /* Recovery never invoked without necessity. */
    ZD_CHECK_EQ(governor.recovery_invocations, 0U);
    ZD_CHECK_EQ(session_wd_degraded, 0);
}

static void test_session_keyboard_navigation_sweep(void) {
    uint32_t first = zd_a11y_focus_next(&a11y);
    uint32_t second = zd_a11y_focus_next(&a11y);
    uint32_t third = zd_a11y_focus_next(&a11y);
    uint32_t fourth = zd_a11y_focus_next(&a11y);
    ZD_CHECK(first != ZD_A11Y_INVALID);
    ZD_CHECK(second != ZD_A11Y_INVALID && second != first);
    /* Exactly two focusable bar controls: traversal visits both, wraps,
     * and revisits them in a stable cycle. */
    ZD_CHECK_EQ(third, first);
    ZD_CHECK_EQ(fourth, second);
    /* Reverse direction lands back on the other control. */
    ZD_CHECK_EQ(zd_a11y_focus_prev(&a11y), first);
}

static void test_notification_badge_sync(void) {
    /* Shell contract: the ZERO Bar badge mirrors the visible
     * notification count; dismissals propagate on the next sync. */
    struct zd_notify notify;
    struct zd_bar bar;
    struct zd_notify_post post;
    zd_notification_id ids[8];
    uint32_t visible;

    zd_notify_init(&notify);
    ZD_CHECK_OK(zd_bar_init(&bar, 1280, 1));

    memset(&post, 0, sizeof(post));
    post.app_id = "mail";
    post.title = "one";
    post.body = "b";
    post.priority = ZD_NOTIFY_NORMAL;
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 1000, &ids[0]));
    post.title = "two";
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 2000, &ids[1]));

    visible = zd_notify_visible(&notify, 3000, ids, 8);
    zd_bar_set_notif_count(&bar, visible);
    ZD_CHECK_EQ(bar.notif_count, 2U);

    ZD_CHECK_OK(zd_notify_dismiss(&notify, ids[0]));
    visible = zd_notify_visible(&notify, 4000, ids, 8);
    zd_bar_set_notif_count(&bar, visible);
    ZD_CHECK_EQ(bar.notif_count, 1U);

    ZD_CHECK_OK(zd_notify_dismiss(&notify, ids[1]));
    visible = zd_notify_visible(&notify, 5000, ids, 8);
    zd_bar_set_notif_count(&bar, visible);
    ZD_CHECK_EQ(bar.notif_count, 0U);
}

void zd_test_integration_suite(void) {
    printf(" suite: integrated session\n");
    ZD_RUN(test_session_lifecycle_to_active);
    ZD_RUN(test_session_present_cycle);
    ZD_RUN(test_session_search_across_providers);
    ZD_RUN(test_session_notification_flow);
    ZD_RUN(test_session_client_crash_recovery);
    ZD_RUN(test_session_pressure_and_suspend);
    ZD_RUN(test_session_keyboard_navigation_sweep);
    ZD_RUN(test_notification_badge_sync);
}
