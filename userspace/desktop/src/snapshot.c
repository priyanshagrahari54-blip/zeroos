/* Snapshot manager.  See snapshot.h. */
#include <zeroos/desktop/snapshot.h>

static int sn_bad(void) { return -22; }
static uint32_t sn_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}
static void sn_copy(char *d, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!d || !cap)
        return;
    if (!src) {
        d[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap) {
        d[i] = src[i];
        ++i;
    }
    d[i] = 0;
}
static int sn_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}
static int sn_find_idx(const struct zd_snapshots *s, const char *name) {
    int i;
    if (!name)
        return -1;
    for (i = 0; i < ZD_SNAP_MAX; ++i)
        if (s->slots[i].state != ZD_SNAP_EMPTY &&
            sn_eq(s->slots[i].name, name))
            return i;
    return -1;
}
static int sn_inflight(const struct zd_snapshots *s) {
    int i;
    for (i = 0; i < ZD_SNAP_MAX; ++i)
        if (s->slots[i].state == ZD_SNAP_CREATING)
            return i;
    return -1;
}

void zd_snapshots_init(struct zd_snapshots *s,
                       const struct zd_snapshot_ops *ops) {
    int i;
    if (!s)
        return;
    for (i = 0; i < ZD_SNAP_MAX; ++i) {
        s->slots[i].name[0] = 0;
        s->slots[i].state = ZD_SNAP_EMPTY;
        s->slots[i].seq = 0;
        s->slots[i].last_error = 0;
    }
    s->ops = ops ? *ops : (struct zd_snapshot_ops){0, 0, 0, 0};
    s->next_seq = 1;
    s->stats.created = s->stats.create_failed = 0;
    s->stats.restored = s->stats.restore_failed = 0;
    s->stats.discarded = s->stats.prune_rejected = 0;
}

int zd_snapshots_create(struct zd_snapshots *s, const char *name) {
    int i, free_idx = -1;
    if (!s || !name || !name[0] || sn_len(name) >= ZD_SNAP_NAME)
        return sn_bad();
    if (sn_find_idx(s, name) >= 0)
        return -17; /* EEXIST */
    if (sn_inflight(s) >= 0)
        return -16; /* EBUSY: one creation at a time */
    for (i = 0; i < ZD_SNAP_MAX; ++i) {
        if (s->slots[i].state == ZD_SNAP_EMPTY) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        /* capacity: prune the OLDEST READY only */
        int oldest = -1;
        for (i = 0; i < ZD_SNAP_MAX; ++i) {
            if (s->slots[i].state != ZD_SNAP_READY)
                continue;
            if (oldest < 0 || s->slots[i].seq < s->slots[oldest].seq)
                oldest = i;
        }
        if (oldest < 0)
            return -28; /* ENOSPC: only FAILED/CREATING occupy slots */
        if (s->ops.discard) {
            int r = s->ops.discard(s->ops.ctx, s->slots[oldest].name);
            if (r < 0) {
                s->stats.prune_rejected++;
                return r;
            }
        }
        s->slots[oldest].state = ZD_SNAP_EMPTY;
        s->stats.discarded++;
        free_idx = oldest;
    }
    sn_copy(s->slots[free_idx].name, ZD_SNAP_NAME, name);
    s->slots[free_idx].state = ZD_SNAP_CREATING;
    s->slots[free_idx].last_error = 0;
    return 0;
}

int zd_snapshots_create_finish(struct zd_snapshots *s, int err) {
    int idx;
    if (!s)
        return sn_bad();
    idx = sn_inflight(s);
    if (idx < 0)
        return -22; /* nothing in flight */
    if (err < 0) {
        s->slots[idx].state = ZD_SNAP_FAILED;
        s->slots[idx].last_error = -err;
        s->stats.create_failed++;
        return 0;
    }
    if (s->ops.capture) {
        /* documented hook: capture payloads into the hook's storage.
         * A failure marks creation FAILED — never a half-READY slot. */
        int r = s->ops.capture(s->ops.ctx, s->slots[idx].name);
        if (r < 0) {
            s->slots[idx].state = ZD_SNAP_FAILED;
            s->slots[idx].last_error = -r;
            s->stats.create_failed++;
            return r;
        }
    }
    s->slots[idx].state = ZD_SNAP_READY;
    s->slots[idx].seq = s->next_seq++;
    s->stats.created++;
    return 0;
}

struct zd_snapshot *zd_snapshots_find(struct zd_snapshots *s,
                                      const char *name) {
    int idx;
    if (!s || !name)
        return 0;
    idx = sn_find_idx(s, name);
    return idx >= 0 ? &s->slots[idx] : 0;
}

struct zd_snapshot *zd_snapshots_latest_ready(struct zd_snapshots *s) {
    struct zd_snapshot *best = 0;
    int i;
    if (!s)
        return 0;
    for (i = 0; i < ZD_SNAP_MAX; ++i)
        if (s->slots[i].state == ZD_SNAP_READY &&
            (!best || s->slots[i].seq > best->seq))
            best = &s->slots[i];
    return best;
}

uint32_t zd_snapshots_ready_count(const struct zd_snapshots *s) {
    uint32_t n = 0;
    int i;
    if (!s)
        return 0;
    for (i = 0; i < ZD_SNAP_MAX; ++i)
        if (s->slots[i].state == ZD_SNAP_READY)
            ++n;
    return n;
}

int zd_snapshots_restore(struct zd_snapshots *s, const char *name) {
    struct zd_snapshot *t;
    int r;
    if (!s)
        return sn_bad();
    if (sn_inflight(s) >= 0)
        return -16; /* creation in flight */
    if (name)
        t = zd_snapshots_find(s, name);
    else
        t = zd_snapshots_latest_ready(s);
    if (!t)
        return -2;
    if (t->state != ZD_SNAP_READY)
        return -22;
    if (s->ops.restore) {
        r = s->ops.restore(s->ops.ctx, t->name);
        if (r < 0) {
            s->stats.restore_failed++;
            return r;
        }
    }
    s->stats.restored++;
    return 0;
}

int zd_snapshots_discard(struct zd_snapshots *s, const char *name) {
    int idx;
    if (!s || !name)
        return sn_bad();
    if (sn_inflight(s) >= 0)
        return -16;
    idx = sn_find_idx(s, name);
    if (idx < 0)
        return -2;
    if (s->slots[idx].state == ZD_SNAP_CREATING)
        return -22;
    if (s->ops.discard) {
        int r = s->ops.discard(s->ops.ctx, s->slots[idx].name);
        if (r < 0)
            return r;
    }
    s->slots[idx].state = ZD_SNAP_EMPTY;
    s->slots[idx].name[0] = 0;
    s->stats.discarded++;
    return 0;
}

/* ---- update pipeline binding (see snapshot.h) ---- */

static int sn_upd_stage(void *ctx) {
    struct zd_snapshots *s = ctx;
    int r;
    if (!s)
        return -22;
    /* replace a stale capture from a previous attempt: absent (-2)
     * is fine, any other error aborts staging fail-closed */
    {
        int d = zd_snapshots_discard(s, "update");
        if (d < 0 && d != -2)
            return d;
    }
    r = zd_snapshots_create(s, "update");
    if (r < 0)
        return r;
    return zd_snapshots_create_finish(s, 0);
}

static int sn_upd_rollback(void *ctx) {
    struct zd_snapshots *s = ctx;
    struct zd_snapshot *latest;
    if (!s)
        return -22;
    latest = zd_snapshots_latest_ready(s);
    if (!latest)
        return -2; /* nothing to restore: caller must fail closed */
    return zd_snapshots_restore(s, latest->name);
}

int zd_snapshots_bind_update(struct zd_snapshots *s,
                             struct zd_update_ops *out) {
    if (!s || !out)
        return -22;
    out->stage_apply = sn_upd_stage;
    out->activate = 0;
    out->rollback = sn_upd_rollback;
    out->commit = 0;
    out->ctx = s;
    return 0;
}
