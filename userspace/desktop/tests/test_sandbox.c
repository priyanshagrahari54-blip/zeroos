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
}
