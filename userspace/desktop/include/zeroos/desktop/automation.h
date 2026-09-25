#ifndef ZEROOS_DESKTOP_AUTOMATION_H
#define ZEROOS_DESKTOP_AUTOMATION_H

/* Automation framework: a bounded, auditable event/action rule engine
 * for shell policy (Stage 5 part K).
 *
 * - Events are explicit inputs (never polled): watchdog outcomes,
 *   notifications, window lifecycle, display state, caller ticks.
 * - Actions run through injected ops (notify / settings / host
 *   callback / log) so the engine is freestanding and host-tested; the
 *   shell binds the real services.
 * - Permission-controlled: a rule fires only while its permission bit
 *   is granted (shell grants it from an explicit user choice); denials
 *   are counted and audited, never silently skipped.
 * - Bounded: fixed rule capacity, fixed audit ring, per-rule cooldown
 *   (rate limiting) and an optional hard fire cap. Overflow increments
 *   drop counters — no allocation, no unbounded history.
 * - Auditable: every fire attempt records rule/event/tick/result in a
 *   fixed ring the shell can drain for the privacy center. */

#include <zeroos/desktop/common.h>

#define ZD_AUTOMATION_MAX_RULES 32
#define ZD_AUTOMATION_AUDIT_RING 64
#define ZD_AUTOMATION_TARGET_LEN 64

enum zd_automation_event {
    ZD_AUTO_EV_SERVICE_FAILED = 1,
    ZD_AUTO_EV_NOTIFICATION_RECEIVED = 2,
    ZD_AUTO_EV_WINDOW_CLOSED = 3,
    ZD_AUTO_EV_DISPLAY_DEGRADED = 4,
    ZD_AUTO_EV_CALLER_TICK = 5,
    ZD_AUTO_EV_INPUT_IDLE = 6
};

enum zd_automation_action {
    ZD_AUTO_ACT_LOG = 1,      /* audit-only, always safe */
    ZD_AUTO_ACT_NOTIFY = 2,   /* ops->notify(target) */
    ZD_AUTO_ACT_SETTING = 3,  /* ops->set_setting(target, value) */
    ZD_AUTO_ACT_CALLBACK = 4  /* ops->callback(target, event) */
};

enum zd_automation_result {
    ZD_AUTO_RESULT_FIRED = 0,
    ZD_AUTO_RESULT_DENIED_PERMISSION = 1,
    ZD_AUTO_RESULT_RATE_LIMITED = 2,
    ZD_AUTO_RESULT_CAP_REACHED = 3,
    ZD_AUTO_RESULT_ACTION_FAILED = 4
};

struct zd_automation_ops {
    int (*notify)(void *context, const char *target);
    int (*set_setting)(void *context, const char *target, int32_t value);
    int (*callback)(void *context, const char *target, uint32_t event);
    void (*log)(void *context, const char *target, uint32_t event);
    void *context;
};

struct zd_automation_rule {
    uint32_t id;              /* assigned by add_rule (nonzero) */
    uint32_t enabled;
    uint32_t event;           /* enum zd_automation_event */
    uint32_t action;          /* enum zd_automation_action */
    char target[ZD_AUTOMATION_TARGET_LEN]; /* NUL-terminated key/name */
    int32_t setting_value;    /* used by ZD_AUTO_ACT_SETTING */
    uint64_t cooldown_ticks;  /* min ticks between fires (0 = none) */
    uint64_t fire_cap;        /* hard lifetime cap (0 = unlimited) */
    uint64_t fires;           /* successful fires */
    uint64_t last_fire_tick;
    uint32_t permission;      /* 1 = user granted; 0 = denied */
};

struct zd_automation_audit_entry {
    uint32_t rule_id;
    uint32_t event;
    uint32_t result;          /* enum zd_automation_result */
    uint32_t padding;
    uint64_t tick;
};

struct zd_automation_stats {
    uint64_t events_seen;
    uint64_t fires;
    uint64_t denied_permission;
    uint64_t rate_limited;
    uint64_t cap_reached;
    uint64_t action_failures;
    uint64_t audit_dropped;
    uint64_t rules_added;
    uint64_t rules_removed;
};

struct zd_automation {
    struct zd_automation_ops ops;
    struct zd_automation_rule rules[ZD_AUTOMATION_MAX_RULES];
    uint32_t rule_count;
    uint32_t next_id;
    struct zd_automation_audit_entry audit[ZD_AUTOMATION_AUDIT_RING];
    uint32_t audit_count;     /* audit[0] is the oldest entry */
    struct zd_automation_stats stats;
};

/* ops must be provided; missing hooks make their action kinds fail
 * cleanly (counted, audited) rather than crash. */
int zd_automation_init(struct zd_automation *engine,
                       const struct zd_automation_ops *ops);

/* Copies the rule template (id is assigned). Returns -ZD_ENOSPC when
 * full, -ZD_EINVAL on bad template (empty target, unknown event/action,
 * cooldown/cap misuse is allowed — they are plain bounds). */
int zd_automation_add(struct zd_automation *engine,
                      const struct zd_automation_rule *rule,
                      uint32_t *rule_id_out);
int zd_automation_remove(struct zd_automation *engine, uint32_t rule_id);
int zd_automation_set_permission(struct zd_automation *engine,
                                 uint32_t rule_id, uint32_t granted);
struct zd_automation_rule *zd_automation_find(struct zd_automation *engine,
                                              uint32_t rule_id);

/* Deliver one event at `now_tick`: every enabled matching rule is
 * evaluated in id order (permission → cap → cooldown → action).
 * Returns the number of rules that fired. */
uint32_t zd_automation_fire(struct zd_automation *engine, uint32_t event,
                            uint64_t now_tick);

/* Drain audit entries oldest-first (up to capacity); when `consumed`
 * is non-null it receives how many were copied. Ring overflow drops
 * the OLDEST entry and counts stats.audit_dropped. */
uint32_t zd_automation_drain_audit(struct zd_automation *engine,
                                   struct zd_automation_audit_entry *out,
                                   uint32_t capacity, uint32_t *consumed);

#endif
