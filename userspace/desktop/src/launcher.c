/* ZEROOS launcher core.  See launcher.h for the contract. */
#include <zeroos/desktop/launcher.h>
#include <zeroos/desktop/capability.h>

static int lc_bad(void) { return -22; }

static int lc_len(const char *s) {
    int n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}

static void lc_lower(char *s) {
    int i;
    for (i = 0; s[i]; ++i)
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] = (char)(s[i] - 'A' + 'a');
}

static void lc_copy(char *d, int cap, const char *s) {
    int i = 0;
    if (!d || cap <= 0)
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

/* case-insensitive substring: does `hay` contain `needle`? */
static int lc_has(const char *hay, const char *needle) {
    int n = lc_len(needle), h = lc_len(hay), i, j;
    if (n == 0)
        return 1;
    if (h < n)
        return 0;
    for (i = 0; i <= h - n; ++i) {
        for (j = 0; j < n; ++j) {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b)
                break;
        }
        if (j == n)
            return 1;
    }
    return 0;
}

static int lc_starts(const char *s, const char *p) {
    int i;
    for (i = 0; p[i]; ++i) {
        char a = s[i], b = p[i];
        if (!a)
            return 0;
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

int zd_launcher_init(struct zd_launcher *l, zd_launch_fn launch, void *ctx) {
    int i;
    if (!l)
        return lc_bad();
    l->caps = 0;
    l->app_count = 0;
    l->recents_counter = 0;
    l->launch = launch;
    l->launch_ctx = ctx;
    l->stats.adds = l->stats.launches = l->stats.launch_rejected = 0;
    l->stats.launch_failures = l->stats.queries = l->stats.empty_queries = 0;
    l->stats.cap_denied = 0;
    for (i = 0; i < ZD_LAUNCHER_MAX_APPS; ++i)
        l->apps[i].name[0] = 0;
    return 0;
}

int zd_launcher_add(struct zd_launcher *l, const char *name,
                    const char *keywords, int visible) {
    struct zd_app *a;
    if (!l || !name || !name[0] || lc_len(name) >= ZD_LAUNCHER_NAME)
        return lc_bad();
    if (zd_launcher_find(l, name))
        return -17; /* EEXIST */
    if (l->app_count >= ZD_LAUNCHER_MAX_APPS)
        return -28; /* ENOSPC */
    a = &l->apps[l->app_count++];
    lc_copy(a->name, ZD_LAUNCHER_NAME, name);
    lc_copy(a->keywords, ZD_LAUNCHER_KEYWORDS, keywords);
    lc_lower(a->keywords);
    a->visible = visible ? 1 : 0;
    a->state = ZD_LAUNCH_IDLE;
    a->fail_errno = 0;
    a->in_recents = 0;
    l->stats.adds++;
    return 0;
}

void zd_launcher_set_caps(struct zd_launcher *l, struct zd_caps *caps) {
    if (l)
        l->caps = caps;
}

struct zd_app *zd_launcher_find(struct zd_launcher *l, const char *name) {
    int i;
    if (!l || !name)
        return 0;
    for (i = 0; i < l->app_count; ++i) {
        const char *a = l->apps[i].name, *b = name;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == 0 && *b == 0)
            return &l->apps[i];
    }
    return 0;
}

int zd_launcher_query(struct zd_launcher *l, const char *query,
                      struct zd_app **out, int max_out) {
    int i, n = 0;
    char q[ZD_LAUNCHER_QUERY];
    uint8_t picked[ZD_LAUNCHER_MAX_APPS];
    if (!l || !out || max_out <= 0)
        return lc_bad();
    l->stats.queries++;
    for (i = 0; i < ZD_LAUNCHER_MAX_APPS; ++i)
        picked[i] = 0;
    lc_copy(q, sizeof(q), query);
    lc_lower(q);
    if (!q[0]) {
        /* recents first (highest counter), visible only */
        int guard;
        l->stats.empty_queries++;
        for (guard = 0; guard < l->app_count && n < max_out; ++guard) {
            struct zd_app *best = 0;
            int best_i = -1;
            for (i = 0; i < l->app_count; ++i) {
                struct zd_app *a = &l->apps[i];
                if (!a->visible || !a->in_recents || picked[i])
                    continue;
                if (!best || a->in_recents > best->in_recents) {
                    best = a;
                    best_i = i;
                }
            }
            if (!best)
                break;
            picked[best_i] = 1;
            out[n++] = best;
        }
        return n;
    }
    /* matches, best rank first: prefix outranks substring, recents
     * (higher counter) break ties */
    for (;;) {
        struct zd_app *best = 0;
        int best_i = -1, best_rank = -1;
        for (i = 0; i < l->app_count; ++i) {
            struct zd_app *a = &l->apps[i];
            int rank;
            if (!a->visible || picked[i])
                continue;
            if (!lc_has(a->name, q) && !lc_has(a->keywords, q))
                continue;
            rank = (lc_starts(a->name, q) ? 1000 : 0) + a->in_recents;
            if (rank > best_rank) {
                best_rank = rank;
                best = a;
                best_i = i;
            }
        }
        if (!best || n >= max_out)
            break;
        picked[best_i] = 1;
        out[n++] = best;
    }
    return n;
}

int zd_launcher_launch(struct zd_launcher *l, const char *name) {
    struct zd_app *a;
    int r;
    if (!l)
        return lc_bad();
    a = zd_launcher_find(l, name);
    if (!a)
        return -2; /* ENOENT */
    if (l->caps &&
        zd_caps_check(l->caps, ZD_SVC_LAUNCHER, ZD_CAP_LAUNCH_APPS) < 0) {
        l->stats.cap_denied++;
        return -1; /* EPERM: launcher lacks the spawn capability */
    }
    if (a->state == ZD_LAUNCH_RUNNING || a->state == ZD_LAUNCH_PENDING) {
        l->stats.launch_rejected++;
        return -16; /* EBUSY */
    }
    if (!l->launch)
        return -38; /* ENOSYS: no shell hook wired */
    r = l->launch(l->launch_ctx, name);
    if (r < 0) {
        a->state = ZD_LAUNCH_FAILED;
        a->fail_errno = -r;
        l->stats.launch_failures++;
        return r;
    }
    a->state = ZD_LAUNCH_PENDING;
    a->fail_errno = 0;
    l->stats.launches++;
    return 0;
}

void zd_launcher_report(struct zd_launcher *l, const char *name,
                        int running, int fail_errno) {
    struct zd_app *a;
    if (!l)
        return;
    a = zd_launcher_find(l, name);
    if (!a)
        return;
    if (running) {
        a->state = ZD_LAUNCH_RUNNING;
        a->fail_errno = 0;
        a->in_recents = ++l->recents_counter;
    } else if (fail_errno) {
        a->state = ZD_LAUNCH_FAILED;
        a->fail_errno = fail_errno;
    } else {
        a->state = ZD_LAUNCH_IDLE; /* clean exit */
        a->fail_errno = 0;
    }
}
