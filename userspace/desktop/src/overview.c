/* Shell overview model.  See overview.h. */
#include <zeroos/desktop/overview.h>

static uint32_t ov_isqrt(uint32_t v) {
    uint32_t r = 0;
    while ((r + 1) * (r + 1) <= v)
        ++r;
    return r;
}

static int ov_layout(struct zd_overview *o) {
    uint32_t n = o->count;
    uint32_t cols, rows, i;
    uint32_t cw, ch;
    uint32_t gap = o->gap;
    if (!n) {
        o->cols = o->rows = 0;
        return 0;
    }
    cols = ov_isqrt(n);
    if (cols * cols < n)
        cols++; /* ceil(sqrt) */
    if (cols == 0)
        cols = 1;
    rows = (n + cols - 1) / cols;
    if ((uint32_t)o->area.w <= 2 * gap + cols ||
        (uint32_t)o->area.h <= 2 * gap + rows)
        return -22; /* degenerate area for this grid */
    cw = ((uint32_t)o->area.w - gap * (cols + 1)) / cols;
    ch = ((uint32_t)o->area.h - gap * (rows + 1)) / rows;
    if (cw == 0 || ch == 0)
        return -22;
    for (i = 0; i < n; ++i) {
        uint32_t c = i % cols;
        uint32_t r = i / cols;
        o->items[i].thumb.x = o->area.x + (int32_t)(gap + c * (cw + gap));
        o->items[i].thumb.y = o->area.y + (int32_t)(gap + r * (ch + gap));
        o->items[i].thumb.w = (int32_t)cw;
        o->items[i].thumb.h = (int32_t)ch;
    }
    o->cols = cols;
    o->rows = rows;
    o->stats.relayouts++;
    return 0;
}

int zd_overview_open(struct zd_overview *o, struct zd_rect area,
                     uint32_t gap) {
    if (!o)
        return -22;
    if (area.w <= 0 || area.h <= 0) {
        o->stats.rejected++;
        return -22;
    }
    o->area = area;
    o->gap = gap;
    o->count = 0;
    o->focus = 0;
    o->cols = o->rows = 0;
    o->stats.opens++;
    return 0;
}

void zd_overview_close(struct zd_overview *o) {
    if (!o)
        return;
    o->count = 0;
    o->focus = 0;
    o->cols = o->rows = 0;
}

int zd_overview_set_windows(struct zd_overview *o, const uint32_t *ids,
                            uint32_t n) {
    uint32_t i;
    if (!o || (!ids && n))
        return -22;
    if (n > ZD_OVERVIEW_MAX) {
        o->stats.rejected++;
        return -28;
    }
    for (i = 0; i < n; ++i)
        o->items[i].win_id = ids[i];
    o->count = n;
    if (o->focus >= n)
        o->focus = n ? n - 1 : 0;
    return ov_layout(o);
}

int zd_overview_hit(const struct zd_overview *o, int32_t x,
                    int32_t y) {
    uint32_t i;
    if (!o || o->count == 0)
        return -22;
    for (i = 0; i < o->count; ++i) {
        const struct zd_rect *r = &o->items[i].thumb;
        if (x >= r->x && x < r->x + r->w && y >= r->y &&
            y < r->y + r->h)
            return (int)i;
    }
    return -2;
}

int zd_overview_focus_next(struct zd_overview *o) {
    if (!o || o->count == 0)
        return -22;
    o->focus = (o->focus + 1) % o->count;
    return 0;
}

int zd_overview_focus_prev(struct zd_overview *o) {
    if (!o || o->count == 0)
        return -22;
    o->focus = (o->focus + o->count - 1) % o->count;
    return 0;
}

uint32_t zd_overview_focused_id(const struct zd_overview *o) {
    if (!o || o->count == 0 || o->focus >= o->count)
        return 0;
    return o->items[o->focus].win_id;
}

int zd_overview_remove(struct zd_overview *o, uint32_t win_id) {
    uint32_t i;
    if (!o)
        return -22;
    for (i = 0; i < o->count; ++i) {
        if (o->items[i].win_id != win_id)
            continue;
        {
            uint32_t k;
            for (k = i; k + 1 < o->count; ++k)
                o->items[k] = o->items[k + 1];
        }
        o->count--;
        o->stats.closes++;
        if (o->focus >= o->count && o->focus)
            o->focus = o->count ? o->count - 1 : 0;
        return ov_layout(o);
    }
    return -2;
}
