/* ZEROOS Dynamic Capsule Tests (Section 12) */
#include <zeroos/desktop/capsule.h>
#include "test_harness.h"
#include <string.h>

void zd_test_capsule_suite(void) {
    zd_test_current = "capsule";
    printf(" suite: capsule\n");

    struct zd_capsule cap;
    zd_capsule_init(&cap, ZD_CAP_ANIM_MINIMAL);

    /* Idle invariant: active state is IDLE, no animation timer running */
    ZD_CHECK(cap.active_state == ZD_CAP_IDLE);
    ZD_CHECK(cap.animation_timer_running == 0);
    ZD_CHECK(zd_capsule_tick(&cap) == 0); /* 0 frames when idle */

    /* Post music playback */
    ZD_CHECK(zd_capsule_post(&cap, ZD_CAP_MUSIC, "Track 01", "Artist", 30) == 0);
    ZD_CHECK(cap.active_state == ZD_CAP_MUSIC);
    ZD_CHECK(cap.animation_timer_running == 1);
    ZD_CHECK(zd_capsule_tick(&cap) == 1); /* Active frame rendered */

    /* Post high-priority security alert -> preempts music immediately */
    ZD_CHECK(zd_capsule_post(&cap, ZD_CAP_SECURITY_ALERT, "Firewall", "Port Scan Blocked", 0) == 0);
    ZD_CHECK(cap.active_state == ZD_CAP_SECURITY_ALERT);
    ZD_CHECK(cap.stats.alerts_prioritized == 1);

    /* Clear security alert -> falls back to music */
    ZD_CHECK(zd_capsule_clear(&cap, ZD_CAP_SECURITY_ALERT) == 0);
    ZD_CHECK(cap.active_state == ZD_CAP_MUSIC);

    /* Clear music -> returns to IDLE, canceling animation timer */
    ZD_CHECK(zd_capsule_clear(&cap, ZD_CAP_MUSIC) == 0);
    ZD_CHECK(cap.active_state == ZD_CAP_IDLE);
    ZD_CHECK(cap.animation_timer_running == 0);
    ZD_CHECK(cap.stats.idle_entries == 1);
    ZD_CHECK(zd_capsule_tick(&cap) == 0); /* Exactly 0 frames when idle */
}
