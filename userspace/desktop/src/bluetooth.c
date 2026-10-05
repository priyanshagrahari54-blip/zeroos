#include <zeroos/desktop/bluetooth.h>

static int zbt_eq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a && b && a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return (!a && !b) || (a && b && a[i] == 0 && b[i] == 0);
}

static void zbt_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!cap)
        return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static struct zd_bt_device *find_dev_mut(struct zd_bt_service *bt, const char *addr) {
    if (!bt || !addr || !addr[0])
        return 0;
    for (uint32_t i = 0; i < ZD_BT_MAX_DEVICES; ++i) {
        if (bt->devices[i].in_use && zbt_eq(bt->devices[i].address, addr))
            return &bt->devices[i];
    }
    return 0;
}

void zd_bt_init(struct zd_bt_service *bt) {
    if (!bt)
        return;
    for (uint32_t i = 0; i < sizeof(*bt); ++i)
        ((uint8_t *)bt)[i] = 0;
    bt->state = ZD_BT_STATE_DORMANT; /* Powered on, dormant by default */
    bt->audio_route = ZD_BT_ROUTE_INTERNAL_SPEAKERS;
}

int zd_bt_power(struct zd_bt_service *bt, int enable) {
    if (!bt)
        return -22;
    if (!enable) {
        /* Disconnect all active devices */
        for (uint32_t i = 0; i < ZD_BT_MAX_DEVICES; ++i) {
            if (bt->devices[i].in_use && bt->devices[i].connected) {
                bt->devices[i].connected = 0;
                bt->devices[i].active_profiles = 0;
            }
        }
        bt->audio_route = ZD_BT_ROUTE_INTERNAL_SPEAKERS;
        bt->active_audio_device[0] = 0;
        bt->state = ZD_BT_STATE_DISABLED;
    } else {
        bt->state = ZD_BT_STATE_DORMANT;
    }
    return 0;
}

int zd_bt_start_scan(struct zd_bt_service *bt) {
    if (!bt)
        return -22;
    if (bt->state == ZD_BT_STATE_DISABLED)
        return -1; /* power is off */
    bt->state = ZD_BT_STATE_SCANNING;
    bt->stats.scans_started++;
    return 0;
}

int zd_bt_stop_scan(struct zd_bt_service *bt) {
    if (!bt)
        return -22;
    if (bt->state != ZD_BT_STATE_SCANNING)
        return 0;

    int any_connected = 0;
    for (uint32_t i = 0; i < ZD_BT_MAX_DEVICES; ++i) {
        if (bt->devices[i].in_use && bt->devices[i].connected)
            any_connected = 1;
    }
    bt->state = any_connected ? ZD_BT_STATE_CONNECTED : ZD_BT_STATE_DORMANT;
    return 0;
}

int zd_bt_device_discovered(struct zd_bt_service *bt, const char *addr, const char *name,
                            int8_t rssi, uint32_t profiles) {
    if (!bt || !addr || !addr[0])
        return -22;

    struct zd_bt_device *dev = find_dev_mut(bt, addr);
    if (dev) {
        dev->rssi = rssi;
        if (name && name[0])
            zbt_copy(dev->name, name, sizeof(dev->name));
        dev->supported_profiles |= profiles;
        return 0;
    }

    for (uint32_t i = 0; i < ZD_BT_MAX_DEVICES; ++i) {
        if (!bt->devices[i].in_use) {
            zbt_copy(bt->devices[i].address, addr, sizeof(bt->devices[i].address));
            if (name && name[0])
                zbt_copy(bt->devices[i].name, name, sizeof(bt->devices[i].name));
            else
                zbt_copy(bt->devices[i].name, "Unknown Device", sizeof(bt->devices[i].name));
            bt->devices[i].rssi = rssi;
            bt->devices[i].supported_profiles = profiles;
            bt->devices[i].battery_pct = 0xFF;
            bt->devices[i].in_use = 1;
            return 0;
        }
    }
    return -28; /* ENOSPC */
}

int zd_bt_pair(struct zd_bt_service *bt, const char *addr, const char *pin) {
    if (!bt || !addr)
        return -22;
    (void)pin;
    struct zd_bt_device *dev = find_dev_mut(bt, addr);
    if (!dev)
        return -2; /* ENOENT */
    dev->paired = 1;
    dev->trusted = 1; /* auto-trust on successful pairing */
    bt->stats.devices_paired++;
    return 0;
}

int zd_bt_unpair(struct zd_bt_service *bt, const char *addr) {
    if (!bt || !addr)
        return -22;
    struct zd_bt_device *dev = find_dev_mut(bt, addr);
    if (!dev)
        return -2;
    if (dev->connected)
        zd_bt_disconnect(bt, addr);
    dev->paired = 0;
    dev->trusted = 0;
    bt->stats.devices_unpaired++;
    return 0;
}

int zd_bt_set_trusted(struct zd_bt_service *bt, const char *addr, int trusted) {
    if (!bt || !addr)
        return -22;
    struct zd_bt_device *dev = find_dev_mut(bt, addr);
    if (!dev)
        return -2;
    dev->trusted = trusted ? 1 : 0;
    return 0;
}

int zd_bt_connect(struct zd_bt_service *bt, const char *addr) {
    if (!bt || !addr)
        return -22;
    if (bt->state == ZD_BT_STATE_DISABLED)
        return -1;
    struct zd_bt_device *dev = find_dev_mut(bt, addr);
    if (!dev || !dev->paired)
        return -2; /* cannot connect unpaired device */

    dev->connected = 1;
    dev->active_profiles = dev->supported_profiles;
    bt->stats.connections++;
    bt->state = ZD_BT_STATE_CONNECTED;

    /* If device supports A2DP (e.g. earbuds/headphones), route audio to Bluetooth */
    if (dev->supported_profiles & ZD_BT_PROFILE_A2DP) {
        bt->audio_route = ZD_BT_ROUTE_BLUETOOTH_A2DP;
        zbt_copy(bt->active_audio_device, addr, sizeof(bt->active_audio_device));
        bt->stats.audio_routed_to_bt++;
    }
    return 0;
}

int zd_bt_disconnect(struct zd_bt_service *bt, const char *addr) {
    if (!bt || !addr)
        return -22;
    struct zd_bt_device *dev = find_dev_mut(bt, addr);
    if (!dev)
        return -2;
    if (!dev->connected)
        return 0;

    dev->connected = 0;
    dev->active_profiles = 0;
    bt->stats.disconnections++;

    /* If disconnecting active audio device, route audio back to internal speakers */
    if (zbt_eq(bt->active_audio_device, addr)) {
        bt->audio_route = ZD_BT_ROUTE_INTERNAL_SPEAKERS;
        bt->active_audio_device[0] = 0;
        bt->stats.audio_routed_to_internal++;
    }

    int any_connected = 0;
    for (uint32_t i = 0; i < ZD_BT_MAX_DEVICES; ++i) {
        if (bt->devices[i].in_use && bt->devices[i].connected)
            any_connected = 1;
    }
    if (!any_connected && bt->state != ZD_BT_STATE_SCANNING)
        bt->state = ZD_BT_STATE_DORMANT;

    return 0;
}

int zd_bt_update_battery(struct zd_bt_service *bt, const char *addr, uint8_t battery_pct) {
    if (!bt || !addr)
        return -22;
    struct zd_bt_device *dev = find_dev_mut(bt, addr);
    if (!dev)
        return -2;
    dev->battery_pct = battery_pct > 100 ? 100 : battery_pct;
    bt->stats.battery_reports++;
    return 0;
}

int zd_bt_send_avrcp(struct zd_bt_service *bt, const char *addr, enum zd_bt_avrcp_cmd cmd) {
    if (!bt || !addr)
        return -22;
    struct zd_bt_device *dev = find_dev_mut(bt, addr);
    if (!dev || !dev->connected)
        return -2;
    if (!(dev->active_profiles & ZD_BT_PROFILE_AVRCP))
        return -95; /* EOPNOTSUPP */

    (void)cmd;
    bt->stats.avrcp_events++;
    return 0;
}

const struct zd_bt_device *zd_bt_find_device(const struct zd_bt_service *bt, const char *addr) {
    if (!bt || !addr)
        return 0;
    for (uint32_t i = 0; i < ZD_BT_MAX_DEVICES; ++i) {
        if (bt->devices[i].in_use && zbt_eq(bt->devices[i].address, addr))
            return &bt->devices[i];
    }
    return 0;
}
