/* ZERO Bar host tests. */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/bar.h>

static int deny_wifi(void *c, int id, int on) {
    int *n = (int *)c;
    (void)on;
    ++*n;
    if (id == ZD_BAR_TOGGLE_WIFI)
        return -1; /* provider refuses wifi */
    return 0;
}

void zd_test_bar_suite(void) {
    struct zd_bar bar;
    int rc, action = -1, arg = -1, calls = 0;

    /* init + validation */
    ZD_CHECK(zd_bar_init(&bar, 1280, 1) == 0);
    ZD_CHECK(zd_bar_init(&bar, 0, 1) == -22);
    ZD_CHECK(zd_bar_init(&bar, 1280, 0) == -22);
    ZD_CHECK(zd_bar_init(0, 1280, 1) == -22);
    ZD_CHECK(zd_bar_physical_width(&bar) == 1280);
    bar.dpi_scale = 2;
    ZD_CHECK(zd_bar_physical_width(&bar) == 2560);
    bar.dpi_scale = 1;

    /* layout: contiguous, ends at width, title absorbs slack */
    ZD_CHECK(zd_bar_layout(&bar) == 0);
    ZD_CHECK(bar.applets[0].x == 0);
    {
        int i;
        uint32_t expect_x = 0;
        for (i = 0; i < (int)ZD_BAR_APPLET_COUNT; ++i) {
            ZD_CHECK_EQ(bar.applets[i].x, expect_x);
            expect_x += bar.applets[i].w;
        }
        ZD_CHECK_EQ(expect_x, bar.width_logical);
        ZD_CHECK(bar.applets[ZD_BAR_APPLET_TITLE].w > 240); /* slack */
    }

    /* click routing */
    rc = zd_bar_click(&bar, bar.applets[ZD_BAR_APPLET_MENU].x,
                      &action, &arg);
    ZD_CHECK(rc == 0);
    ZD_CHECK(action == ZD_BAR_ACT_OPEN_LAUNCHER);
    ZD_CHECK(bar.stats.opens == 1);
    rc = zd_bar_click(&bar, bar.applets[ZD_BAR_APPLET_NOTIFICATIONS].x,
                      &action, &arg);
    ZD_CHECK(rc == 0 && action == ZD_BAR_ACT_OPEN_NOTIFICATIONS);
    /* gap hit: after the bar */
    rc = zd_bar_click(&bar, bar.width_logical + 10, &action, &arg);
    ZD_CHECK(rc == -2 && action == ZD_BAR_ACT_NONE);
    ZD_CHECK(zd_bar_click(&bar, 0, 0, &arg) == -22);
    /* workspace cycling on click */
    bar.workspace_count = 3;
    rc = zd_bar_click(&bar, bar.applets[ZD_BAR_APPLET_WORKSPACES].x,
                      &action, &arg);
    ZD_CHECK(rc == 0 && action == ZD_BAR_ACT_SWITCH_WORKSPACE && arg == 1);
    ZD_CHECK(zd_bar_set_workspace(&bar, 3) == -22);
    ZD_CHECK(zd_bar_set_workspace(&bar, 2) == 0 && bar.workspace == 2);

    /* keyboard focus traversal covers every applet once */
    {
        int seen = 0, steps;
        for (steps = 0; steps < (int)ZD_BAR_APPLET_COUNT; ++steps) {
            rc = zd_bar_activate_focused(&bar, &action, &arg);
            ZD_CHECK(rc == 0 && action != ZD_BAR_ACT_NONE);
            seen++;
            zd_bar_focus_next(&bar);
        }
        ZD_CHECK_EQ(seen, (int)ZD_BAR_APPLET_COUNT);
        ZD_CHECK(bar.stats.focus_moves == (uint32_t)ZD_BAR_APPLET_COUNT);
    }

    /* quick toggles + denial path */
    {
        struct zd_bar_ops ops;
        ops.toggle = deny_wifi;
        ops.ctx = &calls;
        zd_bar_set_ops(&bar, &ops);
    }
    ZD_CHECK(zd_bar_toggle_set(&bar, ZD_BAR_TOGGLE_BLUETOOTH, 1) == 0);
    ZD_CHECK(bar.toggle_on[ZD_BAR_TOGGLE_BLUETOOTH] == 1);
    ZD_CHECK(calls == 1);
    rc = zd_bar_toggle_set(&bar, ZD_BAR_TOGGLE_WIFI, 1);
    ZD_CHECK(rc == -1);
    ZD_CHECK(bar.toggle_on[ZD_BAR_TOGGLE_WIFI] == 0); /* unchanged */
    ZD_CHECK(bar.toggle_denied[ZD_BAR_TOGGLE_WIFI] == 1);
    ZD_CHECK(bar.stats.denied_toggles == 1);
    ZD_CHECK(zd_bar_toggle_set(&bar, 99, 1) == -22);
    ZD_CHECK(zd_bar_toggle_set(&bar, ZD_BAR_TOGGLE_DND, 1) == 0);
    ZD_CHECK(bar.dnd_active == 1);
    ZD_CHECK(zd_bar_toggle_set(&bar, ZD_BAR_TOGGLE_DND, 0) == 0);
    ZD_CHECK(bar.dnd_active == 0);

    /* badge + title */
    zd_bar_set_notif_count(&bar, 5);
    ZD_CHECK(bar.notif_count == 5);
    zd_bar_set_title(&bar, "very-long-window-title-that-truncates-into-the-64-byte-buffer-safely");
    ZD_CHECK(strlen(bar.title) < 64);
    zd_bar_set_title(&bar, 0);
    ZD_CHECK(bar.title[0] == 0);

    ZD_CHECK(bar.stats.toggles == 3); /* bluetooth, dnd on, dnd off */
}
