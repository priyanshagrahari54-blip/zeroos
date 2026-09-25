#include "test_harness.h"
#include <zeroos/desktop/browser.h>

static void test_open_close_bounds(void) {
    struct zd_browser b;
    uint32_t id = 0, ids[ZD_BROWSER_MAX_TABS];
    uint32_t i;
    enum zd_tab_state st;

    zd_browser_init(&b, 100, 300, 900);
    ZD_CHECK_EQ(b.idle_ticks, 100u);
    ZD_CHECK_EQ(b.freeze_ticks, 300u);
    ZD_CHECK_EQ(b.discard_ticks, 900u);

    for (i = 0; i < ZD_BROWSER_MAX_TABS; ++i) {
        ZD_CHECK_OK(zd_browser_open(&b, 1024, &ids[i]));
        ZD_CHECK(ids[i] != 0);
    }
    ZD_CHECK_EQ(zd_browser_open(&b, 1024, &id), -ZD_ENOSPC);
    ZD_CHECK_EQ(b.tab_count, ZD_BROWSER_MAX_TABS);
    ZD_CHECK_EQ(b.stats.opened, ZD_BROWSER_MAX_TABS);

    /* Ids are unique. */
    for (i = 0; i < ZD_BROWSER_MAX_TABS; ++i) {
        uint32_t j;
        for (j = i + 1; j < ZD_BROWSER_MAX_TABS; ++j)
            ZD_CHECK(ids[i] != ids[j]);
    }

    /* Opening focuses the new tab: last id is active. */
    ZD_CHECK_EQ(b.active_id, ids[ZD_BROWSER_MAX_TABS - 1]);

    ZD_CHECK_OK(zd_browser_close(&b, ids[0]));
    ZD_CHECK_EQ(b.tab_count, ZD_BROWSER_MAX_TABS - 1);
    ZD_CHECK_EQ(zd_browser_state(&b, ids[0], &st), -ZD_ENOENT);
    ZD_CHECK_EQ(zd_browser_close(&b, ids[0]), -ZD_ENOENT);
    /* Slot is reusable after close. */
    ZD_CHECK_OK(zd_browser_open(&b, 10, &id));
    ZD_CHECK_EQ(zd_browser_open(&b, 0, 0), -ZD_EINVAL);
}

static void test_ladder_transitions(void) {
    struct zd_browser b;
    uint32_t id = 0;
    enum zd_tab_state st;

    zd_browser_init(&b, 100, 300, 900);
    ZD_CHECK_OK(zd_browser_open(&b, 8192, &id));

    /* Just below idle: still ACTIVE. */
    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_TICK, 99));
    ZD_CHECK_OK(zd_browser_state(&b, id, &st));
    ZD_CHECK_EQ(st, ZD_TAB_ACTIVE);

    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_TICK, 100));
    ZD_CHECK_OK(zd_browser_state(&b, id, &st));
    ZD_CHECK_EQ(st, ZD_TAB_IDLE);

    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_TICK, 300));
    ZD_CHECK_OK(zd_browser_state(&b, id, &st));
    ZD_CHECK_EQ(st, ZD_TAB_FROZEN);
    ZD_CHECK_EQ(b.stats.freezes, 1u);
    /* Frozen still has its document. */
    {
        uint32_t i;
        for (i = 0; i < ZD_BROWSER_MAX_TABS; ++i)
            if (b.tabs[i].used && b.tabs[i].id == id)
                ZD_CHECK_EQ(b.tabs[i].has_document, 1);
    }

    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_TICK, 900));
    ZD_CHECK_OK(zd_browser_state(&b, id, &st));
    ZD_CHECK_EQ(st, ZD_TAB_DISCARDED);
    ZD_CHECK_EQ(b.stats.discards, 1u);
    for (uint32_t i = 0; i < ZD_BROWSER_MAX_TABS; ++i)
        if (b.tabs[i].used && b.tabs[i].id == id) {
            ZD_CHECK_EQ(b.tabs[i].has_document, 0);
            ZD_CHECK_EQ(b.tabs[i].content_bytes, 0);
        }

    /* Focus a discarded tab -> reloads the document. */
    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_FOCUS, 901));
    ZD_CHECK_OK(zd_browser_state(&b, id, &st));
    ZD_CHECK_EQ(st, ZD_TAB_ACTIVE);
    ZD_CHECK_EQ(b.active_id, id);
    for (uint32_t i = 0; i < ZD_BROWSER_MAX_TABS; ++i)
        if (b.tabs[i].used && b.tabs[i].id == id)
            ZD_CHECK_EQ(b.tabs[i].reloads, 1);

    /* Focus resets the ladder clock. */
    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_TICK, 901 + 99));
    ZD_CHECK_OK(zd_browser_state(&b, id, &st));
    ZD_CHECK_EQ(st, ZD_TAB_ACTIVE);
}

static void test_crash_recovery(void) {
    struct zd_browser b;
    uint32_t id = 0;
    enum zd_tab_state st;

    zd_browser_init(&b, 100, 300, 900);
    ZD_CHECK_OK(zd_browser_open(&b, 4096, &id));
    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_RENDERER_CRASH, 10));
    ZD_CHECK_OK(zd_browser_state(&b, id, &st));
    ZD_CHECK_EQ(st, ZD_TAB_CRASHED);
    ZD_CHECK_EQ(b.stats.crashes, 1u);
    ZD_CHECK_NE(b.active_id, id);

    /* Focus before reload is an explicit state error (UI shows the
     * error surface first). */
    ZD_CHECK_EQ(zd_browser_event(&b, id, ZD_TAB_EV_FOCUS, 11), -ZD_ESTATE);
    /* Double crash without recovery is rejected. */
    ZD_CHECK_EQ(zd_browser_event(&b, id, ZD_TAB_EV_RENDERER_CRASH, 12),
                -ZD_ESTATE);

    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_RELOAD, 13));
    ZD_CHECK_OK(zd_browser_state(&b, id, &st));
    ZD_CHECK_EQ(st, ZD_TAB_ACTIVE);
    ZD_CHECK_EQ(b.active_id, id);
    ZD_CHECK_EQ(b.stats.crash_recoveries, 1u);

    /* Reload of a healthy tab is rejected (explicit diagnostic). */
    ZD_CHECK_EQ(zd_browser_event(&b, id, ZD_TAB_EV_RELOAD, 14), -ZD_EINVAL);

    /* Crashed -> discard is rejected. */
    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_RENDERER_CRASH, 15));
    ZD_CHECK_EQ(zd_browser_event(&b, id, ZD_TAB_EV_DISCARD_NOW, 16),
                -ZD_ESTATE);
}

static void test_explicit_discard_and_clock(void) {
    struct zd_browser b;
    uint32_t id = 0;
    enum zd_tab_state st;

    zd_browser_init(&b, 0, 0, 0); /* defaults: 100/300/900 */
    ZD_CHECK_EQ(b.idle_ticks, ZD_BROWSER_IDLE_TICKS_DEFAULT);
    ZD_CHECK_EQ(b.freeze_ticks, ZD_BROWSER_FREEZE_TICKS_DEFAULT);
    ZD_CHECK_EQ(b.discard_ticks, ZD_BROWSER_DISCARD_TICKS_DEFAULT);
    /* Ladder normalisation: mis-ordered inputs are folded. */
    zd_browser_init(&b, 500, 200, 100);
    ZD_CHECK_EQ(b.freeze_ticks >= b.idle_ticks, 1);
    ZD_CHECK_EQ(b.discard_ticks >= b.freeze_ticks, 1);

    zd_browser_init(&b, 100, 300, 900);
    ZD_CHECK_OK(zd_browser_open(&b, 2048, &id));
    ZD_CHECK_OK(zd_browser_event(&b, id, ZD_TAB_EV_DISCARD_NOW, 50));
    ZD_CHECK_OK(zd_browser_state(&b, id, &st));
    ZD_CHECK_EQ(st, ZD_TAB_DISCARDED);

    /* Monotonic clock and bad ids. */
    ZD_CHECK_EQ(zd_browser_event(&b, id, ZD_TAB_EV_TICK, 40), -ZD_EINVAL);
    ZD_CHECK_EQ(zd_browser_event(&b, 9999, ZD_TAB_EV_TICK, 100), -ZD_ENOENT);
    ZD_CHECK_EQ(zd_browser_event(&b, id, (enum zd_tab_event)77, 100),
                -ZD_EINVAL);
    ZD_CHECK_EQ(zd_browser_event(&b, 0, ZD_TAB_EV_TICK, 100), -ZD_ENOENT);
}

static void test_blur_and_focus_single(void) {
    struct zd_browser b;
    uint32_t a = 0, c = 0;

    zd_browser_init(&b, 100, 300, 900);
    ZD_CHECK_OK(zd_browser_open(&b, 1, &a));
    ZD_CHECK_OK(zd_browser_open(&b, 1, &c));
    ZD_CHECK_EQ(b.active_id, c);
    ZD_CHECK_OK(zd_browser_event(&b, a, ZD_TAB_EV_FOCUS, 5));
    ZD_CHECK_EQ(b.active_id, a);
    ZD_CHECK_OK(zd_browser_event(&b, a, ZD_TAB_EV_BLUR, 6));
    ZD_CHECK_EQ(b.active_id, 0);
    ZD_CHECK_OK(zd_browser_close(&b, a));
    ZD_CHECK_EQ(b.active_id, 0);
    ZD_CHECK_EQ(zd_browser_state(&b, a, 0), -ZD_EINVAL);
}

void zd_test_browser_suite(void) {
    printf("  suite: browser tab lifecycle\n");
    ZD_RUN(test_open_close_bounds);
    ZD_RUN(test_ladder_transitions);
    ZD_RUN(test_crash_recovery);
    ZD_RUN(test_explicit_discard_and_clock);
    ZD_RUN(test_blur_and_focus_single);
}
