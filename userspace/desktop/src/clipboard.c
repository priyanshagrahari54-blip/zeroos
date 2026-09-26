/* Clipboard service.  See clipboard.h. */
#include <zeroos/desktop/clipboard.h>

static void c_copy(char *d, uint32_t cap, const char *s, uint32_t n) {
    uint32_t i = 0;
    if (!d || !cap)
        return;
    while (i < n && i + 1 < cap) {
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
}
static uint32_t c_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}

void zd_clipboard_init(struct zd_clipboard *cb) {
    uint32_t i;
    if (!cb)
        return;
    for (i = 0; i < ZD_CLIP_MAX; ++i) {
        cb->slots[i].text[0] = 0;
        cb->slots[i].app[0] = 0;
        cb->slots[i].format = 0;
        cb->slots[i].seq = 0;
        cb->slots[i].sensitive = 0;
        cb->slots[i].in_use = 0;
    }
    cb->current = 0;
    cb->next_seq = 1;
    cb->stats.copies = cb->stats.pastes = 0;
    cb->stats.sensitive_kept = cb->stats.rejected = 0;
    cb->stats.cycles = 0;
}

int zd_clipboard_copy(struct zd_clipboard *cb, const char *app,
                      const char *text, uint32_t format, int sensitive) {
    uint32_t tlen, i;
    struct zd_clip *slot = 0;

    if (!cb || !text || format > ZD_CLIP_FMT_URI) {
        if (cb)
            cb->stats.rejected++;
        return -22;
    }
    tlen = c_len(text);
    if (tlen == 0 || tlen >= ZD_CLIP_TEXT) {
        cb->stats.rejected++;
        return -22;
    }
    if (app && app[0] && c_len(app) >= ZD_CLIP_APP) {
        cb->stats.rejected++;
        return -22;
    }
    if (sensitive) {
        /* reserved transient slot: current only, never in history */
        slot = &cb->slots[ZD_CLIP_MAX - 1];
        cb->stats.sensitive_kept++;
    } else {
        for (i = 0; i < ZD_CLIP_MAX - 1; ++i) {
            if (!cb->slots[i].in_use) {
                slot = &cb->slots[i];
                break;
            }
        }
        if (!slot) {
            uint32_t oldest = 0;
            for (i = 1; i < ZD_CLIP_MAX - 1; ++i)
                if (cb->slots[i].seq < cb->slots[oldest].seq)
                    oldest = i;
            slot = &cb->slots[oldest];
        }
    }
    slot->in_use = 1;
    slot->sensitive = sensitive ? 1 : 0;
    slot->seq = cb->next_seq++;
    slot->format = format;
    c_copy(slot->text, ZD_CLIP_TEXT, text, tlen);
    if (app && app[0])
        c_copy(slot->app, ZD_CLIP_APP, app, c_len(app));
    else
        slot->app[0] = 0;
    cb->current = slot;
    cb->stats.copies++;
    return 0;
}

int zd_clipboard_paste(struct zd_clipboard *cb, char *out,
                       uint32_t out_cap) {
    if (!cb || !out || out_cap == 0) {
        if (cb)
            cb->stats.rejected++;
        return -22;
    }
    if (!cb->current) {
        out[0] = 0;
        return -1; /* empty clipboard */
    }
    c_copy(out, out_cap, cb->current->text, c_len(cb->current->text));
    cb->stats.pastes++;
    return 0;
}

int zd_clipboard_cycle(struct zd_clipboard *cb, uint32_t depth,
                       char *out, uint32_t out_cap) {
    uint8_t visited[ZD_CLIP_MAX - 1];
    uint32_t i, j;
    if (!cb || !out || out_cap == 0) {
        if (cb)
            cb->stats.rejected++;
        return -22;
    }
    out[0] = 0;
    if (depth == 0) {
        if (!cb->current)
            return -1;
        c_copy(out, out_cap, cb->current->text, c_len(cb->current->text));
        cb->stats.pastes++;
        cb->stats.cycles++;
        return 0;
    }
    for (i = 0; i < ZD_CLIP_MAX - 1; ++i)
        visited[i] = 0;
    /* the current clipping counts as depth 0 — skip it here when it
     * lives in the history (so depth 1 = the next distinct entry) */
    if (cb->current && cb->current->in_use && !cb->current->sensitive) {
        for (i = 0; i < ZD_CLIP_MAX - 1; ++i)
            if (&cb->slots[i] == cb->current)
                visited[i] = 1;
    }
    /* depth-th newest non-sensitive entry (selection without state) */
    for (i = 0; i < depth; ++i) {
        struct zd_clip *best = 0;
        uint32_t best_j = 0;
        for (j = 0; j < ZD_CLIP_MAX - 1; ++j) {
            struct zd_clip *c = &cb->slots[j];
            if (!c->in_use || c->sensitive || visited[j])
                continue;
            if (!best || c->seq > best->seq) {
                best = c;
                best_j = j;
            }
        }
        if (!best)
            return -1;
        if (i == depth - 1) {
            c_copy(out, out_cap, best->text, c_len(best->text));
            cb->stats.pastes++;
            cb->stats.cycles++;
            return 0;
        }
        visited[best_j] = 1;
    }
    return -1;
}

void zd_clipboard_clear(struct zd_clipboard *cb) {
    uint32_t i;
    if (!cb)
        return;
    for (i = 0; i < ZD_CLIP_MAX; ++i) {
        uint32_t j;
        for (j = 0; j < ZD_CLIP_TEXT; ++j)
            cb->slots[i].text[j] = 0;
        cb->slots[i].in_use = 0;
        cb->slots[i].sensitive = 0;
        cb->slots[i].seq = 0;
    }
    cb->current = 0;
}

uint32_t zd_clipboard_history_count(const struct zd_clipboard *cb) {
    uint32_t i, n = 0;
    if (!cb)
        return 0;
    for (i = 0; i < ZD_CLIP_MAX - 1; ++i)
        if (cb->slots[i].in_use && !cb->slots[i].sensitive)
            ++n;
    return n;
}
