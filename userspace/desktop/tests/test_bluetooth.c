/* ZEROOS Bluetooth & Earbuds Subsystem Tests (Section 15) */
#include <zeroos/desktop/bluetooth.h>
#include "test_harness.h"
#include <string.h>

void zd_test_bluetooth_suite(void) {
    zd_test_current = "bluetooth";
    printf(" suite: bluetooth\n");

    struct zd_bt_service bt;
    zd_bt_init(&bt);

    /* Initial state: powered on, dormant, internal speakers routed */
    ZD_CHECK(bt.state == ZD_BT_STATE_DORMANT);
    ZD_CHECK(bt.audio_route == ZD_BT_ROUTE_INTERNAL_SPEAKERS);

    /* Start scanning */
    ZD_CHECK(zd_bt_start_scan(&bt) == 0);
    ZD_CHECK(bt.state == ZD_BT_STATE_SCANNING);
    ZD_CHECK(bt.stats.scans_started == 1);

    /* Discovered Earbuds (A2DP + AVRCP + HFP/HSP) */
    const char *earbuds_addr = "12:34:56:78:9A:BC";
    uint32_t buds_profiles = ZD_BT_PROFILE_A2DP | ZD_BT_PROFILE_AVRCP | ZD_BT_PROFILE_HFP_HSP;
    ZD_CHECK(zd_bt_device_discovered(&bt, earbuds_addr, "Zero Buds Pro", -55, buds_profiles) == 0);

    const struct zd_bt_device *d = zd_bt_find_device(&bt, earbuds_addr);
    ZD_CHECK(d != NULL);
    ZD_CHECK(strcmp(d->name, "Zero Buds Pro") == 0);
    ZD_CHECK(d->paired == 0);

    /* Stop scan returns to dormant since nothing is connected */
    ZD_CHECK(zd_bt_stop_scan(&bt) == 0);
    ZD_CHECK(bt.state == ZD_BT_STATE_DORMANT);

    /* Cannot connect unpaired device */
    ZD_CHECK(zd_bt_connect(&bt, earbuds_addr) == -2);

    /* Pair device */
    ZD_CHECK(zd_bt_pair(&bt, earbuds_addr, "0000") == 0);
    ZD_CHECK(d->paired == 1);
    ZD_CHECK(d->trusted == 1);
    ZD_CHECK(bt.stats.devices_paired == 1);

    /* Connect Earbuds -> routes audio to Bluetooth A2DP */
    ZD_CHECK(zd_bt_connect(&bt, earbuds_addr) == 0);
    ZD_CHECK(d->connected == 1);
    ZD_CHECK(bt.state == ZD_BT_STATE_CONNECTED);
    ZD_CHECK(bt.audio_route == ZD_BT_ROUTE_BLUETOOTH_A2DP);
    ZD_CHECK(strcmp(bt.active_audio_device, earbuds_addr) == 0);
    ZD_CHECK(bt.stats.audio_routed_to_bt == 1);

    /* Battery level reporting */
    ZD_CHECK(zd_bt_update_battery(&bt, earbuds_addr, 85) == 0);
    ZD_CHECK(d->battery_pct == 85);
    ZD_CHECK(bt.stats.battery_reports == 1);

    /* AVRCP playback control */
    ZD_CHECK(zd_bt_send_avrcp(&bt, earbuds_addr, ZD_BT_AVRCP_PLAY) == 0);
    ZD_CHECK(zd_bt_send_avrcp(&bt, earbuds_addr, ZD_BT_AVRCP_NEXT_TRACK) == 0);
    ZD_CHECK(bt.stats.avrcp_events == 2);

    /* Disconnect Earbuds -> routes audio back to internal speakers and enters dormant */
    ZD_CHECK(zd_bt_disconnect(&bt, earbuds_addr) == 0);
    ZD_CHECK(d->connected == 0);
    ZD_CHECK(bt.audio_route == ZD_BT_ROUTE_INTERNAL_SPEAKERS);
    ZD_CHECK(bt.active_audio_device[0] == 0);
    ZD_CHECK(bt.stats.audio_routed_to_internal == 1);
    ZD_CHECK(bt.state == ZD_BT_STATE_DORMANT);

    /* Power off Bluetooth */
    ZD_CHECK(zd_bt_power(&bt, 0) == 0);
    ZD_CHECK(bt.state == ZD_BT_STATE_DISABLED);
}
