/* Study Center core (Stage 5 part G): flashcards with a bounded
 * spaced-repetition scheduler, review sessions and focus mode.
 * PDF/OCR/formulas attach later on these contracts. */
#ifndef ZEROOS_DESKTOP_STUDY_H
#define ZEROOS_DESKTOP_STUDY_H

#include <stdint.h>
#include <zeroos/desktop/ai.h> /* AI study assistant request types */

#define ZD_STUDY_MAX_CARDS 64
#define ZD_STUDY_MAX_DECK_NAME 32
#define ZD_STUDY_TEXT 96

enum zd_study_grade {
    ZD_STUDY_AGAIN = 0,   /* repeat today, ease down */
    ZD_STUDY_HARD,        /* same interval tier, ease down */
    ZD_STUDY_GOOD,        /* advance one step */
    ZD_STUDY_EASY         /* advance two steps */
};

struct zd_study_card {
    char front[ZD_STUDY_TEXT];
    char back[ZD_STUDY_TEXT];
    uint32_t interval_days;   /* current scheduling interval */
    uint32_t due_day;         /* day counter when due again */
    uint32_t ease;            /* scaled x100, min 130, max 300 */
    uint32_t reps;
    uint32_t lapses;
};

struct zd_study_session {
    uint32_t reviewed;
    uint32_t again;
    uint32_t hard;
    uint32_t good;
    uint32_t easy;
    uint32_t active;          /* 1 while the session runs */
    uint32_t focus_minutes;   /* configured focus block */
    uint32_t focus_elapsed;   /* minutes consumed */
};

struct zd_study {
    char deck[ZD_STUDY_MAX_DECK_NAME];
    struct zd_study_card cards[ZD_STUDY_MAX_CARDS];
    uint32_t card_count;
    uint32_t day;             /* monotonic day counter */
    struct zd_study_session session;
    struct { uint32_t reviews, lapses, sessions_started, focus_blocks; } stats;
};

int zd_study_init(struct zd_study *s, const char *deck_name);
/* Add card; duplicate front -> -17, full -> -28, bad args -> -22. */
int zd_study_add_card(struct zd_study *s, const char *front,
                      const char *back);
struct zd_study_card *zd_study_find(struct zd_study *s, const char *front);
/* Next due card (due_day <= day), or 0 when none. */
struct zd_study_card *zd_study_next_due(struct zd_study *s);
/* Apply a grade to the given card: updates interval/ease/due using a
 * bounded SM-2-style ladder (0/1/3/7/21/60 days), AGAIN resets the
 * interval and counts a lapse. */
int zd_study_grade(struct zd_study *s, const char *front, int grade);
/* Day advancement (injected by the app; no wall clock here). */
int zd_study_advance_day(struct zd_study *s, uint32_t days);
int zd_study_session_start(struct zd_study *s, uint32_t focus_minutes);
int zd_study_session_tick(struct zd_study *s);   /* +1 minute */
int zd_study_session_end(struct zd_study *s);

/* --- AI study assistant ------------------------------------------
 * Builds a broker request from study context; permission gating is
 * the broker's job (submit rejects ungranted context bits up front). */
/* Build a SUMMARIZE request: context_mask = CONTEXT_SELECTION, payload
 * "study <deck>: <front>" truncated to fit the 64-byte payload.
 * `front` NULL selects the next due card.  -22 bad args, -2 deck
 * empty / front not found. */
int zd_study_assist_request(struct zd_study *s, const char *front,
                            struct zd_ai_request *out);
/* Build + submit to the broker.  -ZD_EPERM when grants lack
 * SELECTION (the broker counts denied_permission); -22 for a NULL
 * broker; other build codes pass through. */
int zd_study_assist_submit(struct zd_study *s, struct zd_ai_broker *b,
                           const char *front);

#endif /* ZEROOS_DESKTOP_STUDY_H */
