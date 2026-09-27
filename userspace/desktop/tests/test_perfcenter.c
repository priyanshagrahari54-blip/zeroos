/* Performance center tests: samples, percentiles, health ladder */
#include "test_harness.h"
#include <zeroos/desktop/perfcenter.h>
#include <string.h>

static void fill_uniform(struct zd_perf_center *pc, uint32_t n,
                         uint32_t frame_us) {
    uint32_t i;
    zd_perf_center_init(pc);
    for (i = 0; i < n; ++i)
        ZD_CHECK_OK(zd_perf_center_record(pc, frame_us));
}

void zd_test_perfcenter_suite(void) {
    struct zd_perf_center pc;
    struct zd_pc_report rep;
    struct zd_pc_input in;

    zd_perf_center_init(&pc);
    /* record validation */
    ZD_CHECK_EQ(zd_perf_center_record(&pc, 0), -22);
    ZD_CHECK_EQ(zd_perf_center_record(&pc, 1000001), -22);
    ZD_CHECK_EQ(zd_perf_center_record(NULL, 100), -22);
    /* assess needs >= 4 samples */
    ZD_CHECK_OK(zd_perf_center_record(&pc, 8000));
    ZD_CHECK_OK(zd_perf_center_record(&pc, 8000));
    ZD_CHECK_OK(zd_perf_center_record(&pc, 8000));
    memset(&in, 0, sizeof(in));
    in.budget_us = 16667;
    ZD_CHECK_EQ(zd_perf_center_assess(&pc, &in, &rep), -22);

    /* uniform 8ms samples, healthy inputs -> GOOD / none */
    fill_uniform(&pc, 8, 8000);
    in.fps_milli = 60000;
    in.budget_us = 16667;
    in.mem_pressure = 30;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_GOOD);
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_NONE);
    ZD_CHECK_EQ(rep.p50_us, 8000);
    ZD_CHECK_EQ(rep.p95_us, 8000);
    ZD_CHECK_EQ(rep.worst_us, 8000);
    ZD_CHECK_EQ(rep.suggestions, 0);
    ZD_CHECK_EQ(rep.samples, 8);

    /* percentile math on sorted spread: 1..100 ms */
    zd_perf_center_init(&pc);
    {
        uint32_t i;
        for (i = 1; i <= 100; ++i)
            ZD_CHECK_OK(zd_perf_center_record(&pc, i * 1000));
    }
    in.fps_milli = 60000;
    in.mem_pressure = 0;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    /* p50 idx 50 -> 51st value = 51000?? n=100 p50i=50 -> a[50]=51000 */
    ZD_CHECK_EQ(rep.p50_us, 51000);
    /* p95i = ceil(95)-1 = 94 -> a[94]=95000 */
    ZD_CHECK_EQ(rep.p95_us, 95000);
    ZD_CHECK_EQ(rep.worst_us, 100000);
    /* p95 95000 <= budget*1.5=25000? NO -> stutter POOR (95000 <= 2*25000=50000? no -> sev 3? lim=25000, p95>lim, lim*2=50000, 95000>50000 -> sev3 CRITICAL) */
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_CRITICAL);
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_STUTTER);
    ZD_CHECK(rep.suggestions & ZD_PC_SUGGEST_LOWER_DETAIL);
    ZD_CHECK(rep.suggestions & ZD_PC_SUGGEST_REDUCE_MOTION);

    /* memory ladder alone */
    fill_uniform(&pc, 8, 8000);
    in.fps_milli = 60000;
    in.budget_us = 16667;
    in.mem_pressure = 70;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_FAIR);
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_NONE); /* fair band has no primary */
    ZD_CHECK(rep.suggestions & ZD_PC_SUGGEST_CHECK_MEMORY);

    in.mem_pressure = 85;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_POOR);
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_MEMORY);
    ZD_CHECK(rep.suggestions & ZD_PC_SUGGEST_CHECK_MEMORY);
    ZD_CHECK_EQ(rep.suggestions & ZD_PC_SUGGEST_CLOSE_BG, 0);

    in.mem_pressure = 97;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_CRITICAL);
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_MEMORY);
    ZD_CHECK(rep.suggestions & ZD_PC_SUGGEST_CLOSE_BG);

    /* low fps ladder */
    in.mem_pressure = 10;
    in.fps_milli = 25000;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_POOR);
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_LOW_FPS);
    ZD_CHECK(rep.suggestions & ZD_PC_SUGGEST_LOWER_DETAIL);

    in.fps_milli = 10000;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_CRITICAL);
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_LOW_FPS);

    /* thermal at low severity, but frames already bad -> power issue */
    in.fps_milli = 60000;
    in.throttled = 1;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_FAIR);
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_THERMAL);
    ZD_CHECK(rep.suggestions & ZD_PC_SUGGEST_COOL_DOWN);

    in.throttled = 0;
    in.governor_eco = 1;
    in.fps_milli = 25000;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_POWER);
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_POOR);

    /* update pending -> fair at worst when nothing else wrong */
    in.governor_eco = 0;
    in.fps_milli = 60000;
    in.update_pending = 1;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_FAIR);
    ZD_CHECK_EQ(rep.issue, ZD_PC_ISSUE_UPDATE);
    ZD_CHECK(rep.suggestions & ZD_PC_SUGGEST_WAIT_UPDATE);

    /* ring wrap: 128 + 8 more replaces oldest */
    fill_uniform(&pc, ZD_PC_SAMPLES, 4000);
    ZD_CHECK_EQ(pc.count, ZD_PC_SAMPLES);
    {
        uint32_t i;
        for (i = 0; i < 8; ++i)
            ZD_CHECK_OK(zd_perf_center_record(&pc, 9000));
    }
    in.update_pending = 0;
    in.budget_us = 16667;
    ZD_CHECK_OK(zd_perf_center_assess(&pc, &in, &rep));
    /* worst now 9000 (120 x 4000 + 8 x 9000) */
    ZD_CHECK_EQ(rep.worst_us, 9000);
    ZD_CHECK_EQ(rep.p50_us, 4000);
    /* 8/128 = 6.25% of samples are 9000 -> the 95th percentile sits
     * inside that top band (idx 121) */
    ZD_CHECK_EQ(rep.p95_us, 9000);
    ZD_CHECK_EQ(rep.health, ZD_PC_HEALTH_GOOD);

    /* labels */
    ZD_CHECK(strcmp(zd_perf_center_health_label(ZD_PC_HEALTH_GOOD),
                    "good") == 0);
    ZD_CHECK(strcmp(zd_perf_center_health_label(ZD_PC_HEALTH_CRITICAL),
                    "critical") == 0);
    ZD_CHECK(strcmp(zd_perf_center_health_label(99), "critical") == 0);
    /* null safety */
    ZD_CHECK_EQ(zd_perf_center_assess(NULL, &in, &rep), -22);
    ZD_CHECK_EQ(zd_perf_center_assess(&pc, NULL, &rep), -22);
    ZD_CHECK_EQ(zd_perf_center_assess(&pc, &in, NULL), -22);
}
