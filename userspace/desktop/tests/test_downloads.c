/* Downloads manager tests */
#include "test_harness.h"
#include <zeroos/desktop/downloads.h>
#include <zeroos/desktop/providers.h>
#include <string.h>

static int start_calls;
static int start_reject;

static int start_hook(void *ctx, uint32_t id, const char *url) {
    (void)ctx;
    (void)id;
    if (!url || !url[0])
        return -22;
    start_calls++;
    if (start_reject)
        return -start_reject;
    return 0;
}

static int run_ok(void *ctx) {
    int *n = ctx;
    *n += 1;
    return 0;
}
void zd_test_downloads_suite(void) {
    struct zd_downloads dl;
    struct zd_dl_item *it;

    /* lifecycle */
    zd_downloads_init(&dl, start_hook, NULL);
    start_calls = 0;
    start_reject = 0;
    ZD_CHECK_EQ(zd_downloads_add(&dl, "https://a/x", "x", 100), 0);
    ZD_CHECK_EQ(zd_downloads_add(&dl, "https://a/y", "y", 0), 0);
    ZD_CHECK_EQ(dl.count, 2);
    ZD_CHECK_EQ(dl.stats.enqueued, 2);
    /* rejects */
    ZD_CHECK_EQ(zd_downloads_add(&dl, "", NULL, 0), -22);
    ZD_CHECK_EQ(zd_downloads_add(&dl, "ok", "thisnameiswaytoolongforthebuffer", 0), -22);
    ZD_CHECK_EQ(dl.stats.rejected, 2);
    /* start first */
    ZD_CHECK_EQ(zd_downloads_start_next(&dl), 0);
    ZD_CHECK_EQ(start_calls, 1);
    ZD_CHECK_EQ(zd_downloads_active(&dl), 1);
    it = zd_downloads_find(&dl, 1);
    ZD_CHECK(it != NULL);
    ZD_CHECK_EQ(it->state, ZD_DL_RUNNING);
    /* second start refused while one runs */
    ZD_CHECK_EQ(zd_downloads_start_next(&dl), -16);
    /* progress: monotonic */
    ZD_CHECK_EQ(zd_downloads_progress(&dl, 1, 40, 100), 0);
    ZD_CHECK_EQ(zd_downloads_progress(&dl, 1, 20, 0), -22);
    ZD_CHECK_EQ(dl.stats.progress_regressions, 1);
    ZD_CHECK_EQ(zd_downloads_progress(&dl, 99, 1, 0), -2);
    it = zd_downloads_find(&dl, 1);
    ZD_CHECK_EQ(it->received, 40);
    ZD_CHECK_EQ(it->total, 100);
    /* finish ok releases the slot; next starts */
    ZD_CHECK_EQ(zd_downloads_finish(&dl, 1, 0), 0);
    ZD_CHECK_EQ(dl.stats.completed, 1);
    ZD_CHECK_EQ(zd_downloads_active(&dl), 0);
    ZD_CHECK_EQ(zd_downloads_start_next(&dl), 0);
    ZD_CHECK_EQ(zd_downloads_active(&dl), 2);
    /* finish without running -> -22 */
    ZD_CHECK_EQ(zd_downloads_finish(&dl, 1, 0), -22);
    /* fail */
    ZD_CHECK_EQ(zd_downloads_finish(&dl, 2, -5), 0);
    ZD_CHECK_EQ(dl.stats.failed, 1);
    it = zd_downloads_find(&dl, 2);
    ZD_CHECK_EQ(it->state, ZD_DL_FAILED);
    ZD_CHECK_EQ(it->fail_errno, 5);
    /* cancel queued item */
    ZD_CHECK_EQ(zd_downloads_cancel(&dl, 1), -22); /* DONE is terminal */
    ZD_CHECK_EQ(zd_downloads_cancel(&dl, 2), -22); /* FAILED terminal */
    /* add one more, cancel while queued */
    ZD_CHECK_EQ(zd_downloads_add(&dl, "https://a/z", "z", 10), 0);
    ZD_CHECK_EQ(zd_downloads_cancel(&dl, 3), 0);
    ZD_CHECK_EQ(dl.stats.canceled, 1);
    ZD_CHECK_EQ(zd_downloads_start_next(&dl), -2); /* nothing runnable */

    /* start-hook failure -> FAILED, counted, slot freed */
    zd_downloads_init(&dl, start_hook, NULL);
    start_reject = 19; /* ENETUNREACH-ish */
    ZD_CHECK(zd_downloads_add(&dl, "https://b/w", "w", 5) == 0);
    ZD_CHECK(zd_downloads_start_next(&dl) < 0);
    it = zd_downloads_find(&dl, 1);
    ZD_CHECK_EQ(it->state, ZD_DL_FAILED);
    ZD_CHECK_EQ(it->fail_errno, 19);
    ZD_CHECK_EQ(dl.stats.failed, 1);
    start_reject = 0;

    /* capacity -> -28 (one item already present from above) */
    {
        int r = 0;
        while (r == 0)
            r = zd_downloads_add(&dl, "https://c/fill", "fill", 1);
        ZD_CHECK_EQ(r, -28);
        ZD_CHECK_EQ(dl.count, ZD_DL_MAX);
    }
    ZD_CHECK_EQ(dl.stats.rejected, 1); /* only the overflow */
    ZD_CHECK_EQ(zd_downloads_active(&dl), 0);
}

static uint32_t diag_line(void *ctx, uint32_t index, char *out,
                          uint32_t cap) {
    static const char *lines[3] = {"uptime:ok", "mem:42pct", "disk:71pct"};
    uint32_t i = 0;
    (void)ctx;
    if (index >= 3)
        return 0;
    while (lines[index][i] && i + 1 < cap) {
        out[i] = lines[index][i];
        ++i;
    }
    out[i] = 0;
    return 1;
}

void zd_test_providers_suite(void) {
    struct zd_cmd_provider cp;
    struct zd_diag_provider dp;
    struct zd_intent intent;
    struct zd_search_result res[8];
    int runs = 0;
    volatile uint32_t gen = 7;
    uint32_t i;

    /* commands */
    ZD_CHECK_EQ(zd_commands_init(&cp), 0);
    ZD_CHECK_EQ(zd_commands_init(NULL), -22);
    ZD_CHECK_EQ(zd_commands_register(&cp, "lock", "lock screen", run_ok,
                                     &runs), 0);
    ZD_CHECK_EQ(zd_commands_register(&cp, "logout", NULL, run_ok, &runs), 0);
    ZD_CHECK_EQ(zd_commands_register(&cp, "lock", NULL, run_ok, &runs), -17);
    ZD_CHECK_EQ(zd_commands_register(&cp, NULL, NULL, run_ok, &runs), -22);
    ZD_CHECK_EQ(zd_commands_register(&cp, "x", NULL, NULL, NULL), -22);
    ZD_CHECK_EQ(cp.count, 2);
    ZD_CHECK_EQ(cp.provider.available(cp.provider.context), 1);

    memset(&intent, 0, sizeof(intent));
    memcpy(intent.raw, "lo", 3);
    memcpy(intent.tokens[0], "lo", 3);
    intent.token_count = 1;
    /* "lo" contains-matches both; "lock" prefix-scores higher */
    {
        int n = cp.provider.query(cp.provider.context, &intent, res, 8,
                                  &gen, 7);
        ZD_CHECK_EQ(n, 2);
        for (i = 0; i < 2; ++i) {
            ZD_CHECK_EQ(res[i].kind, ZD_SEARCH_COMMAND);
            ZD_CHECK_EQ(res[i].available, 1);
        }
        if (strcmp((char *)res[0].label, "lock") == 0)
            ZD_CHECK_EQ(res[0].score, 100);
        else
            ZD_CHECK_EQ(res[1].score, 100);
    }
    /* exact query: only "logout" contains "out"? lock: contains out? l-o-c-k no */
    memcpy(intent.tokens[0], "logout", 7);
    {
        int n = cp.provider.query(cp.provider.context, &intent, res, 8,
                                  &gen, 7);
        ZD_CHECK_EQ(n, 1);
        ZD_CHECK(strcmp((char *)res[0].label, "logout") == 0);
    }
    /* cancellation honored: token != generation */
    {
        int n = cp.provider.query(cp.provider.context, &intent, res, 8,
                                  &gen, 9);
        ZD_CHECK_EQ(n, 0);
    }
    /* execute */
    ZD_CHECK_EQ(zd_commands_execute(&cp, "lock"), 0);
    ZD_CHECK_EQ(runs, 1);
    ZD_CHECK_EQ(zd_commands_execute(&cp, "lock"), 0);
    ZD_CHECK_EQ(runs, 2);
    ZD_CHECK_EQ(zd_commands_execute(&cp, "nope"), -2);
    ZD_CHECK_EQ(zd_commands_execute(&cp, NULL), -22);
    ZD_CHECK_EQ(zd_commands_execute(NULL, "x"), -22);

    /* diagnostics */
    zd_diagnostics_init(&dp, NULL, NULL);
    ZD_CHECK_EQ(dp.provider.available(dp.provider.context), 0);
    ZD_CHECK_EQ(dp.provider.query(dp.provider.context, &intent, res, 8,
                                  &gen, 7), -22); /* no line fn */
    zd_diagnostics_init(&dp, diag_line, NULL);
    ZD_CHECK_EQ(dp.provider.available(dp.provider.context), 1);
    {
        int n = dp.provider.query(dp.provider.context, &intent, res, 8,
                                  &gen, 7);
        ZD_CHECK_EQ(n, 3);
        for (i = 0; i < 3; ++i) {
            ZD_CHECK_EQ(res[i].kind, ZD_SEARCH_DIAGNOSTIC);
            ZD_CHECK_EQ(res[i].available, 1);
            ZD_CHECK(res[i].score > 0);
        }
        ZD_CHECK(strcmp((char *)res[0].label, "uptime:ok") == 0);
        /* capacity clamp */
        n = dp.provider.query(dp.provider.context, &intent, res, 2,
                              &gen, 7);
        ZD_CHECK_EQ(n, 2);
        /* canceled */
        n = dp.provider.query(dp.provider.context, &intent, res, 8,
                              &gen, 99);
        ZD_CHECK_EQ(n, 0);
    }
}
