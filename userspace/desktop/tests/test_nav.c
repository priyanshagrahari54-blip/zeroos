/* Browser navigation controller host tests (part E). */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/nav.h>

void zd_test_nav_suite(void) {
    struct zd_nav n;

    zd_nav_init(&n);
    ZD_CHECK(zd_nav_current(&n)[0] == 0);
    ZD_CHECK(zd_nav_back(&n) == -22);
    ZD_CHECK(zd_nav_forward(&n) == -22);
    ZD_CHECK(zd_nav_finish(&n, 0) == -22); /* nothing loading */

    /* first navigation */
    ZD_CHECK(zd_nav_go(&n, "https://example.com/") == 0);
    ZD_CHECK(n.state == ZD_NAV_LOADING);
    ZD_CHECK(zd_nav_finish(&n, 0) == 0);
    ZD_CHECK(n.state == ZD_NAV_COMMITTED);
    ZD_CHECK(strcmp(zd_nav_current(&n), "https://example.com/") == 0);

    /* blocked schemes never enter history */
    ZD_CHECK(zd_nav_go(&n, "javascript:alert(1)") == -22);
    ZD_CHECK(n.last_reject == ZD_URL_R_BAD_SCHEME);
    ZD_CHECK(zd_nav_go(&n, "data:text/html,x") == -22);
    ZD_CHECK(zd_nav_go(&n, "not a url") == -22);
    ZD_CHECK_EQ(n.stats.blocked, 3);
    ZD_CHECK(n.state == ZD_NAV_COMMITTED); /* unchanged by blocks */
    ZD_CHECK(strcmp(zd_nav_current(&n), "https://example.com/") == 0);

    /* push second entry, back/forward */
    ZD_CHECK(zd_nav_go(&n, "https://example.org/docs") == 0);
    zd_nav_finish(&n, 0);
    ZD_CHECK(zd_nav_go(&n, "http://localhost:8080/") == 0);
    zd_nav_finish(&n, 0);
    ZD_CHECK_EQ(n.count, 3);
    ZD_CHECK(zd_nav_back(&n) == 0);
    zd_nav_finish(&n, 0);
    ZD_CHECK(strcmp(zd_nav_current(&n), "https://example.org/docs") == 0);
    ZD_CHECK(zd_nav_back(&n) == 0);
    zd_nav_finish(&n, 0);
    ZD_CHECK(zd_nav_back(&n) == -22); /* at start */
    ZD_CHECK(zd_nav_forward(&n) == 0);
    zd_nav_finish(&n, 0);
    ZD_CHECK(strcmp(zd_nav_current(&n), "https://example.org/docs") == 0);

    /* navigation while loading refused for back/forward */
    ZD_CHECK(zd_nav_go(&n, "https://third.example/") == 0); /* loading */
    ZD_CHECK(zd_nav_back(&n) == -22);
    ZD_CHECK(zd_nav_forward(&n) == -22);
    /* load failure -> FAILED, history keeps entry for retry */
    ZD_CHECK(zd_nav_finish(&n, -104) == 0);
    ZD_CHECK(n.state == ZD_NAV_FAILED);
    ZD_CHECK_EQ(n.stats.load_failures, 1);
    ZD_CHECK(strcmp(zd_nav_current(&n), "https://third.example/") == 0);

    /* new go() from FAILED drops forward tail */
    ZD_CHECK(zd_nav_go(&n, "https://fourth.example/") == 0);
    zd_nav_finish(&n, 0);
    ZD_CHECK_EQ(n.stats.history_dropped >= 1, 1);

    /* history ring: push past capacity, oldest dropped, no overflow */
    {
        int i;
        char url[48];
        for (i = 0; i < ZD_NAV_HISTORY + 6; ++i) {
            {
                int j = 0;
                const char *p = "https://h";
                int k;
                for (k = 0; p[k]; ++k) url[j++] = p[k];
                url[j++] = (char)('a' + (i % 26));
                url[j++] = '.';
                url[j++] = 'x';
                url[j++] = '/';
                url[j] = 0;
            }
            ZD_CHECK(zd_nav_go(&n, url) == 0);
            zd_nav_finish(&n, 0);
        }
        ZD_CHECK(n.count <= ZD_NAV_HISTORY);
        ZD_CHECK(n.pos < n.count);
        ZD_CHECK(n.stats.history_dropped >= 6);
    }

    /* stats sanity */
    ZD_CHECK(n.stats.navigations >= ZD_NAV_HISTORY);
    ZD_CHECK(n.stats.backs >= 2); /* two successful backs, one rejected */
    ZD_CHECK(n.stats.forwards >= 1);
}
