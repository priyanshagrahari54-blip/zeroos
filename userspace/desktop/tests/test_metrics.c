/* Measured metrics (part L) host tests. */
#include "test_harness.h"
#include <zeroos/desktop/metrics.h>

void zd_test_metrics_suite(void) {
    struct zd_metrics m;
    int i;

    zd_metrics_init(&m);
    ZD_CHECK(zd_metrics_get(&m, ZD_METRIC_FRAMES_PRESENTED) == 0);
    zd_metrics_add(&m, ZD_METRIC_FRAMES_PRESENTED, 3);
    zd_metrics_add(&m, ZD_METRIC_FRAMES_PRESENTED, 2);
    ZD_CHECK(zd_metrics_get(&m, ZD_METRIC_FRAMES_PRESENTED) == 5);
    /* unknown ids and NULL never corrupt neighbours */
    zd_metrics_add(&m, -1, 9);
    zd_metrics_add(&m, ZD_METRIC_COUNT, 9);
    zd_metrics_add(0, ZD_METRIC_FRAMES_PRESENTED, 9);
    ZD_CHECK(zd_metrics_get(&m, ZD_METRIC_FRAMES_PRESENTED) == 5);
    ZD_CHECK(zd_metrics_get(&m, -1) == 0);

    /* bucket boundaries: <=2,<=4,<=8,<=16,<=33,<=50,<=100,over */
    {
        static const uint64_t samples[] = {0, 1, 2, 3, 4, 5, 8,
                                           9, 16, 17, 33, 34, 50,
                                           51, 100, 101, 1000};
        for (i = 0; (unsigned)i < sizeof(samples) / sizeof(samples[0]); ++i)
            zd_metrics_record_interval(&m, samples[i]);
        ZD_CHECK(m.interval_count == 17);
        ZD_CHECK(m.interval_hist[0] == 3); /* 0,1,2 */
        ZD_CHECK(m.interval_hist[1] == 2); /* 3,4 */
        ZD_CHECK(m.interval_hist[2] == 2); /* 5,8 */
        ZD_CHECK(m.interval_hist[3] == 2); /* 9,16 */
        ZD_CHECK(m.interval_hist[4] == 2); /* 17,33 */
        ZD_CHECK(m.interval_hist[5] == 2); /* 34,50 */
        ZD_CHECK(m.interval_hist[6] == 2); /* 51,100 */
        ZD_CHECK(m.interval_hist[7] == 2); /* 101,1000 */
        ZD_CHECK(m.interval_max == 1000);
        /* avg = sum/17 */
        ZD_CHECK(m.interval_sum == 0 + 1 + 2 + 3 + 4 + 5 + 8 + 9 + 16 +
                 17 + 33 + 34 + 50 + 51 + 100 + 101 + 1000);
        ZD_CHECK(zd_metrics_avg_interval(&m) ==
                 m.interval_sum / m.interval_count);
        /* percentiles: p50 -> bucket covering sample 9th of 17 */
        ZD_CHECK(zd_metrics_percentile(&m, 50) == 16);
        ZD_CHECK(zd_metrics_percentile(&m, 100) == 1000);
        ZD_CHECK(zd_metrics_percentile(&m, 0) == 2); /* at least 1 sample */
        ZD_CHECK(zd_metrics_percentile(&m, 99) == 1000);
        ZD_CHECK(zd_metrics_percentile(&m, 200) == 1000); /* clamp */
    }

    /* empty histograms read as 0, never divide by zero */
    zd_metrics_init(&m);
    ZD_CHECK(zd_metrics_percentile(&m, 50) == 0);
    ZD_CHECK(zd_metrics_avg_interval(&m) == 0);
    ZD_CHECK(zd_metrics_percentile(0, 50) == 0);

    /* re-init clears everything */
    zd_metrics_add(&m, ZD_METRIC_CAP_DENIALS, 4);
    zd_metrics_record_interval(&m, 5);
    zd_metrics_init(&m);
    ZD_CHECK(zd_metrics_get(&m, ZD_METRIC_CAP_DENIALS) == 0);
    ZD_CHECK(m.interval_count == 0);
}
