/* Transactional update state machine host tests (part B). */
#include "test_harness.h"
#include <zeroos/desktop/update.h>
#include "crypto.h"

struct hook_log {
    int stage_apply, activate, rollback, commit;
    int fail_activate, fail_commit, fail_rollback;
};
static int h_stage(void *c) { ((struct hook_log *)c)->stage_apply++; return 0; }
static int h_act(void *c) {
    struct hook_log *l = c;
    l->activate++;
    return l->fail_activate ? -5 : 0;
}
static int h_rb(void *c) {
    struct hook_log *l = c;
    l->rollback++;
    return l->fail_rollback ? -5 : 0;
}
static int h_commit(void *c) {
    struct hook_log *l = c;
    l->commit++;
    return l->fail_commit ? -5 : 0;
}
static struct zd_update_ops make_ops(struct hook_log *l) {
    struct zd_update_ops o;
    o.stage_apply = h_stage;
    o.activate = h_act;
    o.rollback = h_rb;
    o.commit = h_commit;
    o.ctx = l;
    return o;
}

static void run_to(struct zd_update *u, int final_event) {
    /* drive the happy prefix: download->verify->stage->preflight ok */
    zd_update_begin(u, "ver");
    zd_update_event(u, ZD_UPD_EV_DOWNLOAD_OK);
    zd_update_event(u, ZD_UPD_EV_VERIFY_OK);
    zd_update_event(u, ZD_UPD_EV_STAGE_OK);
    zd_update_event(u, ZD_UPD_EV_PREFLIGHT_OK);
    zd_update_event(u, ZD_UPD_EV_ACTIVATE_OK);
    zd_update_event(u, ZD_UPD_EV_HEALTH_OK);
    zd_update_event(u, final_event);
}

void zd_test_update_suite(void) {
    struct zd_update u;
    struct hook_log log;
    struct zd_update_ops ops;
    uint32_t seq;

    /* NULL/invalid args */
    zd_update_init(0, 0);
    ZD_CHECK(zd_update_state(0) == -22);
    zd_update_init(&u, 0); /* no hooks: pipeline still valid */
    ZD_CHECK(zd_update_begin(&u, 0) == -22);
    ZD_CHECK(zd_update_begin(&u, "") == -22);
    ZD_CHECK(zd_update_event(&u, 999) == -22);
    ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_HEALTH_OK) == -22);
    ZD_CHECK(u.stats.rejected_events >= 2);

    /* happy path: hooks exactly once, committed */
    log = (struct hook_log){0, 0, 0, 0, 0, 0, 0};
    ops = make_ops(&log);
    zd_update_init(&u, &ops);
    ZD_CHECK(zd_update_begin(&u, "5.1.0") == 0);
    ZD_CHECK(zd_update_begin(&u, "5.1.1") == -16); /* EBUSY */
    ZD_CHECK_EQ(u.stats.started, 1);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_DOWNLOADING);
    /* cancel allowed pre-activation, returns to IDLE */
    ZD_CHECK(zd_update_can_cancel(&u) == 1);
    ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_CANCEL) == 0);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_IDLE);
    ZD_CHECK_EQ(u.stats.cancels, 1);
    run_to(&u, ZD_UPD_EV_COMMIT_OK);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_DONE);
    ZD_CHECK(log.activate == 1 && log.commit == 1 && log.rollback == 0);
    ZD_CHECK(u.stats.committed == 1 && u.stats.rollbacks == 0);

    /* restart from DONE is allowed (next release) */
    seq = u.seq;
    ZD_CHECK(zd_update_begin(&u, "5.1.1") == 0);
    ZD_CHECK(u.seq > seq);

    /* verify failure: NEVER activates, no rollback */
    log = (struct hook_log){0, 0, 0, 0, 0, 0, 0};
    zd_update_init(&u, &ops);
    zd_update_begin(&u, "5.2.0");
    zd_update_event(&u, ZD_UPD_EV_DOWNLOAD_OK);
    ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_VERIFY_FAIL) == 0);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_FAILED);
    ZD_CHECK(log.activate == 0 && log.rollback == 0);
    ZD_CHECK_EQ(u.stats.verify_failures, 1);

    /* download failure */
    zd_update_init(&u, &ops);
    zd_update_begin(&u, "5.2.1");
    ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_DOWNLOAD_FAIL) == 0);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_FAILED);

    /* health failure: activated, must roll back, then FAILED */
    log = (struct hook_log){0, 0, 0, 0, 0, 0, 0};
    zd_update_init(&u, &ops);
    zd_update_begin(&u, "5.3.0");
    zd_update_event(&u, ZD_UPD_EV_DOWNLOAD_OK);
    zd_update_event(&u, ZD_UPD_EV_VERIFY_OK);
    zd_update_event(&u, ZD_UPD_EV_STAGE_OK);
    zd_update_event(&u, ZD_UPD_EV_PREFLIGHT_OK);
    zd_update_event(&u, ZD_UPD_EV_ACTIVATE_OK);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_HEALTH_CHECK);
    ZD_CHECK(zd_update_can_cancel(&u) == 0); /* past activation */
    ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_HEALTH_FAIL) == 0);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_ROLLING_BACK);
    ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_ROLLBACK_DONE) == 0);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_FAILED);
    ZD_CHECK(log.rollback == 1);
    ZD_CHECK_EQ(u.stats.health_failures, 1);
    ZD_CHECK_EQ(u.stats.rollbacks, 1);

    /* activate hook failure -> straight to ROLLING_BACK, error kept */
    log = (struct hook_log){1, 0, 0, 0, 1, 0, 0};
    zd_update_init(&u, &ops);
    zd_update_begin(&u, "5.3.1");
    zd_update_event(&u, ZD_UPD_EV_DOWNLOAD_OK);
    zd_update_event(&u, ZD_UPD_EV_VERIFY_OK);
    zd_update_event(&u, ZD_UPD_EV_STAGE_OK);
    zd_update_event(&u, ZD_UPD_EV_PREFLIGHT_OK);
    ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_ACTIVATE_OK) == -5);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_ROLLING_BACK);
    zd_update_event(&u, ZD_UPD_EV_ROLLBACK_DONE);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_FAILED);

    /* commit hook failure -> FAILED, not DONE */
    log = (struct hook_log){0, 0, 0, 0, 0, 1, 0};
    zd_update_init(&u, &ops);
    zd_update_begin(&u, "5.3.2");
    zd_update_event(&u, ZD_UPD_EV_DOWNLOAD_OK);
    zd_update_event(&u, ZD_UPD_EV_VERIFY_OK);
    zd_update_event(&u, ZD_UPD_EV_STAGE_OK);
    zd_update_event(&u, ZD_UPD_EV_PREFLIGHT_OK);
    zd_update_event(&u, ZD_UPD_EV_ACTIVATE_OK);
    zd_update_event(&u, ZD_UPD_EV_HEALTH_OK);
    ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_COMMIT_OK) == -5);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_FAILED);
    ZD_CHECK(u.stats.committed == 0);

    /* rollback hook failure -> FAILED, completed-rollback not counted */
    log = (struct hook_log){0, 0, 0, 0, 0, 0, 1};
    zd_update_init(&u, &ops);
    zd_update_begin(&u, "5.3.3");
    zd_update_event(&u, ZD_UPD_EV_DOWNLOAD_OK);
    zd_update_event(&u, ZD_UPD_EV_VERIFY_OK);
    zd_update_event(&u, ZD_UPD_EV_STAGE_OK);
    zd_update_event(&u, ZD_UPD_EV_PREFLIGHT_OK);
    zd_update_event(&u, ZD_UPD_EV_ACTIVATE_OK);
    zd_update_event(&u, ZD_UPD_EV_HEALTH_FAIL);
    ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_ROLLBACK_DONE) == -5);
    ZD_CHECK(zd_update_state(&u) == ZD_UPD_FAILED);
    ZD_CHECK_EQ(u.stats.rollbacks, 0);

    /* out-of-order events are rejected without corrupting state */
    zd_update_init(&u, &ops);
    zd_update_begin(&u, "5.4.0");
    {
        uint32_t before = u.stats.rejected_events;
        ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_HEALTH_OK) == -22);
        ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_COMMIT_OK) == -22);
        ZD_CHECK(zd_update_event(&u, ZD_UPD_EV_STAGE_OK) == -22);
        ZD_CHECK_EQ(u.stats.rejected_events, before + 3);
        ZD_CHECK(zd_update_state(&u) == ZD_UPD_DOWNLOADING);
    }

    /* state names are always readable */
    zd_update_init(&u, 0);
    ZD_CHECK(zd_update_state_name(&u) != 0);

    /* AEAD payload verification (real crypto, no mocks) */
    {
        uint8_t key[32], nonce[12], tag[16], payload[64], out_buf[64];
        uint32_t i;
        for (i = 0; i < 32; ++i)
            key[i] = (uint8_t)(i * 3 + 7);
        for (i = 0; i < 12; ++i)
            nonce[i] = (uint8_t)(0x40 + i);
        for (i = 0; i < sizeof(payload); ++i)
            payload[i] = (uint8_t)(i ^ 0x5A);
        /* producer side: encrypt with version bound as AAD */
        ZD_CHECK(zeroos_aead_encrypt(key, nonce,
                                     (const uint8_t *)"5.2.0", 5,
                                     payload, sizeof(payload),
                                     out_buf, tag) == 0);
        /* intact bundle verifies */
        ZD_CHECK(zd_update_verify_payload(key, nonce, "5.2.0", out_buf,
                                          sizeof(payload), tag) == 0);
        /* tampered payload fails */
        out_buf[10] ^= 0x01;
        ZD_CHECK(zd_update_verify_payload(key, nonce, "5.2.0", out_buf,
                                          sizeof(payload), tag) == -3);
        out_buf[10] ^= 0x01;
        /* wrong version (AAD mismatch) fails — no cross-version replay */
        ZD_CHECK(zd_update_verify_payload(key, nonce, "5.2.1", out_buf,
                                          sizeof(payload), tag) == -3);
        /* wrong key fails */
        key[0] ^= 0xFF;
        ZD_CHECK(zd_update_verify_payload(key, nonce, "5.2.0", out_buf,
                                          sizeof(payload), tag) == -3);
        key[0] ^= 0xFF;
        /* bad args and oversize */
        ZD_CHECK(zd_update_verify_payload(0, nonce, "5.2.0", out_buf,
                                          sizeof(payload), tag) == -22);
        ZD_CHECK(zd_update_verify_payload(key, nonce, "5.2.0", out_buf,
                                          0, tag) == -22);
        ZD_CHECK(zd_update_verify_payload(key, nonce, "", out_buf,
                                          sizeof(payload), tag) == -22);
        ZD_CHECK(zd_update_verify_payload(key, nonce, "5.2.0", out_buf,
                                          ZD_UPDATE_VERIFY_MAX + 1,
                                          tag) == -22);
    }
}
