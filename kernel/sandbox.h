#ifndef ZEROOS_SANDBOX_H
#define ZEROOS_SANDBOX_H

#include "types.h"

/* Per-process sandbox policy. Fail-closed: any unconfigured action
 * is denied. The policy is one-way: once a process enters sandboxed
 * mode it cannot escape. */

#define ZEROOS_SANDBOX_MAX_RULES 64
#define ZEROOS_SANDBOX_MAX_PATH 256

enum zd_sandbox_action {
    ZD_SANDBOX_ALLOW = 0,
    ZD_SANDBOX_DENY  = 1
};

enum zd_sandbox_resource {
    ZD_SANDBOX_RES_MEMORY     = 1 << 0,
    ZD_SANDBOX_RES_IPC        = 1 << 1,
    ZD_SANDBOX_RES_STORAGE    = 1 << 2,
    ZD_SANDBOX_RES_NETWORK    = 1 << 3,
    ZD_SANDBOX_RES_DISPLAY    = 1 << 4,
    ZD_SANDBOX_RES_EXEC       = 1 << 5,
    ZD_SANDBOX_RES_FORK       = 1 << 6,
    ZD_SANDBOX_RES_SIGNAL     = 1 << 7,
    ZD_SANDBOX_RES_ALL        = 0xFFFF
};

struct zd_sandbox_rule {
    uint32_t id;
    uint8_t enabled;
    uint8_t action;               /* enum zd_sandbox_action */
    uint8_t resource;             /* enum zd_sandbox_resource */
    char target[ZEROOS_SANDBOX_MAX_PATH]; /* "" = any */
};

struct zd_sandbox {
    struct zd_sandbox_rule rules[ZEROOS_SANDBOX_MAX_RULES];
    uint32_t rule_count;
    uint32_t next_id;
    uint32_t allowed_resources;   /* bitmask of ZD_SANDBOX_RES_* */
    uint8_t  enforced;            /* 1 = fail-closed mode active */
    struct {
        uint32_t checks, allowed, denied, violations, rules_added, rules_removed;
    } stats;
};

void sandbox_init(struct zd_sandbox *sb);
int sandbox_add_rule(struct zd_sandbox *sb, const struct zd_sandbox_rule *rule);
int sandbox_remove_rule(struct zd_sandbox *sb, uint32_t id);
int sandbox_enforce(struct zd_sandbox *sb, uint32_t resource,
                    const char *target);
void sandbox_enforce_all(struct zd_sandbox *sb);
uint32_t sandbox_allowed_resources(const struct zd_sandbox *sb);

#endif