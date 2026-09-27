#include <zeroos/desktop/governor.h>

enum zd_perf_tier zd_governor_detect_tier(const struct zd_capabilities *caps) {
    if (!caps)
        return ZD_TIER_MINIMAL;

    /* Any severe constraint forces the floor tier. */
    if (caps->thermal_state >= 2 || caps->ram_mb < 1024 ||
        caps->cpu_count == 0)
        return ZD_TIER_MINIMAL;
    if (caps->ram_mb < 2048 || caps->cpu_count == 1 ||
        (caps->battery_powered && caps->thermal_state == 1))
        return ZD_TIER_SIMPLIFIED;
    if (caps->ram_mb < 4096 || caps->gpu_tier <= 1 ||
        !caps->hardware_accel || caps->refresh_mhz > 165000)
        return ZD_TIER_BALANCED;
    return ZD_TIER_RICH;
}

void zd_governor_init(struct zd_governor *governor,
                      const struct zd_capabilities *caps) {
    if (!governor)
        return;
    zd_memset(governor, 0, sizeof(*governor));
    if (caps)
        governor->caps = *caps;
    governor->tier = zd_governor_detect_tier(&governor->caps);
    governor->pressure = ZD_PRESSURE_NONE;
    zd_governor_effects_for_tier(governor->tier, &governor->frame_budget_ms,
                                 &governor->effects_mask);
}

uint64_t zd_governor_actions_for_level(enum zd_pressure_level level,
                                       int recovery_necessary) {
    uint64_t actions = 0;
    if ((int)level < 0 || (int)level >= ZD_PRESSURE_COUNT)
        return 0;
    if (level >= ZD_PRESSURE_LOW)
        actions |= ZD_GOVERNOR_PAUSE_OPTIONAL;
    if (level >= ZD_PRESSURE_MODERATE)
        actions |= ZD_GOVERNOR_LOWER_BG_PRIORITY;
    if (level >= ZD_PRESSURE_HIGH)
        actions |= ZD_GOVERNOR_RECLAIM_CACHES | ZD_GOVERNOR_FREEZE_INACTIVE;
    if (level >= ZD_PRESSURE_CRITICAL) {
        actions |= ZD_GOVERNOR_PROTECT_FOREGROUND;
        if (recovery_necessary)
            actions |= ZD_GOVERNOR_RECOVERY;
    }
    return actions;
}

int zd_governor_set_pressure(struct zd_governor *governor,
                             enum zd_pressure_level level,
                             int recovery_necessary) {
    uint64_t actions;
    if (!governor || (int)level < 0 || (int)level >= ZD_PRESSURE_COUNT)
        return -ZD_EINVAL;
    /* Recovery may only be invoked at CRITICAL and only when explicitly
     * necessary; anything else is a contract violation. */
    if (recovery_necessary && level != ZD_PRESSURE_CRITICAL)
        return -ZD_EINVAL;
    actions = zd_governor_actions_for_level(level, recovery_necessary);
    if ((actions & ZD_GOVERNOR_RECOVERY) &&
        !(governor->active_actions & ZD_GOVERNOR_RECOVERY))
        ++governor->recovery_invocations;
    governor->pressure = level;
    governor->active_actions = actions;
    return 0;
}

void zd_governor_effects_for_tier(enum zd_perf_tier tier,
                                  uint32_t *frame_budget_ms,
                                  uint32_t *effects_mask) {
    uint32_t budget = 16;
    uint32_t mask = 0;
    switch (tier) {
    case ZD_TIER_RICH:
        budget = 16;
        mask = ZD_EFFECT_TRANSITIONS | ZD_EFFECT_WORKSPACE_ANIM |
               ZD_EFFECT_WINDOW_SHADOW | ZD_EFFECT_OVERVIEW_ZOOM;
        break;
    case ZD_TIER_BALANCED:
        budget = 16;
        mask = ZD_EFFECT_TRANSITIONS | ZD_EFFECT_WORKSPACE_ANIM;
        break;
    case ZD_TIER_SIMPLIFIED:
        budget = 33;
        mask = ZD_EFFECT_TRANSITIONS;
        break;
    case ZD_TIER_MINIMAL:
    default:
        budget = 50;
        mask = 0;
        break;
    }
    if (frame_budget_ms)
        *frame_budget_ms = budget;
    if (effects_mask)
        *effects_mask = mask;
}

const char *zd_governor_tier_name(enum zd_perf_tier tier) {
    static const char *const names[ZD_TIER_COUNT] = {
        "RICH", "BALANCED", "SIMPLIFIED", "MINIMAL"
    };
    if ((int)tier < 0 || (int)tier >= ZD_TIER_COUNT)
        return "?";
    return names[tier];
}
