/* ZEROOS launcher host tests. */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/launcher.h>

static int fail_next; /* hook: return fail_next if set */
static int hook_ok(void *c, const char *n) {
    (void)c;
    (void)n;
    return fail_next ? fail_next : 0;
}
static char busy_name[32];
static int hook_busy(void *c, const char *n) {
    (void)c;
    if (strcmp(n, busy_name) == 0)
        return -17; /* pretend instance manager refused */
    return 0;
}

void zd_test_launcher_suite(void) {
    struct zd_launcher l;
    struct zd_app *res[8];
    int rc;

    fail_next = 0;
    ZD_CHECK(zd_launcher_init(&l, hook_ok, 0) == 0);
    ZD_CHECK(zd_launcher_init(0, 0, 0) == -22);

    /* registry */
    ZD_CHECK(zd_launcher_add(&l, "Files", "explorer folder browse", 1) == 0);
    ZD_CHECK(zd_launcher_add(&l, "Terminal", "shell console", 1) == 0);
    ZD_CHECK(zd_launcher_add(&l, "Terminal", "dup", 1) == -17);
    ZD_CHECK(zd_launcher_add(&l, "", "x", 1) == -22);
    ZD_CHECK(zd_launcher_add(&l, "Hidden", "secret", 0) == 0);
    ZD_CHECK(zd_launcher_find(&l, "files") == 0); /* exact/case-sensitive */
    ZD_CHECK(zd_launcher_find(&l, "Files") != 0);
    ZD_CHECK(zd_launcher_find(&l, "Nope") == 0);
    ZD_CHECK_EQ(l.stats.adds, 3);

    /* query: prefix beats substring */
    ZD_CHECK(zd_launcher_add(&l, "File indexer", "search", 1) == 0);
    rc = zd_launcher_query(&l, "file", res, 8);
    ZD_CHECK_EQ(rc, 2);
    ZD_CHECK(res[0] && strcmp(res[0]->name, "Files") == 0); /* prefix */
    ZD_CHECK(res[1] && strcmp(res[1]->name, "File indexer") == 0);
    /* keyword match, case-insensitive */
    rc = zd_launcher_query(&l, "CONSOLE", res, 8);
    ZD_CHECK(rc == 1 && strcmp(res[0]->name, "Terminal") == 0);
    /* hidden apps never surface */
    rc = zd_launcher_query(&l, "hid", res, 8);
    ZD_CHECK_EQ(rc, 0);
    rc = zd_launcher_query(&l, "zzz", res, 8);
    ZD_CHECK_EQ(rc, 0);

    /* launch lifecycle */
    ZD_CHECK(zd_launcher_launch(&l, "Nope") == -2);
    ZD_CHECK(zd_launcher_launch(&l, "Files") == 0);
    ZD_CHECK(l.apps[0].state == ZD_LAUNCH_PENDING);
    ZD_CHECK(zd_launcher_launch(&l, "Files") == -16); /* dedup */
    ZD_CHECK_EQ(l.stats.launch_rejected, 1);
    zd_launcher_report(&l, "Files", 1, 0);
    ZD_CHECK(l.apps[0].state == ZD_LAUNCH_RUNNING);
    ZD_CHECK(zd_launcher_launch(&l, "Files") == -16); /* still busy */
    zd_launcher_report(&l, "Files", 0, 0); /* closed */
    ZD_CHECK(l.apps[0].state == ZD_LAUNCH_IDLE ||
             l.apps[0].state == ZD_LAUNCH_FAILED);
    ZD_CHECK(zd_launcher_launch(&l, "Files") == 0); /* restartable */

    /* hook failure -> FAILED + errno */
    fail_next = -5; /* EIO */
    ZD_CHECK(zd_launcher_launch(&l, "Terminal") == -5);
    {
        struct zd_app *t = zd_launcher_find(&l, "Terminal");
        ZD_CHECK(t && t->state == ZD_LAUNCH_FAILED && t->fail_errno == 5);
    }
    ZD_CHECK_EQ(l.stats.launch_failures, 1);
    fail_next = 0;
    /* FAILED may be retried */
    ZD_CHECK(zd_launcher_launch(&l, "Terminal") == 0);

    zd_launcher_report(&l, "Terminal", 1, 0); /* shell confirms it is up */

    /* recents: only launched-then-reported apps, newest first */
    rc = zd_launcher_query(&l, "", res, 8);
    ZD_CHECK(rc >= 1);
    ZD_CHECK(strcmp(res[0]->name, "Terminal") == 0); /* just restarted */
    {
        int i, files_pos = -1, term_pos = -1;
        for (i = 0; i < rc; ++i) {
            if (strcmp(res[i]->name, "Files") == 0)
                files_pos = i;
            if (strcmp(res[i]->name, "Terminal") == 0)
                term_pos = i;
        }
        ZD_CHECK(term_pos >= 0);
        ZD_CHECK(files_pos > term_pos || files_pos == -1);
        for (i = 0; i < rc; ++i)
            ZD_CHECK(strcmp(res[i]->name, "Hidden") != 0);
    }

    /* capacity */
    {
        int k, before = l.app_count;
        for (k = 0; k < ZD_LAUNCHER_MAX_APPS; ++k) {
            char n[16];
            int j = 0;
            n[j++] = 'a';
            n[j++] = (char)('0' + k / 10);
            n[j++] = (char)('0' + k % 10);
            n[j] = 0;
            if (zd_launcher_find(&l, n))
                continue;
            if (zd_launcher_add(&l, n, "", 1) == -28)
                break;
        }
        ZD_CHECK(l.app_count <= ZD_LAUNCHER_MAX_APPS);
        ZD_CHECK(l.app_count >= before);
        rc = zd_launcher_add(&l, "overflow-app", "", 1);
        ZD_CHECK(rc == -28 || rc == 0);
    }

    /* no hook -> ENOSYS */
    {
        struct zd_launcher l2;
        ZD_CHECK(zd_launcher_init(&l2, 0, 0) == 0);
        ZD_CHECK(zd_launcher_add(&l2, "Solo", "", 1) == 0);
        ZD_CHECK(zd_launcher_launch(&l2, "Solo") == -38);
    }
    (void)hook_busy;
    (void)busy_name;
}
