/* Capability gate.  See capability.h for the contract. */
#include <zeroos/desktop/capability.h>

static int cap_bad(void) { return -22; }   /* EINVAL */
static int cap_perm(void) { return -1; }   /* EPERM */

static int cap_valid(int service, int cap) {
    return service >= 0 && service < ZD_SVC_COUNT &&
           cap >= 0 && cap < ZD_CAP_COUNT;
}

void zd_caps_init(struct zd_caps *c) {
    int i;
    if (!c)
        return;
    for (i = 0; i < ZD_SVC_COUNT; ++i) {
        c->granted[i] = 0;
        c->active[i] = 0;
    }
    for (i = 0; i < ZD_CAP_AUDIT_RING; ++i) {
        c->audit[i].service = 0;
        c->audit[i].capability = 0;
        c->audit[i].allowed = 0;
        c->audit[i].revoked = 0;
        c->audit[i].seq = 0;
    }
    c->audit_head = 0;
    c->audit_count = 0;
    c->stats.checks = c->stats.allowed = c->stats.denied = 0;
    c->stats.activations = c->stats.deactivations = c->stats.revocations = 0;
}

static void cap_audit(struct zd_caps *c, int service, int cap,
                      int allowed, int revoked) {
    struct zd_cap_audit_entry *e = &c->audit[c->audit_head];
    e->service = (uint8_t)service;
    e->capability = (uint8_t)cap;
    e->allowed = allowed ? 1 : 0;
    e->revoked = revoked ? 1 : 0;
    e->seq = c->audit_count;
    c->audit_head = (c->audit_head + 1) % ZD_CAP_AUDIT_RING;
    c->audit_count++;
}

int zd_caps_activate(struct zd_caps *c, int service, uint64_t mask) {
    if (!c || service < 0 || service >= ZD_SVC_COUNT)
        return cap_bad();
    if (mask & ~((1ULL << ZD_CAP_COUNT) - 1ULL))
        return cap_bad(); /* unknown capability bits rejected */
    c->granted[service] = mask;
    c->active[service] = 1;
    c->stats.activations++;
    return 0;
}

int zd_caps_deactivate(struct zd_caps *c, int service) {
    if (!c || service < 0 || service >= ZD_SVC_COUNT)
        return cap_bad();
    c->granted[service] = 0; /* no sticky privileges across restarts */
    c->active[service] = 0;
    c->stats.deactivations++;
    return 0;
}

int zd_caps_grant(struct zd_caps *c, int service, int cap) {
    if (!c || !cap_valid(service, cap))
        return cap_bad();
    if (!c->active[service])
        return cap_perm(); /* grants attach to running services only */
    c->granted[service] |= (1ULL << cap);
    return 0;
}

int zd_caps_revoke(struct zd_caps *c, int service, int cap) {
    if (!c || !cap_valid(service, cap))
        return cap_bad();
    if (!c->active[service])
        return cap_perm();
    if (!(c->granted[service] & (1ULL << cap)))
        return 0; /* already absent: idempotent */
    c->granted[service] &= ~(1ULL << cap);
    c->stats.revocations++;
    return 0;
}

int zd_caps_check(struct zd_caps *c, int service, int cap) {
    int ok;
    if (!c || !cap_valid(service, cap))
        return cap_bad();
    c->stats.checks++;
    ok = c->active[service] && (c->granted[service] & (1ULL << cap)) != 0;
    if (ok) {
        c->stats.allowed++;
        cap_audit(c, service, cap, 1, 0);
        return 0;
    }
    c->stats.denied++;
    cap_audit(c, service, cap, 0,
              c->active[service] ? 0 : 1);
    return cap_perm();
}

int zd_caps_audit_recent(const struct zd_caps *c,
                         struct zd_cap_audit_entry *out, int max_out) {
    int n = 0;
    if (!c || !out || max_out <= 0)
        return cap_bad();
    while (n < max_out && (uint32_t)n < c->audit_count) {
        uint32_t idx = (c->audit_head + ZD_CAP_AUDIT_RING - 1 -
                        (uint32_t)n) % ZD_CAP_AUDIT_RING;
        out[n++] = c->audit[idx];
    }
    return n;
}
