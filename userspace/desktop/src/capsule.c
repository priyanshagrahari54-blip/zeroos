#include <zeroos/desktop/capsule.h>

static void zc_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!cap)
        return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void recompute_active_state(struct zd_capsule *cap) {
    /* Highest priority active state wins */
    for (int s = ZD_CAP_STATE_COUNT - 1; s > 0; --s) {
        if (cap->slots[s].active) {
            if (cap->active_state != (enum zd_capsule_state)s) {
                cap->active_state = (enum zd_capsule_state)s;
                cap->stats.state_transitions++;
                if (s == ZD_CAP_SECURITY_ALERT || s == ZD_CAP_CALL)
                    cap->stats.alerts_prioritized++;
                cap->animation_timer_running = (cap->anim_level != ZD_CAP_ANIM_OFF) ? 1 : 0;
            }
            return;
        }
    }

    /* Return to idle */
    if (cap->active_state != ZD_CAP_IDLE) {
        cap->active_state = ZD_CAP_IDLE;
        cap->stats.state_transitions++;
        cap->stats.idle_entries++;
        cap->animation_timer_running = 0; /* STOP all animation timers */
        cap->render_ticks_since_idle = 0;
    }
}

void zd_capsule_init(struct zd_capsule *cap, enum zd_capsule_anim_level level) {
    if (!cap)
        return;
    for (uint32_t i = 0; i < sizeof(*cap); ++i)
        ((uint8_t *)cap)[i] = 0;
    cap->active_state = ZD_CAP_IDLE;
    cap->anim_level = level;
    cap->animation_timer_running = 0;
}

int zd_capsule_post(struct zd_capsule *cap, enum zd_capsule_state state,
                    const char *primary, const char *secondary, uint32_t progress) {
    if (!cap || state <= ZD_CAP_IDLE || state >= ZD_CAP_STATE_COUNT)
        return -22;

    struct zd_capsule_event *ev = &cap->slots[state];
    ev->state = state;
    zc_copy(ev->primary_text, primary ? primary : "", sizeof(ev->primary_text));
    zc_copy(ev->secondary_text, secondary ? secondary : "", sizeof(ev->secondary_text));
    ev->progress_pct = progress > 100 ? 100 : progress;
    ev->active = 1;

    recompute_active_state(cap);
    return 0;
}

int zd_capsule_clear(struct zd_capsule *cap, enum zd_capsule_state state) {
    if (!cap || state <= ZD_CAP_IDLE || state >= ZD_CAP_STATE_COUNT)
        return -22;

    cap->slots[state].active = 0;
    cap->slots[state].primary_text[0] = 0;
    cap->slots[state].secondary_text[0] = 0;

    recompute_active_state(cap);
    return 0;
}

int zd_capsule_tick(struct zd_capsule *cap) {
    if (!cap)
        return 0;
    if (cap->active_state == ZD_CAP_IDLE) {
        /* IDLE INVARIANT: 0 frames rendered, 0 animation timer ticks */
        cap->animation_timer_running = 0;
        return 0;
    }

    cap->stats.frames_rendered++;
    cap->render_ticks_since_idle++;
    return 1;
}
