/* Sandbox policy core host tests (part B). */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/sandbox.h>

void zd_test_sandbox_suite(void) {
    struct zd_sandbox sb;
    struct zd_sb_audit e[8];
    int n;

    zd_sandbox_init(&sb);

    /* fail-closed defaults: nothing defined, everything denies */
    ZD_CHECK(zd_sandbox_check(&sb, "browser", ZD_SB_FS_READ) == -1);
    ZD_CHECK_EQ(sb.stats.denied, 1U);

    /* define profiles */
    ZD_CHECK(zd_sandbox_define(&sb, "browser",
                               (1u << ZD_SB_FS_READ) |
                               (1u << ZD_SB_NET_CLIENT) |
                               (1u << ZD_SB_DISPLAY)) == 0);
    ZD_CHECK(zd_sandbox_define(&sb, "player",
                               (1u << ZD_SB_FS_READ) |
                               (1u << ZD_SB_AUDIO)) == 0);
    ZD_CHECK(zd_sandbox_define(&sb, "browser", 1u << ZD_SB_FS_READ) == 0);
    /* re-define replaces the mask */
    ZD_CHECK(zd_sandbox_profile(&sb, "browser")->allowed ==
             (1u << ZD_SB_FS_READ));
    ZD_CHECK(zd_sandbox_define(&sb, "browser",
                               (1u << ZD_SB_FS_READ) |
                               (1u << ZD_SB_NET_CLIENT) |
                               (1u << ZD_SB_DISPLAY)) == 0);

    /* allowed and denied classes */
    ZD_CHECK(zd_sandbox_check(&sb, "browser", ZD_SB_NET_CLIENT) == 0);
    ZD_CHECK(zd_sandbox_check(&sb, "browser", ZD_SB_FS_WRITE) == -1);
    ZD_CHECK(zd_sandbox_check(&sb, "browser", ZD_SB_DEVICE) == -1);
    ZD_CHECK(zd_sandbox_check(&sb, "player", ZD_SB_AUDIO) == 0);
    ZD_CHECK(zd_sandbox_check(&sb, "player", ZD_SB_NET_CLIENT) == -1);

    /* unknown profile / bad class deny (fail closed) */
    ZD_CHECK(zd_sandbox_check(&sb, "ghost", ZD_SB_FS_READ) == -1);
    ZD_CHECK(zd_sandbox_check(&sb, "browser", 99) == -1);
    ZD_CHECK(zd_sandbox_check(&sb, 0, 0) == -1);
    ZD_CHECK(zd_sandbox_check(&sb, "browser", -3) == -1);

    /* validation */
    ZD_CHECK(zd_sandbox_define(&sb, 0, 0) == -22);
    ZD_CHECK(zd_sandbox_define(&sb, "", 0) == -22);
    ZD_CHECK(zd_sandbox_define(&sb, "bad", 1u << 30) == -22);
    ZD_CHECK(sb.stats.rejected >= 4U);

    /* per-profile counters */
    {
        struct zd_sb_profile *p = zd_sandbox_profile(&sb, "browser");
        ZD_CHECK(p != 0);
        ZD_CHECK_EQ(p->checks, 3U);  /* net ok, fs_write+device denied */
        ZD_CHECK_EQ(p->denied, 2U);
    }

    /* audit: newest first, seq monotonic */
    n = zd_sandbox_audit_recent(&sb, e, 8);
    ZD_CHECK(n > 0);
    {
        int i;
        for (i = 1; i < n; ++i)
            ZD_CHECK(e[i - 1].seq > e[i].seq);
    }
    ZD_CHECK(zd_sandbox_audit_recent(&sb, e, 0) == -22);

    /* capacity: fill 8 profiles, 9th rejected, forget frees */
    {
        int i;
        char nm[8];
        for (i = 0; i < ZD_SB_PROFILES; ++i) {
            int j = 0;
            nm[j++] = 'p';
            nm[j++] = (char)('0' + i);
            nm[j] = 0;
            if (zd_sandbox_profile(&sb, nm))
                continue;
            {
                int rc = zd_sandbox_define(&sb, nm, ZD_SB_ALL);
                ZD_CHECK(rc == 0 || rc == -28); /* cap may hit mid-loop */
            }
        }
        ZD_CHECK(zd_sandbox_define(&sb, "overflow", ZD_SB_ALL) == -28);
        ZD_CHECK(zd_sandbox_forget(&sb, "p0") == 0);
        ZD_CHECK(zd_sandbox_forget(&sb, "p0") == -2);
        ZD_CHECK(zd_sandbox_define(&sb, "overflow", ZD_SB_ALL) == 0);
        /* forgotten profile denies again */
        ZD_CHECK(zd_sandbox_check(&sb, "p0", ZD_SB_FS_READ) == -1);
    }

    /* `profiles_defined` counts the profiles that exist, not the number
     * of define() calls -- test_fault already leans on that invariant,
     * and a replacement or a forget used to break it. A recycled slot
     * also has to start from zero: history belongs to the profile that
     * earned it, not to the one that inherited the slot. */
    zd_sandbox_init(&sb);
    ZD_CHECK(zd_sandbox_define(&sb, "a", 1u << ZD_SB_FS_READ) == 0);
    ZD_CHECK(zd_sandbox_check(&sb, "a", ZD_SB_FS_READ) == 0);
    ZD_CHECK(zd_sandbox_check(&sb, "a", ZD_SB_FS_WRITE) == -1);
    ZD_CHECK(zd_sandbox_define(&sb, "a", ZD_SB_ALL) == 0); /* replace */
    ZD_CHECK_EQ(sb.stats.profiles_defined, 1U);
    {
        struct zd_sb_profile *p = zd_sandbox_profile(&sb, "a");
        ZD_CHECK(p != 0);
        if (p) {
            ZD_CHECK_EQ(p->allowed, (uint32_t)ZD_SB_ALL);
            /* same profile, new mask, counters kept */
            ZD_CHECK_EQ(p->checks, 2U);
            ZD_CHECK_EQ(p->denied, 1U);
        }
    }
    ZD_CHECK(zd_sandbox_check(&sb, "a", ZD_SB_FS_WRITE) == 0); /* replaced */
    ZD_CHECK(zd_sandbox_define(&sb, "b", ZD_SB_ALL) == 0);
    ZD_CHECK_EQ(sb.stats.profiles_defined, 2U);
    ZD_CHECK(zd_sandbox_forget(&sb, "b") == 0);
    ZD_CHECK_EQ(sb.stats.profiles_defined, 1U);
    ZD_CHECK(zd_sandbox_forget(&sb, "a") == 0);
    ZD_CHECK_EQ(sb.stats.profiles_defined, 0U);
    /* the slot "b" freed is now reused by "c", with no history carried */
    ZD_CHECK(zd_sandbox_define(&sb, "c", 1u << ZD_SB_FS_READ) == 0);
    ZD_CHECK(zd_sandbox_check(&sb, "c", ZD_SB_FS_READ) == 0);
    {
        struct zd_sb_profile *p = zd_sandbox_profile(&sb, "c");
        ZD_CHECK(p != 0);
        if (p) {
            ZD_CHECK_EQ(p->checks, 1U);
            ZD_CHECK_EQ(p->denied, 0U);
        }
    }
    ZD_CHECK_EQ(sb.stats.profiles_defined, 1U);
    /* the counter cannot run away past the profiles that can exist */
    {
        int i;
        for (i = 0; i < ZD_SB_PROFILES + 4; ++i)
            (void)zd_sandbox_define(&sb, "c", (uint32_t)i & ZD_SB_ALL);
        ZD_CHECK(sb.stats.profiles_defined <= ZD_SB_PROFILES);
    }
}
