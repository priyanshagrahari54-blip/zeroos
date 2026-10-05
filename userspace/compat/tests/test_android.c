/* ZEROOS Android Compatibility Tests (Stage 8)
 *
 * Verifies demand-loading, 0-byte dormant footprint, app lifecycle,
 * permission gating, isolated path translation, bridges, and fault isolation.
 */
#include <stdio.h>
#include <string.h>
#include <zeroos/compat/android.h>

static int checks = 0, failures = 0;
static const char *current = "";

#define CHECK(expr)                                                          \
    do {                                                                     \
        ++checks;                                                            \
        if (!(expr)) {                                                       \
            ++failures;                                                      \
            fprintf(stderr, "FAIL %s:%d [%s] %s\n", __FILE__, __LINE__,      \
                    current, #expr);                                         \
        }                                                                    \
    } while (0)

#define RUN(fn)                                                              \
    do {                                                                     \
        current = #fn;                                                       \
        fn();                                                                \
    } while (0)

static void test_runtime_lifecycle(void) {
    struct zandroid_runtime rt;
    zandroid_init(&rt, "/data/android");

    /* Initial state is DORMANT with 0 resident bytes */
    CHECK(rt.state == ZANDROID_RT_DORMANT);
    CHECK(rt.total_resident_bytes == 0);

    /* Wake runtime to WARM */
    CHECK(zandroid_runtime_wake(&rt) == ZANDROID_OK);
    CHECK(rt.state == ZANDROID_RT_WARM);
    CHECK(rt.total_resident_bytes > 0);
    CHECK(rt.stats.runtime_wakeups == 1);

    /* Suspend runtime under memory pressure */
    CHECK(zandroid_runtime_suspend(&rt) == ZANDROID_OK);
    CHECK(rt.state == ZANDROID_RT_SUSPENDED);

    /* Full stop returns to DORMANT with 0 resident bytes */
    CHECK(zandroid_runtime_stop(&rt) == ZANDROID_OK);
    CHECK(rt.state == ZANDROID_RT_DORMANT);
    CHECK(rt.total_resident_bytes == 0);
}

static void test_app_lifecycle_and_dormancy(void) {
    struct zandroid_runtime rt;
    enum zandroid_app_state state;
    zandroid_init(&rt, "/data/android");

    struct zandroid_apk_info apk = {
        .package_name = "org.zeroos.calc",
        .min_sdk = 26,
        .target_sdk = 34,
        .version_code = 100,
        .requested_perms = ZANDROID_PERM_INTERNET | ZANDROID_PERM_POST_NOTIFICATIONS,
        .apk_size_bytes = 2048000
    };

    CHECK(zandroid_install_apk(&rt, &apk) == ZANDROID_OK);
    CHECK(zandroid_get_app_state(&rt, "org.zeroos.calc", &state) == ZANDROID_OK);
    CHECK(state == ZANDROID_APP_INSTALLED);
    CHECK(rt.state == ZANDROID_RT_DORMANT); /* installing does not wake ART */
    CHECK(rt.total_resident_bytes == 0);

    /* Starting app demand-wakes runtime and assigns memory */
    CHECK(zandroid_start_app(&rt, "org.zeroos.calc", 8 * 1024 * 1024) == ZANDROID_OK);
    CHECK(zandroid_get_app_state(&rt, "org.zeroos.calc", &state) == ZANDROID_OK);
    CHECK(state == ZANDROID_APP_RUNNING);
    CHECK(rt.state == ZANDROID_RT_ACTIVE);
    CHECK(rt.total_resident_bytes >= 8 * 1024 * 1024);

    /* Pause app */
    CHECK(zandroid_pause_app(&rt, "org.zeroos.calc") == ZANDROID_OK);
    CHECK(zandroid_get_app_state(&rt, "org.zeroos.calc", &state) == ZANDROID_OK);
    CHECK(state == ZANDROID_APP_PAUSED);

    /* Stop app */
    CHECK(zandroid_stop_app(&rt, "org.zeroos.calc") == ZANDROID_OK);
    CHECK(zandroid_get_app_state(&rt, "org.zeroos.calc", &state) == ZANDROID_OK);
    CHECK(state == ZANDROID_APP_STOPPED);

    /* Stop runtime completely -> 0 resident bytes */
    CHECK(zandroid_runtime_stop(&rt) == ZANDROID_OK);
    CHECK(rt.total_resident_bytes == 0);
    CHECK(rt.state == ZANDROID_RT_DORMANT);

    /* Uninstall app */
    CHECK(zandroid_uninstall_app(&rt, "org.zeroos.calc") == ZANDROID_OK);
    CHECK(zandroid_get_app_state(&rt, "org.zeroos.calc", &state) == ZANDROID_NOTFOUND);
}

static void test_permissions_matrix(void) {
    struct zandroid_runtime rt;
    zandroid_init(&rt, "/data/android");

    struct zandroid_apk_info apk = {
        .package_name = "org.zeroos.camera",
        .min_sdk = 28,
        .target_sdk = 34,
        .version_code = 1,
        .requested_perms = ZANDROID_PERM_INTERNET | ZANDROID_PERM_CAMERA | ZANDROID_PERM_RECORD_AUDIO
    };
    CHECK(zandroid_install_apk(&rt, &apk) == ZANDROID_OK);

    /* Normal permission: internet granted automatically */
    CHECK(zandroid_check_permission(&rt, "org.zeroos.camera", ZANDROID_PERM_INTERNET) == ZANDROID_OK);

    /* Dangerous runtime permission: camera denied initially */
    CHECK(zandroid_check_permission(&rt, "org.zeroos.camera", ZANDROID_PERM_CAMERA) == ZANDROID_PERMISSION_DENIED);
    CHECK(rt.stats.permissions_rejected == 1);

    /* Grant camera permission */
    CHECK(zandroid_grant_permission(&rt, "org.zeroos.camera", ZANDROID_PERM_CAMERA) == ZANDROID_OK);
    CHECK(zandroid_check_permission(&rt, "org.zeroos.camera", ZANDROID_PERM_CAMERA) == ZANDROID_OK);

    /* Revoke camera permission */
    CHECK(zandroid_revoke_permission(&rt, "org.zeroos.camera", ZANDROID_PERM_CAMERA) == ZANDROID_OK);
    CHECK(zandroid_check_permission(&rt, "org.zeroos.camera", ZANDROID_PERM_CAMERA) == ZANDROID_PERMISSION_DENIED);
    CHECK(rt.stats.permissions_rejected == 2);

    /* Cannot grant permission not requested in manifest */
    CHECK(zandroid_grant_permission(&rt, "org.zeroos.camera", ZANDROID_PERM_ACCESS_FINE_LOCATION) == ZANDROID_BADARG);
}

static void test_path_translation_and_sandboxing(void) {
    struct zandroid_runtime rt;
    char path[128];
    zandroid_init(&rt, "/data/android");

    struct zandroid_apk_info apk = {
        .package_name = "org.zeroos.notes",
        .min_sdk = 26,
        .target_sdk = 34
    };
    CHECK(zandroid_install_apk(&rt, &apk) == ZANDROID_OK);

    /* Shared storage translation */
    CHECK(zandroid_translate_path(&rt, "org.zeroos.notes", "/sdcard/docs/note.txt", path, sizeof(path)) == ZANDROID_OK);
    CHECK(strcmp(path, "/data/android/shared/docs/note.txt") == 0);

    /* App private storage translation */
    CHECK(zandroid_translate_path(&rt, "org.zeroos.notes", "/data/databases/app.db", path, sizeof(path)) == ZANDROID_OK);
    CHECK(strcmp(path, "/data/android/data/org.zeroos.notes/databases/app.db") == 0);

    /* Sandbox escape attempt via .. rejected */
    CHECK(zandroid_translate_path(&rt, "org.zeroos.notes", "/sdcard/../etc/passwd", path, sizeof(path)) == ZANDROID_UNSUPPORTED);
    CHECK(rt.stats.path_traversals_blocked == 1);

    /* Access outside allowed root rejected */
    CHECK(zandroid_translate_path(&rt, "org.zeroos.notes", "/etc/shadow", path, sizeof(path)) == ZANDROID_UNSUPPORTED);
    CHECK(rt.stats.path_traversals_blocked == 2);
}

static void test_bridges_and_fault_isolation(void) {
    struct zandroid_runtime rt;
    zandroid_init(&rt, "/data/android");

    struct zandroid_apk_info apk = {
        .package_name = "org.zeroos.messenger",
        .min_sdk = 26,
        .target_sdk = 34,
        .requested_perms = ZANDROID_PERM_INTERNET | ZANDROID_PERM_POST_NOTIFICATIONS
    };
    CHECK(zandroid_install_apk(&rt, &apk) == ZANDROID_OK);
    CHECK(zandroid_start_app(&rt, "org.zeroos.messenger", 4 * 1024 * 1024) == ZANDROID_OK);

    /* Graphics bridge */
    struct zandroid_gfx_frame frame = {
        .width = 1366,
        .height = 768,
        .stride = 1366,
        .damage_x = 0,
        .damage_y = 0,
        .damage_w = 400,
        .damage_h = 300,
        .frame_seq = 1
    };
    CHECK(zandroid_bridge_submit_frame(&rt, "org.zeroos.messenger", &frame) == ZANDROID_OK);
    CHECK(rt.stats.frames_rendered == 1);

    /* Notification bridge without permission fails */
    CHECK(zandroid_bridge_post_notification(&rt, "org.zeroos.messenger", 1, "Msg", "Hello", 2) == ZANDROID_PERMISSION_DENIED);

    /* Grant notification permission and post */
    CHECK(zandroid_grant_permission(&rt, "org.zeroos.messenger", ZANDROID_PERM_POST_NOTIFICATIONS) == ZANDROID_OK);
    CHECK(zandroid_bridge_post_notification(&rt, "org.zeroos.messenger", 1, "Msg", "Hello", 2) == ZANDROID_OK);
    CHECK(rt.stats.notifications_posted == 1);
    CHECK(zandroid_bridge_cancel_notification(&rt, "org.zeroos.messenger", 1) == ZANDROID_OK);

    /* Motion bridge */
    struct zandroid_motion_event motion = {
        .action = ZANDROID_MOTION_DOWN,
        .x = 200,
        .y = 150,
        .pointer_id = 0,
        .timestamp_ms = 1000
    };
    CHECK(zandroid_bridge_inject_motion(&rt, "org.zeroos.messenger", &motion) == ZANDROID_OK);

    /* Fault isolation: app crash does NOT kill runtime */
    CHECK(zandroid_crash_app(&rt, "org.zeroos.messenger") == ZANDROID_OK);
    enum zandroid_app_state st;
    CHECK(zandroid_get_app_state(&rt, "org.zeroos.messenger", &st) == ZANDROID_OK);
    CHECK(st == ZANDROID_APP_STOPPED);
    CHECK(rt.apps[0].crashes == 1);
    CHECK(rt.stats.apps_crashed == 1);
    CHECK(rt.state == ZANDROID_RT_WARM); /* runtime is still alive */
}

void test_android_suite(int *p_checks, int *p_failures) {
    RUN(test_runtime_lifecycle);
    RUN(test_app_lifecycle_and_dormancy);
    RUN(test_permissions_matrix);
    RUN(test_path_translation_and_sandboxing);
    RUN(test_bridges_and_fault_isolation);
    if (p_checks) *p_checks += checks;
    if (p_failures) *p_failures += failures;
}
