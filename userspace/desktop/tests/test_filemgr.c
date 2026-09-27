/* File manager core tests (shell surface) */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <string.h>

/* ---- in-memory directory source fixture (never a real fs) ---- */

struct fm_dir {
    const char *path;
    struct zd_fm_entry e[80]; /* entries beyond cap exercise truncation */
    uint32_t n;
    int force_err;            /* source returns this errno */
};

static void fm_add(struct fm_dir *d, const char *name, uint64_t size,
                   int64_t mtime, uint32_t flags) {
    struct zd_fm_entry *e = &d->e[d->n++];
    memset(e, 0, sizeof(*e));
    {
        uint32_t i = 0;
        while (name[i] && i + 1 < ZD_FM_NAME) {
            e->name[i] = name[i];
            ++i;
        }
    }
    e->size = size;
    e->mtime = mtime;
    e->flags = flags;
}

static struct fm_dir dirs[4];
static uint32_t ndirs;

static struct fm_dir *fm_dir(const char *path) {
    uint32_t i;
    for (i = 0; i < ndirs; ++i)
        if (strcmp(dirs[i].path, path) == 0)
            return &dirs[i];
    return 0;
}

static int fm_source(void *ctx, const char *path,
                     struct zd_fm_entry *out, uint32_t cap,
                     uint32_t *out_n) {
    struct fm_dir *d;
    uint32_t i;
    (void)ctx;
    *out_n = 0;
    d = fm_dir(path);
    if (!d)
        return -2; /* ENOENT: unknown directory */
    if (d->force_err)
        return d->force_err;
    for (i = 0; i < d->n && i < cap; ++i)
        out[i] = d->e[i];
    *out_n = (d->n < cap) ? d->n : cap;
    return 0;
}

/* ops fixture */
static char op_log[256];
static int op_len;
static int op_fail;
static void op_note(const char *s) {
    while (*s && op_len + 1 < (int)sizeof(op_log))
        op_log[op_len++] = *s++;
    op_log[op_len] = 0;
}
static int fm_remove_fn(void *ctx, const char *path) {
    (void)ctx;
    op_note("R:");
    op_note(path);
    op_note(";");
    return op_fail;
}
static int fm_mkdir_fn(void *ctx, const char *path) {
    (void)ctx;
    op_note("M:");
    op_note(path);
    op_note(";");
    return op_fail;
}

static void fm_fixture(void) {
    uint32_t i;
    ndirs = 0;
    op_len = 0;
    op_log[0] = 0;
    op_fail = 0;
    memset(dirs, 0, sizeof(dirs));

    dirs[ndirs].path = "/";
    fm_add(&dirs[ndirs], "file.txt", 100, 5, 0);
    fm_add(&dirs[ndirs], "docs", 0, 1, ZD_FM_DIR);
    fm_add(&dirs[ndirs], ".hidden", 7, 2, ZD_FM_HIDDEN);
    fm_add(&dirs[ndirs], ".config", 3, 4, ZD_FM_DIR | ZD_FM_HIDDEN);
    ndirs++;

    dirs[ndirs].path = "/docs";
    fm_add(&dirs[ndirs], "b.txt", 30, 1, 0);
    fm_add(&dirs[ndirs], "a.txt", 10, 2, 0);
    fm_add(&dirs[ndirs], ".secret", 1, 3, ZD_FM_HIDDEN);
    ndirs++;

    dirs[ndirs].path = "/err";
    dirs[ndirs].force_err = -5;
    ndirs++;

    dirs[ndirs].path = "/big";
    for (i = 0; i < ZD_FM_MAX + 16; ++i) {
        char nm[8];
        nm[0] = 'f';
        nm[1] = (char)('a' + (char)(i % 26));
        nm[2] = (char)('0' + (char)(i % 10));
        nm[3] = 0;
        fm_add(&dirs[ndirs], nm, i, (int64_t)i, 0);
    }
    ndirs++;
}

void zd_test_filemgr_suite(void) {
    struct zd_fm fm;

    fm_fixture();
    zd_fm_init(&fm, fm_source, 0);

    /* open + visible filtering */
    ZD_CHECK_OK(zd_fm_open(&fm, "/"));
    ZD_CHECK_EQ(fm.count, 4);
    ZD_CHECK_EQ(zd_fm_visible_count(&fm), 2); /* hidden filtered */
    ZD_CHECK(strcmp(zd_fm_visible(&fm, 0)->name, "docs") == 0);
    ZD_CHECK(strcmp(zd_fm_visible(&fm, 1)->name, "file.txt") == 0);
    ZD_CHECK(zd_fm_visible(&fm, 2) == NULL);
    ZD_CHECK_OK(zd_fm_set_show_hidden(&fm, 1));
    ZD_CHECK_EQ(zd_fm_visible_count(&fm), 4);
    ZD_CHECK_OK(zd_fm_set_show_hidden(&fm, 0));
    ZD_CHECK_EQ(zd_fm_set_show_hidden(&fm, 2), -22);

    /* sorting */
    ZD_CHECK_OK(zd_fm_set_sort(&fm, ZD_FM_SORT_NAME, 1)); /* desc */
    ZD_CHECK(strcmp(zd_fm_visible(&fm, 0)->name, "file.txt") == 0);
    ZD_CHECK_OK(zd_fm_set_sort(&fm, ZD_FM_SORT_NAME, 0));
    ZD_CHECK(strcmp(zd_fm_visible(&fm, 0)->name, "docs") == 0);
    ZD_CHECK_OK(zd_fm_set_sort(&fm, ZD_FM_SORT_SIZE, 1));
    ZD_CHECK(strcmp(zd_fm_visible(&fm, 0)->name, "file.txt") == 0);
    ZD_CHECK_OK(zd_fm_set_sort(&fm, ZD_FM_SORT_MTIME, 0));
    /* docs mtime=1 < file.txt mtime=5 */
    ZD_CHECK(strcmp(zd_fm_visible(&fm, 0)->name, "docs") == 0);
    ZD_CHECK_EQ(zd_fm_set_sort(&fm, 9, 0), -22);
    ZD_CHECK_OK(zd_fm_set_sort(&fm, ZD_FM_SORT_NAME, 0));

    /* selection over visible indices */
    ZD_CHECK_OK(zd_fm_select(&fm, 0)); /* docs */
    ZD_CHECK_OK(zd_fm_select(&fm, 1)); /* file.txt */
    ZD_CHECK_EQ(zd_fm_selected_count(&fm), 2);
    ZD_CHECK_OK(zd_fm_select(&fm, 0)); /* idempotent */
    ZD_CHECK_EQ(zd_fm_selected_count(&fm), 2);
    ZD_CHECK_EQ(zd_fm_toggle(&fm, 0), 0);
    ZD_CHECK_EQ(zd_fm_selected_count(&fm), 1);
    ZD_CHECK_EQ(zd_fm_toggle(&fm, 2), -2); /* oob */
    ZD_CHECK_EQ(zd_fm_select(&fm, 99), -2);
    zd_fm_clear_selection(&fm);
    ZD_CHECK_EQ(zd_fm_selected_count(&fm), 0);
    /* hidden index never selectable while filtered */
    ZD_CHECK_EQ(zd_fm_select(&fm, 3), -2);

    /* history: open / then /docs, back, forward */
    ZD_CHECK_OK(zd_fm_open(&fm, "/docs"));
    ZD_CHECK_EQ(fm.count, 3);
    ZD_CHECK_EQ(fm.stats.navigations, 2);
    ZD_CHECK_OK(zd_fm_back(&fm));
    ZD_CHECK(strcmp(fm.path, "/") == 0);
    ZD_CHECK_EQ(fm.count, 4);
    ZD_CHECK_EQ(fm.stats.backs, 1);
    ZD_CHECK_EQ(zd_fm_back(&fm), -22); /* at root */
    ZD_CHECK_OK(zd_fm_forward(&fm));
    ZD_CHECK(strcmp(fm.path, "/docs") == 0);
    ZD_CHECK_EQ(fm.stats.forwards, 1);
    /* branching after back drops forward entries */
    ZD_CHECK_OK(zd_fm_back(&fm));
    ZD_CHECK_OK(zd_fm_open(&fm, "/big"));
    ZD_CHECK_EQ(fm.hist_count, 2); /* / , /big */
    ZD_CHECK(fm.stats.history_dropped >= 1);

    /* history capacity: 16 entries then overflow drops oldest */
    {
        uint32_t i;
        for (i = 0; i < 20; ++i)
            ZD_CHECK_OK(zd_fm_open(&fm, "/"));
        ZD_CHECK_EQ(fm.hist_count, ZD_FM_HISTORY);
        ZD_CHECK(fm.stats.history_dropped >= 5);
    }

    /* path validation */
    ZD_CHECK_EQ(zd_fm_open(&fm, "relative"), -22);
    ZD_CHECK_EQ(zd_fm_open(&fm, "/a/../b"), -22);
    ZD_CHECK_EQ(zd_fm_open(&fm, ".."), -22);
    ZD_CHECK_EQ(zd_fm_open(&fm, NULL), -22);
    {
        char huge[ZD_FM_PATH + 8];
        memset(huge, 'x', sizeof(huge) - 1);
        huge[0] = '/';
        huge[sizeof(huge) - 1] = 0;
        ZD_CHECK_EQ(zd_fm_open(&fm, huge), -22);
    }
    ZD_CHECK(fm.stats.rejected >= 5);

    /* source error propagates + state failed */
    ZD_CHECK_EQ(zd_fm_open(&fm, "/err"), -5);
    ZD_CHECK_EQ(fm.hist_state, 3);
    ZD_CHECK_EQ(fm.stats.source_errors, 1);
    ZD_CHECK_EQ(zd_fm_refresh(&fm), -5);
    ZD_CHECK_EQ(fm.stats.source_errors, 2);
    /* unknown dir */
    ZD_CHECK_EQ(zd_fm_open(&fm, "/nope"), -2);
    ZD_CHECK_EQ(fm.stats.source_errors, 3);

    /* truncation: source fills capacity -> flagged, not hidden */
    ZD_CHECK_OK(zd_fm_open(&fm, "/big"));
    ZD_CHECK_EQ(fm.count, ZD_FM_MAX);
    ZD_CHECK_EQ(fm.truncated, 1);
    ZD_CHECK(fm.stats.truncations >= 1);
    /* sorted by name asc: "fa0" family first */
    ZD_CHECK(zd_fm_visible(&fm, 0) != NULL);

    /* ---- permission-gated operations ---- */
    zd_fm_init(&fm, fm_source, 0);
    fm_fixture();
    ZD_CHECK_OK(zd_fm_open(&fm, "/docs"));
    /* no ops installed -> -22 */
    ZD_CHECK_EQ(zd_fm_remove(&fm, ZD_FM_PERM_WRITE, "a.txt"), -22);
    ZD_CHECK_EQ(zd_fm_mkdir(&fm, ZD_FM_PERM_WRITE, "new"), -22);
    fm.ops.remove = fm_remove_fn;
    fm.ops.mkdir = fm_mkdir_fn;
    fm.ops.ctx = 0;
    /* read-only actor denied */
    ZD_CHECK_EQ(zd_fm_remove(&fm, ZD_FM_PERM_READ, "a.txt"), -1);
    ZD_CHECK_EQ(fm.stats.ops_perm_denied, 1);
    ZD_CHECK_EQ(zd_fm_mkdir(&fm, 0, "new"), -1);
    ZD_CHECK_EQ(fm.stats.ops_perm_denied, 2);
    ZD_CHECK_EQ(op_len, 0); /* denied before touching ops */
    /* bad names rejected before ops */
    ZD_CHECK_EQ(zd_fm_remove(&fm, ZD_FM_PERM_WRITE, "../evil"), -22);
    ZD_CHECK_EQ(zd_fm_remove(&fm, ZD_FM_PERM_WRITE, ""), -22);
    ZD_CHECK_EQ(zd_fm_remove(&fm, ZD_FM_PERM_WRITE, "a/b"), -22);
    /* success paths: joined absolute paths, refreshed listing */
    ZD_CHECK_OK(zd_fm_remove(&fm, ZD_FM_PERM_WRITE, "a.txt"));
    ZD_CHECK_EQ(fm.stats.removed, 1);
    ZD_CHECK(strstr(op_log, "R:/docs/a.txt;") != 0);
    ZD_CHECK_OK(zd_fm_mkdir(&fm, ZD_FM_PERM_WRITE, "new"));
    ZD_CHECK_EQ(fm.stats.mkdirs, 1);
    ZD_CHECK(strstr(op_log, "M:/docs/new;") != 0);
    /* root join has no double slash */
    ZD_CHECK_OK(zd_fm_open(&fm, "/"));
    ZD_CHECK_OK(zd_fm_mkdir(&fm, ZD_FM_PERM_WRITE, "top"));
    ZD_CHECK(strstr(op_log, "M:/top;") != 0);
    /* op failure errno propagates + counted */
    op_fail = -13;
    ZD_CHECK_EQ(zd_fm_remove(&fm, ZD_FM_PERM_WRITE, "docs"), -13);
    ZD_CHECK_EQ(fm.stats.op_errors, 1);
    ZD_CHECK_EQ(fm.stats.removed, 1); /* unchanged */

    /* refresh clears selection */
    op_fail = 0;
    ZD_CHECK_OK(zd_fm_open(&fm, "/docs"));
    ZD_CHECK_OK(zd_fm_select(&fm, 0));
    ZD_CHECK_EQ(zd_fm_selected_count(&fm), 1);
    ZD_CHECK_OK(zd_fm_refresh(&fm));
    ZD_CHECK_EQ(zd_fm_selected_count(&fm), 0);

    /* null safety */
    zd_fm_init(NULL, fm_source, 0);
    ZD_CHECK_EQ(zd_fm_open(NULL, "/"), -22);
    ZD_CHECK_EQ(zd_fm_refresh(NULL), -22);
    ZD_CHECK_EQ(zd_fm_back(NULL), -22);
    ZD_CHECK_EQ(zd_fm_selected_count(NULL), 0);
    ZD_CHECK(zd_fm_visible(NULL, 0) == NULL);
}
