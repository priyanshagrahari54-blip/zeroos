#include "test_harness.h"
#include <zeroos/desktop/ai.h>

/* --- backend ops used across tests -------------------------------- */

static int ops_select_allow_remote(void *ctx, const struct zd_ai_request *r,
                                   uint32_t grants,
                                   enum zd_ai_backend *out) {
    (void)ctx;
    (void)r;
    (void)grants;
    *out = ZD_AI_BACKEND_REMOTE;
    return 0;
}

static int ops_select_local(void *ctx, const struct zd_ai_request *r,
                            uint32_t grants, enum zd_ai_backend *out) {
    (void)ctx;
    (void)r;
    (void)grants;
    *out = ZD_AI_BACKEND_LOCAL;
    return 0;
}

static int ops_select_denied(void *ctx, const struct zd_ai_request *r,
                             uint32_t grants, enum zd_ai_backend *out) {
    (void)ctx;
    (void)r;
    (void)grants;
    *out = ZD_AI_BACKEND_NONE;
    return -ZD_EPERM;
}

static int ops_run_ok(void *ctx, enum zd_ai_backend backend,
                      const struct zd_ai_request *req, char *out,
                      uint32_t out_cap, uint32_t *out_len) {
    (void)ctx;
    if (backend == ZD_AI_BACKEND_NONE || out_cap < 8)
        return -ZD_EINVAL;
    /* Self-check: the payload must be intact DURING execution; every
     * test submits make_req()'s "hi" marker. */
    if (req->payload[0] != 'h' || req->payload[1] != 'i')
        return -ZD_ESTATE;
    out[0] = 'o';
    out[1] = 'k';
    *out_len = 2;
    return 0;
}

static int ops_run_fail(void *ctx, enum zd_ai_backend backend,
                        const struct zd_ai_request *req, char *out,
                        uint32_t out_cap, uint32_t *out_len) {
    (void)ctx;
    (void)backend;
    (void)req;
    (void)out;
    (void)out_cap;
    (void)out_len;
    return -1;
}

static struct zd_ai_request make_req(uint32_t id, uint32_t ctx_mask,
                                     uint8_t want_remote) {
    struct zd_ai_request r;
    uint32_t i;

    for (i = 0; i < sizeof(r); ++i)
        ((uint8_t *)&r)[i] = 0;
    r.id = id;
    r.kind = ZD_AI_REQ_COMPLETE;
    r.context_mask = ctx_mask;
    r.want_remote = want_remote;
    r.payload[0] = 'h';
    r.payload[1] = 'i';
    return r;
}

/* --- permission gate ------------------------------------------------ */

static void test_permission_gate(void) {
    struct zd_ai_broker b;
    struct zd_ai_request r = make_req(1, ZD_AI_GRANT_CONTEXT_FILES, 0);
    uint32_t done = 0;

    zd_ai_broker_init(&b, 0, 0);
    ZD_CHECK_EQ(b.active, 0); /* dormant at birth */
    ZD_CHECK_EQ(zd_ai_submit(&b, &r), -ZD_EPERM);
    ZD_CHECK_EQ(b.stats.denied_permission, 1u);
    ZD_CHECK_EQ(b.queued, 0);
    ZD_CHECK_EQ(b.active, 0);

    zd_ai_grant(&b, ZD_AI_GRANT_CONTEXT_FILES);
    ZD_CHECK_OK(zd_ai_submit(&b, &r));
    ZD_CHECK_EQ(b.queued, 1);
    ZD_CHECK_EQ(b.active, 1);
    ZD_CHECK_EQ(b.stats.wakeups, 1u);

    /* Revoke before drain: request already accepted, backend selection
     * under current grants decides. */
    zd_ai_revoke(&b, ZD_AI_GRANT_CONTEXT_FILES);
    {
        struct zd_ai_ops ops = {ops_select_denied, ops_run_ok, 0};
        b.ops = ops;
    }
    ZD_CHECK_OK(zd_ai_drain(&b, 8, &done));
    ZD_CHECK_EQ(done, 0);
    ZD_CHECK_EQ(b.stats.denied_permission, 2u);
    ZD_CHECK_EQ(b.queued, 0);
    ZD_CHECK_EQ(b.active, 0); /* dormant again */
}

/* --- downgrade and egress ------------------------------------------- */

static void test_remote_downgrade(void) {
    struct zd_ai_broker b;
    struct zd_ai_ops ops = {ops_select_allow_remote, ops_run_ok, 0};
    struct zd_ai_request r = make_req(7, 0, 1);
    uint32_t done = 0;

    zd_ai_broker_init(&b, &ops, ZD_AI_GRANT_ALL);
    ZD_CHECK_OK(zd_ai_submit(&b, &r));
    zd_ai_revoke(&b, ZD_AI_GRANT_REMOTE_EGRESS);
    ZD_CHECK_OK(zd_ai_drain(&b, 4, &done));
    ZD_CHECK_EQ(done, 1);
    ZD_CHECK_EQ(b.stats.backend_downgrades, 1u);
    ZD_CHECK_EQ(b.stats.completed, 1u);

    /* With the egress grant restored, remote is honoured. */
    zd_ai_grant(&b, ZD_AI_GRANT_REMOTE_EGRESS);
    r = make_req(8, 0, 1);
    ZD_CHECK_OK(zd_ai_submit(&b, &r));
    ZD_CHECK_OK(zd_ai_drain(&b, 4, &done));
    ZD_CHECK_EQ(done, 1);
    ZD_CHECK_EQ(b.stats.backend_downgrades, 1u); /* unchanged */
    ZD_CHECK_EQ(b.stats.completed, 2u);
    ZD_CHECK_EQ(b.stats.wakeups, 2u);
}

/* --- queue bounds ---------------------------------------------------- */

static void test_queue_bounds(void) {
    struct zd_ai_broker b;
    struct zd_ai_ops ops = {ops_select_local, ops_run_ok, 0};
    uint32_t i, done = 0;

    zd_ai_broker_init(&b, &ops, ZD_AI_GRANT_ALL);
    for (i = 1; i <= ZD_AI_QUEUE_DEPTH; ++i) {
        struct zd_ai_request r = make_req(i, 0, 0);
        ZD_CHECK_OK(zd_ai_submit(&b, &r));
    }
    {
        struct zd_ai_request r = make_req(99, 0, 0);
        ZD_CHECK_EQ(zd_ai_submit(&b, &r), -ZD_ENOSPC);
    }
    ZD_CHECK_EQ(b.stats.queue_dropped, 1u);
    ZD_CHECK_EQ(b.stats.wakeups, 1u); /* one dormant -> active edge */

    /* Drain with max_out=1 completes exactly one and stays active. */
    ZD_CHECK_OK(zd_ai_drain(&b, 1, &done));
    ZD_CHECK_EQ(done, 1);
    ZD_CHECK_EQ(b.queued, ZD_AI_QUEUE_DEPTH - 1);
    ZD_CHECK_EQ(b.active, 1);

    ZD_CHECK_OK(zd_ai_drain(&b, ZD_AI_QUEUE_DEPTH, &done));
    ZD_CHECK_EQ(done, ZD_AI_QUEUE_DEPTH - 1);
    ZD_CHECK_EQ(b.queued, 0);
    ZD_CHECK_EQ(b.active, 0);
    ZD_CHECK_EQ(b.stats.completed, ZD_AI_QUEUE_DEPTH);
}

/* --- missing/failing hooks are failures ------------------------------ */

static void test_failure_paths(void) {
    struct zd_ai_broker b;
    struct zd_ai_request r = make_req(3, 0, 0);
    uint32_t done = 0;

    /* No ops at all. */
    zd_ai_broker_init(&b, 0, ZD_AI_GRANT_ALL);
    ZD_CHECK_OK(zd_ai_submit(&b, &r));
    ZD_CHECK_OK(zd_ai_drain(&b, 4, &done));
    ZD_CHECK_EQ(done, 0);
    ZD_CHECK_EQ(b.stats.run_failures, 1u);
    ZD_CHECK_EQ(b.queued, 0);

    /* run() error. */
    {
        struct zd_ai_ops ops = {ops_select_local, ops_run_fail, 0};
        zd_ai_broker_init(&b, &ops, ZD_AI_GRANT_ALL);
    }
    ZD_CHECK_OK(zd_ai_submit(&b, &r));
    ZD_CHECK_OK(zd_ai_drain(&b, 4, &done));
    ZD_CHECK_EQ(done, 0);
    ZD_CHECK_EQ(b.stats.run_failures, 1u);

    /* select() hard denial. */
    {
        struct zd_ai_ops ops = {ops_select_denied, ops_run_ok, 0};
        zd_ai_broker_init(&b, &ops, ZD_AI_GRANT_ALL);
    }
    ZD_CHECK_OK(zd_ai_submit(&b, &r));
    ZD_CHECK_OK(zd_ai_drain(&b, 4, &done));
    ZD_CHECK_EQ(done, 0);
    ZD_CHECK_EQ(b.stats.denied_permission, 1u);

    ZD_CHECK_EQ(zd_ai_drain(&b, 4, 0), -ZD_EINVAL); /* null out */
}

/* --- minimal resident state ------------------------------------------ */

static void test_minimal_resident_state(void) {
    struct zd_ai_broker b;
    struct zd_ai_ops ops = {ops_select_local, ops_run_ok, 0};
    struct zd_ai_request r = make_req(5, 0, 0);
    uint32_t done = 0;

    zd_ai_broker_init(&b, &ops, ZD_AI_GRANT_ALL);
    ZD_CHECK_OK(zd_ai_submit(&b, &r));
    ZD_CHECK_OK(zd_ai_drain(&b, 4, &done));
    ZD_CHECK_EQ(done, 1);

    /* Everything wiped: slot inactive, payload zeroed, stats report
     * zero resident bytes without PERSIST. */
    for (uint32_t i = 0; i < ZD_AI_QUEUE_DEPTH; ++i) {
        ZD_CHECK_EQ(b.queue[i].active, 0);
        ZD_CHECK_EQ(b.queue[i].id, 0);
        for (uint32_t k = 0; k < sizeof(b.queue[i].payload); ++k)
            ZD_CHECK_EQ(b.queue[i].payload[k], 0);
    }
    ZD_CHECK_EQ(b.stats.resident_bytes_after_drain, 0u);

    /* Submitting on a dormant broker without grants still denies. */
    zd_ai_revoke(&b, ZD_AI_GRANT_ALL);
    r = make_req(6, ZD_AI_GRANT_CONTEXT_SELECTION, 0);
    ZD_CHECK_EQ(zd_ai_submit(&b, &r), -ZD_EPERM);
    ZD_CHECK_EQ(zd_ai_submit(&b, 0), -ZD_EINVAL);
}

void zd_test_ai_suite(void) {
    printf("  suite: ai broker\n");
    ZD_RUN(test_permission_gate);
    ZD_RUN(test_remote_downgrade);
    ZD_RUN(test_queue_bounds);
    ZD_RUN(test_failure_paths);
    ZD_RUN(test_minimal_resident_state);
}
