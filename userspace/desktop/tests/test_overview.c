/* Shell overview/expose model tests (part A) */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>

void zd_test_overview_suite(void) {
    struct zd_overview o;
    struct zd_rect area;
    uint32_t ids[40];
    uint32_t i;

    memset(&o, 0, sizeof(o));

    area.x = 0;
    area.y = 0;
    area.w = 100;
    area.h = 100;

    /* open validation */
    ZD_CHECK_EQ(zd_overview_open(NULL, area, 2), -22);
    ZD_CHECK_EQ(zd_overview_open(&o, (struct zd_rect){0, 0, 0, 100},
                                 2), -22);
    ZD_CHECK_EQ(zd_overview_open(&o, (struct zd_rect){0, 0, 100, -5},
                                 2), -22);
    ZD_CHECK_EQ(o.stats.rejected, 2);
    ZD_CHECK_OK(zd_overview_open(&o, area, 2));

    /* empty overview */
    ZD_CHECK_EQ(zd_overview_hit(&o, 50, 50), -22);
    ZD_CHECK_EQ(zd_overview_focus_next(&o), -22);
    ZD_CHECK_EQ(zd_overview_focus_prev(&o), -22);
    ZD_CHECK_EQ(zd_overview_focused_id(&o), 0);

    /* one window: full-area thumb inside the gap inset */
    ids[0] = 101;
    ZD_CHECK_OK(zd_overview_set_windows(&o, ids, 1));
    ZD_CHECK_EQ(o.cols, 1);
    ZD_CHECK_EQ(o.rows, 1);
    ZD_CHECK_EQ(o.items[0].thumb.x, 2);
    ZD_CHECK_EQ(o.items[0].thumb.y, 2);
    ZD_CHECK_EQ(o.items[0].thumb.w, 96);
    ZD_CHECK_EQ(o.items[0].thumb.h, 96);
    ZD_CHECK_EQ(zd_overview_hit(&o, 50, 50), 0);
    ZD_CHECK_EQ(zd_overview_hit(&o, 1, 50), -2);   /* left gap */
    ZD_CHECK_EQ(zd_overview_hit(&o, 97, 97), 0);   /* inside */
    ZD_CHECK_EQ(zd_overview_hit(&o, 99, 99), -2);  /* right gap */
    ZD_CHECK_EQ(zd_overview_hit(&o, 101, 50), -2); /* outside */
    ZD_CHECK_EQ(zd_overview_focused_id(&o), 101);

    /* four windows: exact 2x2 grid */
    for (i = 0; i < 4; ++i)
        ids[i] = 200 + i;
    ZD_CHECK_OK(zd_overview_set_windows(&o, ids, 4));
    ZD_CHECK_EQ(o.cols, 2);
    ZD_CHECK_EQ(o.rows, 2);
    /* gap*(cols+1) edge-inclusive: (100-6)/2 = 47 */
    ZD_CHECK_EQ(o.items[0].thumb.x, 2);
    ZD_CHECK_EQ(o.items[1].thumb.x, 51); /* 2 + 47 + gap 2 */
    ZD_CHECK_EQ(o.items[2].thumb.y, 51);
    ZD_CHECK_EQ(o.items[0].thumb.w, 47);
    /* hit each quadrant center */
    ZD_CHECK_EQ(zd_overview_hit(&o, 25, 25), 0);
    ZD_CHECK_EQ(zd_overview_hit(&o, 75, 25), 1);
    ZD_CHECK_EQ(zd_overview_hit(&o, 25, 75), 2);
    ZD_CHECK_EQ(zd_overview_hit(&o, 75, 75), 3);
    /* gap between cells (x=50 column gap) */
    ZD_CHECK_EQ(zd_overview_hit(&o, 50, 25), -2);

    /* five windows: ceil(sqrt) -> 3 cols x 2 rows */
    for (i = 0; i < 5; ++i)
        ids[i] = 300 + i;
    ZD_CHECK_OK(zd_overview_set_windows(&o, ids, 5));
    ZD_CHECK_EQ(o.cols, 3);
    ZD_CHECK_EQ(o.rows, 2);
    ZD_CHECK_EQ(o.items[0].thumb.w, 30); /* (100 - 2*4)/3 */
    /* item4 = row1 col1: x=[34,64) y=[50,96) */
    ZD_CHECK_EQ(zd_overview_hit(&o, 49, 73), 4);
    ZD_CHECK_EQ(zd_overview_hit(&o, 80, 73), -2); /* empty cell */
    ZD_CHECK_EQ(zd_overview_hit(&o, 33, 73), -2); /* column gap */

    /* focus cycling wraps */
    ZD_CHECK_OK(zd_overview_set_windows(&o, ids, 3));
    o.focus = 0;
    ZD_CHECK_OK(zd_overview_focus_next(&o));
    ZD_CHECK_EQ(o.focus, 1);
    ZD_CHECK_EQ(zd_overview_focused_id(&o), 301);
    ZD_CHECK_OK(zd_overview_focus_next(&o));
    ZD_CHECK_OK(zd_overview_focus_next(&o));
    ZD_CHECK_EQ(o.focus, 0); /* wrapped */
    ZD_CHECK_OK(zd_overview_focus_prev(&o));
    ZD_CHECK_EQ(o.focus, 2); /* wrapped back */

    /* remove relayouts and clamps focus */
    for (i = 0; i < 4; ++i)
        ids[i] = 400 + i;
    ZD_CHECK_OK(zd_overview_set_windows(&o, ids, 4));
    o.focus = 3;
    ZD_CHECK_OK(zd_overview_remove(&o, 401));
    ZD_CHECK_EQ(o.count, 3);
    ZD_CHECK_EQ(zd_overview_remove(&o, 999), -2);
    ZD_CHECK_EQ(o.items[0].win_id, 400);
    ZD_CHECK_EQ(o.items[1].win_id, 402); /* shifted down */
    ZD_CHECK(o.focus < o.count);
    ZD_CHECK(o.stats.closes >= 1);

    /* capacity */
    for (i = 0; i < 40; ++i)
        ids[i] = 500 + i;
    ZD_CHECK_EQ(zd_overview_set_windows(&o, ids, 33), -28);
    ZD_CHECK(o.stats.rejected >= 1);
    /* exactly at cap works */
    ZD_CHECK_OK(zd_overview_set_windows(&o, ids, ZD_OVERVIEW_MAX));
    ZD_CHECK_EQ(o.count, ZD_OVERVIEW_MAX);

    /* degenerate area for a grid -> -22 (no zero-size thumbs) */
    {
        struct zd_rect tiny;
        tiny.x = 0;
        tiny.y = 0;
        tiny.w = 4;
        tiny.h = 4;
        ZD_CHECK_OK(zd_overview_open(&o, tiny, 1));
        /* 4 windows need a 2x2 grid the 4x4 area can't hold */
        ZD_CHECK_EQ(zd_overview_set_windows(&o, ids, 4), -22);
        /* 1 window still fits: (4-2)/1 = 2px thumb */
        ZD_CHECK_OK(zd_overview_set_windows(&o, ids, 1));
    }

    /* close clears */
    zd_overview_close(&o);
    ZD_CHECK_EQ(o.count, 0);
    ZD_CHECK_EQ(zd_overview_hit(&o, 1, 1), -22);
    zd_overview_close(NULL);
}
