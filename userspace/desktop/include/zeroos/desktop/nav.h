/* Browser navigation controller (Stage 5 part E): URL validation via
 * the strict parser, bounded back/forward history, explicit load
 * failure state.  No rendering — the engine binds on top. */
#ifndef ZEROOS_DESKTOP_NAV_H
#define ZEROOS_DESKTOP_NAV_H

#include <stdint.h>
#include <zeroos/desktop/url.h>

#define ZD_NAV_HISTORY 16

struct zd_nav_entry {
    char url[ZD_URL_MAX];
};

struct zd_nav {
    struct zd_nav_entry history[ZD_NAV_HISTORY];
    uint32_t count;          /* entries in history */
    uint32_t pos;            /* current entry index */
    int state;               /* enum zd_nav_state */
    int last_reject;         /* zd_url_reject of last failed go() */
    struct {
        uint32_t navigations, blocked, load_failures, backs, forwards,
                 history_dropped;
    } stats;
};

enum zd_nav_state {
    ZD_NAV_EMPTY = 0,
    ZD_NAV_LOADING,
    ZD_NAV_COMMITTED,
    ZD_NAV_FAILED
};

void zd_nav_init(struct zd_nav *n);
/* Validate + push.  Blocked scheme/malformed -> -22 with
 * last_reject set (stats.blocked++), state unchanged.  Success ->
 * LOADING with the entry current; old forward entries are dropped
 * (counted). */
int zd_nav_go(struct zd_nav *n, const char *url);
/* Engine reports: 0 -> COMMITTED; err -> FAILED (history keeps the
 * entry so retry is possible). */
int zd_nav_finish(struct zd_nav *n, int err);
int zd_nav_back(struct zd_nav *n);   /* -22 when no back */
int zd_nav_forward(struct zd_nav *n);
const char *zd_nav_current(const struct zd_nav *n); /* "" if empty */

#endif /* ZEROOS_DESKTOP_NAV_H */
