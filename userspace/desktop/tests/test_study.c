/* Study Center core host tests (part G). */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/study.h>

/* --- assistant broker fixtures --------------------------------- */
static int as_select(void *ctx, const struct zd_ai_request *r,
                     uint32_t grants, enum zd_ai_backend *out) {
    (void)ctx;
    (void)r;
    if (!(grants & ZD_AI_GRANT_CONTEXT_SELECTION))
        return -ZD_EPERM;
    *out = ZD_AI_BACKEND_LOCAL;
    return 0;
}

static int as_run(void *ctx, enum zd_ai_backend backend,
                  const struct zd_ai_request *req, char *out,
                  uint32_t out_cap, uint32_t *out_len) {
    (void)ctx;
    (void)req;
    if (backend == ZD_AI_BACKEND_NONE || out_cap < 3)
        return -ZD_EINVAL;
    out[0] = 'o';
    out[1] = 'k';
    out[2] = 0;
    *out_len = 2;
    return 0;
}

static void test_study_assist(void) {
    struct zd_study s;
    struct zd_ai_broker b;
    struct zd_ai_request r;
    uint32_t done = 0;

    zd_study_init(&s, "Spanish");
    /* empty deck: nothing to ask about */
    ZD_CHECK_EQ(zd_study_assist_request(&s, 0, &r), -2);
    ZD_CHECK_EQ(zd_study_assist_request(&s, "hola", &r), -2);
    ZD_CHECK_EQ(zd_study_assist_request(0, 0, &r), -22);
    ZD_CHECK_OK(zd_study_add_card(&s, "hola", "hello"));
    ZD_CHECK_OK(zd_study_add_card(&s, "gracias", "thanks"));

    /* explicit card */
    ZD_CHECK_OK(zd_study_assist_request(&s, "hola", &r));
    ZD_CHECK_EQ((int)r.kind, (int)ZD_AI_REQ_SUMMARIZE);
    ZD_CHECK_EQ(r.context_mask, (uint32_t)ZD_AI_GRANT_CONTEXT_SELECTION);
    ZD_CHECK_EQ(r.active, 0u);
    ZD_CHECK(strncmp(r.payload, "study Spanish: hola",
                     strlen("study Spanish: hola")) == 0);
    ZD_CHECK_EQ(r.payload[63], 0);
    /* unknown card */
    ZD_CHECK_EQ(zd_study_assist_request(&s, "nope", &r), -2);
    /* NULL front picks the next due card */
    ZD_CHECK_OK(zd_study_assist_request(&s, 0, &r));
    ZD_CHECK(strstr(r.payload, "hola") != 0);
    /* long front truncates safely inside the 64-byte payload */
    {
        char front[ZD_STUDY_TEXT];
        memset(front, 'F', sizeof(front) - 1);
        front[sizeof(front) - 1] = 0;
        ZD_CHECK_OK(zd_study_add_card(&s, front, "back"));
        ZD_CHECK_OK(zd_study_assist_request(&s, front, &r));
        ZD_CHECK_EQ(r.payload[63], 0);
        ZD_CHECK(strlen(r.payload) == 63);
    }

    /* broker path: granted -> accepted and drained */
    memset(&b, 0, sizeof(b));
    {
        struct zd_ai_ops ops = {as_select, as_run, 0};
        b.ops = ops;
    }
    zd_ai_grant(&b, ZD_AI_GRANT_CONTEXT_SELECTION);
    ZD_CHECK_OK(zd_study_assist_submit(&s, &b, "hola"));
    ZD_CHECK_OK(zd_ai_drain(&b, 8, &done));
    ZD_CHECK_EQ(done, 1u);
    ZD_CHECK_EQ(b.stats.completed, 1u);
    /* revoked: build succeeds, broker rejects before any backend */
    zd_ai_revoke(&b, ZD_AI_GRANT_CONTEXT_SELECTION);
    ZD_CHECK_EQ(zd_study_assist_submit(&s, &b, "hola"), -ZD_EPERM);
    ZD_CHECK_EQ(b.stats.denied_permission, 1u);
    /* NULL broker rejected at the wrapper */
    ZD_CHECK_EQ(zd_study_assist_submit(&s, 0, "hola"), -22);
}

void zd_test_study_suite(void) {
    struct zd_study s;
    struct zd_study_card *c;

    /* init validation */
    ZD_CHECK(zd_study_init(&s, "Biology 101") == 0);
    ZD_CHECK(zd_study_init(&s, 0) == -22);
    ZD_CHECK(zd_study_init(&s, "") == -22);
    ZD_CHECK(zd_study_init(0, "x") == -22);

    /* cards */
    ZD_CHECK(zd_study_add_card(&s, "mitosis", "cell division") == 0);
    ZD_CHECK(zd_study_add_card(&s, "meiosis", "gamete division") == 0);
    ZD_CHECK(zd_study_add_card(&s, "mitosis", "dup") == -17);
    ZD_CHECK(zd_study_add_card(&s, "", "x") == -22);
    ZD_CHECK(zd_study_add_card(&s, "y", "") == -22);
    ZD_CHECK_EQ(s.card_count, 2);
    ZD_CHECK(zd_study_find(&s, "MITOSIS") == 0); /* exact fronts */
    c = zd_study_find(&s, "mitosis");
    ZD_CHECK(c != 0 && c->ease == 250 && c->interval_days == 0);

    /* both due now; most overdue first (tie -> first listed) */
    ZD_CHECK(zd_study_next_due(&s) != 0);

    /* scheduling ladder */
    ZD_CHECK(zd_study_grade(&s, "mitosis", ZD_STUDY_GOOD) == 0);
    c = zd_study_find(&s, "mitosis");
    ZD_CHECK_EQ(c->interval_days, 1);  /* 0 -> step1 */
    ZD_CHECK_EQ(c->due_day, 1);
    ZD_CHECK(zd_study_grade(&s, "mitosis", ZD_STUDY_GOOD) == 0);
    ZD_CHECK_EQ(c->interval_days, 3);
    ZD_CHECK(zd_study_grade(&s, "mitosis", ZD_STUDY_EASY) == 0);
    ZD_CHECK_EQ(c->interval_days, 21); /* +2 rungs: 3 -> 21 */
    ZD_CHECK_EQ(c->ease, 265);         /* 250 +15 after EASY */
    ZD_CHECK(zd_study_grade(&s, "mitosis", ZD_STUDY_HARD) == 0);
    ZD_CHECK_EQ(c->interval_days, 21); /* stays */
    ZD_CHECK_EQ(c->ease, 250);         /* HARD takes 15 back */
    ZD_CHECK(zd_study_grade(&s, "mitosis", ZD_STUDY_AGAIN) == 0);
    ZD_CHECK_EQ(c->interval_days, 1);
    ZD_CHECK_EQ(c->ease, 230);         /* AGAIN drops 20 */
    ZD_CHECK_EQ(c->lapses, 1);         /* AGAIN from interval 21 = lapse */
    ZD_CHECK(c->reps == 5);

    /* grade errors */
    ZD_CHECK(zd_study_grade(&s, "nope", ZD_STUDY_GOOD) == -2);
    ZD_CHECK(zd_study_grade(&s, "mitosis", 99) == -22);

    /* day advancement drives due scheduling */
    ZD_CHECK(zd_study_advance_day(&s, 0) == -22);
    ZD_CHECK(zd_study_advance_day(&s, 1) == 0);
    ZD_CHECK(zd_study_advance_day(&s, 400) == 0);
    ZD_CHECK_EQ(s.day, 401);
    ZD_CHECK(zd_study_next_due(&s) != 0); /* all overdue */

    /* capacity */
    {
        int k;
        for (k = 0; k < ZD_STUDY_MAX_CARDS + 5; ++k) {
            char f[16];
            int j = 0;
            f[j++] = 'c';
            f[j++] = (char)('0' + k / 10);
            f[j++] = (char)('0' + k % 10);
            f[j] = 0;
            if (zd_study_find(&s, f))
                continue;
            if (zd_study_add_card(&s, f, "back") == -28)
                break;
        }
        ZD_CHECK(s.card_count <= ZD_STUDY_MAX_CARDS);
        ZD_CHECK(zd_study_add_card(&s, "overflow-card", "b") == -28);
    }

    /* sessions + focus mode */
    ZD_CHECK(zd_study_session_start(&s, 25) == 0);
    ZD_CHECK(zd_study_session_start(&s, 25) == -16); /* EBUSY */
    ZD_CHECK(zd_study_session_start(&s, 0) == -22);
    ZD_CHECK(zd_study_session_tick(&s) == 0);
    ZD_CHECK(zd_study_session_tick(&s) == 0);
    ZD_CHECK_EQ(s.session.focus_elapsed, 2);
    /* grade inside session counts toward the review tally */
    ZD_CHECK(zd_study_grade(&s, "meiosis", ZD_STUDY_GOOD) == 0);
    ZD_CHECK_EQ(s.session.reviewed, 1);
    /* burn the focus block to completion, then extra ticks are inert */
    {
        uint32_t i;
        for (i = 2; i < 25; ++i)
            zd_study_session_tick(&s);
        ZD_CHECK_EQ(s.session.focus_elapsed, 25);
        ZD_CHECK_EQ(s.stats.focus_blocks, 1);
        zd_study_session_tick(&s);
        zd_study_session_tick(&s);
        ZD_CHECK_EQ(s.stats.focus_blocks, 1); /* counted once */
    }
    ZD_CHECK(zd_study_session_end(&s) == 0);
    ZD_CHECK(zd_study_session_end(&s) == -22);
    ZD_CHECK(zd_study_session_tick(&s) == -22);
    ZD_CHECK(zd_study_session_start(&s, 10) == 0); /* restartable */
    zd_study_session_end(&s);

    ZD_CHECK(s.stats.reviews >= 6);
    ZD_CHECK(s.stats.sessions_started == 2);

    test_study_assist();
}
