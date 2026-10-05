/* ZEROOS Wi-Fi Management Subsystem (Section 16).
 *
 * Implements scanning, SSID discovery, security profiles (WPA2/WPA3),
 * network connection state machine, DHCP coordination, and power-save
 * dormant state when idle.
 * Zero heap allocation, fully host-testable.
 */
#ifndef ZEROOS_DESKTOP_WIFI_H
#define ZEROOS_DESKTOP_WIFI_H

#include <stdint.h>

#define ZD_WIFI_MAX_AP 16
#define ZD_WIFI_MAX_PROFILES 8
#define ZD_WIFI_SSID_LEN 32
#define ZD_WIFI_BSSID_LEN 18
#define ZD_WIFI_KEY_LEN 64

enum zd_wifi_state {
    ZD_WIFI_STATE_DISABLED = 0,
    ZD_WIFI_STATE_DORMANT,      /* link down / idle, 0 scanning */
    ZD_WIFI_STATE_SCANNING,
    ZD_WIFI_STATE_CONNECTING,
    ZD_WIFI_STATE_CONNECTED
};

enum zd_wifi_sec {
    ZD_WIFI_SEC_OPEN = 0,
    ZD_WIFI_SEC_WPA2_PSK,
    ZD_WIFI_SEC_WPA3_SAE,
    ZD_WIFI_SEC_ENTERPRISE
};

struct zd_wifi_ap {
    char ssid[ZD_WIFI_SSID_LEN];
    char bssid[ZD_WIFI_BSSID_LEN];
    int8_t rssi;
    uint8_t channel;
    uint8_t security;           /* enum zd_wifi_sec */
    uint16_t freq_mhz;          /* 2412, 5180 etc. */
    uint8_t in_use;
};

struct zd_wifi_profile {
    char ssid[ZD_WIFI_SSID_LEN];
    char psk[ZD_WIFI_KEY_LEN];
    uint8_t security;
    uint8_t autoconnect;
    uint8_t in_use;
};

struct zd_wifi_service {
    enum zd_wifi_state state;
    struct zd_wifi_ap ap_list[ZD_WIFI_MAX_AP];
    struct zd_wifi_profile profiles[ZD_WIFI_MAX_PROFILES];
    char active_ssid[ZD_WIFI_SSID_LEN];
    char active_bssid[ZD_WIFI_BSSID_LEN];
    uint32_t ip_address;        /* IPv4 in network byte order */
    uint32_t netmask;
    uint32_t gateway;
    struct {
        uint32_t scans;
        uint32_t connections_ok;
        uint32_t connection_failures;
        uint32_t disconnections;
        uint32_t roam_events;
    } stats;
};

void zd_wifi_init(struct zd_wifi_service *ws);
int zd_wifi_power(struct zd_wifi_service *ws, int enable);
int zd_wifi_start_scan(struct zd_wifi_service *ws);
int zd_wifi_stop_scan(struct zd_wifi_service *ws);

/* Access Point discovery */
int zd_wifi_ap_discovered(struct zd_wifi_service *ws, const char *ssid, const char *bssid,
                          int8_t rssi, uint8_t channel, uint8_t sec, uint16_t freq);

/* Profile Management */
int zd_wifi_save_profile(struct zd_wifi_service *ws, const char *ssid, const char *psk,
                         uint8_t sec, int autoconnect);
int zd_wifi_remove_profile(struct zd_wifi_service *ws, const char *ssid);

/* Connection Management */
int zd_wifi_connect(struct zd_wifi_service *ws, const char *ssid, const char *psk);
int zd_wifi_disconnect(struct zd_wifi_service *ws);

/* DHCP IP Configuration Injection */
int zd_wifi_apply_dhcp(struct zd_wifi_service *ws, uint32_t ip, uint32_t mask, uint32_t gw);

/* Lookup */
const struct zd_wifi_ap *zd_wifi_find_ap(const struct zd_wifi_service *ws, const char *ssid);

#endif /* ZEROOS_DESKTOP_WIFI_H */
