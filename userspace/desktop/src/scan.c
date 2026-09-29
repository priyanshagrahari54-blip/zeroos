/* Security scanning architecture.  See scan.h for the honest scope:
 * the queue, scheduler, quarantine policy, audit ring and fail-closed
 * reporting are implemented here; detection is a pluggable engine and
 * no engine is linked in this build. */
#include <zeroos/desktop/common.h>
#include <zeroos/desktop/scan.h>

/* Slice buffer. File scope, not a local: the guest stack is 64 KiB and
 * this is exactly the buffer a bounded design is supposed to have one
 * of rather than one per call. */
static uint8_t sc_chunk[ZD_SCAN_SLICE_BYTES];

static int sc_path_ok(const char *path) {
    uint32_t n = 0;
    if (!path || path[0] != '/')
        return 0;
    while (path[n])
        ++n;
    /* "/" is the root directory, not a file: scanning it would mean
     * walking the whole tree, which is the one thing a bounded,
     * low-priority queue must never do implicitly. */
    return n > 1 && n < ZD_SCAN_PATH_CAP;
}

static struct zd_scan_item *sc_find(struct zd_scan *scan, const char *path) {
    uint32_t index;
    if (!scan || !path)
        return (struct zd_scan_item *)0;
    for (index = 0; index < ZD_SCAN_QUEUE; ++index)
        if (scan->items[index].in_use &&
            zd_str_equal(scan->items[index].path, path))
            return &scan->items[index];
    return (struct zd_scan_item *)0;
}

static int sc_active(const struct zd_scan_item *item) {
    return item->state == ZD_SCAN_QUEUED || item->state == ZD_SCAN_RUNNING;
}

static void sc_audit(struct zd_scan *scan, const struct zd_scan_item *item) {
    if (scan->audit_count < ZD_SCAN_AUDIT) {
        struct zd_scan_audit_row *row = &scan->audit[scan->audit_next];
        zd_str_copy(row->path, sizeof(row->path), item->path);
        row->verdict = item->verdict;
        row->quarantined = item->quarantined;
        row->slices = item->slices;
        scan->audit_next = (scan->audit_next + 1U) % ZD_SCAN_AUDIT;
        ++scan->audit_count;
        return;
    }
    /* Ring full: overwrite the oldest row, and say so. A silently
     * wrapped audit log is a security log that can lose the very
     * events it exists to keep. */
    {
        struct zd_scan_audit_row *row = &scan->audit[scan->audit_next];
        zd_str_copy(row->path, sizeof(row->path), item->path);
        row->verdict = item->verdict;
        row->quarantined = item->quarantined;
        row->slices = item->slices;
        scan->audit_next = (scan->audit_next + 1U) % ZD_SCAN_AUDIT;
    }
    ++scan->stats.audit_dropped;
}

/* Finish an item: apply the quarantine policy, record the audit row and
 * the counters. `verdict` is the decision the core will report. */
static void sc_finish(struct zd_scan *scan, struct zd_scan_item *item,
                      enum zd_scan_verdict verdict) {
    int contained = 0;

    item->verdict = verdict;
    /* The verdict is counted whatever happens next: containment failing
     * does not un-detect the file. */
    if (verdict == ZD_SCAN_CLEAN)
        ++scan->stats.clean;
    else if (verdict == ZD_SCAN_SUSPECT)
        ++scan->stats.suspect;
    else if (verdict == ZD_SCAN_MALICIOUS)
        ++scan->stats.malicious;
    else if (verdict == ZD_SCAN_UNAVAILABLE)
        ++scan->stats.unavailable;

    if (verdict == ZD_SCAN_MALICIOUS ||
        (verdict == ZD_SCAN_SUSPECT && scan->quarantine_suspect)) {
        if (!scan->quarantine) {
            /* No containment path exists. Report the verdict but do not
             * claim the file was contained: that is the difference
             * between a policy and a wish. */
            ++scan->stats.quarantine_errors;
            ++scan->stats.failed;
            item->state = ZD_SCAN_FAILED;
            sc_audit(scan, item);
            return;
        }
        if (scan->quarantine(scan->quarantine_ctx, item->path, verdict) != 0) {
            ++scan->stats.quarantine_errors;
            ++scan->stats.failed;
            item->state = ZD_SCAN_FAILED;
            sc_audit(scan, item);
            return;
        }
        contained = 1;
        item->quarantined = 1;
        ++scan->stats.quarantined;
    }

    item->state = contained ? ZD_SCAN_QUARANTINED : ZD_SCAN_DONE;
    ++scan->stats.completed;
    sc_audit(scan, item);
}

void zd_scan_init(struct zd_scan *scan) {
    uint32_t index;
    if (!scan)
        return;
    zd_memset(scan, 0, sizeof(*scan));
    for (index = 0; index < ZD_SCAN_QUEUE; ++index)
        scan->items[index].in_use = 0;
    scan->quarantine_suspect = 0;
}

uint32_t zd_scan_available(const struct zd_scan *scan) {
    return (scan && scan->engine.inspect && scan->engine.read) ? 1U : 0U;
}

int zd_scan_set_engine(struct zd_scan *scan,
                       const struct zd_scan_engine *engine) {
    if (!scan)
        return -22;
    if (!engine || !engine->inspect || !engine->read) {
        scan->engine.inspect = (int (*)(void *, const uint8_t *, uint32_t,
                                        uint64_t, uint64_t,
                                        enum zd_scan_verdict *, char *,
                                        uint32_t))0;
        scan->engine.read = (int (*)(void *, const char *, uint64_t, uint8_t *,
                                     uint32_t, uint32_t *))0;
        scan->engine.ctx = (void *)0;
        return 0;
    }
    scan->engine = *engine;
    return 0;
}

int zd_scan_set_quarantine_hook(struct zd_scan *scan,
                                zd_scan_quarantine_fn hook, void *ctx) {
    if (!scan)
        return -22;
    scan->quarantine = hook;
    scan->quarantine_ctx = ctx;
    return 0;
}

void zd_scan_set_quarantine_policy(struct zd_scan *scan,
                                   uint32_t include_suspect) {
    if (!scan)
        return;
    scan->quarantine_suspect = include_suspect ? 1U : 0U;
}

int zd_scan_submit(struct zd_scan *scan, const char *path, uint64_t size) {
    struct zd_scan_item *item;
    uint32_t index;
    int reusing = 0;

    if (!scan)
        return -22;
    if (!sc_path_ok(path))
        return -22;
    item = sc_find(scan, path);
    if (item) {
        if (sc_active(item))
            return -17; /* EEXIST: already queued or being scanned */
        /* Re-arm a finished item: same slot, fresh scan. */
        zd_str_copy(item->path, sizeof(item->path), path);
        item->size = size;
        item->scanned_bytes = 0;
        item->state = ZD_SCAN_QUEUED;
        item->verdict = ZD_SCAN_NONE;
        item->detail[0] = 0;
        item->quarantined = 0;
        item->slices = 0;
        ++scan->stats.submitted;
        return 0;
    }
    for (index = 0; index < ZD_SCAN_QUEUE; ++index)
        if (!scan->items[index].in_use)
            break;
    if (index == ZD_SCAN_QUEUE) {
        /* Every slot is taken. A slot holding finished work is recycled
         * -- the audit ring already has its row -- so a scan of a large
         * directory is not capped at ZD_SCAN_QUEUE files. Only live work
         * is a real overflow. */
        for (index = 0; index < ZD_SCAN_QUEUE; ++index) {
            struct zd_scan_item *done = &scan->items[index];
            if (done->in_use && !sc_active(done))
                break;
        }
        if (index == ZD_SCAN_QUEUE) {
            ++scan->stats.queue_dropped;
            return -28; /* ENOSPC */
        }
        ++scan->stats.reclaimed;
    }
    item = &scan->items[index];
    reusing = item->in_use ? 1 : 0;
    zd_memset(item, 0, sizeof(*item));
    item->in_use = 1;
    zd_str_copy(item->path, sizeof(item->path), path);
    item->size = size;
    item->state = ZD_SCAN_QUEUED;
    item->verdict = ZD_SCAN_NONE;
    if (!reusing)
        ++scan->count; /* occupied slots, not total submissions */
    ++scan->stats.submitted;
    return 0;
}

int zd_scan_cancel(struct zd_scan *scan, const char *path) {
    struct zd_scan_item *item = sc_find(scan, path);
    if (!item)
        return -2; /* ENOENT */
    if (!sc_active(item))
        return 0;
    item->state = ZD_SCAN_CANCELED;
    ++scan->stats.canceled;
    return 0;
}

uint32_t zd_scan_cancel_all(struct zd_scan *scan) {
    uint32_t index;
    uint32_t canceled = 0;
    if (!scan)
        return 0;
    for (index = 0; index < ZD_SCAN_QUEUE; ++index) {
        struct zd_scan_item *item = &scan->items[index];
        if (!item->in_use || !sc_active(item))
            continue;
        item->state = ZD_SCAN_CANCELED;
        ++canceled;
        ++scan->stats.canceled;
    }
    return canceled;
}

int zd_scan_step(struct zd_scan *scan, uint32_t budget_items,
                 uint32_t budget_bytes) {
    uint32_t processed = 0;
    uint32_t scanned_slots = 0;

    if (!scan)
        return -22;
    if (budget_items == 0)
        budget_items = 1;
    if (budget_bytes == 0)
        budget_bytes = ZD_SCAN_SLICE_BYTES;
    if (scan->paused)
        return 0;

    while (processed < budget_items && scanned_slots < ZD_SCAN_QUEUE) {
        struct zd_scan_item *item = &scan->items[scan->cursor];
        scan->cursor = (scan->cursor + 1U) % ZD_SCAN_QUEUE;
        ++scanned_slots;
        if (!item->in_use)
            continue;
        if (item->state == ZD_SCAN_QUEUED)
            item->state = ZD_SCAN_RUNNING;
        if (item->state != ZD_SCAN_RUNNING)
            continue;

        /* No engine: fail closed. The item is completed as UNAVAILABLE
         * so a caller can never read CLEAN out of a build that has no
         * detector, and the counters say how much went unscanned. */
        if (!zd_scan_available(scan)) {
            zd_str_copy(item->detail, sizeof(item->detail), "no engine");
            sc_finish(scan, item, ZD_SCAN_UNAVAILABLE);
            ++processed;
            continue;
        }
        {
            enum zd_scan_verdict verdict = ZD_SCAN_NONE;
            uint32_t consumed = 0;
            uint32_t want = budget_bytes;
            int rc;

            if (want > item->size - item->scanned_bytes)
                want = (uint32_t)(item->size - item->scanned_bytes);
            if (want > ZD_SCAN_SLICE_BYTES)
                want = ZD_SCAN_SLICE_BYTES;
            rc = scan->engine.read(scan->engine.ctx, item->path,
                                   item->scanned_bytes, sc_chunk, want,
                                   &consumed);
            if (rc < 0) {
                ++scan->stats.engine_errors;
                ++scan->stats.failed;
                item->state = ZD_SCAN_FAILED;
                item->verdict = ZD_SCAN_ERROR;
                sc_audit(scan, item);
                ++processed;
                continue;
            }
            if (consumed > want)
                consumed = want;
            /* inspect runs even for a zero-length chunk: whether an empty
             * file is clean is the engine's call, not the core's. */
            rc = scan->engine.inspect(scan->engine.ctx, sc_chunk, consumed,
                                      item->scanned_bytes, item->size,
                                      &verdict, item->detail,
                                      (uint32_t)sizeof(item->detail));
            if (rc < 0) {
                ++scan->stats.engine_errors;
                ++scan->stats.failed;
                item->state = ZD_SCAN_FAILED;
                item->verdict = ZD_SCAN_ERROR;
                sc_audit(scan, item);
                ++processed;
                continue;
            }
            if (verdict != ZD_SCAN_NONE)
                item->verdict = verdict;
            item->scanned_bytes += consumed;
            scan->stats.bytes_scanned += consumed;
            ++item->slices;
            ++scan->stats.slices_run;
            /* Finished: nothing more to read, or the file is exhausted. */
            if (consumed == 0 || item->scanned_bytes >= item->size) {
                if (item->verdict == ZD_SCAN_NONE) {
                    /* An engine that never decides is a broken engine,
                     * not a clean file. */
                    ++scan->stats.engine_errors;
                    ++scan->stats.failed;
                    item->state = ZD_SCAN_FAILED;
                    item->verdict = ZD_SCAN_ERROR;
                    sc_audit(scan, item);
                } else {
                    sc_finish(scan, item, item->verdict);
                }
            }
        }
        ++processed;
    }
    /* Caught up only when nothing is left queued or running. */
    {
        uint32_t index;
        for (index = 0; index < ZD_SCAN_QUEUE; ++index)
            if (scan->items[index].in_use &&
                sc_active(&scan->items[index]))
                return 1;
    }
    return 0;
}

void zd_scan_set_paused(struct zd_scan *scan, uint32_t paused) {
    if (!scan)
        return;
    scan->paused = paused ? 1U : 0U;
}

const struct zd_scan_item *zd_scan_item(const struct zd_scan *scan,
                                        const char *path) {
    uint32_t index;
    if (!scan || !path)
        return (const struct zd_scan_item *)0;
    for (index = 0; index < ZD_SCAN_QUEUE; ++index)
        if (scan->items[index].in_use &&
            zd_str_equal(scan->items[index].path, path))
            return &scan->items[index];
    return (const struct zd_scan_item *)0;
}

uint32_t zd_scan_audit(const struct zd_scan *scan,
                       struct zd_scan_audit_row *out, uint32_t capacity) {
    uint32_t written = 0;
    uint32_t index;
    uint32_t start;
    if (!scan || !out || capacity == 0 || scan->audit_count == 0)
        return 0;
    start = (scan->audit_next + ZD_SCAN_AUDIT - scan->audit_count) %
            ZD_SCAN_AUDIT;
    for (index = 0; index < scan->audit_count && written < capacity; ++index) {
        const struct zd_scan_audit_row *row =
            &scan->audit[(start + index) % ZD_SCAN_AUDIT];
        out[written] = *row;
        ++written;
    }
    return written;
}

const char *zd_scan_verdict_name(enum zd_scan_verdict verdict) {
    static const char *const names[ZD_SCAN_VERDICT_COUNT] = {
        "none", "clean", "suspect", "malicious", "unavailable", "error"
    };
    if ((int)verdict < 0 || (int)verdict >= ZD_SCAN_VERDICT_COUNT)
        return "?";
    return names[verdict];
}

const char *zd_scan_state_name(enum zd_scan_state state) {
    static const char *const names[ZD_SCAN_STATE_COUNT] = {
        "queued", "running", "done", "failed", "quarantined", "canceled"
    };
    if ((int)state < 0 || (int)state >= ZD_SCAN_STATE_COUNT)
        return "?";
    return names[state];
}

const char *zd_scan_status_name(int rc) {
    if (rc == 0)
        return "ok";
    if (rc == -22)
        return "malformed";
    if (rc == -17)
        return "exists";
    if (rc == -28)
        return "no-space";
    if (rc == -2)
        return "no-entry";
    if (rc == -95)
        return "unsupported-feature";
    return "error";
}
