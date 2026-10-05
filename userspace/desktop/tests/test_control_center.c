/* ZEROOS Control Center & G560 Dashboard Tests (Section 23) */
#include <zeroos/desktop/control_center.h>
#include "test_harness.h"
#include <string.h>

void zd_test_control_center_suite(void) {
    zd_test_current = "control_center";
    printf(" suite: control_center\n");

    struct zd_control_center cc;
    zd_cc_init(&cc);

    /* Initial state */
    ZD_CHECK(cc.active_section == ZD_CC_SEC_PERFORMANCE);
    ZD_CHECK(cc.brightness_pct == 70);
    ZD_CHECK(cc.master_volume_pct == 50);

    /* Verify G560 Dashboard metrics */
    ZD_CHECK(cc.g560.ram_total_mb == 2048);
    ZD_CHECK(cc.g560.ram_used_mb <= 200); /* Low-memory target */
    ZD_CHECK(strcmp(cc.g560.display_resolution, "1366x768") == 0);
    ZD_CHECK(cc.g560.video_accel_available == 1);
    ZD_CHECK(cc.g560.drivers_ok == 1);

    /* Section switching */
    ZD_CHECK(zd_cc_select_section(&cc, ZD_CC_SEC_BATTERY) == 0);
    ZD_CHECK(cc.active_section == ZD_CC_SEC_BATTERY);
    ZD_CHECK(zd_cc_select_section(&cc, ZD_CC_SEC_FIREWALL) == 0);
    ZD_CHECK(cc.active_section == ZD_CC_SEC_FIREWALL);
    ZD_CHECK(cc.stats.section_switches == 2);

    /* Quick controls adjustment */
    ZD_CHECK(zd_cc_set_brightness(&cc, 85) == 0);
    ZD_CHECK(cc.brightness_pct == 85);

    ZD_CHECK(zd_cc_set_night_mode(&cc, 1) == 0);
    ZD_CHECK(cc.night_mode_enabled == 1);

    ZD_CHECK(zd_cc_set_volume(&cc, 75) == 0);
    ZD_CHECK(cc.master_volume_pct == 75);
    ZD_CHECK(cc.g560.audio_volume_pct == 75);

    /* Event-driven refresh */
    ZD_CHECK(zd_cc_refresh_g560(&cc) == 0);
    ZD_CHECK(cc.stats.telemetry_refreshes == 1);
}
