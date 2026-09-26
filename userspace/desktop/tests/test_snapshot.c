/* Snapshot manager host tests (part B). */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/snapshot.h>

struct sn_log { int capture, restore, discard; int fail_restore, fail_discard; };
static int sn_cap(void *c, const char *n) { (void)n; ((struct sn_log *)c)->capture++; return 0; }
static int sn_res(void *c, const char *n) {
    struct sn_log *l = c;
    (void)n;
    l->restore++;
    return l->fail_restore ? -5 : 0;
}
static int sn_dis(void *c, const char *n) {
    struct sn_log *l = c;
    (void)n;
    l->discard++;
    return l->fail_discard ? -5 : 0;
}
static struct zd_snapshot_ops sn_ops(struct sn_log *l) {
    struct zd_snapshot_ops o;
    o.capture = sn_cap;
    o.restore = sn_res;
    o.discard = sn_dis;
    o.ctx = l;
    return o;
}

void zd_test_snapshot_suite(void) {
    struct zd_snapshots s;
    struct sn_log log;
    struct zd_snapshot_ops ops;

    log = (struct sn_log){0, 0, 0, 0, 0};
    ops = sn_ops(&log);
    zd_snapshots_init(&s, &ops);
    ZD_CHECK(zd_snapshots_ready_count(&s) == 0);

    /* create lifecycle: CREATING -> READY */
    ZD_CHECK(zd_snapshots_create(&s, "pre-upgrade") == 0);
    ZD_CHECK(zd_snapshots_create(&s, "second") == -16); /* one at a time */
    ZD_CHECK(zd_snapshots_create_finish(&s, 0) == 0);
    ZD_CHECK(zd_snapshots_find(&s, "pre-upgrade")->state == ZD_SNAP_READY);
    ZD_CHECK_EQ(zd_snapshots_ready_count(&s), 1);
    ZD_CHECK_EQ(s.stats.created, 1);

    /* duplicate name rejected while READY */
    ZD_CHECK(zd_snapshots_create(&s, "pre-upgrade") == -17);
    /* bad args */
    ZD_CHECK(zd_snapshots_create(&s, 0) == -22);
    ZD_CHECK(zd_snapshots_create(&s, "") == -22);
    /* finish with nothing in flight */
    ZD_CHECK(zd_snapshots_create_finish(&s, 0) == -22);

    /* failed creation keeps FAILED state, counted */
    ZD_CHECK(zd_snapshots_create(&s, "broken") == 0);
    ZD_CHECK(zd_snapshots_create_finish(&s, -5) == 0);
    ZD_CHECK(zd_snapshots_find(&s, "broken")->state == ZD_SNAP_FAILED);
    ZD_CHECK(zd_snapshots_find(&s, "broken")->last_error == 5);
    ZD_CHECK_EQ(s.stats.create_failed, 1);
    ZD_CHECK_EQ(zd_snapshots_ready_count(&s), 1);

    /* restore newest READY; hook error -> retryable, counted */
    ZD_CHECK(zd_snapshots_restore(&s, 0) == 0);
    ZD_CHECK_EQ(s.stats.restored, 1);
    log.fail_restore = 1;
    ZD_CHECK(zd_snapshots_restore(&s, "pre-upgrade") == -5);
    ZD_CHECK_EQ(s.stats.restore_failed, 1);
    ZD_CHECK(zd_snapshots_find(&s, "pre-upgrade")->state == ZD_SNAP_READY);
    log.fail_restore = 0;
    /* restore missing */
    ZD_CHECK(zd_snapshots_restore(&s, "nope") == -2);

    /* failed entries do not block; drop it so capacity math is exact */
    ZD_CHECK(zd_snapshots_discard(&s, "broken") == 0);

    /* fill capacity: prune path engages at slot exhaustion */
    {
        int i;
        char name[16];
        for (i = 0; i < 10; ++i) {
            int j = 0;
            name[j++] = 's';
            name[j++] = (char)('0' + i);
            name[j] = 0;
            if (zd_snapshots_create(&s, name) == 0)
                zd_snapshots_create_finish(&s, 0); /* all READY */
        }
        ZD_CHECK_EQ(zd_snapshots_ready_count(&s), ZD_SNAP_MAX);
        /* discard-hook failure blocks the prune */
        log.fail_discard = 1;
        ZD_CHECK(zd_snapshots_create(&s, "overflow") != 0);
        ZD_CHECK_EQ(s.stats.prune_rejected, 1);
        log.fail_discard = 0;
        ZD_CHECK(zd_snapshots_create(&s, "overflow") == 0); /* prunes */
        zd_snapshots_create_finish(&s, 0);
        ZD_CHECK_EQ(zd_snapshots_ready_count(&s), ZD_SNAP_MAX);
        /* oldest was s0 -> pruned; newest exists */
        ZD_CHECK(zd_snapshots_find(&s, "s0") == 0);
        ZD_CHECK(zd_snapshots_find(&s, "overflow") != 0);
    }

    /* explicit discard */
    ZD_CHECK(zd_snapshots_discard(&s, "overflow") == 0);
    ZD_CHECK(zd_snapshots_discard(&s, "overflow") == -2);
    ZD_CHECK(zd_snapshots_discard(&s, 0) == -22);
    ZD_CHECK(s.stats.discarded >= 2);

    /* latest ready is monotonic by seq */
    {
        struct zd_snapshot *a = zd_snapshots_latest_ready(&s);
        ZD_CHECK(a != 0 && a->state == ZD_SNAP_READY);
    }

    /* ---- production glue: update pipeline <-> snapshots ---- */
    {
        struct zd_snapshots sn2;
        struct sn_log log2;
        struct zd_snapshot_ops ops2;
        struct zd_update u;
        struct zd_update_ops uops;
        int rc;

        log2 = (struct sn_log){0, 0, 0, 0, 0};
        ops2 = sn_ops(&log2);
        zd_snapshots_init(&sn2, &ops2);

        /* bind validation */
        ZD_CHECK_EQ(zd_snapshots_bind_update(NULL, &uops), -22);
        ZD_CHECK_EQ(zd_snapshots_bind_update(&sn2, NULL), -22);
        ZD_CHECK_OK(zd_snapshots_bind_update(&sn2, &uops));
        ZD_CHECK(uops.stage_apply != 0);
        ZD_CHECK(uops.rollback != 0);
        ZD_CHECK(uops.activate == 0); /* A/B flips stay in block layer */
        ZD_CHECK(uops.commit == 0);

        /* happy path: stage captures, activation fails, rollback
         * restores the captured snapshot atomically */
        zd_update_init(&u, &uops);
        ZD_CHECK_OK(zd_update_begin(&u, "7.7.7"));
        ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_DOWNLOADING);
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_DOWNLOAD_OK));
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_VERIFY_OK));
        ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_STAGING);
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_STAGE_OK));
        ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_PREFLIGHT);
        /* snapshot captured during staging */
        ZD_CHECK(zd_snapshots_find(&sn2, "update") != 0);
        ZD_CHECK(zd_snapshots_ready_count(&sn2) >= 1);
        ZD_CHECK(log2.capture >= 1);
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_PREFLIGHT_OK));
        ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_ACTIVATING);
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_ACTIVATE_FAIL));
        ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_ROLLING_BACK);
        rc = zd_update_event(&u, ZD_UPD_EV_ROLLBACK_DONE);
        ZD_CHECK_OK(rc);
        ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_FAILED);
        ZD_CHECK_EQ(u.stats.rollbacks, 1u);
        ZD_CHECK(log2.restore >= 1); /* restore hook actually ran */

        /* fail-closed path: stage re-captures, then the snapshot is
         * removed out-of-band — rollback must find nothing and the
         * machine must fail without counting a completed rollback */
        zd_update_init(&u, &uops);
        ZD_CHECK_OK(zd_update_begin(&u, "8.0.0"));
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_DOWNLOAD_OK));
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_VERIFY_OK));
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_STAGE_OK));
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_PREFLIGHT_OK));
        ZD_CHECK_OK(zd_snapshots_discard(&sn2, "update"));
        ZD_CHECK_OK(zd_update_event(&u, ZD_UPD_EV_ACTIVATE_FAIL));
        ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_ROLLING_BACK);
        rc = zd_update_event(&u, ZD_UPD_EV_ROLLBACK_DONE);
        ZD_CHECK_EQ(rc, -2);
        ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_FAILED);
        ZD_CHECK_EQ(u.stats.rollbacks, 0u);

        /* stage capture error aborts staging (hook errno surfaces) */
        {
            struct zd_snapshots sn3;
            struct sn_log log3;
            struct zd_snapshot_ops ops3;
            struct zd_update u3;
            struct zd_update_ops u3ops;
            log3 = (struct sn_log){0, 0, 0, 0, 1}; /* discard fails */
            ops3 = sn_ops(&log3);
            zd_snapshots_init(&sn3, &ops3);
            /* pre-existing stale entry so the discard hook really runs */
            ZD_CHECK_OK(zd_snapshots_create(&sn3, "update"));
            ZD_CHECK_OK(zd_snapshots_create_finish(&sn3, 0));
            ZD_CHECK_OK(zd_snapshots_bind_update(&sn3, &u3ops));
            zd_update_init(&u3, &u3ops);
            ZD_CHECK_OK(zd_update_begin(&u3, "9.0.0"));
            ZD_CHECK_OK(zd_update_event(&u3, ZD_UPD_EV_DOWNLOAD_OK));
            ZD_CHECK_OK(zd_update_event(&u3, ZD_UPD_EV_VERIFY_OK));
            /* discard hook fails (-5) -> stage_apply surfaces it */
            rc = zd_update_event(&u3, ZD_UPD_EV_STAGE_OK);
            ZD_CHECK_EQ(rc, -5);
        }
    }
}
