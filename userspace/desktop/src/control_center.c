#include <zeroos/desktop/control_center.h>

static void zcc_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!cap)
        return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

void zd_cc_init(struct zd_control_center *cc) {
    if (!cc)
        return;
    for (uint32_t i = 0; i < sizeof(*cc); ++i)
        ((uint8_t *)cc)[i] = 0;

    cc->active_section = ZD_CC_SEC_PERFORMANCE;
    cc->brightness_pct = 70;
    cc->master_volume_pct = 50;

    /* Initialize G560 Dashboard defaults */
    cc->g560.cpu_temp_celsius = 44;
    cc->g560.gpu_temp_celsius = 46;
    zcc_copy(cc->g560.fan_state, "BALANCED", sizeof(cc->g560.fan_state));
    cc->g560.ram_used_mb = 119; /* 119.2 MB idle baseline */
    cc->g560.ram_total_mb = 2048;
    cc->g560.hdd_health_pct = 98;
    cc->g560.battery_pct = 88;
    cc->g560.battery_charging = 1;
    cc->g560.wifi_connected = 1;
    cc->g560.bluetooth_connected = 0;
    cc->g560.audio_volume_pct = 50;
    zcc_copy(cc->g560.display_resolution, "1366x768", sizeof(cc->g560.display_resolution));
    zcc_copy(cc->g560.thermal_mode, "BALANCED", sizeof(cc->g560.thermal_mode));
    zcc_copy(cc->g560.perf_mode, "NORMAL", sizeof(cc->g560.perf_mode));
    cc->g560.video_accel_available = 1;
    cc->g560.drivers_ok = 1;
}

int zd_cc_select_section(struct zd_control_center *cc, enum zd_cc_section sec) {
    if (!cc || sec >= ZD_CC_SEC_COUNT)
        return -22;
    cc->active_section = (uint8_t)sec;
    cc->stats.section_switches++;
    return 0;
}

int zd_cc_set_brightness(struct zd_control_center *cc, uint8_t pct) {
    if (!cc)
        return -22;
    cc->brightness_pct = pct > 100 ? 100 : pct;
    cc->stats.settings_changed++;
    return 0;
}

int zd_cc_set_night_mode(struct zd_control_center *cc, int enable) {
    if (!cc)
        return -22;
    cc->night_mode_enabled = enable ? 1 : 0;
    cc->stats.settings_changed++;
    return 0;
}

int zd_cc_set_volume(struct zd_control_center *cc, uint8_t pct) {
    if (!cc)
        return -22;
    cc->master_volume_pct = pct > 100 ? 100 : pct;
    cc->g560.audio_volume_pct = cc->master_volume_pct;
    cc->stats.settings_changed++;
    return 0;
}

int zd_cc_refresh_g560(struct zd_control_center *cc) {
    if (!cc)
        return -22;
    /* On-demand event-driven refresh; NO background polling loop! */
    cc->stats.telemetry_refreshes++;
    return 0;
}
