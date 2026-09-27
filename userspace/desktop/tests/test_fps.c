/* FPS monitor + profiles host tests (part I). */
#include "test_harness.h"
#include <zeroos/desktop/fps.h>

void zd_test_fps_suite(void) {
    struct zd_fps f;
    uint32_t i;

    /* init validation */
    ZD_CHECK(zd_fps_init(&f, 1000, ZD_PERF_BALANCED) == 0);
    ZD_CHECK(zd_fps_init(&f, 0, ZD_PERF_BALANCED) == -22);
    ZD_CHECK(zd_fps_init(&f, 1000, 99) == -22);
    ZD_CHECK(zd_fps_init(0, 1000, 0) == -22);

    /* profiles publish budgets for the governor */
    ZD_CHECK(zd_fps_budget_ms(ZD_PERF_LOW_LATENCY) == 8);
    ZD_CHECK(zd_fps_budget_ms(ZD_PERF_BALANCED) == 16);
    ZD_CHECK(zd_fps_budget_ms(ZD_PERF_QUALITY) == 33);
    ZD_CHECK(zd_fps_budget_ms(99) == 0);
    ZD_CHECK(zd_fps_set_profile(&f, ZD_PERF_LOW_LATENCY) == 0);
    ZD_CHECK(zd_fps_set_profile(&f, 42) == -22);
    ZD_CHECK(zd_fps_set_profile(&f, ZD_PERF_BALANCED) == 0);

    /* 60 frames over one second at 16 ms each */
    for (i = 1; i <= 60; ++i)
        ZD_CHECK(zd_fps_record(&f, i * 16, 16) == 0);
    ZD_CHECK_EQ(f.frames_total, 60);
    ZD_CHECK(zd_fps_current(&f, 60 * 16) > 0);
    /* a mid-history instant whose 1 s window covers ~35 frames */
    ZD_CHECK(zd_fps_current(&f, 1400) >= 30);
    ZD_CHECK(zd_fps_avg_frame_ms(&f) == 16);

    /* monotonic clock enforced */
    ZD_CHECK(zd_fps_record(&f, 100, 5) == -22);
    ZD_CHECK(zd_fps_record(&f, 60 * 16 + 1, 16) == 0);

    /* budget breaches counted against the active profile */
    ZD_CHECK_EQ(f.budget_breaches, 0); /* 16ms <= 16ms budget */
    ZD_CHECK(zd_fps_record(&f, 60 * 16 + 17, 40) == 0);
    ZD_CHECK_EQ(f.budget_breaches, 1);
    ZD_CHECK(zd_fps_set_profile(&f, ZD_PERF_QUALITY) == 0);
    ZD_CHECK(zd_fps_record(&f, 60 * 16 + 34, 40) == 0);
    ZD_CHECK_EQ(f.budget_breaches, 2); /* 40 > 33 quality budget too */
    ZD_CHECK(zd_fps_record(&f, 60 * 16 + 50, 30) == 0);
    ZD_CHECK_EQ(f.budget_breaches, 2); /* 30 <= 33: no new breach */

    /* percentiles over stored frame times */
    ZD_CHECK(zd_fps_percentile(&f, 0) == 16);      /* min */
    ZD_CHECK(zd_fps_percentile(&f, 100) >= 40);    /* max */
    ZD_CHECK(zd_fps_percentile(&f, 50) == 16);
    ZD_CHECK(zd_fps_percentile(&f, 99) >= 16);

    /* empty state */
    {
        struct zd_fps e;
        ZD_CHECK(zd_fps_init(&e, 1000, ZD_PERF_BALANCED) == 0);
        ZD_CHECK(zd_fps_current(&e, 5000) == 0);
        ZD_CHECK(zd_fps_percentile(&e, 50) == 0);
        ZD_CHECK(zd_fps_avg_frame_ms(&e) == 0);
        ZD_CHECK(zd_fps_record(&e, 10, 8) == 0);
        ZD_CHECK(zd_fps_current(&e, 5) == 0); /* now < window */
    }

    /* ring wrap: push well past capacity, no corruption */
    {
        struct zd_fps w;
        ZD_CHECK(zd_fps_init(&w, 1000, ZD_PERF_BALANCED) == 0);
        for (i = 1; i <= ZD_FPS_RING * 2; ++i)
            ZD_CHECK(zd_fps_record(&w, (uint64_t)i * 16, 16) == 0);
        ZD_CHECK_EQ(w.count, ZD_FPS_RING);
        ZD_CHECK(zd_fps_avg_frame_ms(&w) == 16);
        ZD_CHECK(zd_fps_current(&w, ZD_FPS_RING * 2 * 16) > 0);
    }
}
