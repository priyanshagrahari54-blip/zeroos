/* Gaming core.  See gaming.h. */
#include <zeroos/desktop/gaming.h>

#define ZD_GAME_PRESSURE_FLOOR 80
#define ZD_GAME_TARGET_FLOOR 30u

static uint32_t g_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}
static int g_eq(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static void g_copy(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!dst || !cap)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

void zd_gaming_init(struct zd_gaming *g) {
    uint32_t i;
    if (!g)
        return;
    for (i = 0; i < ZD_GAME_PROFILES; ++i) {
        g->profiles[i].app[0] = 0;
        g->profiles[i].in_use = 0;
        g->profiles[i].mode = 0;
        g->profiles[i].target_fps = 0;
        g->profiles[i].flags = 0;
        g->profiles[i].overlay_frames = 0;
        {
            uint32_t b;
            for (b = 0; b < ZD_GAME_BTNS; ++b)
                g->profiles[i].btn_map[b] = 0xFF; /* 0xFF = unmapped */
        }
    }
    g->count = 0;
    g->stats.sets = g->stats.remaps = g->stats.yields = 0;
    g->stats.rejected = g->stats.unknown_buttons = 0;
}

struct zd_game_profile *zd_gaming_profile(struct zd_gaming *g,
                                          const char *app) {
    uint32_t i;
    if (!g || !app)
        return 0;
    for (i = 0; i < ZD_GAME_PROFILES; ++i)
        if (g->profiles[i].in_use && g_eq(g->profiles[i].app, app))
            return &g->profiles[i];
    return 0;
}

int zd_gaming_profile_set(struct zd_gaming *g, const char *app,
                          uint32_t mode, uint32_t target_fps,
                          uint32_t flags) {
    struct zd_game_profile *p;
    uint32_t i;
    if (!g || !app || !app[0] || g_len(app) >= ZD_GAME_APP ||
        mode > ZD_GAME_PERF || target_fps == 0 || target_fps > 240 ||
        (flags & ~(ZD_GAME_FLAG_OVERLAY | ZD_GAME_FLAG_LOW_LATENCY))) {
        if (g)
            g->stats.rejected++;
        return -22;
    }
    p = zd_gaming_profile(g, app);
    if (p) {
        p->mode = (uint8_t)mode;
        p->target_fps = (uint16_t)target_fps;
        p->flags = flags;
        g->stats.sets++;
        return 0;
    }
    if (g->count >= ZD_GAME_PROFILES)
        return -28;
    for (i = 0; i < ZD_GAME_PROFILES; ++i) {
        if (g->profiles[i].in_use)
            continue;
        p = &g->profiles[i];
        g_copy(p->app, ZD_GAME_APP, app);
        p->in_use = 1;
        p->mode = (uint8_t)mode;
        p->target_fps = (uint16_t)target_fps;
        p->flags = flags;
        p->overlay_frames = 0;
        {
            uint32_t b;
            for (b = 0; b < ZD_GAME_BTNS; ++b)
                p->btn_map[b] = 0xFF;
        }
        g->count++;
        g->stats.sets++;
        return 0;
    }
    return -28;
}

int zd_gaming_profile_remove(struct zd_gaming *g, const char *app) {
    struct zd_game_profile *p;
    if (!g)
        return -22;
    p = zd_gaming_profile(g, app);
    if (!p)
        return -2;
    p->in_use = 0;
    p->app[0] = 0;
    g->count--;
    return 0;
}

int zd_gaming_remap(struct zd_gaming *g, const char *app,
                    uint32_t physical, uint32_t action) {
    struct zd_game_profile *p;
    if (!g)
        return -22;
    p = zd_gaming_profile(g, app);
    if (!p)
        return -2;
    if (physical >= ZD_GAME_BTNS || action >= ZD_GAME_ACTIONS) {
        g->stats.rejected++;
        return -22;
    }
    p->btn_map[physical] = (uint8_t)action;
    g->stats.remaps++;
    return 0;
}

int zd_gaming_lookup(struct zd_gaming *g, const char *app,
                     uint32_t physical) {
    struct zd_game_profile *p;
    if (!g)
        return -22;
    p = zd_gaming_profile(g, app);
    if (!p)
        return -2;
    if (physical >= ZD_GAME_BTNS) {
        g->stats.unknown_buttons++;
        return -2;
    }
    if (p->btn_map[physical] == 0xFF) {
        g->stats.unknown_buttons++;
        return -2;
    }
    return p->btn_map[physical];
}

int zd_gaming_cooperative(struct zd_gaming *g, const char *app,
                          uint32_t fps_milli, uint32_t pressure) {
    struct zd_game_profile *p;
    int yield = ZD_GAME_YIELD_NONE;
    if (!g)
        return -22;
    p = zd_gaming_profile(g, app);
    if (!p)
        return -2;
    if (pressure > 100)
        pressure = 100;
    {
        int high = pressure >= ZD_GAME_PRESSURE_FLOOR;
        int slow = fps_milli != 0 &&
                   fps_milli < ZD_GAME_TARGET_FLOOR * 1000u;
        int overlay = (p->flags & ZD_GAME_FLAG_OVERLAY) != 0;
        if (high && slow)
            yield = ZD_GAME_YIELD_DEGRADE;
        else if (overlay && (high || slow))
            yield = ZD_GAME_YIELD_OVERLAY_OFF;
        else if (high)
            yield = ZD_GAME_YIELD_TARGET_FLOOR;
    }
    if (yield != ZD_GAME_YIELD_NONE)
        g->stats.yields++;
    return yield;
}
