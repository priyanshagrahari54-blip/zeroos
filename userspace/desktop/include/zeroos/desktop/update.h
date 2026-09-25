/* Transactional update core (Stage 5 part B / ARCHITECTURE section 20).
 * Pipeline: download -> verify -> stage -> preflight -> activate ->
 * health check -> commit, with rollback on any post-verify failure.
 * Step-driven (no polling): the shell feeds completion/failure events.
 * Hooks are injected; nothing fabricates success.  Host-testable. */
#ifndef ZEROOS_DESKTOP_UPDATE_H
#define ZEROOS_DESKTOP_UPDATE_H

#include <stdint.h>

enum zd_update_state {
    ZD_UPD_IDLE = 0,
    ZD_UPD_DOWNLOADING,
    ZD_UPD_VERIFYING,
    ZD_UPD_STAGING,
    ZD_UPD_PREFLIGHT,
    ZD_UPD_ACTIVATING,
    ZD_UPD_HEALTH_CHECK,
    ZD_UPD_COMMITTING,
    ZD_UPD_DONE,
    ZD_UPD_ROLLING_BACK,
    ZD_UPD_FAILED,
    ZD_UPD_STATE_COUNT
};

enum zd_update_event {
    ZD_UPD_EV_START = 0,     /* begin: -> DOWNLOADING */
    ZD_UPD_EV_DOWNLOAD_OK,
    ZD_UPD_EV_DOWNLOAD_FAIL,
    ZD_UPD_EV_VERIFY_OK,
    ZD_UPD_EV_VERIFY_FAIL,   /* nothing activated; -> FAILED */
    ZD_UPD_EV_STAGE_OK,
    ZD_UPD_EV_PREFLIGHT_OK,
    ZD_UPD_EV_PREFLIGHT_FAIL,
    ZD_UPD_EV_ACTIVATE_OK,
    ZD_UPD_EV_ACTIVATE_FAIL,
    ZD_UPD_EV_HEALTH_OK,
    ZD_UPD_EV_HEALTH_FAIL,   /* -> ROLLING_BACK */
    ZD_UPD_EV_COMMIT_OK,
    ZD_UPD_EV_ROLLBACK_DONE,
    ZD_UPD_EV_CANCEL          /* allowed before ACTIVATING only */
};

/* Side-effect hooks (all optional; run during the transition). */
struct zd_update_ops {
    int (*stage_apply)(void *ctx);      /* write payload to staging */
    int (*activate)(void *ctx);         /* flip to new A/B slot */
    int (*rollback)(void *ctx);         /* restore previous slot */
    int (*commit)(void *ctx);           /* mark new slot good */
    void *ctx;
};

struct zd_update {
    int state;                          /* enum zd_update_state */
    struct zd_update_ops ops;
    char version[32];                   /* target version label */
    uint32_t seq;                       /* transitions applied */
    struct {
        uint32_t started, committed, rollbacks, verify_failures,
                 health_failures, rejected_events, cancels;
    } stats;
};

void zd_update_init(struct zd_update *u, const struct zd_update_ops *ops);
int zd_update_begin(struct zd_update *u, const char *version);
/* Feed one pipeline event; returns 0 on accepted transition,
 * -22 (EINVAL) invalid event for current state, -16 (EBUSY) if a
 * lifecycle is already running where applicable, hook <0 propagates
 * (mapped to FAILED/ROLLING_BACK per stage). */
int zd_update_event(struct zd_update *u, int event);
int zd_update_state(const struct zd_update *u);
const char *zd_update_state_name(const struct zd_update *u);
int zd_update_can_cancel(const struct zd_update *u);

#endif /* ZEROOS_DESKTOP_UPDATE_H */
