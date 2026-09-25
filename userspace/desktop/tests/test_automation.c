#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

/* Automation framework: permission gating, rate limiting, fire caps,
 * action dispatch through injected ops, audit ring ordering/overflow,
 * bounded capacity. */

static int notify_calls;
static int setting_calls;
static int callback_calls;
static int log_calls;
static char last_target[64];
static int32_t last_value;
static uint32_t last_event;
static int notify_fail;

static int t_notify(void *context, const char *target) {
    (void)context;
    notify_calls++;
    strcpy(last_target, target);
    return notify_fail ? -ZD_EBUSY : 0;
}

static int t_setting(void *context, const char *target, int32_t value) {
    (void)context;
    setting_calls++;
    strcpy(last_target, target);
    last_value = value;
    return 0;
}

static int t_callback(void *context, const char *target, uint32_t event) {
    (void)context;
    callback_calls++;
    strcpy(last_target, target);
    last_event = event;
    return 0;
}

static void t_log(void *context, const char *target, uint32_t event) {
    (void)context;
    (void)target;
    (void)event;
    log_calls++;
}

static struct zd_automation_ops make_ops(void) {
    struct zd_automation_ops ops;
    memset(&ops, 0, sizeof(ops));
    ops.notify = t_notify;
    ops.set_setting = t_setting;
    ops.callback = t_callback;
    ops.log = t_log;
    ops.context = 0;
    return ops;
}

static struct zd_automation_rule make_rule(uint32_t event, uint32_t action,
                                           const char *target) {
    struct zd_automation_rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.enabled = 1;
    rule.event = event;
    rule.action = action;
    strcpy(rule.target, target);
    rule.permission = 1;
    return rule;
}

static void reset_ops(void) {
    notify_calls = setting_calls = callback_calls = log_calls = 0;
    notify_fail = 0;
    last_target[0] = 0;
    last_value = 0;
    last_event = 0;
}

static void test_init_and_validation(void) {
    struct zd_automation engine;
    struct zd_automation_ops ops = make_ops();
    struct zd_automation_rule rule;
    uint32_t id = 0;

    ZD_CHECK_EQ(zd_automation_init(0, &ops), -ZD_EINVAL);
    ZD_CHECK_EQ(zd_automation_init(&engine, 0), -ZD_EINVAL);
    ZD_CHECK_EQ(zd_automation_init(&engine, &ops), 0);

    rule = make_rule(ZD_AUTO_EV_CALLER_TICK, ZD_AUTO_ACT_LOG, "audit");
    rule.target[0] = 0;
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), -ZD_EINVAL);
    rule = make_rule(999, ZD_AUTO_ACT_LOG, "audit");
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), -ZD_EINVAL);
    rule = make_rule(ZD_AUTO_EV_CALLER_TICK, 999, "audit");
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), -ZD_EINVAL);

    /* Capacity: fill to the bound. */
    for (uint32_t i = 0; i < ZD_AUTOMATION_MAX_RULES; ++i) {
        rule = make_rule(ZD_AUTO_EV_CALLER_TICK, ZD_AUTO_ACT_LOG, "audit");
        ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), 0);
        ZD_CHECK(id != 0);
    }
    rule = make_rule(ZD_AUTO_EV_CALLER_TICK, ZD_AUTO_ACT_LOG, "audit");
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), -ZD_ENOSPC);
}

static void test_permission_gate(void) {
    struct zd_automation engine;
    struct zd_automation_ops ops = make_ops();
    struct zd_automation_rule rule;
    uint32_t id = 0;

    reset_ops();
    zd_automation_init(&engine, &ops);
    rule = make_rule(ZD_AUTO_EV_SERVICE_FAILED, ZD_AUTO_ACT_NOTIFY, "svc");
    rule.permission = 0; /* not granted yet */
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), 0);

    /* Denied: no action, audited, counted. */
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_SERVICE_FAILED, 10), 0);
    ZD_CHECK_EQ(notify_calls, 0);
    ZD_CHECK_EQ(engine.stats.denied_permission, 1);

    /* Grant → fires. */
    ZD_CHECK_EQ(zd_automation_set_permission(&engine, id, 1), 0);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_SERVICE_FAILED, 11), 1);
    ZD_CHECK_EQ(notify_calls, 1);
    ZD_CHECK(strcmp(last_target, "svc") == 0);
    ZD_CHECK_EQ(engine.stats.fires, 1);

    /* Revoke → denied again. */
    ZD_CHECK_EQ(zd_automation_set_permission(&engine, id, 0), 0);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_SERVICE_FAILED, 12), 0);
    ZD_CHECK_EQ(engine.stats.denied_permission, 2);
    ZD_CHECK_EQ(zd_automation_set_permission(&engine, 9999, 1), -ZD_ENOENT);
}

static void test_rate_limit_and_cap(void) {
    struct zd_automation engine;
    struct zd_automation_ops ops = make_ops();
    struct zd_automation_rule rule;
    uint32_t id = 0;

    reset_ops();
    zd_automation_init(&engine, &ops);
    rule = make_rule(ZD_AUTO_EV_CALLER_TICK, ZD_AUTO_ACT_SETTING, "fps");
    rule.setting_value = 30;
    rule.cooldown_ticks = 5;
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), 0);

    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_CALLER_TICK, 100), 1);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_CALLER_TICK, 102), 0);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_CALLER_TICK, 104), 0);
    ZD_CHECK_EQ(engine.stats.rate_limited, 2);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_CALLER_TICK, 105), 1);
    ZD_CHECK_EQ(setting_calls, 2);
    ZD_CHECK_EQ(last_value, 30);

    /* Lifetime cap. */
    rule = make_rule(ZD_AUTO_EV_INPUT_IDLE, ZD_AUTO_ACT_LOG, "idle");
    rule.fire_cap = 1;
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), 0);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_INPUT_IDLE, 200), 1);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_INPUT_IDLE, 300), 0);
    ZD_CHECK_EQ(engine.stats.cap_reached, 1);
}

static void test_action_dispatch(void) {
    struct zd_automation engine;
    struct zd_automation_ops ops = make_ops();
    struct zd_automation_rule rule;
    uint32_t id = 0;
    struct zd_automation_audit_entry entries[8];
    uint32_t consumed = 0;

    reset_ops();
    zd_automation_init(&engine, &ops);

    rule = make_rule(ZD_AUTO_EV_WINDOW_CLOSED, ZD_AUTO_ACT_CALLBACK, "wm");
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), 0);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_WINDOW_CLOSED, 1), 1);
    ZD_CHECK_EQ(callback_calls, 1);
    ZD_CHECK_EQ(last_event, ZD_AUTO_EV_WINDOW_CLOSED);

    rule = make_rule(ZD_AUTO_EV_DISPLAY_DEGRADED, ZD_AUTO_ACT_LOG, "fb");
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), 0);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_DISPLAY_DEGRADED, 2),
                1);
    ZD_CHECK_EQ(log_calls, 1);

    /* Missing hook: counted as action failure, never a crash. */
    ops.notify = 0;
    zd_automation_init(&engine, &ops);
    rule = make_rule(ZD_AUTO_EV_SERVICE_FAILED, ZD_AUTO_ACT_NOTIFY, "x");
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), 0);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_SERVICE_FAILED, 3), 0);
    ZD_CHECK_EQ(engine.stats.action_failures, 1);

    /* Audit: fired entries drain in order with rule/event/tick. */
    reset_ops();
    zd_automation_init(&engine, &ops);
    rule = make_rule(ZD_AUTO_EV_CALLER_TICK, ZD_AUTO_ACT_LOG, "a");
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id), 0);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_CALLER_TICK, 7), 1);
    ZD_CHECK_EQ(zd_automation_fire(&engine, ZD_AUTO_EV_CALLER_TICK, 8), 1);
    ZD_CHECK_EQ(zd_automation_drain_audit(&engine, entries, 8, &consumed), 2);
    ZD_CHECK_EQ(consumed, 2);
    ZD_CHECK_EQ(entries[0].rule_id, id);
    ZD_CHECK_EQ(entries[0].result, ZD_AUTO_RESULT_FIRED);
    ZD_CHECK_EQ(entries[0].tick, 7);
    ZD_CHECK_EQ(entries[1].tick, 8);
    ZD_CHECK_EQ(engine.audit_count, 0);
}

static void test_audit_overflow_and_remove(void) {
    struct zd_automation engine;
    struct zd_automation_ops ops = make_ops();
    struct zd_automation_rule rule;
    uint32_t id_a = 0, id_b = 0;
    struct zd_automation_audit_entry entries[ZD_AUTOMATION_AUDIT_RING + 4];
    uint32_t consumed = 0;

    reset_ops();
    zd_automation_init(&engine, &ops);
    rule = make_rule(ZD_AUTO_EV_CALLER_TICK, ZD_AUTO_ACT_LOG, "a");
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id_a), 0);
    rule.enabled = 0;
    rule.target[0] = 0;
    strcpy(rule.target, "b");
    ZD_CHECK_EQ(zd_automation_add(&engine, &rule, &id_b), 0);

    /* Overflow the audit ring with denied-permission events. */
    ZD_CHECK_EQ(zd_automation_set_permission(&engine, id_a, 0), 0);
    for (uint64_t t = 1; t <= ZD_AUTOMATION_AUDIT_RING + 5; ++t)
        (void)zd_automation_fire(&engine, ZD_AUTO_EV_CALLER_TICK, t);
    ZD_CHECK_EQ(engine.audit_count, ZD_AUTOMATION_AUDIT_RING);
    ZD_CHECK_EQ(engine.stats.audit_dropped, 5);

    /* Oldest entries were dropped: first drained tick is 6. */
    ZD_CHECK_EQ(zd_automation_drain_audit(&engine, entries,
                                          ZD_AUTOMATION_AUDIT_RING + 4,
                                          &consumed),
                ZD_AUTOMATION_AUDIT_RING);
    ZD_CHECK_EQ(entries[0].tick, 6);
    ZD_CHECK_EQ(entries[consumed - 1].tick, ZD_AUTOMATION_AUDIT_RING + 5);
    ZD_CHECK_EQ(engine.audit_count, 0);

    /* Removal preserves order for remaining rules. */
    ZD_CHECK_EQ(zd_automation_remove(&engine, id_a), 0);
    ZD_CHECK_EQ(engine.rule_count, 1);
    ZD_CHECK_EQ(engine.rules[0].id, id_b);
    ZD_CHECK_EQ(zd_automation_remove(&engine, id_a), -ZD_ENOENT);
    ZD_CHECK_EQ(zd_automation_remove(&engine, 0), -ZD_EINVAL);
    ZD_CHECK(zd_automation_find(&engine, id_b) != 0);
    ZD_CHECK(zd_automation_find(&engine, 424242) == 0);
}

void zd_test_automation_suite(void) {
    zd_test_current = "automation";
    test_init_and_validation();
    test_permission_gate();
    test_rate_limit_and_cap();
    test_action_dispatch();
    test_audit_overflow_and_remove();
    zd_test_current = "main";
}
