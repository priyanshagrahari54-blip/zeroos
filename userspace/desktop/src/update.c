/* Transactional update state machine.  See update.h; pipeline matches
 * ARCHITECTURE section 20.  Transitions are atomic: a hook failure
 * moves the machine to FAILED or ROLLING_BACK in one step, never a
 * half-applied intermediate. */
#include <zeroos/desktop/update.h>
#include "crypto.h"

static const char *state_names[] = {
    "idle", "downloading", "verifying", "staging", "preflight",
    "activating", "health", "committing", "done", "rolling_back",
    "failed"
};

void zd_update_init(struct zd_update *u, const struct zd_update_ops *ops) {
    if (!u)
        return;
    u->state = ZD_UPD_IDLE;
    u->ops = ops ? *ops : (struct zd_update_ops){0, 0, 0, 0, 0};
    u->version[0] = 0;
    u->seq = 0;
    u->stats.started = u->stats.committed = u->stats.rollbacks = 0;
    u->stats.verify_failures = u->stats.health_failures = 0;
    u->stats.rejected_events = u->stats.cancels = 0;
}

int zd_update_state(const struct zd_update *u) {
    return u ? u->state : -22;
}

static int upd_active(int st) {
    return st != ZD_UPD_IDLE && st != ZD_UPD_DONE && st != ZD_UPD_FAILED;
}

int zd_update_can_cancel(const struct zd_update *u) {
    if (!u)
        return 0;
    /* Cancel is allowed until the A/B slot flip begins. */
    return u->state == ZD_UPD_DOWNLOADING || u->state == ZD_UPD_VERIFYING ||
           u->state == ZD_UPD_STAGING || u->state == ZD_UPD_PREFLIGHT;
}

static void upd_copy(char *d, unsigned cap, const char *s) {
    unsigned i = 0;
    if (!d || !cap)
        return;
    if (!s) {
        d[0] = 0;
        return;
    }
    while (s[i] && i + 1 < cap) {
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
}

int zd_update_begin(struct zd_update *u, const char *version) {
    if (!u || !version || !version[0])
        return -22;
    if (upd_active(u->state))
        return -16; /* EBUSY: a lifecycle is already running */
    u->state = ZD_UPD_DOWNLOADING;
    u->seq++;
    u->stats.started++;
    upd_copy(u->version, sizeof(u->version), version);
    return 0;
}

static int upd_fail(struct zd_update *u) {
    u->state = ZD_UPD_FAILED;
    u->seq++;
    return 0;
}

int zd_update_event(struct zd_update *u, int event) {
    int prev;
    if (!u || event < 0 || event > ZD_UPD_EV_CANCEL) {
        if (u)
            u->stats.rejected_events++;
        return -22;
    }
    prev = u->state;

#define REJECT()  do { u->stats.rejected_events++; return -22; } while (0)
    switch (event) {
    case ZD_UPD_EV_START:
        if (prev != ZD_UPD_IDLE && prev != ZD_UPD_DONE &&
            prev != ZD_UPD_FAILED)
            REJECT();
        return zd_update_begin(u, u->version[0] ? u->version : "update");

    case ZD_UPD_EV_DOWNLOAD_OK:
        if (prev != ZD_UPD_DOWNLOADING)
            REJECT();
        u->state = ZD_UPD_VERIFYING;
        break;
    case ZD_UPD_EV_DOWNLOAD_FAIL:
        if (prev != ZD_UPD_DOWNLOADING)
            REJECT();
        upd_fail(u);
        return 0;
    case ZD_UPD_EV_VERIFY_OK:
        if (prev != ZD_UPD_VERIFYING)
            REJECT();
        u->state = ZD_UPD_STAGING;
        break;
    case ZD_UPD_EV_VERIFY_FAIL:
        if (prev != ZD_UPD_VERIFYING)
            REJECT();
        u->stats.verify_failures++;
        upd_fail(u); /* nothing was activated */
        return 0;
    case ZD_UPD_EV_STAGE_OK:
        if (prev != ZD_UPD_STAGING)
            REJECT();
        u->state = ZD_UPD_PREFLIGHT;
        break;
    case ZD_UPD_EV_PREFLIGHT_OK:
        if (prev != ZD_UPD_PREFLIGHT)
            REJECT();
        u->state = ZD_UPD_ACTIVATING;
        break;
    case ZD_UPD_EV_PREFLIGHT_FAIL:
        if (prev != ZD_UPD_PREFLIGHT)
            REJECT();
        upd_fail(u);
        return 0;
    case ZD_UPD_EV_ACTIVATE_OK:
        if (prev != ZD_UPD_ACTIVATING)
            REJECT();
        if (u->ops.activate) {
            int r = u->ops.activate(u->ops.ctx);
            if (r < 0) {
                u->state = ZD_UPD_ROLLING_BACK;
                u->seq++;
                return r;
            }
        }
        u->state = ZD_UPD_HEALTH_CHECK;
        break;
    case ZD_UPD_EV_ACTIVATE_FAIL:
        if (prev != ZD_UPD_ACTIVATING)
            REJECT();
        u->state = ZD_UPD_ROLLING_BACK;
        u->seq++;
        return 0;
    case ZD_UPD_EV_HEALTH_OK:
        if (prev != ZD_UPD_HEALTH_CHECK)
            REJECT();
        u->state = ZD_UPD_COMMITTING;
        break;
    case ZD_UPD_EV_HEALTH_FAIL:
        if (prev != ZD_UPD_HEALTH_CHECK)
            REJECT();
        u->stats.health_failures++;
        u->state = ZD_UPD_ROLLING_BACK;
        u->seq++;
        return 0;
    case ZD_UPD_EV_COMMIT_OK:
        if (prev != ZD_UPD_COMMITTING)
            REJECT();
        if (u->ops.commit) {
            int r = u->ops.commit(u->ops.ctx);
            if (r < 0) {
                upd_fail(u);
                return r;
            }
        }
        u->state = ZD_UPD_DONE;
        u->stats.committed++;
        u->seq++;
        return 0;
    case ZD_UPD_EV_ROLLBACK_DONE:
        if (prev != ZD_UPD_ROLLING_BACK)
            REJECT();
        if (u->ops.rollback) {
            int r = u->ops.rollback(u->ops.ctx);
            if (r < 0) {
                upd_fail(u);
                return r;
            }
        }
        u->state = ZD_UPD_FAILED;
        u->stats.rollbacks++;
        u->seq++;
        return 0;
    case ZD_UPD_EV_CANCEL:
        if (!zd_update_can_cancel(u))
            REJECT();
        u->stats.cancels++;
        u->state = ZD_UPD_IDLE;
        u->seq++;
        return 0;
    default:
        REJECT();
    }
    u->seq++;
    return 0;
#undef REJECT
}

/* Keep the state-name table referenced for diagnostics callers. */
const char *zd_update_state_name(const struct zd_update *u) {
    if (!u || u->state < 0 || u->state >= (int)(sizeof(state_names) /
                                                sizeof(state_names[0])))
        return "?";
    return state_names[u->state];
}

/* AEAD payload integrity check — see update.h for the contract. */
int zd_update_verify_payload(const uint8_t key[32], const uint8_t nonce[12],
                             const char *version, const uint8_t *payload,
                             uint32_t payload_len, const uint8_t tag[16]) {
    uint8_t scratch[ZD_UPDATE_VERIFY_MAX];
    uint32_t vlen = 0, out_len;
    int r;
    if (!key || !nonce || !version || !payload || !tag)
        return -22;
    if (payload_len == 0 || payload_len > ZD_UPDATE_VERIFY_MAX)
        return -22;
    while (version[vlen] && vlen < 64u)
        ++vlen;
    if (vlen == 0 || vlen >= 64u)
        return -22;
    out_len = 0;
    r = zeroos_aead_decrypt(key, nonce, (const uint8_t *)version, vlen,
                            payload, payload_len, tag, scratch);
    /* scratch is transient; wipe regardless of outcome */
    {
        uint32_t i;
        for (i = 0; i < payload_len; ++i)
            scratch[i] = 0;
    }
    (void)out_len;
    if (r != ZCRYPTO_OK)
        return -3;
    return 0;
}
