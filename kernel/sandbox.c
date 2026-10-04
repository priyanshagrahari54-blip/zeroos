#include "sandbox.h"
#include "kstring.h"
#include "memory.h"

void sandbox_init(struct zd_sandbox *sb) {
    if (!sb) return;
    memset(sb->rules, 0, sizeof(sb->rules));
    sb->rule_count = 0;
    sb->next_id = 1;
    sb->allowed_resources = 0;
    sb->enforced = 0;
    memset(&sb->stats, 0, sizeof(sb->stats));
}

static int rule_valid(const struct zd_sandbox_rule *r) {
    if (!r) return 0;
    if (r->resource == 0 || r->resource & ~ZD_SANDBOX_RES_ALL) return 0;
    if (r->action != ZD_SANDBOX_ALLOW && r->action != ZD_SANDBOX_DENY) return 0;
    /* target must be NUL-terminated within the buffer */
    int terminated = 0;
    for (int i = 0; i < ZEROOS_SANDBOX_MAX_PATH; ++i) {
        if (r->target[i] == '\0') { terminated = 1; break; }
        if ((unsigned char)r->target[i] < 0x20) return 0;
    }
    return terminated;
}

int sandbox_add_rule(struct zd_sandbox *sb,
                       const struct zd_sandbox_rule *rule) {
    if (!sb || !rule || !rule_valid(rule)) return -22;
    if (sb->rule_count >= ZEROOS_SANDBOX_MAX_RULES) return -28;

    struct zd_sandbox_rule *r = &sb->rules[sb->rule_count];
    *r = *rule;
    r->id = sb->next_id++;
    r->enabled = 1;
    sb->rule_count++;
    sb->stats.rules_added++;
    return 0;
}

int sandbox_remove_rule(struct zd_sandbox *sb, uint32_t id) {
    if (!sb) return -22;
    for (uint32_t i = 0; i < sb->rule_count; ++i) {
        if (sb->rules[i].id == id && sb->rules[i].enabled) {
            for (uint32_t j = i + 1; j < sb->rule_count; ++j)
                sb->rules[j - 1] = sb->rules[j];
            sb->rule_count--;
            sb->stats.rules_removed++;
            return 0;
        }
    }
    return -2;
}

/* Evaluate a single rule against a (resource, target) pair.
 * Returns 1 if the rule matches, 0 otherwise. */
static int rule_matches(const struct zd_sandbox_rule *r,
                          uint32_t resource, const char *target) {
    if (!r->enabled) return 0;
    if (!(r->resource & resource)) return 0;
    if (r->target[0]) {
        if (!target) return 0;
        for (uint32_t i = 0; i < ZEROOS_SANDBOX_MAX_PATH; ++i) {
            if (r->target[i] != target[i]) return 0;
            if (r->target[i] == '\0') return 1;
        }
        return 0;
    }
    return 1;
}

int sandbox_enforce(struct zd_sandbox *sb, uint32_t resource,
                      const char *target) {
    if (!sb) return -22;
    sb->stats.checks++;

    /* Fail-closed: if no rules exist and enforcement is active, deny. */
    if (sb->enforced && sb->rule_count == 0) {
        sb->stats.denied++;
        sb->stats.violations++;
        return ZD_SANDBOX_DENY;
    }

    /* Walk rules in insertion order; first match wins. */
    for (uint32_t i = 0; i < sb->rule_count; ++i) {
        if (rule_matches(&sb->rules[i], resource, target)) {
            if (sb->rules[i].action == ZD_SANDBOX_ALLOW) {
                sb->stats.allowed++;
                return ZD_SANDBOX_ALLOW;
            }
            sb->stats.denied++;
            sb->stats.violations++;
            return ZD_SANDBOX_DENY;
        }
    }

    /* No match = fail-closed when enforced, else deny. */
    sb->stats.denied++;
    if (sb->enforced) sb->stats.violations++;
    return ZD_SANDBOX_DENY;
}

void sandbox_enforce_all(struct zd_sandbox *sb) {
    if (!sb) return;
    sb->enforced = 1;
}

uint32_t sandbox_allowed_resources(const struct zd_sandbox *sb) {
    if (!sb) return 0;
    return sb->allowed_resources;
}