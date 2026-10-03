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

/* Update operations are injected by the platform integration. A missing
 * required operation is a hard failure; success events cannot substitute for
 * an absent enforcement or persistence boundary. */
struct zd_update_ops {
    /* Authenticate the exact downloaded package/version against trust policy. */
    int (*verify_package)(void *ctx, const char *version);
    int (*stage_apply)(void *ctx);      /* write verified payload to staging */
    int (*preflight)(void *ctx);        /* validate staged activation target */
    int (*activate)(void *ctx);         /* flip to new A/B slot */
    int (*health_check)(void *ctx);     /* validate newly activated system */
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
 * lifecycle is already running where applicable, and -95 (ENOTSUP) when a
 * required verifier, staging, preflight, activation, health, commit, or
 * rollback operation is absent. *_OK events invoke their corresponding
 * operation; caller-supplied success is never accepted as proof. Hook <0
 * propagates (mapped to FAILED/ROLLING_BACK per stage). */
int zd_update_event(struct zd_update *u, int event);
int zd_update_state(const struct zd_update *u);
/* Symmetric AEAD payload-integrity helper for tests/prototype integrations.
 * This is NOT a public-key package signature or a production trust store.
 * The update state machine only proceeds when a configured verify_package
 * provider returns success. Uses ChaCha20-Poly1305 with a caller-supplied key
 * and nonce; 0 = intact, -3 = tampered/wrong key/version, -22 = bad args or
 * oversize (bounded to 4 KiB). */
#define ZD_UPDATE_VERIFY_MAX 4096u
int zd_update_verify_payload(const uint8_t key[32], const uint8_t nonce[12],
                             const char *version, const uint8_t *payload,
                             uint32_t payload_len, const uint8_t tag[16]);
const char *zd_update_state_name(const struct zd_update *u);
int zd_update_can_cancel(const struct zd_update *u);

#endif /* ZEROOS_DESKTOP_UPDATE_H */
