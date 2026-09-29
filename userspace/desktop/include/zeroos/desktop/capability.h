/* Capability-based privilege separation for desktop services (part B).
 * Each userspace service activates with an explicit capability set;
 * privileged operations check before acting, denials are audited.
 * Deactivation (crash/stop) drops every grant — a restarted service
 * must be granted again (no sticky privileges).  Host-testable. */
#ifndef ZEROOS_DESKTOP_CAPABILITY_H
#define ZEROOS_DESKTOP_CAPABILITY_H

#include <stdint.h>

enum zd_service_id {
    ZD_SVC_COMPOSITOR = 0,
    ZD_SVC_BAR,
    ZD_SVC_LAUNCHER,
    ZD_SVC_AI,
    ZD_SVC_COMPAT,
    ZD_SVC_COUNT
};

enum zd_capability {
    ZD_CAP_PRESENT = 0,      /* submit frames to the display */
    ZD_CAP_INPUT,            /* read device input */
    ZD_CAP_SETTINGS_WRITE,   /* mutate persisted settings */
    ZD_CAP_NETWORK_REMOTE,   /* egress beyond the device */
    ZD_CAP_LAUNCH_APPS,      /* start application processes */
    ZD_CAP_COMPAT_SPAWN,     /* spawn Windows-compat processes */
    ZD_CAP_COUNT
};

#define ZD_CAP_AUDIT_RING 64

/* Why a check was denied.  "Never had it", "policy took it away" and
 * "the service is not running" are three different facts, and an audit
 * trail that collapses them cannot answer the question it exists to
 * answer. */
enum zd_cap_deny {
    ZD_CAP_DENY_NONE = 0,    /* allowed; no denial to explain */
    ZD_CAP_DENY_ABSENT = 1,  /* service up, capability never granted */
    ZD_CAP_DENY_REVOKED = 2, /* revoked while the service was up */
    ZD_CAP_DENY_INACTIVE = 3 /* service is down: every check fails */
};

struct zd_cap_audit_entry {
    uint8_t service;
    uint8_t capability;
    uint8_t allowed;
    uint8_t reason;          /* enum zd_cap_deny; ZD_CAP_DENY_NONE when allowed */
    uint32_t seq;
};

struct zd_caps {
    uint64_t granted[ZD_SVC_COUNT];    /* bit per zd_capability */
    uint64_t revoked[ZD_SVC_COUNT];    /* caps policy took away while up */
    uint8_t active[ZD_SVC_COUNT];      /* 1 = service is up */
    struct zd_cap_audit_entry audit[ZD_CAP_AUDIT_RING];
    uint32_t audit_head;               /* next write slot */
    uint32_t audit_count;              /* total entries ever written */
    struct {
        uint32_t checks, allowed, denied, activations,
                 deactivations, revocations;
    } stats;
};

void zd_caps_init(struct zd_caps *c);
/* Service start: must pass the full intended mask; cleared on
 * deactivate.  Inactive service re-activation counts. */
int zd_caps_activate(struct zd_caps *c, int service, uint64_t mask);
/* Service stop/crash: grants cleared immediately. */
int zd_caps_deactivate(struct zd_caps *c, int service);
/* Runtime grant/revoke while active (policy tooling).  A revoke is
 * remembered, so the audit can say a denial was a revoke rather than a
 * capability that was simply never there; the record is cleared by a
 * re-grant and by any activate/deactivate, because a restarted service
 * starts from its mask and not from what was taken away last time. */
int zd_caps_grant(struct zd_caps *c, int service, int cap);
int zd_caps_revoke(struct zd_caps *c, int service, int cap);
/* Gate for a privileged op: 0 allowed; -1 (-EPERM) denied+audited. */
int zd_caps_check(struct zd_caps *c, int service, int cap);
/* Audit read: newest first; returns entries copied (0..max). */
int zd_caps_audit_recent(const struct zd_caps *c,
                         struct zd_cap_audit_entry *out, int max_out);

#endif /* ZEROOS_DESKTOP_CAPABILITY_H */
