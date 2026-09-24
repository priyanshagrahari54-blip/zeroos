#ifndef ZEROOS_DESKTOP_GOVERNOR_H
#define ZEROOS_DESKTOP_GOVERNOR_H

/* Capability detection and pressure governance for the desktop session.
 *
 * Capabilities are measured inputs; the tier mapping is policy. Under
 * pressure the governor applies a strict ladder:
 *   1 pause optional work, 2 lower background priority, 3 reclaim caches,
 *   4 freeze/discard inactive features, 5 protect foreground work,
 *   6 invoke recovery only when explicitly marked necessary.
 * Correctness and responsiveness outrank visual effects at every tier. */

#include <zeroos/desktop/common.h>

enum zd_perf_tier {
    ZD_TIER_RICH = 0,
    ZD_TIER_BALANCED = 1,
    ZD_TIER_SIMPLIFIED = 2,
    ZD_TIER_MINIMAL = 3,
    ZD_TIER_COUNT = 4
};

enum zd_pressure_level {
    ZD_PRESSURE_NONE = 0,
    ZD_PRESSURE_LOW = 1,
    ZD_PRESSURE_MODERATE = 2,
    ZD_PRESSURE_HIGH = 3,
    ZD_PRESSURE_CRITICAL = 4,
    ZD_PRESSURE_COUNT = 5
};

/* Governor action bits; cumulative with increasing pressure. */
#define ZD_GOVERNOR_PAUSE_OPTIONAL     (1ULL << 0)
#define ZD_GOVERNOR_LOWER_BG_PRIORITY  (1ULL << 1)
#define ZD_GOVERNOR_RECLAIM_CACHES     (1ULL << 2)
#define ZD_GOVERNOR_FREEZE_INACTIVE    (1ULL << 3)
#define ZD_GOVERNOR_PROTECT_FOREGROUND (1ULL << 4)
#define ZD_GOVERNOR_RECOVERY           (1U << 5)

struct zd_capabilities {
    uint32_t cpu_count;
    uint32_t ram_mb;
    uint32_t gpu_tier;          /* 0 none/soft, 1 basic, 2 mid, 3 high */
    uint32_t display_width;
    uint32_t display_height;
    uint32_t refresh_mhz;       /* millihertz, e.g. 60000 = 60 Hz */
    uint32_t hardware_accel;    /* 0 software fallback path */
    uint32_t thermal_state;     /* 0 normal, 1 warm, 2 constrained */
    uint32_t battery_powered;   /* 1 on battery */
};

struct zd_governor {
    struct zd_capabilities caps;
    enum zd_perf_tier tier;
    enum zd_pressure_level pressure;
    uint64_t active_actions;
    uint32_t recovery_invocations;
    uint32_t frame_budget_ms;
    uint32_t effects_mask;
};

#define ZD_EFFECT_TRANSITIONS   (1U << 0)
#define ZD_EFFECT_WORKSPACE_ANIM (1U << 1)
#define ZD_EFFECT_WINDOW_SHADOW (1U << 2)
#define ZD_EFFECT_OVERVIEW_ZOOM  (1U << 3)

void zd_governor_init(struct zd_governor *governor,
                      const struct zd_capabilities *caps);
enum zd_perf_tier zd_governor_detect_tier(const struct zd_capabilities *caps);
/* recovery_necessary must be true only when data integrity or session
 * survival requires step 6; otherwise recovery stays unset. */
int zd_governor_set_pressure(struct zd_governor *governor,
                             enum zd_pressure_level level,
                             int recovery_necessary);
uint64_t zd_governor_actions_for_level(enum zd_pressure_level level,
                                       int recovery_necessary);
/* Frame budget/effects for a tier; lower tiers always get <= budget of
 * higher tiers and a subset of effects. */
void zd_governor_effects_for_tier(enum zd_perf_tier tier,
                                  uint32_t *frame_budget_ms,
                                  uint32_t *effects_mask);
const char *zd_governor_tier_name(enum zd_perf_tier tier);

#endif
