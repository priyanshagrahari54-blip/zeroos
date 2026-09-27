/* Performance center.  See perfcenter.h. */
#include <zeroos/desktop/perfcenter.h>

#define PC_MAX_FRAME_US 1000000u /* reject absurd samples (>1s) */

void zd_perf_center_init(struct zd_perf_center *pc) {
    uint32_t i;
    if (!pc)
        return;
    for (i = 0; i < ZD_PC_SAMPLES; ++i)
        pc->frames[i] = 0;
    pc->count = 0;
    pc->cursor = 0;
    pc->last.health = ZD_PC_HEALTH_GOOD;
    pc->last.issue = ZD_PC_ISSUE_NONE;
    pc->last.suggestions = 0;
    pc->last.p50_us = pc->last.p95_us = pc->last.worst_us = 0;
    pc->last.samples = 0;
}

int zd_perf_center_record(struct zd_perf_center *pc, uint32_t frame_us) {
    if (!pc || frame_us == 0 || frame_us > PC_MAX_FRAME_US)
        return -22;
    pc->frames[pc->cursor] = frame_us;
    pc->cursor = (pc->cursor + 1) % ZD_PC_SAMPLES;
    if (pc->count < ZD_PC_SAMPLES)
        pc->count++;
    return 0;
}

static void pc_sort(uint32_t *a, uint32_t n) {
    uint32_t i, j, key;
    for (i = 1; i < n; ++i) {
        key = a[i];
        j = i;
        while (j > 0 && a[j - 1] > key) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = key;
    }
}
static uint32_t pc_max(uint32_t a, uint32_t b) { return a > b ? a : b; }

int zd_perf_center_assess(struct zd_perf_center *pc,
                          const struct zd_pc_input *in,
                          struct zd_pc_report *out) {
    uint32_t sorted[ZD_PC_SAMPLES], n, p50i, p95i;
    uint32_t sev = 0;
    int primary = ZD_PC_ISSUE_NONE;
    struct zd_pc_report r;
    if (!pc || !in || !out)
        return -22;
    if (pc->count < 4)
        return -22;
    n = pc->count;
    {
        uint32_t i;
        /* ring may be partial: cursor order == age order */
        for (i = 0; i < n; ++i)
            sorted[i] = pc->frames[i];
    }
    pc_sort(sorted, n);
    p50i = n / 2;
    p95i = (n * 95u + 99u) / 100u - 1u; /* ceil(0.95n)-1 */
    if (p95i >= n)
        p95i = n - 1;

    r.p50_us = sorted[p50i];
    r.p95_us = sorted[p95i];
    r.worst_us = sorted[n - 1];
    r.samples = n;
    r.suggestions = 0;

    /* --- memory pressure ladder --- */
    if (in->mem_pressure >= ZD_PC_MEM_PRESSURE_CRITICAL) {
        sev = pc_max(sev, 3);
        primary = ZD_PC_ISSUE_MEMORY;
        r.suggestions |= ZD_PC_SUGGEST_CLOSE_BG |
                         ZD_PC_SUGGEST_CHECK_MEMORY;
    } else if (in->mem_pressure >= ZD_PC_MEM_PRESSURE_POOR) {
        sev = pc_max(sev, 2);
        if (primary == ZD_PC_ISSUE_NONE)
            primary = ZD_PC_ISSUE_MEMORY;
        r.suggestions |= ZD_PC_SUGGEST_CHECK_MEMORY;
    } else if (in->mem_pressure >= ZD_PC_MEM_PRESSURE_FAIR) {
        sev = pc_max(sev, 1);
        r.suggestions |= ZD_PC_SUGGEST_CHECK_MEMORY;
    }

    /* --- frame pacing: p95 vs budget (explicit 1.5x threshold) --- */
    if (in->budget_us) {
        uint32_t lim = (in->budget_us / ZD_PC_STUTTER_P95_DEN) *
                       ZD_PC_STUTTER_P95_NUM;
        if (r.p95_us > lim) {
            sev = pc_max(sev, r.p95_us > lim * 2 ? 3 : 2);
            primary = ZD_PC_ISSUE_STUTTER; /* frames outrank memory */
            r.suggestions |= ZD_PC_SUGGEST_LOWER_DETAIL |
                             ZD_PC_SUGGEST_REDUCE_MOTION;
        }
    }

    /* --- throughput floor --- */
    if (in->fps_milli && in->fps_milli < ZD_PC_LOW_FPS) {
        sev = pc_max(sev, in->fps_milli < ZD_PC_LOW_FPS / 2 ? 3 : 2);
        if (primary == ZD_PC_ISSUE_NONE ||
            primary == ZD_PC_ISSUE_MEMORY)
            primary = ZD_PC_ISSUE_LOW_FPS;
        r.suggestions |= ZD_PC_SUGGEST_LOWER_DETAIL;
    }

    /* --- thermal / power --- */
    if (in->throttled) {
        sev = pc_max(sev, 1);
        if (primary == ZD_PC_ISSUE_NONE)
            primary = ZD_PC_ISSUE_THERMAL;
        r.suggestions |= ZD_PC_SUGGEST_COOL_DOWN;
    } else if (in->governor_eco &&
               (primary == ZD_PC_ISSUE_STUTTER ||
                primary == ZD_PC_ISSUE_LOW_FPS)) {
        sev = pc_max(sev, 1);
        primary = ZD_PC_ISSUE_POWER;
    }

    /* --- staged update (lowest priority issue) --- */
    if (in->update_pending) {
        sev = pc_max(sev, 1);
        if (primary == ZD_PC_ISSUE_NONE)
            primary = ZD_PC_ISSUE_UPDATE;
        r.suggestions |= ZD_PC_SUGGEST_WAIT_UPDATE;
    }

    r.health = (int)(sev > 3 ? 3 : sev);
    r.issue = primary;
    pc->last = r;
    *out = r;
    return 0;
}

const char *zd_perf_center_health_label(int health) {
    switch (health) {
    case ZD_PC_HEALTH_GOOD:
        return "good";
    case ZD_PC_HEALTH_FAIR:
        return "fair";
    case ZD_PC_HEALTH_POOR:
        return "poor";
    default:
        return "critical";
    }
}
