/* FPS monitor.  See fps.h. */
#include <zeroos/desktop/fps.h>

static const uint32_t budgets[3] = {8, 16, 33};

int zd_fps_init(struct zd_fps *f, uint32_t window_ms, int profile) {
    uint32_t i;
    if (!f || window_ms == 0 || profile < 0 || profile > ZD_PERF_QUALITY)
        return -22;
    for (i = 0; i < ZD_FPS_RING; ++i) {
        f->stamp[i] = 0;
        f->frame_ms[i] = 0;
    }
    f->head = 0;
    f->count = 0;
    f->profile = profile;
    f->window_ms = window_ms;
    f->budget_breaches = 0;
    f->frames_total = 0;
    return 0;
}

int zd_fps_set_profile(struct zd_fps *f, int profile) {
    if (!f || profile < 0 || profile > ZD_PERF_QUALITY)
        return -22;
    f->profile = profile;
    return 0;
}

uint32_t zd_fps_budget_ms(int profile) {
    if (profile < 0 || profile > ZD_PERF_QUALITY)
        return 0;
    return budgets[profile];
}

int zd_fps_record(struct zd_fps *f, uint64_t now_ms, uint64_t frame_ms) {
    if (!f)
        return -22;
    if (f->count && now_ms <= f->stamp[(f->head + ZD_FPS_RING - 1) %
                                       ZD_FPS_RING])
        return -22; /* monotonic clock required */
    f->stamp[f->head] = now_ms;
    f->frame_ms[f->head] = frame_ms;
    f->head = (f->head + 1) % ZD_FPS_RING;
    if (f->count < ZD_FPS_RING)
        f->count++;
    f->frames_total++;
    if (frame_ms > budgets[f->profile])
        f->budget_breaches++;
    return 0;
}

uint32_t zd_fps_current(const struct zd_fps *f, uint64_t now_ms) {
    uint64_t win_start;
    uint32_t i, n = 0;
    if (!f || f->count == 0)
        return 0;
    win_start = (now_ms > f->window_ms) ? now_ms - f->window_ms : 0;
    for (i = 0; i < f->count; ++i) {
        uint32_t idx = (f->head + ZD_FPS_RING - 1 - i) % ZD_FPS_RING;
        if (f->stamp[idx] > win_start && f->stamp[idx] <= now_ms)
            ++n;
        else if (f->stamp[idx] <= win_start)
            break;
    }
    if (n == 0)
        return 0;
    return (uint32_t)((uint64_t)n * 1000 / f->window_ms);
}

static uint32_t f_sorted_copy(const struct zd_fps *f, uint64_t *out) {
    uint32_t i, j, n = f->count;
    for (i = 0; i < n; ++i)
        out[i] = f->frame_ms[i];
    for (i = 1; i < n; ++i) {
        uint64_t key = out[i];
        j = i;
        while (j > 0 && out[j - 1] > key) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = key;
    }
    return n;
}

uint32_t zd_fps_percentile(const struct zd_fps *f, uint32_t p) {
    uint64_t tmp[ZD_FPS_RING];
    uint32_t n, idx;
    if (!f || f->count == 0)
        return 0;
    if (p > 100)
        p = 100;
    n = f_sorted_copy(f, tmp);
    idx = (n * p) / 100;
    if (idx >= n)
        idx = n - 1;
    return (uint32_t)tmp[idx];
}

uint32_t zd_fps_avg_frame_ms(const struct zd_fps *f) {
    uint64_t sum = 0;
    uint32_t i;
    if (!f || f->count == 0)
        return 0;
    for (i = 0; i < f->count; ++i)
        sum += f->frame_ms[i];
    return (uint32_t)(sum / f->count);
}
