/* Measured performance metrics (Stage 5 part L).
 * Fixed counter registry plus a frame/present interval histogram with
 * percentile reads.  Units are the caller's monotonic clock unit; the
 * display service records scheduler ticks.  Host- and guest-safe
 * (no allocation, no libc). */
#ifndef ZEROOS_DESKTOP_METRICS_H
#define ZEROOS_DESKTOP_METRICS_H

#include <stdint.h>

enum zd_metric_id {
    ZD_METRIC_FRAMES_PRESENTED = 0,
    ZD_METRIC_FRAMES_REFUSED,
    ZD_METRIC_PRESENT_FAILURES,
    ZD_METRIC_INPUT_EVENTS,
    ZD_METRIC_LAUNCHES,
    ZD_METRIC_AI_REQUESTS,
    ZD_METRIC_CAP_DENIALS,
    ZD_METRIC_COUNT
};

#define ZD_METRIC_BUCKETS 8 /* up to 2,4,8,16,33,50,100,over */

struct zd_metrics {
    uint64_t counters[ZD_METRIC_COUNT];
    uint64_t interval_hist[ZD_METRIC_BUCKETS];
    uint64_t interval_sum;
    uint64_t interval_count;
    uint64_t interval_max;
};

void zd_metrics_init(struct zd_metrics *m);
/* Bump a counter by n.  Unknown id or NULL is ignored (never
 * corrupts neighbours). */
void zd_metrics_add(struct zd_metrics *m, int id, uint64_t n);
/* Record one observed present/frame interval (same unit as the
 * clock fed to the display service). */
void zd_metrics_record_interval(struct zd_metrics *m, uint64_t interval);
uint64_t zd_metrics_get(const struct zd_metrics *m, int id);
/* Percentile over recorded intervals: p in [0,100]; 0 samples -> 0.
 * Returns the bucket's upper bound (conservative estimate). */
uint64_t zd_metrics_percentile(const struct zd_metrics *m, uint32_t p);
uint64_t zd_metrics_avg_interval(const struct zd_metrics *m);

#endif /* ZEROOS_DESKTOP_METRICS_H */
