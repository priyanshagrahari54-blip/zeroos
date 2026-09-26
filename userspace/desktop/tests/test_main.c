#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

int zd_test_failures = 0;
int zd_test_checks = 0;
const char *zd_test_current = "main";

/* Declared per suite. */
void zd_test_lifecycle_suite(void);
void zd_test_governor_suite(void);
void zd_test_window_suite(void);
void zd_test_compositor_suite(void);
void zd_test_search_suite(void);
void zd_test_settings_suite(void);
void zd_test_notify_suite(void);
void zd_test_a11y_suite(void);
void zd_test_i18n_suite(void);
void zd_test_watchdog_suite(void);
void zd_test_input_suite(void);
void zd_test_display_suite(void);
void zd_test_automation_suite(void);
void zd_test_browser_suite(void);
void zd_test_bar_suite(void);
void zd_test_launcher_suite(void);
void zd_test_capability_suite(void);
void zd_test_metrics_suite(void);
void zd_test_update_suite(void);
void zd_test_url_suite(void);
void zd_test_study_suite(void);
void zd_test_snapshot_suite(void);
void zd_test_vault_suite(void);
void zd_test_nav_suite(void);
void zd_test_firewall_suite(void);
void zd_test_sandbox_suite(void);
void zd_test_clipboard_suite(void);
void zd_test_downloads_suite(void);
void zd_test_providers_suite(void);
void zd_test_perfcenter_suite(void);
void zd_test_stress_suite(void);
void zd_test_fps_suite(void);
void zd_test_ai_suite(void);
void zd_test_integration_suite(void);

int main(void) {
    printf("ZEROOS desktop platform core tests\n");
    zd_test_lifecycle_suite();
    zd_test_governor_suite();
    zd_test_window_suite();
    zd_test_compositor_suite();
    zd_test_search_suite();
    zd_test_settings_suite();
    zd_test_notify_suite();
    zd_test_a11y_suite();
    zd_test_i18n_suite();
    zd_test_watchdog_suite();
    zd_test_input_suite();
    zd_test_display_suite();
    zd_test_automation_suite();
    zd_test_browser_suite();
    zd_test_bar_suite();
    zd_test_launcher_suite();
    zd_test_capability_suite();
    zd_test_metrics_suite();
    zd_test_update_suite();
    zd_test_url_suite();
    zd_test_study_suite();
    zd_test_snapshot_suite();
    zd_test_vault_suite();
    zd_test_nav_suite();
    zd_test_firewall_suite();
    zd_test_sandbox_suite();
    zd_test_clipboard_suite();
    zd_test_downloads_suite();
    zd_test_providers_suite();
    zd_test_perfcenter_suite();
    zd_test_stress_suite();
    zd_test_fps_suite();
    zd_test_ai_suite();
    zd_test_integration_suite();
    printf("checks=%d failures=%d\n", zd_test_checks, zd_test_failures);
    if (zd_test_failures) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
