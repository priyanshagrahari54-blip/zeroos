#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

static int decision_events;
static int degraded_events;
static enum zd_watchdog_decision last_decision;

static void on_decision(void *context, uint32_t service_id,
                        enum zd_watchdog_decision decision,
                        uint64_t delay_ns) {
    (void)context;
    (void)service_id;
    (void)delay_ns;
    ++decision_events;
    last_decision = decision;
}

static void on_degraded(void *context) {
    (void)context;
    ++degraded_events;
}

static void test_register_and_policy(void) {
    struct zd_watchdog watchdog;
    uint32_t id = 0;
    struct zd_watchdog_result result;
    zd_watchdog_init(&watchdog);
    ZD_CHECK_OK(zd_watchdog_set_listener(&watchdog, on_decision, on_degraded,
                                         0));
    ZD_CHECK_OK(zd_watchdog_register(&watchdog, "compositor", ZD_WD_ALWAYS,
                                     3, 60000000000ULL, 1000000ULL,
                                     8000000ULL, &id));
    /* Duplicate name rejected. */
    ZD_CHECK_ERR(zd_watchdog_register(&watchdog, "compositor", ZD_WD_ALWAYS,
                                      3, 0, 1000000ULL, 0, 0),
                 ZD_EBUSY);
    /* Invalid arguments rejected. */
    ZD_CHECK_ERR(zd_watchdog_register(&watchdog, "", ZD_WD_ALWAYS, 3, 0, 1, 0,
                                      0),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_watchdog_register(&watchdog, "x",
                                      (enum zd_watchdog_policy)9, 3, 0, 1, 0,
                                      0),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_watchdog_register(&watchdog, "y", ZD_WD_ALWAYS, 3, 0, 0,
                                      0, 0),
                 ZD_EINVAL); /* zero backoff base */
    ZD_CHECK_ERR(zd_watchdog_register(&watchdog, "z", ZD_WD_ALWAYS, 3, 0, 100,
                                      10, 0),
                 ZD_EINVAL); /* cap < base */

    /* Clean exit under ALWAYS still restarts; under ON_FAILURE it doesn't. */
    decision_events = 0;
    ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, 1000ULL));
    ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, id, 2000ULL, 0, &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_RESTART);
    ZD_CHECK_EQ(result.delay_ns, 1000000ULL); /* first backoff = base */
    ZD_CHECK_EQ(decision_events, 1);

    /* Backoff not due yet -> WAIT with remaining delay (due at exit+base). */
    ZD_CHECK_OK(zd_watchdog_evaluate(&watchdog, id, 2500ULL, &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_WAIT);
    ZD_CHECK_EQ(result.delay_ns, 999500ULL); /* 1002000 due - 2500 now */
    ZD_CHECK_EQ(zd_watchdog_restart_pending(&watchdog, id, 2500ULL, 0), 2U);
    /* Due -> RESTART. */
    ZD_CHECK_OK(zd_watchdog_evaluate(&watchdog, id, 3000000ULL, &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_RESTART);
    ZD_CHECK_EQ(zd_watchdog_restart_pending(&watchdog, id, 3000000ULL, 0), 0U);
}

static void test_backoff_escalation(void) {
    struct zd_watchdog watchdog;
    uint32_t id = 0;
    struct zd_watchdog_result result;
    uint32_t attempt;
    uint64_t now = 0;
    uint64_t previous_backoff = 0;
    degraded_events = 0;

    zd_watchdog_init(&watchdog);
    ZD_CHECK_OK(zd_watchdog_set_listener(&watchdog, on_decision, on_degraded,
                                         0));
    ZD_CHECK_OK(zd_watchdog_register(&watchdog, "shell", ZD_WD_ON_FAILURE, 3,
                                     0, 1000000ULL, 4000000ULL, &id));

    /* Clean exits never consume restart budget under ON_FAILURE. */
    for (attempt = 0; attempt < 10U; ++attempt) {
        ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, now));
        ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, id, now + 10ULL, 0,
                                          &result));
        ZD_CHECK_EQ(result.decision, ZD_WD_CONTINUE);
        now += 10000000ULL;
    }

    /* Three failing exits exhaust max_restarts=3, then GIVE_UP. */
    for (attempt = 0; attempt < 3U; ++attempt) {
        ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, now));
        ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, id, now + 10ULL, 1,
                                          &result));
        ZD_CHECK_EQ(result.decision, ZD_WD_RESTART);
        if (previous_backoff)
            ZD_CHECK(result.delay_ns > previous_backoff ||
                     result.delay_ns == watchdog.services[0].backoff_cap_ns);
        previous_backoff = result.delay_ns;
        ZD_CHECK(result.delay_ns <= 4000000ULL); /* capped */
        now += result.delay_ns + 1000000ULL;
    }
    ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, now));
    ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, id, now + 10ULL, 1,
                                      &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_GIVE_UP);
    ZD_CHECK_EQ(degraded_events, 1);
    ZD_CHECK(zd_watchdog_session_degraded(&watchdog));
    /* Further evaluations on a gave-up service escalate, not restart. */
    ZD_CHECK_OK(zd_watchdog_evaluate(&watchdog, id, now + 100000000ULL,
                                     &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_GIVE_UP);
    ZD_CHECK(watchdog.total_giveups >= 1U);
}

static void test_health_window_resets_budget(void) {
    struct zd_watchdog watchdog;
    uint32_t id = 0;
    struct zd_watchdog_result result;
    uint64_t now = 1000000ULL;
    zd_watchdog_init(&watchdog);
    /* Health window 100ms: a service that survives that long gets a fresh
     * restart budget after each failure. */
    ZD_CHECK_OK(zd_watchdog_register(&watchdog, "indexer", ZD_WD_ON_FAILURE,
                                     1, 100000000ULL, 1000ULL, 10000ULL,
                                     &id));
    ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, now));
    ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, id, now + 200000000ULL, 1,
                                      &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_RESTART);
    ZD_CHECK_EQ(watchdog.services[0].restart_count, 1U);
    now += 300000000ULL;
    /* Short-lived second run fails: budget already consumed -> GIVE_UP. */
    ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, now));
    ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, id, now + 1000ULL, 1,
                                      &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_GIVE_UP);
}

static void test_never_policy_and_unregister(void) {
    struct zd_watchdog watchdog;
    uint32_t id = 0;
    struct zd_watchdog_result result;
    zd_watchdog_init(&watchdog);
    ZD_CHECK_OK(zd_watchdog_register(&watchdog, "onshot", ZD_WD_NEVER, 0, 0,
                                     1000ULL, 0ULL, &id));
    ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, 100ULL));
    ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, id, 200ULL, 9, &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_CONTINUE);
    ZD_CHECK_ERR(zd_watchdog_note_exit(&watchdog, 9999, 300ULL, 1, &result),
                 ZD_ENOENT);
    ZD_CHECK_OK(zd_watchdog_unregister(&watchdog, id));
    ZD_CHECK_ERR(zd_watchdog_unregister(&watchdog, id), ZD_ENOENT);
    ZD_CHECK(zd_watchdog_service(&watchdog, id) == 0);
}

static void test_heartbeat_escalation(void) {
    struct zd_watchdog watchdog;
    uint32_t id = 0;
    struct zd_watchdog_result result;
    degraded_events = 0;
    zd_watchdog_init(&watchdog);
    ZD_CHECK_OK(zd_watchdog_set_listener(&watchdog, on_decision, on_degraded,
                                         0));
    ZD_CHECK_OK(zd_watchdog_register(&watchdog, "render", ZD_WD_ALWAYS, 3, 0,
                                     1000ULL, 10000ULL, &id));
    ZD_CHECK_OK(zd_watchdog_set_heartbeat(&watchdog, id, 5000ULL));
    ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, 1000ULL));
    ZD_CHECK_OK(zd_watchdog_evaluate(&watchdog, id, 4000ULL, &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_CONTINUE);
    /* Heartbeat extends liveness. */
    ZD_CHECK_OK(zd_watchdog_note_heartbeat(&watchdog, id, 5000ULL));
    ZD_CHECK_OK(zd_watchdog_evaluate(&watchdog, id, 9000ULL, &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_CONTINUE);
    /* Silence beyond the interval escalates. */
    ZD_CHECK_OK(zd_watchdog_evaluate(&watchdog, id, 15000ULL, &result));
    ZD_CHECK_EQ(result.decision, ZD_WD_ESCALATE);
    ZD_CHECK_EQ(degraded_events, 1);
    /* Heartbeats only valid while running. */
    ZD_CHECK_ERR(zd_watchdog_note_heartbeat(&watchdog, id, 16000ULL),
                 ZD_ESTATE);
    ZD_CHECK_ERR(zd_watchdog_set_heartbeat(&watchdog, 9999, 1), ZD_ENOENT);
}

static void test_audit_log(void) {
    struct zd_watchdog watchdog;
    struct zd_watchdog_log_entry entries[ZD_WATCHDOG_MAX_LOG];
    uint32_t count;
    uint32_t id = 0;
    uint32_t index;
    zd_watchdog_init(&watchdog);
    ZD_CHECK_OK(zd_watchdog_register(&watchdog, "svc", ZD_WD_ON_FAILURE, 5, 0,
                                     100ULL, 1000ULL, &id));
    ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, 1ULL));
    ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, id, 2ULL, 1, 0));
    count = zd_watchdog_log(&watchdog, entries, 16);
    ZD_CHECK(count >= 3U); /* register, start, exit, decision */
    /* Log wraps without corrupting order: entries carry the service id. */
    for (index = 0; index < count; ++index)
        ZD_CHECK_EQ(entries[index].service, id);
    for (index = 0; index < ZD_WATCHDOG_MAX_LOG + 5U; ++index) {
        ZD_CHECK_OK(zd_watchdog_note_start(&watchdog, id, 100ULL + index));
        ZD_CHECK_OK(zd_watchdog_note_exit(&watchdog, id, 101ULL + index, 1,
                                          0));
    }
    count = zd_watchdog_log(&watchdog, entries, ZD_WATCHDOG_MAX_LOG);
    ZD_CHECK_EQ(count, ZD_WATCHDOG_MAX_LOG);
    ZD_CHECK(strcmp(zd_watchdog_decision_name(ZD_WD_RESTART), "restart") == 0);
}

void zd_test_watchdog_suite(void) {
    printf(" suite: watchdog\n");
    ZD_RUN(test_register_and_policy);
    ZD_RUN(test_backoff_escalation);
    ZD_RUN(test_health_window_resets_budget);
    ZD_RUN(test_never_policy_and_unregister);
    ZD_RUN(test_heartbeat_escalation);
    ZD_RUN(test_audit_log);
}
