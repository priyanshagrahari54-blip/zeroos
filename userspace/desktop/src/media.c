/* Media policy core.  See media.h. */
#include <zeroos/desktop/media.h>

static uint32_t m_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}
static int m_streq(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static void m_copy(char *dst, uint32_t cap, const char *src) {
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

void zd_media_init(struct zd_media *m) {
    uint32_t i;
    if (!m)
        return;
    for (i = 0; i < ZD_MEDIA_SOURCES; ++i) {
        m->sources[i].origin[0] = 0;
        m->sources[i].rights = 0;
        m->sources[i].in_use = 0;
        m->sources[i].plays = 0;
        m->sources[i].refusals = 0;
    }
    for (i = 0; i < ZD_MEDIA_ITEMS; ++i) {
        m->items[i].origin[0] = 0;
        m->items[i].title[0] = 0;
        m->items[i].source_idx = 0;
        m->items[i].drm = 0;
        m->items[i].in_use = 0;
    }
    m->source_count = 0;
    m->item_count = 0;
    m->stats.plays = m->stats.refusals_drm = 0;
    m->stats.refusals_rights = m->stats.refusals_origin = 0;
    m->stats.rejected = m->stats.registered = 0;
    m->stats.items_added = 0;
}

static struct zd_media_source *m_find_source(struct zd_media *m,
                                             const char *origin) {
    uint32_t i;
    if (!m || !origin)
        return 0;
    for (i = 0; i < ZD_MEDIA_SOURCES; ++i)
        if (m->sources[i].in_use &&
            m_streq(m->sources[i].origin, origin))
            return &m->sources[i];
    return 0;
}

int zd_media_register_source(struct zd_media *m, const char *origin,
                             uint32_t rights) {
    struct zd_media_source *s;
    uint32_t i;
    if (!m || !origin || !origin[0] || m_len(origin) >= ZD_MEDIA_ORIGIN ||
        (rights & ~ZD_MEDIA_RIGHT_ALL)) {
        if (m)
            m->stats.rejected++;
        return -22;
    }
    s = m_find_source(m, origin);
    if (s) {
        s->rights = rights; /* replace */
        return 0;
    }
    if (m->source_count >= ZD_MEDIA_SOURCES)
        return -28;
    for (i = 0; i < ZD_MEDIA_SOURCES; ++i) {
        if (!m->sources[i].in_use) {
            m_copy(m->sources[i].origin, ZD_MEDIA_ORIGIN, origin);
            m->sources[i].rights = rights;
            m->sources[i].in_use = 1;
            m->sources[i].plays = 0;
            m->sources[i].refusals = 0;
            m->source_count++;
            m->stats.registered++;
            return 0;
        }
    }
    return -28;
}

int zd_media_unregister_source(struct zd_media *m, const char *origin) {
    struct zd_media_source *s;
    if (!m)
        return -22;
    s = m_find_source(m, origin);
    if (!s)
        return -2;
    s->in_use = 0;
    s->origin[0] = 0;
    s->rights = 0;
    m->source_count--;
    return 0;
}

int zd_media_add_item(struct zd_media *m, const char *origin,
                      const char *title, uint32_t drm) {
    uint32_t i;
    if (!m || !origin || !origin[0] || m_len(origin) >= ZD_MEDIA_ORIGIN ||
        !title || !title[0] || m_len(title) >= ZD_MEDIA_TITLE) {
        if (m)
            m->stats.rejected++;
        return -22;
    }
    if (m->item_count >= ZD_MEDIA_ITEMS)
        return -28;
    for (i = 0; i < ZD_MEDIA_ITEMS; ++i) {
        struct zd_media_item *it = &m->items[i];
        if (it->in_use)
            continue;
        m_copy(it->origin, ZD_MEDIA_ORIGIN, origin);
        m_copy(it->title, ZD_MEDIA_TITLE, title);
        it->drm = drm ? 1u : 0u;
        it->source_idx = 0;
        it->in_use = 1;
        m->item_count++;
        m->stats.items_added++;
        return 0; /* id = slot + 1 */
    }
    return -28;
}

static struct zd_media_item *m_item(struct zd_media *m, uint32_t item_id) {
    if (!m || item_id == 0 || item_id > ZD_MEDIA_ITEMS)
        return 0;
    if (!m->items[item_id - 1].in_use)
        return 0;
    return &m->items[item_id - 1];
}

/* shared gate: origin registered? right granted? then (for play)
 * DRM check.  Returns 0 granted or -1 with stats recorded. */
static int m_gate(struct zd_media *m, struct zd_media_item *it,
                  uint32_t right, int is_play) {
    struct zd_media_source *s = m_find_source(m, it->origin);
    if (!s) {
        m->stats.refusals_origin++;
        s = 0;
        return -1;
    }
    if (!(s->rights & right)) {
        m->stats.refusals_rights++;
        s->refusals++;
        return -1;
    }
    if (is_play && it->drm) {
        /* never bypassed: refusal is the only path */
        m->stats.refusals_drm++;
        s->refusals++;
        return -1;
    }
    return 0;
}

int zd_media_play(struct zd_media *m, uint32_t item_id) {
    struct zd_media_item *it = m_item(m, item_id);
    if (!m)
        return -22;
    if (!it)
        return -2;
    if (m_gate(m, it, ZD_MEDIA_RIGHT_PLAY, 1) < 0)
        return -1;
    m->stats.plays++;
    {
        struct zd_media_source *s = m_find_source(m, it->origin);
        if (s)
            s->plays++;
    }
    return 0;
}

int zd_media_request(struct zd_media *m, uint32_t item_id,
                     uint32_t right) {
    struct zd_media_item *it = m_item(m, item_id);
    if (!m)
        return -22;
    if (!it)
        return -2;
    if (right == 0 || (right & (right - 1)) ||
        (right & ~ZD_MEDIA_RIGHT_ALL)) {
        m->stats.rejected++;
        return -22; /* exactly one known right bit */
    }
    if (m_gate(m, it, right, 0) < 0)
        return -1;
    return 0;
}
