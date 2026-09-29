/* Security scanning architecture (scan.h).
 *
 * These tests pin the two promises the module makes: no engine means no
 * clean verdict (never a fabricated bill of health), and with an engine
 * the queue, the bounded slices, the quarantine policy and the audit
 * ring all really run. The detector is a fixture -- a signature matcher
 * over in-memory bytes -- because this build ships no signature corpus;
 * the fixture proves the pipeline, not the detection. */
#include <zeroos/desktop/desktop.h>
#include "test_harness.h"
#include <string.h>

/* --- fixture engine: signature matcher over in-memory "files" --------- */

struct fx_file {
    const char *path;
    const char *content;
    uint32_t length;
};

struct fx_engine {
    const struct fx_file *files;
    uint32_t file_count;
    const char *needle;         /* signature: presence => malicious */
    uint32_t needle_length;
    uint32_t read_calls;
    uint32_t inspect_calls;
    uint32_t max_chunk_seen;
    int fail_read;              /* force an I/O error */
    int fail_inspect;           /* force an engine error */
    int never_decide;           /* engine bug: finish without a verdict */
};

static const struct fx_file *fx_find(struct fx_engine *fx, const char *path) {
    uint32_t index;
    for (index = 0; index < fx->file_count; ++index)
        if (zd_str_equal(fx->files[index].path, path))
            return &fx->files[index];
    return (const struct fx_file *)0;
}

static int fx_read(void *ctx, const char *path, uint64_t offset,
                   uint8_t *buffer, uint32_t length, uint32_t *out_read) {
    struct fx_engine *fx = (struct fx_engine *)ctx;
    const struct fx_file *file;
    uint32_t available;

    ++fx->read_calls;
    if (!fx || !buffer || !out_read)
        return -22;
    *out_read = 0;
    if (fx->fail_read)
        return -5;
    file = fx_find(fx, path);
    if (!file)
        return -2;
    if (offset >= file->length)
        return 0; /* end of file */
    available = file->length - (uint32_t)offset;
    if (available > length)
        available = length;
    zd_memcpy(buffer, file->content + offset, available);
    *out_read = available;
    return 0;
}

/* Naive substring match over the chunk handed to it. A signature that
 * straddles a slice boundary is the fixture's blind spot, not the
 * core's, so host fixtures keep their needles inside one slice. */
static int fx_inspect(void *ctx, const uint8_t *chunk, uint32_t length,
                      uint64_t offset, uint64_t size,
                      enum zd_scan_verdict *verdict, char *detail,
                      uint32_t detail_cap) {
    struct fx_engine *fx = (struct fx_engine *)ctx;
    uint32_t index;

    ++fx->inspect_calls;
    (void)offset;
    (void)size;
    if (length > fx->max_chunk_seen)
        fx->max_chunk_seen = length;
    if (!verdict)
        return -22;
    if (fx->fail_inspect)
        return -5;
    if (fx->never_decide)
        return 0; /* never reaches a verdict */
    if (length == 0) {
        *verdict = ZD_SCAN_CLEAN; /* an empty file is the engine's call */
        zd_str_copy(detail, detail_cap, "empty");
        return 0;
    }
    if (fx->needle && fx->needle_length && length >= fx->needle_length) {
        for (index = 0; index + fx->needle_length <= length; ++index) {
            uint32_t k = 0;
            while (k < fx->needle_length &&
                   chunk[index + k] == (uint8_t)fx->needle[k])
                ++k;
            if (k == fx->needle_length) {
                *verdict = ZD_SCAN_MALICIOUS;
                zd_str_copy(detail, detail_cap, "signature match");
                return 0;
            }
        }
    }
    *verdict = ZD_SCAN_CLEAN;
    return 0;
}

static void fx_register(struct zd_scan *scan, struct fx_engine *fx) {
    struct zd_scan_engine engine;
    memset(&engine, 0, sizeof(engine));
    engine.read = fx_read;
    engine.inspect = fx_inspect;
    engine.ctx = fx;
    ZD_CHECK_OK(zd_scan_set_engine(scan, &engine));
    ZD_CHECK_EQ(zd_scan_available(scan), 1U);
}

/* --- quarantine hook -------------------------------------------------- */

static int quarantine_calls;
static char quarantine_last[96];
static int quarantine_fail;

static int on_quarantine(void *ctx, const char *path,
                         enum zd_scan_verdict verdict) {
    (void)ctx;
    (void)verdict;
    ++quarantine_calls;
    zd_str_copy(quarantine_last, sizeof(quarantine_last), path);
    return quarantine_fail ? -1 : 0;
}

/* --- tests ------------------------------------------------------------- */

static void test_fail_closed_without_engine(void) {
    struct zd_scan scan;
    const struct zd_scan_item *item;
    struct zd_scan_audit_row rows[4];
    uint32_t rows_out;

    zd_scan_init(&scan);
    ZD_CHECK_EQ(zd_scan_available(&scan), 0U);

    /* Queuing is allowed; the verdict is where the promise is kept. */
    ZD_CHECK_OK(zd_scan_submit(&scan, "/ram/shell/inbox/a.txt", 128));
    ZD_CHECK_OK(zd_scan_submit(&scan, "/ram/shell/inbox/b.txt", 4096));
    ZD_CHECK_EQ(zd_scan_step(&scan, 8, 4096), 0);
    ZD_CHECK_EQ(scan.stats.unavailable, 2ULL);
    ZD_CHECK_EQ(scan.stats.clean, 0ULL);
    ZD_CHECK_EQ(scan.stats.completed, 2ULL);

    /* The single property that matters: nothing reads as CLEAN. */
    item = zd_scan_item(&scan, "/ram/shell/inbox/a.txt");
    ZD_CHECK(item != 0);
    ZD_CHECK_EQ((int)item->verdict, (int)ZD_SCAN_UNAVAILABLE);
    ZD_CHECK_NE((int)item->verdict, (int)ZD_SCAN_CLEAN);
    ZD_CHECK_EQ((int)item->state, (int)ZD_SCAN_DONE);
    ZD_CHECK_EQ(item->quarantined, 0U);

    /* The audit ring records what was not scanned, not a clean result. */
    rows_out = zd_scan_audit(&scan, rows, 4);
    ZD_CHECK_EQ(rows_out, 2U);
    ZD_CHECK_EQ((int)rows[0].verdict, (int)ZD_SCAN_UNAVAILABLE);

    /* A verdict of UNAVAILABLE is never quarantined, and pausing stops
     * the queue without losing it. */
    ZD_CHECK_EQ(scan.stats.quarantined, 0ULL);
    ZD_CHECK_OK(zd_scan_submit(&scan, "/ram/shell/inbox/c.txt", 16));
    zd_scan_set_paused(&scan, 1);
    ZD_CHECK_EQ(zd_scan_step(&scan, 8, 4096), 0);
    ZD_CHECK_EQ((int)zd_scan_item(&scan, "/ram/shell/inbox/c.txt")->state,
                (int)ZD_SCAN_QUEUED);
    zd_scan_set_paused(&scan, 0);
    ZD_CHECK_EQ(zd_scan_step(&scan, 8, 4096), 0);
    ZD_CHECK_EQ(scan.stats.unavailable, 3ULL);
}

static void test_submission_and_bounds(void) {
    struct zd_scan scan;
    char long_path[256];
    uint32_t index;

    zd_scan_init(&scan);

    /* Path discipline: absolute, non-empty, bounded. */
    ZD_CHECK_ERR(zd_scan_submit(&scan, 0, 10), 22);
    ZD_CHECK_ERR(zd_scan_submit(&scan, "relative/path", 10), 22);
    ZD_CHECK_ERR(zd_scan_submit(&scan, "/", 10), 22);
    memset(long_path, 'p', sizeof(long_path));
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1] = 0;
    ZD_CHECK_ERR(zd_scan_submit(&scan, long_path, 10), 22);

    /* Capacity: the queue is bounded, and a full queue is counted. */
    for (index = 0; index < ZD_SCAN_QUEUE; ++index) {
        char path[32];
        snprintf(path, sizeof(path), "/ram/shell/f%u", index);
        ZD_CHECK_OK(zd_scan_submit(&scan, path, 64));
    }
    ZD_CHECK_ERR(zd_scan_submit(&scan, "/ram/shell/overflow", 64), 28);
    ZD_CHECK_EQ(scan.stats.queue_dropped, 1U);
    ZD_CHECK_EQ(scan.count, ZD_SCAN_QUEUE);

    /* Finished slots are recycled, so scanning a directory larger than
     * the queue still works; only live work is a real overflow. */
    {
        struct fx_engine fx;
        memset(&fx, 0, sizeof(fx));
        fx_register(&scan, &fx);
        while (zd_scan_step(&scan, ZD_SCAN_QUEUE, 4096) == 1) {
        }
        ZD_CHECK_OK(zd_scan_submit(&scan, "/ram/shell/after", 64));
        ZD_CHECK_EQ(scan.stats.reclaimed, 1U);
        /* `count` is occupied slots, so reclaiming does not inflate it. */
        ZD_CHECK_EQ(scan.count, ZD_SCAN_QUEUE);
        ZD_CHECK_OK(zd_scan_submit(&scan, "/ram/shell/after2", 64));
        ZD_CHECK_EQ(scan.count, ZD_SCAN_QUEUE);
    }

    /* Re-submitting live work is refused; finished work is re-armed
     * without consuming another slot. "after" is still queued, f0
     * finished above. */
    ZD_CHECK_ERR(zd_scan_submit(&scan, "/ram/shell/after", 64), 17);
    ZD_CHECK_OK(zd_scan_submit(&scan, "/ram/shell/f0", 64));
    ZD_CHECK_EQ((int)zd_scan_item(&scan, "/ram/shell/f0")->state,
                (int)ZD_SCAN_QUEUED);

    /* Cancellation: unknown path is not an error, a queued item is
     * canceled and never reported as scanned. f0 was re-armed above, so
     * it is the live one. */
    ZD_CHECK_ERR(zd_scan_cancel(&scan, "/ram/shell/nope"), 2);
    ZD_CHECK_OK(zd_scan_cancel(&scan, "/ram/shell/f0"));
    ZD_CHECK_EQ((int)zd_scan_item(&scan, "/ram/shell/f0")->state,
                (int)ZD_SCAN_CANCELED);
    ZD_CHECK_EQ(scan.stats.canceled, 1ULL);
    /* cancel_all only touches live items: the two just submitted, not
     * the finished ones. */
    ZD_CHECK_EQ(zd_scan_cancel_all(&scan), 2U);
    ZD_CHECK_EQ(scan.stats.canceled, 3ULL);
}

static void test_engine_pipeline(void) {
    static const struct fx_file files[] = {
        {"/ram/shell/clean.txt", "the quick brown fox jumps over the dog", 39},
        {"/ram/shell/bad.bin", "padding-EICAR-signature-padding", 31},
        {"/ram/shell/empty.txt", "", 0}
    };
    struct fx_engine fx;
    struct zd_scan scan;
    const struct zd_scan_item *item;

    memset(&fx, 0, sizeof(fx));
    fx.files = files;
    fx.file_count = 3;
    fx.needle = "EICAR";
    fx.needle_length = 5;

    zd_scan_init(&scan);
    fx_register(&scan, &fx);
    ZD_CHECK_OK(zd_scan_set_quarantine_hook(&scan, on_quarantine, 0));
    quarantine_calls = 0;
    quarantine_fail = 0;

    ZD_CHECK_OK(zd_scan_submit(&scan, files[0].path, files[0].length));
    ZD_CHECK_OK(zd_scan_submit(&scan, files[1].path, files[1].length));
    ZD_CHECK_OK(zd_scan_submit(&scan, files[2].path, files[2].length));

    /* Bounded: one item per step, so the caller owns the pacing. */
    ZD_CHECK_EQ(zd_scan_step(&scan, 1, 4096), 1);
    while (zd_scan_step(&scan, 1, 4096) == 1) {
    }

    ZD_CHECK_EQ(scan.stats.completed, 3ULL);
    ZD_CHECK_EQ(scan.stats.clean, 2ULL);
    ZD_CHECK_EQ(scan.stats.malicious, 1ULL);
    ZD_CHECK_EQ(scan.stats.unavailable, 0ULL);

    item = zd_scan_item(&scan, files[0].path);
    ZD_CHECK_EQ((int)item->verdict, (int)ZD_SCAN_CLEAN);
    ZD_CHECK_EQ(item->scanned_bytes, (uint64_t)files[0].length);
    /* An empty file is still the engine's call, answered CLEAN. */
    item = zd_scan_item(&scan, files[2].path);
    ZD_CHECK_EQ((int)item->verdict, (int)ZD_SCAN_CLEAN);
    ZD_CHECK(strcmp(item->detail, "empty") == 0);

    /* The malicious file went through quarantine, not just detection. */
    item = zd_scan_item(&scan, files[1].path);
    ZD_CHECK_EQ((int)item->verdict, (int)ZD_SCAN_MALICIOUS);
    ZD_CHECK_EQ(item->quarantined, 1U);
    ZD_CHECK_EQ((int)item->state, (int)ZD_SCAN_QUARANTINED);
    ZD_CHECK_EQ(quarantine_calls, 1);
    ZD_CHECK(strcmp(quarantine_last, files[1].path) == 0);
    ZD_CHECK_EQ(scan.stats.quarantined, 1ULL);
    ZD_CHECK(strcmp(zd_scan_verdict_name(item->verdict), "malicious") == 0);
    ZD_CHECK(strcmp(zd_scan_state_name(item->state), "quarantined") == 0);
}

static void test_slice_budget_and_engine_failures(void) {
    static const struct fx_file files[] = {
        {"/ram/shell/big.bin",
         "0123456789abcdef0123456789abcdef0123456789abcdef", 48}
    };
    struct fx_engine fx;
    struct zd_scan scan;

    memset(&fx, 0, sizeof(fx));
    fx.files = files;
    fx.file_count = 1;

    /* A 16-byte slice budget is honoured: the file takes several slices
     * and no chunk the engine sees is larger than the budget. */
    zd_scan_init(&scan);
    fx_register(&scan, &fx);
    ZD_CHECK_OK(zd_scan_submit(&scan, files[0].path, files[0].length));
    while (zd_scan_step(&scan, 1, 16) == 1) {
    }
    ZD_CHECK_EQ(fx.max_chunk_seen, 16U);
    ZD_CHECK_EQ(scan.stats.slices_run, 3ULL); /* 48 bytes at 16 per slice */
    ZD_CHECK_EQ(scan.stats.bytes_scanned, (uint64_t)files[0].length);
    ZD_CHECK_EQ(zd_scan_item(&scan, files[0].path)->slices, 3U);

    /* A read error fails the item; it is not reported as clean. */
    zd_scan_init(&scan);
    fx.read_calls = 0;
    fx.fail_read = 1;
    fx_register(&scan, &fx);
    ZD_CHECK_OK(zd_scan_submit(&scan, files[0].path, files[0].length));
    ZD_CHECK_EQ(zd_scan_step(&scan, 4, 4096), 0);
    ZD_CHECK_EQ(scan.stats.engine_errors, 1U);
    ZD_CHECK_EQ(scan.stats.clean, 0ULL);
    ZD_CHECK_EQ((int)zd_scan_item(&scan, files[0].path)->state,
                (int)ZD_SCAN_FAILED);

    /* An engine that never decides is a broken engine, not a clean
     * file: the item FAILS and the error is counted. */
    zd_scan_init(&scan);
    fx.fail_read = 0;
    fx.never_decide = 1;
    fx_register(&scan, &fx);
    ZD_CHECK_OK(zd_scan_submit(&scan, files[0].path, files[0].length));
    while (zd_scan_step(&scan, 1, 4096) == 1) {
    }
    ZD_CHECK_EQ(scan.stats.engine_errors, 1U);
    ZD_CHECK_EQ(scan.stats.clean, 0ULL);
    ZD_CHECK_EQ((int)zd_scan_item(&scan, files[0].path)->verdict,
                (int)ZD_SCAN_ERROR);

    /* Containment failing is reported, not swallowed: the verdict
     * stands but the item does not claim to be contained. */
    fx.never_decide = 0;
    fx.needle = "abcd";
    fx.needle_length = 4;
    zd_scan_init(&scan);
    fx_register(&scan, &fx);
    ZD_CHECK_OK(zd_scan_set_quarantine_hook(&scan, on_quarantine, 0));
    quarantine_fail = 1;
    ZD_CHECK_OK(zd_scan_submit(&scan, files[0].path, files[0].length));
    while (zd_scan_step(&scan, 1, 4096) == 1) {
    }
    ZD_CHECK_EQ(scan.stats.malicious, 1ULL);  /* detection stands */
    ZD_CHECK_EQ(scan.stats.quarantine_errors, 1U);
    ZD_CHECK_EQ(scan.stats.failed, 1ULL);
    ZD_CHECK_EQ(scan.stats.quarantined, 0ULL);
    ZD_CHECK_EQ((int)zd_scan_item(&scan, files[0].path)->state,
                (int)ZD_SCAN_FAILED);
    ZD_CHECK_EQ(zd_scan_item(&scan, files[0].path)->quarantined, 0U);
    quarantine_fail = 0;
}

static void test_audit_ring_and_names(void) {
    struct zd_scan scan;
    struct zd_scan_audit_row rows[ZD_SCAN_AUDIT];
    uint32_t index;
    uint32_t written;

    zd_scan_init(&scan);
    for (index = 0; index < ZD_SCAN_AUDIT + 4U; ++index) {
        char path[32];
        uint32_t slot;
        snprintf(path, sizeof(path), "/ram/shell/a%u", index);
        /* Reuse the bounded queue: finish each item before the next. */
        if (zd_scan_submit(&scan, path, 8) != 0) {
            while (zd_scan_step(&scan, ZD_SCAN_QUEUE, 4096) == 1) {
            }
            ZD_CHECK_OK(zd_scan_submit(&scan, path, 8));
        }
        while (zd_scan_step(&scan, ZD_SCAN_QUEUE, 4096) == 1) {
        }
        (void)slot;
    }
    /* The ring is bounded, and it says when it dropped rows. */
    ZD_CHECK_EQ(scan.audit_count, ZD_SCAN_AUDIT);
    ZD_CHECK_EQ(scan.stats.audit_dropped, 4U);
    written = zd_scan_audit(&scan, rows, ZD_SCAN_AUDIT);
    ZD_CHECK_EQ(written, ZD_SCAN_AUDIT);
    ZD_CHECK_EQ(rows[0].path[0], '/');
    ZD_CHECK_EQ(zd_scan_audit(&scan, 0, 4), 0U);

    /* Names cover the enums and refuse anything outside them. */
    ZD_CHECK(strcmp(zd_scan_verdict_name(ZD_SCAN_CLEAN), "clean") == 0);
    ZD_CHECK(strcmp(zd_scan_verdict_name(ZD_SCAN_UNAVAILABLE),
                    "unavailable") == 0);
    ZD_CHECK(strcmp(zd_scan_verdict_name(ZD_SCAN_VERDICT_COUNT), "?") == 0);
    ZD_CHECK(strcmp(zd_scan_state_name(ZD_SCAN_QUEUED), "queued") == 0);
    ZD_CHECK(strcmp(zd_scan_state_name(ZD_SCAN_STATE_COUNT), "?") == 0);
    ZD_CHECK(strcmp(zd_scan_status_name(0), "ok") == 0);
    ZD_CHECK(strcmp(zd_scan_status_name(-17), "exists") == 0);
    ZD_CHECK(strcmp(zd_scan_status_name(-28), "no-space") == 0);
    ZD_CHECK(strcmp(zd_scan_status_name(-2), "no-entry") == 0);
    ZD_CHECK(strcmp(zd_scan_status_name(-95), "unsupported-feature") == 0);
}

void zd_test_scan_suite(void) {
    printf(" suite: security scanning architecture\n");
    ZD_RUN(test_fail_closed_without_engine);
    ZD_RUN(test_submission_and_bounds);
    ZD_RUN(test_engine_pipeline);
    ZD_RUN(test_slice_budget_and_engine_failures);
    ZD_RUN(test_audit_ring_and_names);
}
