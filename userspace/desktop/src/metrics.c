/* Metrics recorder.  See metrics.h for the contract. */
#include <zeroos/desktop/metrics.h>

static const uint64_t bucket_bound[ZD_METRIC_BUCKETS] = {
    2, 4, 8, 16, 33, 50, 100, 0 /* 0 = unbounded top bucket */
};

void zd_metrics_init(struct zd_metrics *m) {
    int i;
    if (!m)
        return;
    for (i = 0; i < ZD_METRIC_COUNT; ++i)
        m->counters[i] = 0;
    for (i = 0; i < ZD_METRIC_BUCKETS; ++i)
        m->interval_hist[i] = 0;
    m->interval_sum = 0;
    m->interval_count = 0;
    m->interval_max = 0;
}

void zd_metrics_add(struct zd_metrics *m, int id, uint64_t n) {
    if (!m || id < 0 || id >= ZD_METRIC_COUNT)
        return;
    m->counters[id] += n;
}

static int bucket_for(uint64_t interval) {
    int i;
    for (i = 0; i < ZD_METRIC_BUCKETS - 1; ++i)
        if (interval <= bucket_bound[i])
            return i;
    return ZD_METRIC_BUCKETS - 1;
}

void zd_metrics_record_interval(struct zd_metrics *m, uint64_t interval) {
    if (!m)
        return;
    m->interval_hist[bucket_for(interval)]++;
    m->interval_sum += interval;
    m->interval_count++;
    if (interval > m->interval_max)
        m->interval_max = interval;
}

uint64_t zd_metrics_get(const struct zd_metrics *m, int id) {
    if (!m || id < 0 || id >= ZD_METRIC_COUNT)
        return 0;
    return m->counters[id];
}

uint64_t zd_metrics_percentile(const struct zd_metrics *m, uint32_t p) {
    uint64_t target, seen = 0;
    int i;
    if (!m || m->interval_count == 0)
        return 0;
    if (p > 100)
        p = 100;
    /* smallest interval count covering p% (at least 1 sample) */
    target = (m->interval_count * (uint64_t)p + 99) / 100;
    if (target == 0)
        target = 1;
    for (i = 0; i < ZD_METRIC_BUCKETS; ++i) {
        seen += m->interval_hist[i];
        if (seen >= target)
            return bucket_bound[i] ? bucket_bound[i] : m->interval_max;
    }
    return m->interval_max;
}

uint64_t zd_metrics_avg_interval(const struct zd_metrics *m) {
    if (!m || m->interval_count == 0)
        return 0;
    return m->interval_sum / m->interval_count;
}
