/* Capability gate host tests (part B privilege separation). */
#include "test_harness.h"
#include <zeroos/desktop/capability.h>

void zd_test_capability_suite(void) {
    struct zd_caps c;
    struct zd_cap_audit_entry e[8];
    int i, n, rc;

    zd_caps_init(&c);
    ZD_CHECK(zd_caps_activate(&c, ZD_SVC_COMPOSITOR,
                              (1ULL << ZD_CAP_PRESENT)) == 0);
    ZD_CHECK(zd_caps_check(&c, ZD_SVC_COMPOSITOR, ZD_CAP_PRESENT) == 0);
    /* not granted -> denied + audited */
    ZD_CHECK(zd_caps_check(&c, ZD_SVC_COMPOSITOR, ZD_CAP_NETWORK_REMOTE) == -1);
    ZD_CHECK(c.stats.checks == 2 && c.stats.allowed == 1 && c.stats.denied == 1);

    /* inactive service cannot check or receive grants */
    ZD_CHECK(zd_caps_check(&c, ZD_SVC_BAR, ZD_CAP_INPUT) == -1);
    ZD_CHECK(zd_caps_grant(&c, ZD_SVC_BAR, ZD_CAP_INPUT) == -1);
    ZD_CHECK(zd_caps_activate(&c, ZD_SVC_BAR, (1ULL << ZD_CAP_INPUT)) == 0);
    ZD_CHECK(zd_caps_check(&c, ZD_SVC_BAR, ZD_CAP_INPUT) == 0);

    /* unknown bits and ids rejected */
    ZD_CHECK(zd_caps_activate(&c, ZD_SVC_BAR, (1ULL << 40)) == -22);
    ZD_CHECK(zd_caps_activate(&c, 99, 0) == -22);
    ZD_CHECK(zd_caps_check(&c, ZD_SVC_BAR, 99) == -22);
    ZD_CHECK(zd_caps_check(&c, 99, ZD_CAP_INPUT) == -22);

    /* runtime grant + revoke */
    ZD_CHECK(zd_caps_grant(&c, ZD_SVC_BAR, ZD_CAP_SETTINGS_WRITE) == 0);
    ZD_CHECK(zd_caps_check(&c, ZD_SVC_BAR, ZD_CAP_SETTINGS_WRITE) == 0);
    ZD_CHECK(zd_caps_revoke(&c, ZD_SVC_BAR, ZD_CAP_SETTINGS_WRITE) == 0);
    ZD_CHECK(zd_caps_check(&c, ZD_SVC_BAR, ZD_CAP_SETTINGS_WRITE) == -1);
    ZD_CHECK(zd_caps_revoke(&c, ZD_SVC_BAR, ZD_CAP_SETTINGS_WRITE) == 0); /* idem */
    ZD_CHECK_EQ(c.stats.revocations, 1);

    /* crash/stop drops grants: re-activation is not sticky */
    ZD_CHECK(zd_caps_deactivate(&c, ZD_SVC_BAR) == 0);
    ZD_CHECK(zd_caps_check(&c, ZD_SVC_BAR, ZD_CAP_INPUT) == -1);
    ZD_CHECK(zd_caps_activate(&c, ZD_SVC_BAR, 0) == 0); /* restarted bare */
    ZD_CHECK(zd_caps_check(&c, ZD_SVC_BAR, ZD_CAP_INPUT) == -1); /* gone */

    /* audit ring: wrap past capacity, newest first, seq monotonic */
    for (i = 0; i < ZD_CAP_AUDIT_RING + 10; ++i)
        (void)zd_caps_check(&c, ZD_SVC_BAR, ZD_CAP_INPUT);
    n = zd_caps_audit_recent(&c, e, 8);
    ZD_CHECK_EQ(n, 8);
    for (i = 0; i < n; ++i) {
        ZD_CHECK(e[i].service == ZD_SVC_BAR);
        if (i > 0)
            ZD_CHECK(e[i - 1].seq > e[i].seq); /* newest first */
    }
    ZD_CHECK_EQ(c.audit_count, c.stats.checks); /* every check audited */
    rc = zd_caps_audit_recent(&c, e, 0);
    ZD_CHECK(rc == -22);

    /* stats sanity across the suite */
    ZD_CHECK(c.stats.activations >= 3);
    ZD_CHECK(c.stats.deactivations >= 1);
    ZD_CHECK(c.stats.denied > 0);
}
