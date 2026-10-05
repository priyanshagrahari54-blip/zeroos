/* ZEROOS Wi-Fi Subsystem Tests (Section 16) */
#include <zeroos/desktop/wifi.h>
#include "test_harness.h"
#include <string.h>

void zd_test_wifi_suite(void) {
    zd_test_current = "wifi";
    printf(" suite: wifi\n");

    struct zd_wifi_service ws;
    zd_wifi_init(&ws);

    ZD_CHECK(ws.state == ZD_WIFI_STATE_DORMANT);
    ZD_CHECK(ws.active_ssid[0] == 0);

    /* Start scan */
    ZD_CHECK(zd_wifi_start_scan(&ws) == 0);
    ZD_CHECK(ws.state == ZD_WIFI_STATE_SCANNING);
    ZD_CHECK(ws.stats.scans == 1);

    /* Discover APs */
    ZD_CHECK(zd_wifi_ap_discovered(&ws, "ZeroHome", "00:11:22:33:44:55", -60, 6, ZD_WIFI_SEC_WPA2_PSK, 2437) == 0);
    ZD_CHECK(zd_wifi_ap_discovered(&ws, "Guest-Free", "00:11:22:33:44:66", -75, 1, ZD_WIFI_SEC_OPEN, 2412) == 0);

    const struct zd_wifi_ap *ap = zd_wifi_find_ap(&ws, "ZeroHome");
    ZD_CHECK(ap != NULL);
    ZD_CHECK(ap->security == ZD_WIFI_SEC_WPA2_PSK);

    /* Connecting to secured network without PSK fails */
    ZD_CHECK(zd_wifi_connect(&ws, "ZeroHome", NULL) == -13);
    ZD_CHECK(ws.stats.connection_failures == 1);

    /* Save profile with passphrase */
    ZD_CHECK(zd_wifi_save_profile(&ws, "ZeroHome", "SuperSecretKey", ZD_WIFI_SEC_WPA2_PSK, 1) == 0);

    /* Connect to ZeroHome using saved profile */
    ZD_CHECK(zd_wifi_connect(&ws, "ZeroHome", NULL) == 0);
    ZD_CHECK(ws.state == ZD_WIFI_STATE_CONNECTED);
    ZD_CHECK(strcmp(ws.active_ssid, "ZeroHome") == 0);
    ZD_CHECK(ws.stats.connections_ok == 1);

    /* Apply DHCP IP address (192.168.1.50) */
    ZD_CHECK(zd_wifi_apply_dhcp(&ws, 0xc0a80132U, 0xffffff00U, 0xc0a80101U) == 0);
    ZD_CHECK(ws.ip_address == 0xc0a80132U);

    /* Disconnect returns to dormant state */
    ZD_CHECK(zd_wifi_disconnect(&ws) == 0);
    ZD_CHECK(ws.state == ZD_WIFI_STATE_DORMANT);
    ZD_CHECK(ws.active_ssid[0] == 0);
    ZD_CHECK(ws.ip_address == 0);
    ZD_CHECK(ws.stats.disconnections == 1);

    /* Power off Wi-Fi */
    ZD_CHECK(zd_wifi_power(&ws, 0) == 0);
    ZD_CHECK(ws.state == ZD_WIFI_STATE_DISABLED);
}
