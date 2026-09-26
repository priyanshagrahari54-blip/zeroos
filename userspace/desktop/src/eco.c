/* Cloud/device ecosystem core.  See eco.h. */
#include <zeroos/desktop/eco.h>

static uint32_t e_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}
static void e_copy(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!dst || !cap)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

void zd_eco_init(struct zd_eco *e) {
    uint32_t i;
    if (!e)
        return;
    for (i = 0; i < ZD_ECO_DEVICES; ++i) {
        e->devices[i].name[0] = 0;
        e->devices[i].permissions = 0;
        e->devices[i].paired = 0;
        e->devices[i].in_use = 0;
        e->devices[i].sent = 0;
        e->devices[i].refused = 0;
    }
    for (i = 0; i < ZD_ECO_QUEUE; ++i) {
        e->queue[i].device_idx = 0;
        e->queue[i].permission = 0;
        e->queue[i].payload[0] = 0;
        e->queue[i].in_use = 0;
        e->queue[i].flushed = 0;
    }
    e->device_count = 0;
    e->queued = 0;
    e->queued_ever = 0;
    e->conn = ZD_ECO_OFFLINE; /* offline-first: start disconnected */
    e->stats.paired = e->stats.unpairs = 0;
    e->stats.granted = e->stats.revoked = 0;
    e->stats.queued = e->stats.flushed = 0;
    e->stats.refused_perm = e->stats.refused_offline = 0;
    e->stats.refused_pairing = e->stats.rejected = 0;
}

void zd_eco_set_conn(struct zd_eco *e, uint32_t conn) {
    if (e)
        e->conn = conn ? ZD_ECO_ONLINE : ZD_ECO_OFFLINE;
}

static struct zd_eco_device *e_dev(struct zd_eco *e, uint32_t idx) {
    if (!e || idx >= ZD_ECO_DEVICES || !e->devices[idx].in_use)
        return 0;
    return &e->devices[idx];
}

int zd_eco_pair(struct zd_eco *e, const char *name, uint32_t *out_idx) {
    uint32_t i;
    if (!e || !name || !name[0] || e_len(name) >= ZD_ECO_NAME) {
        if (e)
            e->stats.rejected++;
        return -22;
    }
    if (e->device_count >= ZD_ECO_DEVICES)
        return -28;
    for (i = 0; i < ZD_ECO_DEVICES; ++i) {
        if (e->devices[i].in_use)
            continue;
        e_copy(e->devices[i].name, ZD_ECO_NAME, name);
        e->devices[i].permissions = 0; /* explicit grants only */
        e->devices[i].paired = 1;
        e->devices[i].in_use = 1;
        e->devices[i].sent = 0;
        e->devices[i].refused = 0;
        e->device_count++;
        e->stats.paired++;
        if (out_idx)
            *out_idx = i;
        return 0;
    }
    return -28;
}

int zd_eco_unpair(struct zd_eco *e, uint32_t idx) {
    struct zd_eco_device *d = e_dev(e, idx);
    if (!e)
        return -22;
    if (!d)
        return -2;
    d->in_use = 0;
    d->paired = 0;
    d->permissions = 0;
    d->name[0] = 0;
    e->device_count--;
    e->stats.unpairs++;
    /* queued entries for this device are dropped (pairing gone) */
    {
        uint32_t i;
        for (i = 0; i < ZD_ECO_QUEUE; ++i) {
            if (e->queue[i].in_use && e->queue[i].device_idx == idx) {
                e->queue[i].in_use = 0;
                e->queue[i].payload[0] = 0;
                if (e->queued)
                    e->queued--;
            }
        }
    }
    return 0;
}

int zd_eco_grant(struct zd_eco *e, uint32_t idx, uint32_t perm) {
    struct zd_eco_device *d = e_dev(e, idx);
    if (!e)
        return -22;
    if (!d)
        return -2;
    if (perm == 0 || (perm & (perm - 1)) ||
        (perm & ~ZD_ECO_PERM_ALL)) {
        e->stats.rejected++;
        return -22;
    }
    if (d->permissions & perm)
        return 0; /* already granted: idempotent */
    d->permissions |= perm;
    e->stats.granted++;
    return 0;
}

int zd_eco_revoke(struct zd_eco *e, uint32_t idx, uint32_t perm) {
    struct zd_eco_device *d = e_dev(e, idx);
    if (!e)
        return -22;
    if (!d)
        return -2;
    if (perm == 0 || (perm & (perm - 1)) ||
        (perm & ~ZD_ECO_PERM_ALL)) {
        e->stats.rejected++;
        return -22;
    }
    if (!(d->permissions & perm))
        return -1; /* not held */
    d->permissions &= ~perm;
    e->stats.revoked++;
    /* queued entries needing the revoked permission are dropped */
    {
        uint32_t i;
        for (i = 0; i < ZD_ECO_QUEUE; ++i) {
            if (e->queue[i].in_use && e->queue[i].device_idx == idx &&
                e->queue[i].permission == perm) {
                e->queue[i].in_use = 0;
                e->queue[i].payload[0] = 0;
                if (e->queued)
                    e->queued--;
            }
        }
    }
    return 0;
}

int zd_eco_enqueue(struct zd_eco *e, uint32_t idx, uint32_t perm,
                   const char *payload) {
    struct zd_eco_device *d = e_dev(e, idx);
    uint32_t i;
    if (!e || !payload || !payload[0] ||
        e_len(payload) >= ZD_ECO_PAYLOAD) {
        if (e)
            e->stats.rejected++;
        return -22;
    }
    if (!d) {
        e->stats.refused_pairing++;
        return -1;
    }
    if (perm == 0 || (perm & (perm - 1)) ||
        (perm & ~ZD_ECO_PERM_ALL)) {
        e->stats.rejected++;
        return -22;
    }
    if (!(d->permissions & perm)) {
        e->stats.refused_perm++;
        d->refused++;
        return -1;
    }
    if (e->queued >= ZD_ECO_QUEUE)
        return -28;
    for (i = 0; i < ZD_ECO_QUEUE; ++i) {
        if (e->queue[i].in_use)
            continue;
        e->queue[i].device_idx = idx;
        e->queue[i].permission = perm;
        e_copy(e->queue[i].payload, ZD_ECO_PAYLOAD, payload);
        e->queue[i].in_use = 1;
        e->queue[i].flushed = 0;
        e->queued++;
        e->queued_ever++;
        e->stats.queued++;
        return 0;
    }
    return -28;
}

uint32_t zd_eco_flush(struct zd_eco *e) {
    uint32_t i, sent = 0;
    if (!e)
        return 0;
    if (e->conn != ZD_ECO_ONLINE) {
        /* offline: nothing lost, nothing sent early */
        return 0;
    }
    for (i = 0; i < ZD_ECO_QUEUE; ++i) {
        struct zd_eco_entry *en = &e->queue[i];
        struct zd_eco_device *d;
        if (!en->in_use || en->flushed)
            continue;
        d = e_dev(e, en->device_idx);
        if (!d) {
            /* pairing vanished: drop, keep queue consistent */
            en->in_use = 0;
            en->payload[0] = 0;
            if (e->queued)
                e->queued--;
            continue;
        }
        if (!(d->permissions & en->permission)) {
            /* revoked mid-queue: refuse this entry, keep the rest */
            e->stats.refused_perm++;
            d->refused++;
            en->in_use = 0;
            en->payload[0] = 0;
            if (e->queued)
                e->queued--;
            continue;
        }
        en->flushed = 1;
        d->sent++;
        e->stats.flushed++;
        sent++;
    }
    /* reclaim transmitted slots: `queued` counts pending work only */
    for (i = 0; i < ZD_ECO_QUEUE; ++i) {
        if (e->queue[i].in_use && e->queue[i].flushed) {
            e->queue[i].in_use = 0;
            e->queue[i].flushed = 0;
            e->queue[i].payload[0] = 0;
            if (e->queued)
                e->queued--;
        }
    }
    return sent;
}
