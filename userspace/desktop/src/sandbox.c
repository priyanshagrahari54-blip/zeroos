/* Sandbox policy core.  See sandbox.h. */
#include <zeroos/desktop/sandbox.h>

static int sb_bad(void) { return -22; }
static int sb_perm(void) { return -1; }

static int sb_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

void zd_sandbox_init(struct zd_sandbox *sb) {
    int i;
    if (!sb)
        return;
    for (i = 0; i < ZD_SB_PROFILES; ++i) {
        sb->profiles[i].name[0] = 0;
        sb->profiles[i].allowed = 0;
        sb->profiles[i].in_use = 0;
        sb->profiles[i].checks = sb->profiles[i].denied = 0;
    }
    for (i = 0; i < ZD_SB_AUDIT_RING; ++i) {
        sb->audit[i].profile = 0;
        sb->audit[i].cls = 0;
        sb->audit[i].allowed = 0;
        sb->audit[i].seq = 0;
    }
    sb->audit_head = 0;
    sb->audit_count = 0;
    sb->stats.profiles_defined = sb->stats.checks = 0;
    sb->stats.denied = sb->stats.rejected = 0;
}

static int sb_find(const struct zd_sandbox *sb, const char *name) {
    int i;
    for (i = 0; i < ZD_SB_PROFILES; ++i)
        if (sb->profiles[i].in_use && sb_eq(sb->profiles[i].name, name))
            return i;
    return -1;
}

int zd_sandbox_define(struct zd_sandbox *sb, const char *name,
                      uint32_t allowed_mask) {
    int idx, i;
    uint32_t n;
    if (!sb || !name || !name[0]) {
        if (sb)
            sb->stats.rejected++;
        return sb_bad();
    }
    for (n = 0; n < ZD_SB_NAME && name[n]; ++n)
        ;
    if (n >= ZD_SB_NAME) {
        sb->stats.rejected++;
        return sb_bad();
    }
    if (allowed_mask & ~ZD_SB_ALL) {
        sb->stats.rejected++;
        return sb_bad();
    }
    idx = sb_find(sb, name);
    if (idx < 0) {
        for (i = 0; i < ZD_SB_PROFILES; ++i)
            if (!sb->profiles[i].in_use) {
                idx = i;
                break;
            }
    }
    if (idx < 0) {
        sb->stats.rejected++;
        return -28;
    }
    for (i = 0; i < (int)n; ++i)
        sb->profiles[idx].name[i] = name[i];
    sb->profiles[idx].name[n] = 0;
    sb->profiles[idx].allowed = allowed_mask;
    sb->profiles[idx].in_use = 1;
    sb->stats.profiles_defined++;
    return 0;
}

int zd_sandbox_forget(struct zd_sandbox *sb, const char *name) {
    int idx;
    if (!sb || !name)
        return sb_bad();
    idx = sb_find(sb, name);
    if (idx < 0)
        return -2;
    sb->profiles[idx].in_use = 0;
    sb->profiles[idx].name[0] = 0;
    sb->profiles[idx].allowed = 0;
    return 0;
}

struct zd_sb_profile *zd_sandbox_profile(struct zd_sandbox *sb,
                                         const char *name) {
    int idx;
    if (!sb || !name)
        return 0;
    idx = sb_find(sb, name);
    return idx >= 0 ? &sb->profiles[idx] : 0;
}

static void sb_audit(struct zd_sandbox *sb, int pidx, int cls, int ok) {
    struct zd_sb_audit *e = &sb->audit[sb->audit_head];
    e->profile = (uint8_t)pidx;
    e->cls = (uint8_t)cls;
    e->allowed = ok ? 1 : 0;
    e->seq = sb->audit_count;
    sb->audit_head = (sb->audit_head + 1) % ZD_SB_AUDIT_RING;
    sb->audit_count++;
}

int zd_sandbox_check(struct zd_sandbox *sb, const char *profile,
                     int cls) {
    int idx, ok;
    if (!sb || !profile || cls < 0 || cls >= (int)ZD_SB_CLASS_COUNT) {
        if (sb)
            sb->stats.rejected++;
        return sb_perm(); /* fail closed */
    }
    idx = sb_find(sb, profile);
    sb->stats.checks++;
    if (idx < 0) {
        sb->stats.denied++;
        sb_audit(sb, 255, cls, 0);
        return sb_perm();
    }
    sb->profiles[idx].checks++;
    ok = (sb->profiles[idx].allowed & (1u << cls)) != 0;
    if (ok) {
        sb_audit(sb, idx, cls, 1);
        return 0;
    }
    sb->profiles[idx].denied++;
    sb->stats.denied++;
    sb_audit(sb, idx, cls, 0);
    return sb_perm();
}

int zd_sandbox_audit_recent(const struct zd_sandbox *sb,
                            struct zd_sb_audit *out, int max_out) {
    int n = 0;
    if (!sb || !out || max_out <= 0)
        return sb_bad();
    while (n < max_out && (uint32_t)n < sb->audit_count) {
        uint32_t idx = (sb->audit_head + ZD_SB_AUDIT_RING - 1 -
                        (uint32_t)n) % ZD_SB_AUDIT_RING;
        out[n++] = sb->audit[idx];
    }
    return n;
}
