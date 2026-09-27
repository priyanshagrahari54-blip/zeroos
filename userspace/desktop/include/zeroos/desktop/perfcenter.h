/* Performance center (Stage 5 part A shell surface + part L metrics).
 * Records frame samples and system inputs, computes percentiles, and
 * derives a health assessment with concrete, measured suggestions.
 * All thresholds are explicit constants — no vibes. */
#ifndef ZEROOS_DESKTOP_PERFCENTER_H
#define ZEROOS_DESKTOP_PERFCENTER_H

#include <stdint.h>

#define ZD_PC_SAMPLES 128

enum zd_pc_health {
    ZD_PC_HEALTH_GOOD = 0,
    ZD_PC_HEALTH_FAIR,
    ZD_PC_HEALTH_POOR,
    ZD_PC_HEALTH_CRITICAL
};

enum zd_pc_issue {
    ZD_PC_ISSUE_NONE = 0,
    ZD_PC_ISSUE_STUTTER,      /* p95 frame over budget */
    ZD_PC_ISSUE_LOW_FPS,
    ZD_PC_ISSUE_MEMORY,       /* elevated pressure */
    ZD_PC_ISSUE_THERMAL,      /* throttled */
    ZD_PC_ISSUE_POWER,        /* eco governor with misses */
    ZD_PC_ISSUE_UPDATE        /* update pending, degrade pending */
};

/* suggestion bits (combinable) */
#define ZD_PC_SUGGEST_CLOSE_BG     (1u << 0)
#define ZD_PC_SUGGEST_LOWER_DETAIL (1u << 1)
#define ZD_PC_SUGGEST_REDUCE_MOTION (1u << 2)
#define ZD_PC_SUGGEST_COOL_DOWN    (1u << 3)
#define ZD_PC_SUGGEST_WAIT_UPDATE  (1u << 4)
#define ZD_PC_SUGGEST_CHECK_MEMORY (1u << 5)

/* measured thresholds (microseconds / percent / fps) */
#define ZD_PC_STUTTER_P95_NUM 3   /* p95 > budget * 3/2 => stutter */
#define ZD_PC_STUTTER_P95_DEN 2
#define ZD_PC_LOW_FPS 30000       /* fps milli-units: 30.000 */
#define ZD_PC_MEM_PRESSURE_FAIR 60
#define ZD_PC_MEM_PRESSURE_POOR 80
#define ZD_PC_MEM_PRESSURE_CRITICAL 95

struct zd_pc_input {
    uint32_t fps_milli;      /* e.g. 60000 = 60.000 fps */
    uint32_t budget_us;      /* frame budget (e.g. 16667) */
    uint32_t mem_pressure;   /* 0..100 */
    uint32_t throttled;      /* 1 = thermal/power throttle active */
    uint32_t governor_eco;   /* 1 = power-saving mode */
    uint32_t update_pending; /* 1 = update staged but not active */
};

struct zd_pc_report {
    int health;              /* enum zd_pc_health */
    int issue;               /* enum zd_pc_issue, primary */
    uint32_t suggestions;    /* ZD_PC_SUGGEST_* mask */
    uint32_t p50_us;
    uint32_t p95_us;
    uint32_t worst_us;
    uint32_t samples;
};

struct zd_perf_center {
    uint32_t frames[ZD_PC_SAMPLES]; /* frame durations, us */
    uint32_t count;                 /* samples in ring */
    uint32_t cursor;
    struct zd_pc_report last;
};

void zd_perf_center_init(struct zd_perf_center *pc);
/* Record one frame duration; 0 or absurd (>1s) rejected. */
int zd_perf_center_record(struct zd_perf_center *pc, uint32_t frame_us);
/* Recompute percentiles from samples (needs >= 4 samples else -22)
 * and assess against `in`.  Report written to pc->last. */
int zd_perf_center_assess(struct zd_perf_center *pc,
                          const struct zd_pc_input *in,
                          struct zd_pc_report *out);
/* Health label for i18n lookup: "good"/"fair"/"poor"/"critical". */
const char *zd_perf_center_health_label(int health);

#endif /* ZEROOS_DESKTOP_PERFCENTER_H */
