#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

static int lifecycle_listener_calls;
static enum zd_lifecycle_state lifecycle_listener_last_next;

static void lifecycle_listener(void *context, enum zd_lifecycle_state previous,
                               enum zd_lifecycle_state next,
                               enum zd_lifecycle_event cause) {
    (void)context;
    (void)previous;
    (void)cause;
    ++lifecycle_listener_calls;
    lifecycle_listener_last_next = next;
}

static void test_legal_path(void) {
    struct zd_lifecycle lifecycle;
    zd_lifecycle_init(&lifecycle);
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_STOPPED);
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_START, 0));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_DORMANT);
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_CONTROLLERS_UP, 1));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_WARM);
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_ACTIVATE, 2));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_ACTIVE);
    ZD_CHECK(zd_lifecycle_allows_heavy_work(&lifecycle));
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_PRESSURE, 3));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_THROTTLED);
    ZD_CHECK(!zd_lifecycle_allows_heavy_work(&lifecycle));
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_SUSPEND, 4));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_SUSPENDED);
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_RESUME, 5));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_THROTTLED);
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_PRESSURE_RELEASED, 6));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_ACTIVE);
    ZD_CHECK_EQ(lifecycle.transition_count, 7U);
}

static void test_illegal_events(void) {
    struct zd_lifecycle lifecycle;
    uint32_t before;
    zd_lifecycle_init(&lifecycle);
    /* ACTIVATE from STOPPED is illegal. */
    ZD_CHECK_ERR(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_ACTIVATE, 0),
                 ZD_EINVAL);
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_STOPPED);
    ZD_CHECK_EQ(lifecycle.illegal_event_count, 1U);
    /* STOP from ACTIVE is illegal: must leave ACTIVE first. */
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_START, 0));
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_CONTROLLERS_UP, 0));
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_ACTIVATE, 0));
    before = lifecycle.illegal_event_count;
    ZD_CHECK_ERR(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_STOP, 0),
                 ZD_EINVAL);
    ZD_CHECK_EQ(lifecycle.illegal_event_count, before + 1U);
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_ACTIVE);
    /* Out-of-range event. */
    ZD_CHECK_ERR(zd_lifecycle_dispatch(&lifecycle,
                                       (enum zd_lifecycle_event)99, 0),
                 ZD_EINVAL);
}

static void test_failure_isolation_path(void) {
    struct zd_lifecycle lifecycle;
    zd_lifecycle_init(&lifecycle);
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_START, 0));
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_CONTROLLERS_UP, 0));
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_ACTIVATE, 0));
    /* A desktop service failure degrades but does not stop the session. */
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_SERVICE_FAILURE, 0));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_THROTTLED);
    ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_USER_ACTIVITY, 0));
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), ZD_LIFECYCLE_ACTIVE);
}

static void test_listeners_and_history(void) {
    struct zd_lifecycle lifecycle;
    uint32_t index;
    lifecycle_listener_calls = 0;
    lifecycle_listener_last_next = ZD_LIFECYCLE_STOPPED;
    zd_lifecycle_init(&lifecycle);
    ZD_CHECK_OK(zd_lifecycle_add_listener(&lifecycle, lifecycle_listener, 0));
    /* Fill history beyond the ring to verify bounded wrap. Each iteration
     * walks STOPPED -> ... -> STOPPED legally (two STOPs: ACTIVE-side path
     * lands in DORMANT, second STOP returns to STOPPED). */
    for (index = 0; index < 40U; ++index) {
        ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_START, 0));
        ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_CONTROLLERS_UP, 0));
        ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_ACTIVATE, 0));
        ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_USER_IDLE, 0));
        ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_STOP, 0));
        ZD_CHECK_OK(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_STOP, 0));
    }
    ZD_CHECK(lifecycle.transition_count >= 40U);
    ZD_CHECK_EQ(lifecycle.history_count, ZD_LIFECYCLE_MAX_HISTORY);
    ZD_CHECK_EQ(lifecycle_listener_calls, (int)lifecycle.transition_count);
    ZD_CHECK_EQ(lifecycle_listener_last_next, ZD_LIFECYCLE_STOPPED);
    /* Latest history entry must be the last transition. */
    ZD_CHECK_EQ(lifecycle.history[(lifecycle.history_head +
                                   ZD_LIFECYCLE_MAX_HISTORY - 1U) %
                                  ZD_LIFECYCLE_MAX_HISTORY].sequence,
                lifecycle.transition_count);
}

static void test_state_names(void) {
    ZD_CHECK(strcmp(zd_lifecycle_state_name(ZD_LIFECYCLE_ACTIVE), "ACTIVE") == 0);
    ZD_CHECK(strcmp(zd_lifecycle_event_name(ZD_LIFECYCLE_SUSPEND), "SUSPEND") == 0);
    ZD_CHECK(strcmp(zd_lifecycle_state_name((enum zd_lifecycle_state)99), "?") == 0);
}

void zd_test_lifecycle_suite(void) {
    printf(" suite: lifecycle\n");
    ZD_RUN(test_legal_path);
    ZD_RUN(test_illegal_events);
    ZD_RUN(test_failure_isolation_path);
    ZD_RUN(test_listeners_and_history);
    ZD_RUN(test_state_names);
}
