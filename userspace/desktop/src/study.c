/* Study Center core.  See study.h. */
#include <zeroos/desktop/study.h>

static uint32_t s_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}
static void s_copy(char *d, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!d || !cap)
        return;
    if (!src) {
        d[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap) {
        d[i] = src[i];
        ++i;
    }
    d[i] = 0;
}
static int s_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

int zd_study_init(struct zd_study *s, const char *deck_name) {
    if (!s || !deck_name || !deck_name[0] ||
        s_len(deck_name) >= ZD_STUDY_MAX_DECK_NAME)
        return -22;
    s_copy(s->deck, sizeof(s->deck), deck_name);
    s->card_count = 0;
    s->day = 0;
    s->session.reviewed = s->session.again = s->session.hard = 0;
    s->session.good = s->session.easy = s->session.active = 0;
    s->session.focus_minutes = s->session.focus_elapsed = 0;
    s->stats.reviews = s->stats.lapses = 0;
    s->stats.sessions_started = s->stats.focus_blocks = 0;
    return 0;
}

int zd_study_add_card(struct zd_study *s, const char *front,
                      const char *back) {
    struct zd_study_card *c;
    if (!s || !front || !front[0] || !back || !back[0])
        return -22;
    if (s_len(front) >= ZD_STUDY_TEXT || s_len(back) >= ZD_STUDY_TEXT)
        return -22;
    if (zd_study_find(s, front))
        return -17; /* EEXIST */
    if (s->card_count >= ZD_STUDY_MAX_CARDS)
        return -28; /* ENOSPC */
    c = &s->cards[s->card_count++];
    s_copy(c->front, sizeof(c->front), front);
    s_copy(c->back, sizeof(c->back), back);
    c->interval_days = 0;
    c->due_day = 0;   /* due immediately */
    c->ease = 250;
    c->reps = 0;
    c->lapses = 0;
    return 0;
}

struct zd_study_card *zd_study_find(struct zd_study *s, const char *front) {
    uint32_t i;
    if (!s || !front)
        return 0;
    for (i = 0; i < s->card_count; ++i)
        if (s_eq(s->cards[i].front, front))
            return &s->cards[i];
    return 0;
}

struct zd_study_card *zd_study_next_due(struct zd_study *s) {
    struct zd_study_card *best = 0;
    uint32_t i;
    if (!s)
        return 0;
    for (i = 0; i < s->card_count; ++i) {
        struct zd_study_card *c = &s->cards[i];
        if (c->due_day > s->day)
            continue;
        if (!best || c->due_day < best->due_day)
            best = c; /* most overdue first */
    }
    return best;
}

static uint32_t s_next_interval(const struct zd_study_card *c, int grade) {
    static const uint32_t ladder[] = {0, 1, 3, 7, 21, 60};
    const uint32_t n = 6;
    uint32_t step, i;
    /* intervals are always ladder values; locate the exact rung */
    step = n - 1;
    for (i = 0; i < n; ++i) {
        if (c->interval_days == ladder[i]) {
            step = i;
            break;
        }
        if (c->interval_days < ladder[i]) {
            step = (i > 0) ? i - 1 : 0;
            break;
        }
    }
    if (grade == ZD_STUDY_AGAIN)
        return 1; /* relearn tomorrow */
    if (grade == ZD_STUDY_HARD)
        return ladder[step]; /* stay */
    if (grade == ZD_STUDY_EASY)
        step = (step + 2 < n) ? step + 2 : n - 1;
    else /* GOOD */
        step = (step + 1 < n) ? step + 1 : n - 1;
    return ladder[step];
}

int zd_study_grade(struct zd_study *s, const char *front, int grade) {
    struct zd_study_card *c;
    if (!s)
        return -22;
    if (grade < ZD_STUDY_AGAIN || grade > ZD_STUDY_EASY)
        return -22;
    c = zd_study_find(s, front);
    if (!c)
        return -2;
    c->reps++;
    s->stats.reviews++;
    if (s->session.active) {
        s->session.reviewed++;
        if (grade == ZD_STUDY_AGAIN)
            s->session.again++;
        else if (grade == ZD_STUDY_HARD)
            s->session.hard++;
        else if (grade == ZD_STUDY_GOOD)
            s->session.good++;
        else
            s->session.easy++;
    }
    switch (grade) {
    case ZD_STUDY_AGAIN:
        if (c->interval_days > 0)
            c->lapses++;
        s->stats.lapses += (c->interval_days > 0) ? 1 : 0;
        c->interval_days = 1;
        if (c->ease > 130)
            c->ease -= 20;
        break;
    case ZD_STUDY_HARD:
        if (c->ease > 130)
            c->ease -= 15;
        c->interval_days = s_next_interval(c, grade);
        break;
    case ZD_STUDY_EASY:
        if (c->ease < 300)
            c->ease += 15;
        c->interval_days = s_next_interval(c, grade);
        break;
    default: /* GOOD */
        c->interval_days = s_next_interval(c, grade);
        break;
    }
    if (c->interval_days > 60)
        c->interval_days = 60; /* bounded ladder */
    c->due_day = s->day + c->interval_days;
    return 0;
}

int zd_study_advance_day(struct zd_study *s, uint32_t days) {
    if (!s || days == 0)
        return -22;
    s->day += days;
    return 0;
}

int zd_study_session_start(struct zd_study *s, uint32_t focus_minutes) {
    if (!s || focus_minutes == 0 || focus_minutes > 240)
        return -22;
    if (s->session.active)
        return -16;
    s->session.active = 1;
    s->session.focus_minutes = focus_minutes;
    s->session.focus_elapsed = 0;
    s->session.reviewed = s->session.again = s->session.hard = 0;
    s->session.good = s->session.easy = 0;
    s->stats.sessions_started++;
    return 0;
}

int zd_study_session_tick(struct zd_study *s) {
    if (!s || !s->session.active)
        return -22;
    if (s->session.focus_elapsed < s->session.focus_minutes) {
        s->session.focus_elapsed++;
        if (s->session.focus_elapsed == s->session.focus_minutes)
            s->stats.focus_blocks++; /* counted once per block */
    }
    return 0;
}

int zd_study_session_end(struct zd_study *s) {
    if (!s || !s->session.active)
        return -22;
    s->session.active = 0;
    s->session.focus_minutes = 0;
    s->session.focus_elapsed = 0;
    return 0;
}
