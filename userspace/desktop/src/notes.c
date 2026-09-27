/* Study notes.  See notes.h. */
#include <zeroos/desktop/common.h>
#include <zeroos/desktop/notes.h>

static char nt_lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int nt_contains_ci(const char *hay, const char *needle) {
    size_t nl = zd_str_length(needle);
    size_t i;
    if (!nl)
        return 1;
    for (i = 0; hay[i]; ++i) {
        size_t k = 0;
        while (k < nl && hay[i + k] &&
               nt_lower(hay[i + k]) == nt_lower(needle[k]))
            k++;
        if (k == nl)
            return 1;
    }
    return 0;
}

/* Index of the first case-insensitive occurrence, or -1. */
static long nt_find_ci(const char *hay, const char *needle) {
    size_t nl = zd_str_length(needle);
    size_t i;
    if (!nl)
        return 0;
    for (i = 0; hay[i]; ++i) {
        size_t k = 0;
        while (k < nl && hay[i + k] &&
               nt_lower(hay[i + k]) == nt_lower(needle[k]))
            k++;
        if (k == nl)
            return (long)i;
    }
    return -1;
}

static struct zd_note *nt_find(struct zd_notes *n, uint32_t id) {
    uint32_t i;
    for (i = 0; i < n->count; ++i)
        if (n->items[i].id == id)
            return &n->items[i];
    return 0;
}

void zd_notes_init(struct zd_notes *n) {
    if (!n)
        return;
    zd_memset(n, 0, sizeof(*n));
    n->next_id = 1;
}

int zd_notes_create(struct zd_notes *n, const char *title,
                    const char *body, int64_t mtime, uint32_t *out_id) {
    struct zd_note *note;
    if (!n || !title || !body || !out_id)
        return -22;
    if (!title[0] || zd_str_length(title) >= ZD_NOTES_TITLE ||
        zd_str_length(body) >= ZD_NOTES_BODY) {
        n->stats.rejected++;
        return -22;
    }
    if (n->count >= ZD_NOTES_MAX)
        return -28;
    note = &n->items[n->count++];
    zd_memset(note, 0, sizeof(*note));
    note->id = n->next_id++;
    note->mtime = mtime;
    zd_memcpy(note->title, title, zd_str_length(title) + 1);
    zd_memcpy(note->body, body, zd_str_length(body) + 1);
    note->body_len = (uint32_t)zd_str_length(body);
    n->stats.created++;
    *out_id = note->id;
    return 0;
}

int zd_notes_update(struct zd_notes *n, uint32_t id, const char *title,
                    const char *body, int64_t mtime) {
    struct zd_note *note;
    if (!n || !body)
        return -22;
    note = nt_find(n, id);
    if (!note)
        return -2;
    if (zd_str_length(body) >= ZD_NOTES_BODY ||
        (title && (!title[0] || zd_str_length(title) >= ZD_NOTES_TITLE))) {
        n->stats.rejected++;
        return -22;
    }
    if (title)
        zd_memcpy(note->title, title, zd_str_length(title) + 1);
    zd_memcpy(note->body, body, zd_str_length(body) + 1);
    note->body_len = (uint32_t)zd_str_length(body);
    note->mtime = mtime;
    n->stats.updated++;
    return 0;
}

int zd_notes_delete(struct zd_notes *n, uint32_t id) {
    uint32_t i;
    if (!n)
        return -22;
    for (i = 0; i < n->count; ++i) {
        if (n->items[i].id != id)
            continue;
        if (i + 1 < n->count)
            zd_memmove(&n->items[i], &n->items[i + 1],
                    (n->count - i - 1) * sizeof(n->items[0]));
        n->count--;
        n->stats.deleted++;
        return 0;
    }
    return -2;
}

int zd_notes_set_pinned(struct zd_notes *n, uint32_t id, int pinned) {
    struct zd_note *note;
    if (!n)
        return -22;
    note = nt_find(n, id);
    if (!note)
        return -2;
    if (pinned && !note->pinned) {
        note->pinned = 1;
        n->stats.pinned++;
    } else if (!pinned && note->pinned) {
        note->pinned = 0;
        n->stats.unpinned++;
    }
    return 0;
}

const struct zd_note *zd_notes_get(const struct zd_notes *n,
                                   uint32_t id) {
    uint32_t i;
    if (!n)
        return 0;
    for (i = 0; i < n->count; ++i)
        if (n->items[i].id == id)
            return &n->items[i];
    return 0;
}

int zd_notes_search(const struct zd_notes *n, const char *sub,
                    uint32_t *out, uint32_t cap) {
    uint32_t i;
    int total = 0;
    struct zd_notes *mut = (struct zd_notes *)n;
    if (!n || !sub)
        return -22;
    for (i = 0; i < n->count; ++i) {
        int match = !sub[0] ||
                    nt_contains_ci(n->items[i].title, sub) ||
                    nt_contains_ci(n->items[i].body, sub);
        if (!match)
            continue;
        if (out && total < (int)cap)
            out[total] = n->items[i].id;
        total++;
    }
    mut->stats.searches++;
    if (total > 0)
        mut->stats.search_hits++;
    /* with an output buffer the return is bounded by cap; without it
     * (out==NULL) it is the full match count — both documented. */
    if (out && total > (int)cap)
        return (int)cap;
    return total;
}

int zd_notes_snippet(const struct zd_note *note, const char *needle,
                     char *out, uint32_t cap) {
    const char *field;
    long at;
    uint32_t n = 0;
    if (!note || !needle || !out || cap == 0)
        return -22;
    out[0] = 0;
    field = note->body;
    at = nt_find_ci(note->body, needle);
    if (at < 0) {
        field = note->title;
        at = nt_find_ci(note->title, needle);
    }
    if (at < 0)
        return -2;
    /* The window starts at the match so the caller can rely on the needle
     * being present in the copy, even after truncation. */
    while (field[at + n] && n + 1 < cap) {
        out[n] = field[at + n];
        ++n;
    }
    out[n] = 0;
    return (int)n;
}
