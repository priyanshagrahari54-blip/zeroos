/* Downloads manager (Stage 5 shell surface).
 * Bounded queue with explicit lifecycle QUEUED -> RUNNING -> DONE /
 * FAILED / CANCELED, progress accounting, single-active-run policy
 * (cooperative resource sharing) and injected start hooks. */
#ifndef ZEROOS_DESKTOP_DOWNLOADS_H
#define ZEROOS_DESKTOP_DOWNLOADS_H

#include <stdint.h>

#define ZD_DL_MAX 8
#define ZD_DL_URL 128
#define ZD_DL_NAME 32

enum zd_dl_state {
    ZD_DL_QUEUED = 0,
    ZD_DL_RUNNING,
    ZD_DL_DONE,
    ZD_DL_FAILED,
    ZD_DL_CANCELED
};

struct zd_downloads;

/* start hook: begin transfer of item id; return 0 accepted, <0 errno */
typedef int (*zd_dl_start_fn)(void *ctx, uint32_t id, const char *url);

struct zd_dl_item {
    char url[ZD_DL_URL];
    char name[ZD_DL_NAME];
    uint32_t id;
    int state;                 /* enum zd_dl_state */
    uint32_t received;         /* bytes so far */
    uint32_t total;            /* 0 = unknown */
    int fail_errno;
};

struct zd_downloads {
    struct zd_dl_item items[ZD_DL_MAX];
    uint32_t count;
    uint32_t next_id;
    zd_dl_start_fn start;
    void *start_ctx;
    uint32_t active_id;        /* 0 = none running */
    struct {
        uint32_t enqueued, started, completed, failed, canceled,
                 rejected, progress_regressions;
    } stats;
};

void zd_downloads_init(struct zd_downloads *d, zd_dl_start_fn start,
                       void *ctx);
/* Enqueue; empty/bad url -> -22, full -> -28 (counted). */
int zd_downloads_add(struct zd_downloads *d, const char *url,
                     const char *name, uint32_t total);
/* Promote the oldest QUEUED item to RUNNING via the start hook.
 * Another item running -> -16.  Hook failure -> FAILED, counted. */
int zd_downloads_start_next(struct zd_downloads *d);
/* Progress from the owner; monotonic enforced (regression -> -22 +
 * counter).  total may be refined once. */
int zd_downloads_progress(struct zd_downloads *d, uint32_t id,
                          uint32_t received, uint32_t total);
int zd_downloads_finish(struct zd_downloads *d, uint32_t id, int err);
int zd_downloads_cancel(struct zd_downloads *d, uint32_t id);
struct zd_dl_item *zd_downloads_find(struct zd_downloads *d, uint32_t id);
uint32_t zd_downloads_active(const struct zd_downloads *d);

#endif /* ZEROOS_DESKTOP_DOWNLOADS_H */
