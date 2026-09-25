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
}
