/* Application sandbox policy core (Stage 5 part B).
 * Named profiles carry an explicit capability mask; every mediated
 * operation asks check() before acting.  Fail-closed: unknown profile
 * or unknown class denies.  Denials are counted and auditable; the
 * enforcement hook (seccomp/namespace binding) attaches later. */
#ifndef ZEROOS_DESKTOP_SANDBOX_H
#define ZEROOS_DESKTOP_SANDBOX_H

#include <stdint.h>

#define ZD_SB_PROFILES 8
#define ZD_SB_NAME 32
#define ZD_SB_AUDIT_RING 32

enum zd_sb_class {
    ZD_SB_FS_READ = 0,
    ZD_SB_FS_WRITE,
    ZD_SB_NET_CLIENT,
    ZD_SB_NET_SERVER,
    ZD_SB_PROC_SPAWN,
    ZD_SB_DISPLAY,
    ZD_SB_AUDIO,
    ZD_SB_DEVICE,          /* raw devices (usb/serial/...) */
    ZD_SB_CLASS_COUNT
};
#define ZD_SB_ALL ((1u << ZD_SB_CLASS_COUNT) - 1u)

struct zd_sb_audit {
    uint8_t profile;
    uint8_t cls;
    uint8_t allowed;
    uint32_t seq;
};

struct zd_sb_profile {
    char name[ZD_SB_NAME];
    uint32_t allowed;         /* bitmask of enum zd_sb_class */
    uint8_t in_use;
    uint32_t checks, denied;
};

struct zd_sandbox {
    struct zd_sb_profile profiles[ZD_SB_PROFILES];
    struct zd_sb_audit audit[ZD_SB_AUDIT_RING];
    uint32_t audit_head, audit_count;
    struct { uint32_t profiles_defined, checks, denied, rejected; } stats;
};

void zd_sandbox_init(struct zd_sandbox *sb);
/* Define/replace a profile's mask.  Bad mask/name -> -22, full -> -28. */
int zd_sandbox_define(struct zd_sandbox *sb, const char *name,
                      uint32_t allowed_mask);
int zd_sandbox_forget(struct zd_sandbox *sb, const char *name);
struct zd_sb_profile *zd_sandbox_profile(struct zd_sandbox *sb,
                                         const char *name);
/* Gate one operation: 0 allowed, -1 (-EPERM) denied (unknown profile
 * and unknown class both deny — fail closed). */
int zd_sandbox_check(struct zd_sandbox *sb, const char *profile,
                     int cls);
int zd_sandbox_audit_recent(const struct zd_sandbox *sb,
                            struct zd_sb_audit *out, int max_out);

#endif /* ZEROOS_DESKTOP_SANDBOX_H */
