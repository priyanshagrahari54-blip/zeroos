/* Security scanning architecture (Stage 5 part B).
 *
 * Honest scope: this is the *architecture*, not a detector. The queue,
 * the incremental scheduler, the quarantine policy, the audit ring and
 * the fail-closed reporting are real and host-tested; the part that
 * actually recognises a threat is a pluggable engine, and no engine is
 * linked in this build (a signature corpus is data this repository does
 * not ship, and claiming detection without one would be a fabricated
 * clean bill of health).
 *
 * Two rules follow from that, and both are asserted:
 *
 *  1. With no engine registered, `zd_scan_available()` is 0 and every
 *     item completes as ZD_SCAN_UNAVAILABLE -- never CLEAN. "Not
 *     scanned" is not "clean", and no caller can read a verdict of
 *     CLEAN out of a build that has no engine.
 *  2. An engine that finishes an item without deciding is treated as a
 *     broken engine (the item FAILS and the error is counted), not as
 *     a scan that found nothing.
 *
 * When an engine lands it registers through zd_scan_set_engine() and
 * the same call path returns real verdicts; the contract does not
 * change. Claims stay at "architecture + policy, no detection until an
 * engine is linked".
 *
 * Freestanding: no libc. The store is caller-owned so it can live in
 * the session's file scope (the guest stack is 64 KiB). */
#ifndef ZEROOS_DESKTOP_SCAN_H
#define ZEROOS_DESKTOP_SCAN_H

#include <stdint.h>

#define ZD_SCAN_PATH_CAP 96
#define ZD_SCAN_DETAIL_CAP 48
#define ZD_SCAN_QUEUE 16
#define ZD_SCAN_AUDIT 12
/* Default bytes offered to the engine per slice. A caller that wants a
 * smaller working set passes a smaller budget to zd_scan_step(). */
#define ZD_SCAN_SLICE_BYTES 4096U

enum zd_scan_verdict {
    ZD_SCAN_NONE = 0,        /* no decision yet */
    ZD_SCAN_CLEAN = 1,       /* engine decided: nothing found */
    ZD_SCAN_SUSPECT = 2,     /* engine decided: needs review */
    ZD_SCAN_MALICIOUS = 3,   /* engine decided: quarantine policy applies */
    ZD_SCAN_UNAVAILABLE = 4, /* no engine: fail closed, never CLEAN */
    ZD_SCAN_ERROR = 5,       /* engine or quarantine hook failed */
    ZD_SCAN_VERDICT_COUNT = 6
};

enum zd_scan_state {
    ZD_SCAN_QUEUED = 0,
    ZD_SCAN_RUNNING = 1,
    ZD_SCAN_DONE = 2,
    ZD_SCAN_FAILED = 3,
    ZD_SCAN_QUARANTINED = 4,
    ZD_SCAN_CANCELED = 5,
    ZD_SCAN_STATE_COUNT = 6
};

/* Pluggable detector.
 *
 * The core owns the byte plumbing: it reads at most `budget_bytes` at
 * the item's current offset through `read` and hands that chunk to
 * `inspect`, advancing by however many bytes were read. Scanning is
 * low-priority work, so both halves are bounded and neither is allowed
 * to hold the whole file.
 *
 * `read` returns 0 with *out_read set (out_read == 0 means end of
 * file), or negative on an I/O error.
 *
 * `inspect` sets *verdict on every call: ZD_SCAN_NONE means "still
 * deciding", any other value is a decision the core will act on at
 * once. It returns 0 to continue or negative to report an engine
 * error (the item FAILS and the error is counted). An item that
 * reaches end of file with no decision at all is a broken engine, not
 * a clean file: it FAILS too. */
struct zd_scan_engine {
    int (*read)(void *ctx, const char *path, uint64_t offset,
                uint8_t *buffer, uint32_t length, uint32_t *out_read);
    int (*inspect)(void *ctx, const uint8_t *chunk, uint32_t length,
                   uint64_t offset, uint64_t size,
                   enum zd_scan_verdict *verdict, char *detail,
                   uint32_t detail_cap);
    void *ctx;
};

/* Quarantine policy hook: called once for an item whose verdict the
 * policy acts on (MALICIOUS always; SUSPECT only when the quarantine
 * policy includes it). 0 = contained, negative = containment failed. */
typedef int (*zd_scan_quarantine_fn)(void *ctx, const char *path,
                                     enum zd_scan_verdict verdict);

struct zd_scan_item {
    char path[ZD_SCAN_PATH_CAP];
    char detail[ZD_SCAN_DETAIL_CAP];
    uint64_t size;
    uint64_t scanned_bytes;
    enum zd_scan_state state;
    enum zd_scan_verdict verdict;
    uint32_t in_use;
    uint32_t quarantined;
    uint32_t slices;
};

struct zd_scan_audit_row {
    char path[ZD_SCAN_PATH_CAP];
    enum zd_scan_verdict verdict;
    uint32_t quarantined;
    uint32_t slices;
};

struct zd_scan_stats {
    uint64_t submitted;
    uint64_t completed;
    uint64_t failed;
    uint64_t canceled;
    uint64_t clean;
    uint64_t suspect;
    uint64_t malicious;
    uint64_t unavailable;
    uint64_t quarantined;
    uint64_t slices_run;
    uint64_t bytes_scanned;
    /* Slots are bounded. A slot holding finished work is reclaimed for
     * the next submission (its audit row is already written), so a
     * scanner that walks a large directory is not limited to
     * ZD_SCAN_QUEUE files; while every slot holds live work the
     * submission is refused and counted in queue_dropped. */
    uint32_t reclaimed;
    uint32_t queue_dropped;
    uint32_t engine_errors;
    uint32_t quarantine_errors;
    uint32_t audit_dropped;
};

struct zd_scan {
    struct zd_scan_item items[ZD_SCAN_QUEUE];
    uint32_t count;
    uint32_t cursor;
    uint32_t paused;
    struct zd_scan_engine engine;
    zd_scan_quarantine_fn quarantine;
    void *quarantine_ctx;
    uint32_t quarantine_suspect; /* policy: 1 = quarantine SUSPECT too */
    struct zd_scan_audit_row audit[ZD_SCAN_AUDIT];
    uint32_t audit_count;
    uint32_t audit_next;
    struct zd_scan_stats stats;
};

void zd_scan_init(struct zd_scan *scan);
/* 1 when an engine is registered, else 0. Nothing reports CLEAN while
 * this is 0. */
uint32_t zd_scan_available(const struct zd_scan *scan);
/* Register (NULL detaches) an engine; also clears the "no decision"
 * state of nothing -- queued items simply see the new engine. */
int zd_scan_set_engine(struct zd_scan *scan, const struct zd_scan_engine *engine);
int zd_scan_set_quarantine_hook(struct zd_scan *scan,
                                zd_scan_quarantine_fn hook, void *ctx);
/* Policy: with flag set, SUSPECT is contained as well as MALICIOUS. */
void zd_scan_set_quarantine_policy(struct zd_scan *scan, uint32_t include_suspect);

/* Queue a file for scanning. Path must be absolute, non-empty and fit
 * the per-item path store. Re-submitting an item that is still QUEUED
 * or RUNNING returns -17 (EEXIST); a finished item is re-armed so a
 * caller can rescan after an engine upgrade. Full queue -> -28. */
int zd_scan_submit(struct zd_scan *scan, const char *path, uint64_t size);
int zd_scan_cancel(struct zd_scan *scan, const char *path);
uint32_t zd_scan_cancel_all(struct zd_scan *scan);

/* Incremental, bounded, pausable: processes at most `budget_items`
 * items, offering the engine at most `budget_bytes` per slice. Returns
 * 1 when work remains, 0 when caught up, negative on a bad argument. */
int zd_scan_step(struct zd_scan *scan, uint32_t budget_items,
                 uint32_t budget_bytes);
void zd_scan_set_paused(struct zd_scan *scan, uint32_t paused);

const struct zd_scan_item *zd_scan_item(const struct zd_scan *scan,
                                        const char *path);
/* Audit ring, newest last, bounded by ZD_SCAN_AUDIT. */
uint32_t zd_scan_audit(const struct zd_scan *scan,
                       struct zd_scan_audit_row *out, uint32_t capacity);

const char *zd_scan_verdict_name(enum zd_scan_verdict verdict);
const char *zd_scan_state_name(enum zd_scan_state state);
const char *zd_scan_status_name(int rc);

#endif /* ZEROOS_DESKTOP_SCAN_H */
