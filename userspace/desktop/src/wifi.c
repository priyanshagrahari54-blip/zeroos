#include <zeroos/desktop/wifi.h>

static int zw_eq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a && b && a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return (!a && !b) || (a && b && a[i] == 0 && b[i] == 0);
}

static void zw_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!cap)
        return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static struct zd_wifi_ap *find_ap_mut(struct zd_wifi_service *ws, const char *ssid) {
    if (!ws || !ssid || !ssid[0])
        return 0;
    for (uint32_t i = 0; i < ZD_WIFI_MAX_AP; ++i) {
        if (ws->ap_list[i].in_use && zw_eq(ws->ap_list[i].ssid, ssid))
            return &ws->ap_list[i];
    }
    return 0;
}

static struct zd_wifi_profile *find_profile_mut(struct zd_wifi_service *ws, const char *ssid) {
    if (!ws || !ssid || !ssid[0])
        return 0;
    for (uint32_t i = 0; i < ZD_WIFI_MAX_PROFILES; ++i) {
        if (ws->profiles[i].in_use && zw_eq(ws->profiles[i].ssid, ssid))
            return &ws->profiles[i];
    }
    return 0;
}

void zd_wifi_init(struct zd_wifi_service *ws) {
    if (!ws)
        return;
    for (uint32_t i = 0; i < sizeof(*ws); ++i)
        ((uint8_t *)ws)[i] = 0;
    ws->state = ZD_WIFI_STATE_DORMANT;
}

int zd_wifi_power(struct zd_wifi_service *ws, int enable) {
    if (!ws)
        return -22;
    if (!enable) {
        zd_wifi_disconnect(ws);
        ws->state = ZD_WIFI_STATE_DISABLED;
    } else {
        ws->state = ZD_WIFI_STATE_DORMANT;
    }
    return 0;
}

int zd_wifi_start_scan(struct zd_wifi_service *ws) {
    if (!ws)
        return -22;
    if (ws->state == ZD_WIFI_STATE_DISABLED)
        return -1;
    ws->state = ZD_WIFI_STATE_SCANNING;
    ws->stats.scans++;
    return 0;
}

int zd_wifi_stop_scan(struct zd_wifi_service *ws) {
    if (!ws)
        return -22;
    if (ws->state != ZD_WIFI_STATE_SCANNING)
        return 0;
    ws->state = (ws->active_ssid[0]) ? ZD_WIFI_STATE_CONNECTED : ZD_WIFI_STATE_DORMANT;
    return 0;
}

int zd_wifi_ap_discovered(struct zd_wifi_service *ws, const char *ssid, const char *bssid,
                          int8_t rssi, uint8_t channel, uint8_t sec, uint16_t freq) {
    if (!ws || !ssid || !ssid[0])
        return -22;

    struct zd_wifi_ap *ap = find_ap_mut(ws, ssid);
    if (ap) {
        ap->rssi = rssi;
        ap->channel = channel;
        ap->security = sec;
        ap->freq_mhz = freq;
        if (bssid && bssid[0])
            zw_copy(ap->bssid, bssid, sizeof(ap->bssid));
        return 0;
    }

    for (uint32_t i = 0; i < ZD_WIFI_MAX_AP; ++i) {
        if (!ws->ap_list[i].in_use) {
            zw_copy(ws->ap_list[i].ssid, ssid, sizeof(ws->ap_list[i].ssid));
            if (bssid && bssid[0])
                zw_copy(ws->ap_list[i].bssid, bssid, sizeof(ws->ap_list[i].bssid));
            ws->ap_list[i].rssi = rssi;
            ws->ap_list[i].channel = channel;
            ws->ap_list[i].security = sec;
            ws->ap_list[i].freq_mhz = freq;
            ws->ap_list[i].in_use = 1;
            return 0;
        }
    }
    return -28; /* ENOSPC */
}

int zd_wifi_save_profile(struct zd_wifi_service *ws, const char *ssid, const char *psk,
                         uint8_t sec, int autoconnect) {
    if (!ws || !ssid || !ssid[0])
        return -22;

    struct zd_wifi_profile *prof = find_profile_mut(ws, ssid);
    if (prof) {
        if (psk)
            zw_copy(prof->psk, psk, sizeof(prof->psk));
        prof->security = sec;
        prof->autoconnect = autoconnect ? 1 : 0;
        return 0;
    }

    for (uint32_t i = 0; i < ZD_WIFI_MAX_PROFILES; ++i) {
        if (!ws->profiles[i].in_use) {
            zw_copy(ws->profiles[i].ssid, ssid, sizeof(ws->profiles[i].ssid));
            if (psk)
                zw_copy(ws->profiles[i].psk, psk, sizeof(ws->profiles[i].psk));
            ws->profiles[i].security = sec;
            ws->profiles[i].autoconnect = autoconnect ? 1 : 0;
            ws->profiles[i].in_use = 1;
            return 0;
        }
    }
    return -28;
}

int zd_wifi_remove_profile(struct zd_wifi_service *ws, const char *ssid) {
    if (!ws || !ssid)
        return -22;
    struct zd_wifi_profile *prof = find_profile_mut(ws, ssid);
    if (!prof)
        return -2;
    prof->in_use = 0;
    return 0;
}

int zd_wifi_connect(struct zd_wifi_service *ws, const char *ssid, const char *psk) {
    if (!ws || !ssid)
        return -22;
    if (ws->state == ZD_WIFI_STATE_DISABLED)
        return -1;

    struct zd_wifi_ap *ap = find_ap_mut(ws, ssid);
    if (!ap) {
        ws->stats.connection_failures++;
        return -2; /* AP not found in scan */
    }

    /* Validate security requirement */
    if (ap->security != ZD_WIFI_SEC_OPEN) {
        const char *key = psk;
        if (!key || !key[0]) {
            struct zd_wifi_profile *prof = find_profile_mut(ws, ssid);
            if (prof && prof->psk[0])
                key = prof->psk;
        }
        if (!key || !key[0]) {
            ws->stats.connection_failures++;
            return -13; /* EACCES: password required */
        }
    }

    zw_copy(ws->active_ssid, ssid, sizeof(ws->active_ssid));
    zw_copy(ws->active_bssid, ap->bssid, sizeof(ws->active_bssid));
    ws->state = ZD_WIFI_STATE_CONNECTED;
    ws->stats.connections_ok++;
    return 0;
}

int zd_wifi_disconnect(struct zd_wifi_service *ws) {
    if (!ws)
        return -22;
    if (ws->state != ZD_WIFI_STATE_CONNECTED)
        return 0;

    ws->active_ssid[0] = 0;
    ws->active_bssid[0] = 0;
    ws->ip_address = 0;
    ws->netmask = 0;
    ws->gateway = 0;
    ws->stats.disconnections++;
    ws->state = ZD_WIFI_STATE_DORMANT;
    return 0;
}

int zd_wifi_apply_dhcp(struct zd_wifi_service *ws, uint32_t ip, uint32_t mask, uint32_t gw) {
    if (!ws)
        return -22;
    if (ws->state != ZD_WIFI_STATE_CONNECTED)
        return -1; /* not connected */
    ws->ip_address = ip;
    ws->netmask = mask;
    ws->gateway = gw;
    return 0;
}

const struct zd_wifi_ap *zd_wifi_find_ap(const struct zd_wifi_service *ws, const char *ssid) {
    if (!ws || !ssid)
        return 0;
    for (uint32_t i = 0; i < ZD_WIFI_MAX_AP; ++i) {
        if (ws->ap_list[i].in_use && zw_eq(ws->ap_list[i].ssid, ssid))
            return &ws->ap_list[i];
    }
    return 0;
}
