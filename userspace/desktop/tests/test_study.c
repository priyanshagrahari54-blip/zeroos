/* Study Center core host tests (part G). */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/study.h>

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
}
