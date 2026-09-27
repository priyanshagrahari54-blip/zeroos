/* File manager core.  See filemgr.h. */
#include <zeroos/desktop/filemgr.h>

static uint32_t f_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}
static void f_copy(char *dst, uint32_t cap, const char *src) {
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
static int f_streq(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
/* absolute path, bounded, no ".." traversal */
static int f_path_ok(const char *p) {
    uint32_t i, n;
    if (!p || p[0] != '/')
        return 0;
    n = f_len(p);
    if (n == 0 || n >= ZD_FM_PATH)
        return 0;
    for (i = 0; i + 1 < n; ++i)
        if (p[i] == '.' && p[i + 1] == '.')
            return 0;
    if (n >= 2 && p[n - 1] == '.' && p[n - 2] == '.')
        return 0;
    return 1;
}
static int f_name_ok(const char *name) {
    uint32_t i, n;
    if (!name || !name[0])
        return 0;
    n = f_len(name);
    if (n >= ZD_FM_NAME)
        return 0;
    if (f_streq(name, ".") || f_streq(name, ".."))
        return 0;
    for (i = 0; i < n; ++i)
        if (name[i] == '/')
            return 0;
    return 1;
}

static void f_clear_sel(struct zd_fm *fm) {
    uint32_t i;
    for (i = 0; i < sizeof(fm->selected) / sizeof(fm->selected[0]);
         ++i)
        fm->selected[i] = 0;
}

void zd_fm_init(struct zd_fm *fm, zd_fm_source_fn source, void *ctx) {
    uint32_t i;
    if (!fm)
        return;
    for (i = 0; i < ZD_FM_MAX; ++i) {
        fm->entries[i].name[0] = 0;
        fm->entries[i].size = 0;
        fm->entries[i].mtime = 0;
        fm->entries[i].flags = 0;
    }
    for (i = 0; i < ZD_FM_HISTORY; ++i)
        fm->history[i][0] = 0;
    fm->path[0] = 0;
    fm->count = 0;
    fm->truncated = 0;
    f_clear_sel(fm);
    fm->sort_mode = ZD_FM_SORT_NAME;
    fm->sort_desc = 0;
    fm->show_hidden = 0;
    fm->hist_count = 0;
    fm->hist_pos = 0;
    fm->hist_state = 0;
    fm->source = source;
    fm->source_ctx = ctx;
    fm->ops.remove = 0;
    fm->ops.mkdir = 0;
    fm->ops.ctx = 0;
    fm->stats.refreshes = fm->stats.source_errors = 0;
    fm->stats.truncations = fm->stats.rejected = 0;
    fm->stats.navigations = fm->stats.backs = fm->stats.forwards = 0;
    fm->stats.history_dropped = fm->stats.selects = 0;
    fm->stats.ops_perm_denied = fm->stats.removed = 0;
    fm->stats.mkdirs = fm->stats.op_errors = 0;
}

/* ---- sort (stable insertion over the raw array) ---- */

static int f_cmp(const struct zd_fm_entry *a,
                 const struct zd_fm_entry *b, uint32_t mode) {
    if (mode == ZD_FM_SORT_SIZE) {
        if (a->size < b->size)
            return -1;
        if (a->size > b->size)
            return 1;
        return 0;
    }
    if (mode == ZD_FM_SORT_MTIME) {
        if (a->mtime < b->mtime)
            return -1;
        if (a->mtime > b->mtime)
            return 1;
        return 0;
    }
    /* name: byte order, case-sensitive (documented) */
    {
        const unsigned char *x = (const unsigned char *)a->name;
        const unsigned char *y = (const unsigned char *)b->name;
        while (*x && *y && *x == *y) {
            ++x;
            ++y;
        }
        if (*x < *y)
            return -1;
        if (*x > *y)
            return 1;
        return 0;
    }
}

static void f_sort(struct zd_fm *fm) {
    uint32_t i, j;
    for (i = 1; i < fm->count; ++i) {
        struct zd_fm_entry key = fm->entries[i];
        j = i;
        while (j > 0) {
            int c = f_cmp(&fm->entries[j - 1], &key, fm->sort_mode);
            if (fm->sort_desc)
                c = -c;
            if (c <= 0)
                break;
            fm->entries[j] = fm->entries[j - 1];
            --j;
        }
        fm->entries[j] = key;
    }
}

static int f_load(struct zd_fm *fm) {
    uint32_t n = 0;
    int r;
    if (!fm->source)
        return -22;
    fm->hist_state = 1; /* loading */
    r = fm->source(fm->source_ctx, fm->path, fm->entries, ZD_FM_MAX,
                   &n);
    if (r < 0) {
        fm->stats.source_errors++;
        fm->hist_state = 3; /* failed */
        fm->count = 0;
        return r;
    }
    if (n > ZD_FM_MAX)
        n = ZD_FM_MAX;
    fm->count = n;
    fm->truncated = (n == ZD_FM_MAX) ? 1u : 0u;
    if (fm->truncated)
        fm->stats.truncations++;
    f_clear_sel(fm);
    f_sort(fm);
    fm->stats.refreshes++;
    fm->hist_state = 2;
    return 0;
}

int zd_fm_open(struct zd_fm *fm, const char *path) {
    if (!fm || !f_path_ok(path)) {
        if (fm)
            fm->stats.rejected++;
        return -22;
    }
    f_copy(fm->path, ZD_FM_PATH, path);
    /* push history; drop forward entries when branching */
    if (fm->hist_count &&
        fm->hist_pos + 1 < fm->hist_count) {
        fm->stats.history_dropped += fm->hist_count - fm->hist_pos - 1;
        fm->hist_count = fm->hist_pos + 1;
    }
    if (fm->hist_count == ZD_FM_HISTORY) {
        uint32_t i;
        for (i = 0; i + 1 < ZD_FM_HISTORY; ++i)
            f_copy(fm->history[i], ZD_FM_PATH, fm->history[i + 1]);
        fm->hist_count--;
        fm->hist_pos--;
        fm->stats.history_dropped++;
    }
    f_copy(fm->history[fm->hist_count], ZD_FM_PATH, path);
    fm->hist_count++;
    fm->hist_pos = fm->hist_count - 1;
    fm->stats.navigations++;
    return f_load(fm);
}

int zd_fm_refresh(struct zd_fm *fm) {
    if (!fm || !fm->path[0])
        return -22;
    return f_load(fm);
}

int zd_fm_back(struct zd_fm *fm) {
    if (!fm)
        return -22;
    if (fm->hist_pos == 0)
        return -22;
    fm->hist_pos--;
    f_copy(fm->path, ZD_FM_PATH, fm->history[fm->hist_pos]);
    fm->stats.backs++;
    return f_load(fm);
}

int zd_fm_forward(struct zd_fm *fm) {
    if (!fm)
        return -22;
    if (fm->hist_pos + 1 >= fm->hist_count)
        return -22;
    fm->hist_pos++;
    f_copy(fm->path, ZD_FM_PATH, fm->history[fm->hist_pos]);
    fm->stats.forwards++;
    return f_load(fm);
}

static int f_vis_pred(const struct zd_fm *fm,
                      const struct zd_fm_entry *e) {
    if (!fm->show_hidden && (e->flags & ZD_FM_HIDDEN))
        return 0;
    return 1;
}

uint32_t zd_fm_visible_count(const struct zd_fm *fm) {
    uint32_t i, n = 0;
    if (!fm)
        return 0;
    for (i = 0; i < fm->count; ++i)
        if (f_vis_pred(fm, &fm->entries[i]))
            ++n;
    return n;
}

const struct zd_fm_entry *zd_fm_visible(const struct zd_fm *fm,
                                        uint32_t idx) {
    uint32_t i, n = 0;
    if (!fm)
        return 0;
    for (i = 0; i < fm->count; ++i) {
        if (!f_vis_pred(fm, &fm->entries[i]))
            continue;
        if (n == idx)
            return &fm->entries[i];
        ++n;
    }
    return 0;
}

/* visible index -> raw index; -1 when out of range */
static int f_raw(const struct zd_fm *fm, uint32_t idx) {
    uint32_t i, n = 0;
    for (i = 0; i < fm->count; ++i) {
        if (!f_vis_pred(fm, &fm->entries[i]))
            continue;
        if (n == idx)
            return (int)i;
        ++n;
    }
    return -1;
}

int zd_fm_set_sort(struct zd_fm *fm, uint32_t mode, uint32_t desc) {
    if (!fm || mode > ZD_FM_SORT_MTIME || desc > 1) {
        if (fm)
            fm->stats.rejected++;
        return -22;
    }
    fm->sort_mode = mode;
    fm->sort_desc = desc;
    f_sort(fm);
    f_clear_sel(fm); /* indices shifted under the selection */
    return 0;
}

int zd_fm_set_show_hidden(struct zd_fm *fm, uint32_t show) {
    if (!fm || show > 1)
        return -22;
    if (fm->show_hidden != show) {
        fm->show_hidden = show;
        f_clear_sel(fm); /* visible indexing changed */
    }
    return 0;
}

static int f_sel_op(struct zd_fm *fm, uint32_t idx, int toggle) {
    int raw;
    if (!fm)
        return -22;
    raw = f_raw(fm, idx);
    if (raw < 0)
        return -2;
    if (toggle) {
        uint32_t w = (uint32_t)raw / 64;
        uint32_t b = (uint32_t)raw % 64;
        fm->selected[w] ^= (uint64_t)1 << b;
    } else {
        uint32_t w = (uint32_t)raw / 64;
        uint32_t b = (uint32_t)raw % 64;
        fm->selected[w] |= (uint64_t)1 << b;
    }
    fm->stats.selects++;
    return 0;
}

int zd_fm_select(struct zd_fm *fm, uint32_t idx) {
    return f_sel_op(fm, idx, 0);
}
int zd_fm_toggle(struct zd_fm *fm, uint32_t idx) {
    return f_sel_op(fm, idx, 1);
}
void zd_fm_clear_selection(struct zd_fm *fm) {
    if (fm)
        f_clear_sel(fm);
}
uint32_t zd_fm_selected_count(const struct zd_fm *fm) {
    uint32_t i, n = 0;
    if (!fm)
        return 0;
    for (i = 0; i < fm->count; ++i) {
        uint32_t w = i / 64, b = i % 64;
        if (fm->selected[w] & ((uint64_t)1 << b))
            ++n;
    }
    return n;
}

/* full path for an operation: dir + '/' + name */
static int f_join(const struct zd_fm *fm, const char *name,
                  char *out, uint32_t cap) {
    uint32_t d = f_len(fm->path);
    uint32_t n = f_len(name);
    if (!d || !n || d + 1 + n + 1 > cap)
        return -22;
    f_copy(out, cap, fm->path);
    if (d > 1) { /* root already provides the slash */
        if (d + 1 >= cap)
            return -22;
        out[d++] = '/';
    }
    if (d + n + 1 > cap)
        return -22;
    {
        uint32_t i;
        for (i = 0; i < n; ++i)
            out[d + i] = name[i];
        out[d + n] = 0;
    }
    return 0;
}

int zd_fm_remove(struct zd_fm *fm, uint32_t actor_perms,
                 const char *name) {
    char full[ZD_FM_PATH];
    int r;
    if (!fm || !f_name_ok(name) || !fm->path[0]) {
        if (fm)
            fm->stats.rejected++;
        return -22;
    }
    if (!fm->ops.remove)
        return -22;
    if (!(actor_perms & ZD_FM_PERM_WRITE)) {
        fm->stats.ops_perm_denied++;
        return -1;
    }
    if (f_join(fm, name, full, sizeof(full)) < 0) {
        fm->stats.rejected++;
        return -22;
    }
    r = fm->ops.remove(fm->ops.ctx, full);
    if (r < 0) {
        fm->stats.op_errors++;
        return r;
    }
    fm->stats.removed++;
    (void)f_load(fm); /* refresh; failure surfaces via hist_state */
    return 0;
}

int zd_fm_mkdir(struct zd_fm *fm, uint32_t actor_perms,
                const char *name) {
    char full[ZD_FM_PATH];
    int r;
    if (!fm || !f_name_ok(name) || !fm->path[0]) {
        if (fm)
            fm->stats.rejected++;
        return -22;
    }
    if (!fm->ops.mkdir)
        return -22;
    if (!(actor_perms & ZD_FM_PERM_WRITE)) {
        fm->stats.ops_perm_denied++;
        return -1;
    }
    if (f_join(fm, name, full, sizeof(full)) < 0) {
        fm->stats.rejected++;
        return -22;
    }
    r = fm->ops.mkdir(fm->ops.ctx, full);
    if (r < 0) {
        fm->stats.op_errors++;
        return r;
    }
    fm->stats.mkdirs++;
    (void)f_load(fm);
    return 0;
}
