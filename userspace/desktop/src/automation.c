#include <zeroos/desktop/automation.h>

static int target_valid(const char *target) {
    return target && target[0];
}

static int event_known(uint32_t event) {
    return event >= ZD_AUTO_EV_SERVICE_FAILED && event <= ZD_AUTO_EV_INPUT_IDLE;
}

static int action_known(uint32_t action) {
    return action >= ZD_AUTO_ACT_LOG && action <= ZD_AUTO_ACT_CALLBACK;
}

static void audit_push(struct zd_automation *engine, uint32_t rule_id,
                       uint32_t event, uint32_t result, uint64_t tick) {
    if (engine->audit_count == ZD_AUTOMATION_AUDIT_RING) {
        /* Drop the oldest: shift by moving the logical start — we keep
         * a head index semantics simple by compacting once full. */
        for (uint32_t i = 1; i < ZD_AUTOMATION_AUDIT_RING; ++i)
            engine->audit[i - 1] = engine->audit[i];
        engine->audit_count--;
        engine->stats.audit_dropped++;
    }
    engine->audit[engine->audit_count].rule_id = rule_id;
    engine->audit[engine->audit_count].event = event;
    engine->audit[engine->audit_count].result = result;
    engine->audit[engine->audit_count].padding = 0;
    engine->audit[engine->audit_count].tick = tick;
    engine->audit_count++;
}

int zd_automation_init(struct zd_automation *engine,
                       const struct zd_automation_ops *ops) {
    uint32_t i, j;

    if (!engine || !ops)
        return -ZD_EINVAL;
    engine->ops = *ops;
    engine->rule_count = 0;
    engine->next_id = 1;
    engine->audit_count = 0;
    for (i = 0; i < ZD_AUTOMATION_MAX_RULES; ++i) {
        struct zd_automation_rule *rule = &engine->rules[i];
        for (j = 0; j < sizeof(*rule); ++j)
            ((uint8_t *)rule)[j] = 0;
    }
    for (i = 0; i < sizeof(engine->stats); ++i)
        ((uint8_t *)&engine->stats)[i] = 0;
    return 0;
}

int zd_automation_add(struct zd_automation *engine,
                      const struct zd_automation_rule *rule,
                      uint32_t *rule_id_out) {
    struct zd_automation_rule *slot;

    if (!engine || !rule || !target_valid(rule->target) ||
        !event_known(rule->event) || !action_known(rule->action))
        return -ZD_EINVAL;
    if (engine->rule_count >= ZD_AUTOMATION_MAX_RULES)
        return -ZD_ENOSPC;
    slot = &engine->rules[engine->rule_count++];
    *slot = *rule;
    slot->id = engine->next_id++;
    slot->fires = 0;
    slot->last_fire_tick = 0;
    if (rule_id_out)
        *rule_id_out = slot->id;
    engine->stats.rules_added++;
    return 0;
}

struct zd_automation_rule *zd_automation_find(struct zd_automation *engine,
                                              uint32_t rule_id) {
    if (!engine || !rule_id)
        return 0;
    for (uint32_t i = 0; i < engine->rule_count; ++i)
        if (engine->rules[i].id == rule_id)
            return &engine->rules[i];
    return 0;
}

int zd_automation_remove(struct zd_automation *engine, uint32_t rule_id) {
    if (!engine || !rule_id)
        return -ZD_EINVAL;
    for (uint32_t i = 0; i < engine->rule_count; ++i) {
        if (engine->rules[i].id != rule_id)
            continue;
        for (uint32_t j = i + 1; j < engine->rule_count; ++j)
            engine->rules[j - 1] = engine->rules[j];
        engine->rule_count--;
        engine->stats.rules_removed++;
        return 0;
    }
    return -ZD_ENOENT;
}

int zd_automation_set_permission(struct zd_automation *engine,
                                 uint32_t rule_id, uint32_t granted) {
    struct zd_automation_rule *rule = zd_automation_find(engine, rule_id);

    if (!rule)
        return -ZD_ENOENT;
    rule->permission = granted ? 1U : 0U;
    return 0;
}

static int run_action(struct zd_automation *engine,
                      struct zd_automation_rule *rule, uint32_t event) {
    switch (rule->action) {
    case ZD_AUTO_ACT_LOG:
        if (engine->ops.log)
            engine->ops.log(engine->ops.context, rule->target, event);
        return 0;
    case ZD_AUTO_ACT_NOTIFY:
        if (!engine->ops.notify)
            return -ZD_EINVAL;
        return engine->ops.notify(engine->ops.context, rule->target);
    case ZD_AUTO_ACT_SETTING:
        if (!engine->ops.set_setting)
            return -ZD_EINVAL;
        return engine->ops.set_setting(engine->ops.context, rule->target,
                                       rule->setting_value);
    case ZD_AUTO_ACT_CALLBACK:
        if (!engine->ops.callback)
            return -ZD_EINVAL;
        return engine->ops.callback(engine->ops.context, rule->target, event);
    default:
        return -ZD_EINVAL;
    }
}

uint32_t zd_automation_fire(struct zd_automation *engine, uint32_t event,
                            uint64_t now_tick) {
    uint32_t fired = 0;

    if (!engine || !event_known(event))
        return 0;
    engine->stats.events_seen++;

    for (uint32_t i = 0; i < engine->rule_count; ++i) {
        struct zd_automation_rule *rule = &engine->rules[i];

        if (!rule->enabled || rule->event != event)
            continue;
        if (!rule->permission) {
            engine->stats.denied_permission++;
            audit_push(engine, rule->id, event,
                       ZD_AUTO_RESULT_DENIED_PERMISSION, now_tick);
            continue;
        }
        if (rule->fire_cap && rule->fires >= rule->fire_cap) {
            engine->stats.cap_reached++;
            audit_push(engine, rule->id, event, ZD_AUTO_RESULT_CAP_REACHED,
                       now_tick);
            continue;
        }
        if (rule->cooldown_ticks && rule->fires &&
            now_tick - rule->last_fire_tick < rule->cooldown_ticks) {
            engine->stats.rate_limited++;
            audit_push(engine, rule->id, event, ZD_AUTO_RESULT_RATE_LIMITED,
                       now_tick);
            continue;
        }
        if (run_action(engine, rule, event) != 0) {
            engine->stats.action_failures++;
            audit_push(engine, rule->id, event, ZD_AUTO_RESULT_ACTION_FAILED,
                       now_tick);
            continue;
        }
        rule->fires++;
        rule->last_fire_tick = now_tick;
        engine->stats.fires++;
        audit_push(engine, rule->id, event, ZD_AUTO_RESULT_FIRED, now_tick);
        fired++;
    }
    return fired;
}

uint32_t zd_automation_drain_audit(struct zd_automation *engine,
                                   struct zd_automation_audit_entry *out,
                                   uint32_t capacity, uint32_t *consumed) {
    uint32_t copied = 0;

    if (!engine || (!out && capacity)) {
        if (consumed)
            *consumed = 0;
        return 0;
    }
    while (copied < capacity && engine->audit_count) {
        if (out)
            out[copied] = engine->audit[0];
        for (uint32_t i = 1; i < engine->audit_count; ++i)
            engine->audit[i - 1] = engine->audit[i];
        engine->audit_count--;
        copied++;
    }
    if (consumed)
        *consumed = copied;
    return copied;
}
