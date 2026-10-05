/* User-data snapshot manager (Stage 5 part B).
 * Bounded registry of named snapshot points with explicit lifecycle:
 * CREATING -> READY, or FAILED (never half-registered). Restore
 * applies the newest READY snapshot older than the current state;
 * capacity pruning confirms the discard hook first and never evicts the
 * active update rollback point. Hook-driven, host-testable. */
#ifndef ZEROOS_DESKTOP_SNAPSHOT_H
#define ZEROOS_DESKTOP_SNAPSHOT_H

#include <stdint.h>
#include <zeroos/desktop/update.h>

#define ZD_SNAP_MAX 8
#define ZD_SNAP_NAME 32

enum zd_snap_state {
    ZD_SNAP_EMPTY = 0,
    ZD_SNAP_CREATING,
    ZD_SNAP_READY,
    ZD_SNAP_FAILED
};

struct zd_snapshot_ops {
    /* capture/restore payloads into the hook's own storage */
    int (*capture)(void *ctx, const char *name);
    int (*restore)(void *ctx, const char *name);
    int (*discard)(void *ctx, const char *name);
    void *ctx;
};

struct zd_snapshot {
    char name[ZD_SNAP_NAME];
    int state;                  /* enum zd_snap_state */
    uint32_t seq;               /* creation order stamp */
    int last_error;             /* hook errno for FAILED entries */
};

struct zd_snapshots {
    struct zd_snapshot slots[ZD_SNAP_MAX];
    struct zd_snapshot_ops ops;
    char update_rollback_name[ZD_SNAP_NAME];
    uint32_t next_seq;
    struct {
        uint32_t created, create_failed, restored, restore_failed,
                 discarded, prune_rejected;
    } stats;
};

void zd_snapshots_init(struct zd_snapshots *s,
                       const struct zd_snapshot_ops *ops);
/* Begin creation: reserves CREATING; at capacity, pruning selects the
 * oldest discardable READY (never the active update rollback point) and
 * occurs only after the discard hook succeeds. */
int zd_snapshots_create(struct zd_snapshots *s, const char *name);
/* Finish (0) or fail (errno) the in-flight creation. */
int zd_snapshots_create_finish(struct zd_snapshots *s, int err);
/* Restore the newest READY snapshot (name optional selector: NULL =
 * newest).  Hook error -> -errno counted, state kept READY (retryable). */
int zd_snapshots_restore(struct zd_snapshots *s, const char *name);
/* Explicit discard returns -16 (EBUSY) for the active update rollback
 * point; update glue releases it only after replacement capture is READY. */
int zd_snapshots_discard(struct zd_snapshots *s, const char *name);
struct zd_snapshot *zd_snapshots_find(struct zd_snapshots *s,
                                      const char *name);
struct zd_snapshot *zd_snapshots_latest_ready(struct zd_snapshots *s);
uint32_t zd_snapshots_ready_count(const struct zd_snapshots *s);

/* Update glue alternates update-a/update-b, captures into the inactive
 * slot, and preserves the current rollback snapshot until replacement
 * capture succeeds. At capacity it may discard only a non-active snapshot;
 * rollback restores the exact snapshot recorded for the update. A missing
 * active snapshot fails closed. activate/commit stay NULL: A/B slot flips
 * belong to the block layer, not to snapshot state. */
int zd_snapshots_bind_update(struct zd_snapshots *s,
                             struct zd_update_ops *out);

#endif /* ZEROOS_DESKTOP_SNAPSHOT_H */
