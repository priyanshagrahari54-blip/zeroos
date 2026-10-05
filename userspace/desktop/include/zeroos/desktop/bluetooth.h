/* ZEROOS Event-Driven Bluetooth & Earbuds Subsystem (Section 15).
 *
 * Implements discovery, pairing, trusted-device database, automatic reconnect,
 * A2DP/AVRCP audio routing, HFP/HSP, earbuds battery level telemetry,
 * and dormant zero-resource idle state when unused.
 * Zero heap allocation, fully host-testable.
 */
#ifndef ZEROOS_DESKTOP_BLUETOOTH_H
#define ZEROOS_DESKTOP_BLUETOOTH_H

#include <stdint.h>

#define ZD_BT_MAX_DEVICES 16
#define ZD_BT_ADDR_LEN 18    /* "XX:XX:XX:XX:XX:XX" */
#define ZD_BT_NAME_LEN 32

enum zd_bt_state {
    ZD_BT_STATE_DISABLED = 0,
    ZD_BT_STATE_DORMANT,     /* powered on, 0 active connections, 0 polling */
    ZD_BT_STATE_SCANNING,
    ZD_BT_STATE_CONNECTED
};

/* Profiles bitmask */
#define ZD_BT_PROFILE_A2DP    (1u << 0)  /* Advanced Audio Distribution */
#define ZD_BT_PROFILE_AVRCP   (1u << 1)  /* Audio/Video Remote Control */
#define ZD_BT_PROFILE_HFP_HSP (1u << 2)  /* Hands-Free / Headset */
#define ZD_BT_PROFILE_HID     (1u << 3)  /* Human Interface Device */

/* AVRCP commands */
enum zd_bt_avrcp_cmd {
    ZD_BT_AVRCP_PLAY = 1,
    ZD_BT_AVRCP_PAUSE,
    ZD_BT_AVRCP_STOP,
    ZD_BT_AVRCP_NEXT_TRACK,
    ZD_BT_AVRCP_PREV_TRACK,
    ZD_BT_AVRCP_VOL_UP,
    ZD_BT_AVRCP_VOL_DOWN
};

enum zd_bt_audio_route {
    ZD_BT_ROUTE_INTERNAL_SPEAKERS = 0,
    ZD_BT_ROUTE_BLUETOOTH_A2DP,
    ZD_BT_ROUTE_HEADPHONES_JACK
};

struct zd_bt_device {
    char address[ZD_BT_ADDR_LEN];
    char name[ZD_BT_NAME_LEN];
    int8_t rssi;
    uint8_t paired;
    uint8_t trusted;
    uint8_t connected;
    uint8_t battery_pct;       /* 0..100%, 0xFF if unsupported */
    uint32_t supported_profiles;
    uint32_t active_profiles;
    uint8_t in_use;
};

struct zd_bt_service {
    enum zd_bt_state state;
    enum zd_bt_audio_route audio_route;
    struct zd_bt_device devices[ZD_BT_MAX_DEVICES];
    char active_audio_device[ZD_BT_ADDR_LEN];
    struct {
        uint32_t scans_started;
        uint32_t devices_paired;
        uint32_t devices_unpaired;
        uint32_t connections;
        uint32_t disconnections;
        uint32_t audio_routed_to_bt;
        uint32_t audio_routed_to_internal;
        uint32_t avrcp_events;
        uint32_t battery_reports;
    } stats;
};

void zd_bt_init(struct zd_bt_service *bt);
int zd_bt_power(struct zd_bt_service *bt, int enable);
int zd_bt_start_scan(struct zd_bt_service *bt);
int zd_bt_stop_scan(struct zd_bt_service *bt);

/* Device discovery / advertisement ingestion */
int zd_bt_device_discovered(struct zd_bt_service *bt, const char *addr, const char *name,
                            int8_t rssi, uint32_t profiles);

/* Pairing & Trust */
int zd_bt_pair(struct zd_bt_service *bt, const char *addr, const char *pin);
int zd_bt_unpair(struct zd_bt_service *bt, const char *addr);
int zd_bt_set_trusted(struct zd_bt_service *bt, const char *addr, int trusted);

/* Connect / Disconnect */
int zd_bt_connect(struct zd_bt_service *bt, const char *addr);
int zd_bt_disconnect(struct zd_bt_service *bt, const char *addr);

/* Earbuds battery reporting */
int zd_bt_update_battery(struct zd_bt_service *bt, const char *addr, uint8_t battery_pct);

/* AVRCP command dispatch */
int zd_bt_send_avrcp(struct zd_bt_service *bt, const char *addr, enum zd_bt_avrcp_cmd cmd);

/* Device lookup */
const struct zd_bt_device *zd_bt_find_device(const struct zd_bt_service *bt, const char *addr);

#endif /* ZEROOS_DESKTOP_BLUETOOTH_H */
