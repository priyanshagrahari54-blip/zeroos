/* ZEROOS Native Lightweight Control Center (Section 23).
 *
 * Implements the 28 control and status sections and the dedicated
 * Lenovo G560 Dashboard with on-demand event-driven telemetry and
 * zero uncontrolled polling.
 * Zero heap allocation, fully host-testable.
 */
#ifndef ZEROOS_DESKTOP_CONTROL_CENTER_H
#define ZEROOS_DESKTOP_CONTROL_CENTER_H

#include <stdint.h>

enum zd_cc_section {
    ZD_CC_SEC_PERFORMANCE = 0,
    ZD_CC_SEC_CPU,
    ZD_CC_SEC_GPU,
    ZD_CC_SEC_RAM,
    ZD_CC_SEC_MEMORY_PRESSURE,
    ZD_CC_SEC_STORAGE,
    ZD_CC_SEC_HDD_HEALTH,
    ZD_CC_SEC_NETWORK,
    ZD_CC_SEC_WIFI,
    ZD_CC_SEC_BLUETOOTH,
    ZD_CC_SEC_AUDIO,
    ZD_CC_SEC_DISPLAY,
    ZD_CC_SEC_BRIGHTNESS,
    ZD_CC_SEC_NIGHT_MODE,
    ZD_CC_SEC_BATTERY,
    ZD_CC_SEC_CHARGING,
    ZD_CC_SEC_THERMAL,
    ZD_CC_SEC_FAN,
    ZD_CC_SEC_GAMING,
    ZD_CC_SEC_RECORDING,
    ZD_CC_SEC_SECURITY,
    ZD_CC_SEC_FIREWALL,
    ZD_CC_SEC_PERMISSIONS,
    ZD_CC_SEC_UPDATES,
    ZD_CC_SEC_RECOVERY,
    ZD_CC_SEC_DIAGNOSTICS,
    ZD_CC_SEC_DRIVERS,
    ZD_CC_SEC_ACCESSIBILITY,
    ZD_CC_SEC_THEMES,
    ZD_CC_SEC_ZERO_AI,
    ZD_CC_SEC_COUNT
};

/* G560 Dashboard Snapshot */
struct zd_g560_dashboard {
    int32_t cpu_temp_celsius;
    int32_t gpu_temp_celsius;
    char fan_state[16];           /* "QUIET", "BALANCED", "MAX" */
    uint32_t ram_used_mb;
    uint32_t ram_total_mb;
    uint32_t hdd_health_pct;      /* SMART health 0..100 */
    uint8_t battery_pct;
    uint8_t battery_charging;
    uint8_t wifi_connected;
    uint8_t bluetooth_connected;
    uint8_t audio_volume_pct;
    char display_resolution[16];  /* "1366x768" */
    char thermal_mode[16];        /* "BALANCED", "QUIET", "PERF" */
    char perf_mode[16];
    uint8_t video_accel_available;
    uint8_t drivers_ok;
};

struct zd_control_center {
    uint8_t active_section;
    uint8_t night_mode_enabled;
    uint8_t brightness_pct;
    uint8_t master_volume_pct;
    uint8_t gaming_mode_active;
    uint8_t recording_active;
    struct zd_g560_dashboard g560;
    struct {
        uint32_t section_switches;
        uint32_t telemetry_refreshes;
        uint32_t settings_changed;
    } stats;
};

void zd_cc_init(struct zd_control_center *cc);
int zd_cc_select_section(struct zd_control_center *cc, enum zd_cc_section sec);
int zd_cc_set_brightness(struct zd_control_center *cc, uint8_t pct);
int zd_cc_set_night_mode(struct zd_control_center *cc, int enable);
int zd_cc_set_volume(struct zd_control_center *cc, uint8_t pct);
int zd_cc_refresh_g560(struct zd_control_center *cc);

#endif /* ZEROOS_DESKTOP_CONTROL_CENTER_H */
