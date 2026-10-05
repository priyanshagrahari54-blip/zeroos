/* ZEROOS Dynamic Capsule Core (Section 12).
 *
 * Implements an event-driven status capsule with zero-render idle discipline:
 *  - Event states: IDLE, NOTIFICATION, MUSIC, DOWNLOAD, CALL, RECORDING,
 *    TIMER, NAVIGATION, AI_ACTIVITY, UPDATE, DEVICE_CONNECTION, SECURITY_ALERT
 *  - Priority arbitration: SECURITY_ALERT > CALL > RECORDING > UPDATE >
 *    TIMER > NAVIGATION > DOWNLOAD > NOTIFICATION > AI_ACTIVITY > MUSIC > IDLE
 *  - Animation levels: OFF, MINIMAL (G560 default), STANDARD, EXPRESSIVE
 *  - Idle invariant: 0 animation timer ticks, 0 polling, 0 continuous rendering.
 * Zero heap allocation, fully host-testable.
 */
#ifndef ZEROOS_DESKTOP_CAPSULE_H
#define ZEROOS_DESKTOP_CAPSULE_H

#include <stdint.h>

enum zd_capsule_state {
    ZD_CAP_IDLE = 0,
    ZD_CAP_MUSIC,
    ZD_CAP_AI_ACTIVITY,
    ZD_CAP_NOTIFICATION,
    ZD_CAP_DOWNLOAD,
    ZD_CAP_NAVIGATION,
    ZD_CAP_TIMER,
    ZD_CAP_UPDATE,
    ZD_CAP_RECORDING,
    ZD_CAP_CALL,
    ZD_CAP_DEVICE_CONNECTION,
    ZD_CAP_SECURITY_ALERT,
    ZD_CAP_STATE_COUNT
};

enum zd_capsule_anim_level {
    ZD_CAP_ANIM_OFF = 0,
    ZD_CAP_ANIM_MINIMAL = 1,   /* G560 default */
    ZD_CAP_ANIM_STANDARD = 2,
    ZD_CAP_ANIM_EXPRESSIVE = 3
};

struct zd_capsule_event {
    enum zd_capsule_state state;
    char primary_text[48];
    char secondary_text[32];
    uint32_t progress_pct;      /* 0..100 for download/update/timer */
    uint32_t timeout_ms;
    uint8_t active;
};

struct zd_capsule {
    enum zd_capsule_state active_state;
    enum zd_capsule_anim_level anim_level;
    struct zd_capsule_event slots[ZD_CAP_STATE_COUNT];
    uint32_t render_ticks_since_idle;
    uint8_t animation_timer_running;
    struct {
        uint32_t state_transitions;
        uint32_t alerts_prioritized;
        uint32_t idle_entries;
        uint32_t frames_rendered;
    } stats;
};

void zd_capsule_init(struct zd_capsule *cap, enum zd_capsule_anim_level level);
int zd_capsule_post(struct zd_capsule *cap, enum zd_capsule_state state,
                    const char *primary, const char *secondary, uint32_t progress);
int zd_capsule_clear(struct zd_capsule *cap, enum zd_capsule_state state);

/* Update tick: returns 1 if redraw needed, 0 if idle (0 frames when idle!) */
int zd_capsule_tick(struct zd_capsule *cap);

#endif /* ZEROOS_DESKTOP_CAPSULE_H */
