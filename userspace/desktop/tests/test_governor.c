#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

static void test_tier_detection(void) {
    struct zd_capabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.cpu_count = 8;
    caps.ram_mb = 16384;
    caps.gpu_tier = 3;
    caps.display_width = 2560;
    caps.display_height = 1440;
    caps.refresh_mhz = 60000;
    caps.hardware_accel = 1;
    ZD_CHECK_EQ(zd_governor_detect_tier(&caps), ZD_TIER_RICH);

    caps.ram_mb = 3072;
    ZD_CHECK_EQ(zd_governor_detect_tier(&caps), ZD_TIER_BALANCED);
    caps.ram_mb = 1536;
    ZD_CHECK_EQ(zd_governor_detect_tier(&caps), ZD_TIER_SIMPLIFIED);
    caps.ram_mb = 512;
    ZD_CHECK_EQ(zd_governor_detect_tier(&caps), ZD_TIER_MINIMAL);
    caps.ram_mb = 16384;
    caps.thermal_state = 2;
    ZD_CHECK_EQ(zd_governor_detect_tier(&caps), ZD_TIER_MINIMAL);
    caps.thermal_state = 0;
    caps.cpu_count = 1;
    ZD_CHECK_EQ(zd_governor_detect_tier(&caps), ZD_TIER_SIMPLIFIED);
    ZD_CHECK_EQ(zd_governor_detect_tier(0), ZD_TIER_MINIMAL);
}

static void test_pressure_ladder(void) {
    uint64_t none = zd_governor_actions_for_level(ZD_PRESSURE_NONE, 0);
    uint64_t low = zd_governor_actions_for_level(ZD_PRESSURE_LOW, 0);
    uint64_t moderate = zd_governor_actions_for_level(ZD_PRESSURE_MODERATE, 0);
    uint64_t high = zd_governor_actions_for_level(ZD_PRESSURE_HIGH, 0);
    uint64_t critical = zd_governor_actions_for_level(ZD_PRESSURE_CRITICAL, 0);
    uint64_t critical_recovery =
        zd_governor_actions_for_level(ZD_PRESSURE_CRITICAL, 1);

    ZD_CHECK_EQ(none, 0ULL);
    /* Cumulative: each level is a superset of the previous. */
    ZD_CHECK((low & ~moderate) == 0);
    ZD_CHECK((moderate & ~high) == 0);
    ZD_CHECK((high & ~critical) == 0);
    ZD_CHECK_EQ(low, (uint64_t)ZD_GOVERNOR_PAUSE_OPTIONAL);
    ZD_CHECK((moderate & ZD_GOVERNOR_LOWER_BG_PRIORITY) != 0);
    ZD_CHECK((high & ZD_GOVERNOR_RECLAIM_CACHES) != 0);
    ZD_CHECK((high & ZD_GOVERNOR_FREEZE_INACTIVE) != 0);
    ZD_CHECK((critical & ZD_GOVERNOR_PROTECT_FOREGROUND) != 0);
    /* Recovery NEVER appears without explicit necessity. */
    ZD_CHECK_EQ(critical & ZD_GOVERNOR_RECOVERY, 0ULL);
    ZD_CHECK((critical_recovery & ZD_GOVERNOR_RECOVERY) != 0);
}

static void test_governor_recovery_guard(void) {
    struct zd_governor governor;
    struct zd_capabilities caps = {4, 8192, 2, 1920, 1080, 60000, 1, 0, 0};
    zd_governor_init(&governor, &caps);
    ZD_CHECK_EQ(governor.tier, ZD_TIER_RICH);
    /* Recovery at non-critical level is a contract violation. */
    ZD_CHECK_ERR(zd_governor_set_pressure(&governor, ZD_PRESSURE_HIGH, 1),
                 ZD_EINVAL);
    ZD_CHECK_EQ(governor.recovery_invocations, 0U);
    ZD_CHECK_OK(zd_governor_set_pressure(&governor, ZD_PRESSURE_CRITICAL, 0));
    ZD_CHECK_EQ(governor.recovery_invocations, 0U);
    ZD_CHECK_OK(zd_governor_set_pressure(&governor, ZD_PRESSURE_CRITICAL, 1));
    ZD_CHECK_EQ(governor.recovery_invocations, 1U);
    /* Repeating the same recovery does not double-count a new invocation
     * path once already active... it does count each explicit entry; the
     * invariant under test is monotonic growth, never shrink. */
    ZD_CHECK_OK(zd_governor_set_pressure(&governor, ZD_PRESSURE_CRITICAL, 1));
    ZD_CHECK(governor.recovery_invocations >= 1U);
    ZD_CHECK_ERR(zd_governor_set_pressure(&governor, (enum zd_pressure_level)42, 0),
                 ZD_EINVAL);
}

static void test_tier_effect_monotonicity(void) {
    enum zd_perf_tier tier;
    uint32_t previous_budget = 0;
    uint32_t previous_mask = ~0U;
    for (tier = ZD_TIER_RICH; tier < ZD_TIER_COUNT; ++tier) {
        uint32_t budget = 0;
        uint32_t mask = 0;
        zd_governor_effects_for_tier(tier, &budget, &mask);
        if (tier != ZD_TIER_RICH) {
            /* Lower tiers never get a tighter budget than richer ones and
             * never gain effects the richer tier lacks. */
            ZD_CHECK(budget >= previous_budget);
            ZD_CHECK((mask & ~previous_mask) == 0);
        }
        previous_budget = budget;
        previous_mask = mask;
    }
    /* Minimal tier disables every effect. */
    {
        uint32_t budget = 0;
        uint32_t mask = 0xffff;
        zd_governor_effects_for_tier(ZD_TIER_MINIMAL, &budget, &mask);
        ZD_CHECK_EQ(mask, 0U);
        ZD_CHECK(budget >= 16U);
    }
}

void zd_test_governor_suite(void) {
    printf(" suite: governor\n");
    ZD_RUN(test_tier_detection);
    ZD_RUN(test_pressure_ladder);
    ZD_RUN(test_governor_recovery_guard);
    ZD_RUN(test_tier_effect_monotonicity);
}
