/* Navigation controller.  See nav.h. */
#include <zeroos/desktop/nav.h>

static void n_copy(char *d, uint32_t cap, const char *src) {
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

void zd_nav_init(struct zd_nav *n) {
    uint32_t i;
    if (!n)
        return;
    for (i = 0; i < ZD_NAV_HISTORY; ++i)
        n->history[i].url[0] = 0;
    n->count = 0;
    n->pos = 0;
    n->state = ZD_NAV_EMPTY;
    n->last_reject = 0;
    n->stats.navigations = n->stats.blocked = n->stats.load_failures = 0;
    n->stats.backs = n->stats.forwards = n->stats.history_dropped = 0;
}

int zd_nav_go(struct zd_nav *n, const char *url) {
    struct zd_url parsed;
    int r;
    if (!n)
        return -22;
    r = zd_url_parse(url, &parsed);
    if (r < 0) {
        n->last_reject = (int)parsed.reject_reason;
        n->stats.blocked++;
        return -22;
    }
    if (n->count == 0) {
        n_copy(n->history[0].url, ZD_URL_MAX, url);
        n->pos = 0;
        n->count = 1;
    } else {
        /* drop forward entries past pos */
        if (n->count > n->pos + 1)
            n->stats.history_dropped += (n->count - n->pos - 1);
        if (n->pos + 1 >= ZD_NAV_HISTORY) {
            /* shift left by one, oldest falls off the back */
            uint32_t i;
            for (i = 1; i < ZD_NAV_HISTORY; ++i)
                n_copy(n->history[i - 1].url, ZD_URL_MAX,
                       n->history[i].url);
            n->pos--;
            n->count = ZD_NAV_HISTORY;
            n->stats.history_dropped++;
        }
        n_copy(n->history[n->pos + 1].url, ZD_URL_MAX, url);
        n->pos++;
        n->count = n->pos + 1;
    }
    n->state = ZD_NAV_LOADING;
    n->stats.navigations++;
    return 0;
}

int zd_nav_finish(struct zd_nav *n, int err) {
    if (!n)
        return -22;
    if (n->state != ZD_NAV_LOADING)
        return -22;
    if (err < 0) {
        n->state = ZD_NAV_FAILED;
        n->stats.load_failures++;
        return 0;
    }
    n->state = ZD_NAV_COMMITTED;
    return 0;
}

int zd_nav_back(struct zd_nav *n) {
    if (!n)
        return -22;
    if (n->pos == 0 || n->state == ZD_NAV_LOADING)
        return -22;
    n->pos--;
    n->state = ZD_NAV_LOADING;
    n->stats.backs++;
    return 0;
}

int zd_nav_forward(struct zd_nav *n) {
    if (!n)
        return -22;
    if (n->pos + 1 >= n->count || n->state == ZD_NAV_LOADING)
        return -22;
    n->pos++;
    n->state = ZD_NAV_LOADING;
    n->stats.forwards++;
    return 0;
}

const char *zd_nav_current(const struct zd_nav *n) {
    if (!n || n->count == 0 || n->pos >= n->count)
        return "";
    return n->history[n->pos].url;
}
