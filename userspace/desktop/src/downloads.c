/* Downloads manager.  See downloads.h. */
#include <zeroos/desktop/downloads.h>

static int d_bad(void) { return -22; }
static uint32_t d_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}
static void d_copy(char *dst, uint32_t cap, const char *src) {
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

void zd_downloads_init(struct zd_downloads *d, zd_dl_start_fn start,
                       void *ctx) {
    uint32_t i;
    if (!d)
        return;
    for (i = 0; i < ZD_DL_MAX; ++i) {
        d->items[i].url[0] = 0;
        d->items[i].name[0] = 0;
        d->items[i].id = 0;
        d->items[i].state = -1;
        d->items[i].received = d->items[i].total = 0;
        d->items[i].fail_errno = 0;
    }
    d->count = 0;
    d->next_id = 1;
    d->start = start;
    d->start_ctx = ctx;
    d->active_id = 0;
    d->stats.enqueued = d->stats.started = d->stats.completed = 0;
    d->stats.failed = d->stats.canceled = d->stats.rejected = 0;
    d->stats.progress_regressions = 0;
}

int zd_downloads_add(struct zd_downloads *d, const char *url,
                     const char *name, uint32_t total) {
    struct zd_dl_item *it = 0;
    uint32_t i;
    if (!d || !url || !url[0] || d_len(url) >= ZD_DL_URL) {
        if (d)
            d->stats.rejected++;
        return d_bad();
    }
    if (name && name[0] && d_len(name) >= ZD_DL_NAME) {
        d->stats.rejected++;
        return d_bad();
    }
    if (d->count >= ZD_DL_MAX) {
        d->stats.rejected++;
        return -28;
    }
    for (i = 0; i < ZD_DL_MAX; ++i) {
        if (d->items[i].id == 0) {
            it = &d->items[i];
            break;
        }
    }
    if (!it) {
        d->stats.rejected++;
        return -28;
    }
    it->id = d->next_id++;
    d_copy(it->url, ZD_DL_URL, url);
    if (name && name[0])
        d_copy(it->name, ZD_DL_NAME, name);
    else
        it->name[0] = 0;
    it->state = ZD_DL_QUEUED;
    it->received = 0;
    it->total = total;
    it->fail_errno = 0;
    d->count++;
    d->stats.enqueued++;
    return 0;
}

int zd_downloads_start_next(struct zd_downloads *d) {
    uint32_t i;
    if (!d)
        return d_bad();
    if (d->active_id)
        return -16; /* one active transfer (cooperative policy) */
    for (i = 0; i < ZD_DL_MAX; ++i) {
        if (d->items[i].id && d->items[i].state == ZD_DL_QUEUED) {
            if (d->start) {
                int r = d->start(d->start_ctx, d->items[i].id,
                                 d->items[i].url);
                if (r < 0) {
                    d->items[i].state = ZD_DL_FAILED;
                    d->items[i].fail_errno = -r;
                    d->stats.failed++;
                    return r;
                }
            }
            d->items[i].state = ZD_DL_RUNNING;
            d->active_id = d->items[i].id;
            d->stats.started++;
            return 0;
        }
    }
    return -2; /* nothing queued */
}

int zd_downloads_progress(struct zd_downloads *d, uint32_t id,
                          uint32_t received, uint32_t total) {
    struct zd_dl_item *it;
    if (!d)
        return d_bad();
    it = zd_downloads_find(d, id);
    if (!it)
        return -2;
    if (it->state != ZD_DL_RUNNING)
        return -22;
    if (received < it->received) {
        d->stats.progress_regressions++;
        return d_bad();
    }
    it->received = received;
    if (total && it->total == 0)
        it->total = total;
    return 0;
}

int zd_downloads_finish(struct zd_downloads *d, uint32_t id, int err) {
    struct zd_dl_item *it;
    if (!d)
        return d_bad();
    it = zd_downloads_find(d, id);
    if (!it)
        return -2;
    if (it->state != ZD_DL_RUNNING || d->active_id != id)
        return -22;
    if (err < 0) {
        it->state = ZD_DL_FAILED;
        it->fail_errno = -err;
        d->stats.failed++;
    } else {
        it->state = ZD_DL_DONE;
        d->stats.completed++;
    }
    d->active_id = 0;
    return 0;
}

int zd_downloads_cancel(struct zd_downloads *d, uint32_t id) {
    struct zd_dl_item *it;
    if (!d)
        return d_bad();
    it = zd_downloads_find(d, id);
    if (!it)
        return -2;
    if (it->state == ZD_DL_DONE || it->state == ZD_DL_FAILED ||
        it->state == ZD_DL_CANCELED)
        return -22; /* terminal states are final */
    if (d->active_id == id)
        d->active_id = 0;
    it->state = ZD_DL_CANCELED;
    d->stats.canceled++;
    return 0;
}

struct zd_dl_item *zd_downloads_find(struct zd_downloads *d, uint32_t id) {
    uint32_t i;
    if (!d || id == 0)
        return 0;
    for (i = 0; i < ZD_DL_MAX; ++i)
        if (d->items[i].id == id)
            return &d->items[i];
    return 0;
}

uint32_t zd_downloads_active(const struct zd_downloads *d) {
    return d ? d->active_id : 0;
}
