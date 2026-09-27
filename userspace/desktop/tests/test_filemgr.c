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


/* ---- content fixture for copy/move/preview ----------------------------
 * In-memory file bodies behind the read_file/write_file ops; the session
 * binds the same callback shape to OPEN/READ/WRITE/CLOSE. */
struct fm_file {
    char path[ZD_FM_PATH];
    char data[256];
    uint32_t len;
};
static struct fm_file fm_files[4];
static uint32_t fm_nfiles;

static void fm_files_reset(void) {
    memset(fm_files, 0, sizeof(fm_files));
    fm_nfiles = 0;
}

static struct fm_file *fm_file(const char *path) {
    uint32_t i;
    for (i = 0; i < fm_nfiles; ++i)
        if (strcmp(fm_files[i].path, path) == 0)
            return &fm_files[i];
    return 0;
}

static void fm_file_add(const char *path, const char *data) {
    struct fm_file *f;
    ZD_CHECK(fm_nfiles < 4);
    if (fm_nfiles >= 4)
        return;
    f = &fm_files[fm_nfiles++];
    snprintf(f->path, sizeof(f->path), "%s", path);
    snprintf(f->data, sizeof(f->data), "%s", data);
    f->len = (uint32_t)strlen(data);
}

static int fm_read_fn(void *ctx, const char *path, void *buffer,
                      uint32_t capacity, uint32_t *out_length) {
    struct fm_file *f;
    uint32_t n;
    (void)ctx;
    op_note("D:");
    op_note(path);
    op_note(";");
    if (op_fail)
        return op_fail;
    f = fm_file(path);
    if (!f)
        return -2;
    n = f->len < capacity ? f->len : capacity;
    memcpy(buffer, f->data, n);
    if (out_length)
        *out_length = n;
    return 0;
}

static int fm_write_fn(void *ctx, const char *path, const void *buffer,
                       uint32_t length) {
    struct fm_file *f;
    (void)ctx;
    op_note("W:");
    op_note(path);
    op_note(";");
    if (op_fail)
        return op_fail;
    if (length >= 256)
        return -27;
    f = fm_file(path);
    if (!f) {
        ZD_CHECK(fm_nfiles < 4);
        if (fm_nfiles >= 4)
            return -28;
        f = &fm_files[fm_nfiles++];
        snprintf(f->path, sizeof(f->path), "%s", path);
    }
    memcpy(f->data, buffer, length);
    f->data[length] = 0;
    f->len = length;
    return 0;
}

static int fm_rename_fn(void *ctx, const char *from, const char *to) {
    (void)ctx;
    op_note("N:");
    op_note(from);
    op_note(">");
    op_note(to);
    op_note(";");
    return op_fail;
}

static void fm_xfer_fixture(void) {
    ndirs = 0;
    op_len = 0;
    op_log[0] = 0;
    op_fail = 0;
    memset(dirs, 0, sizeof(dirs));
    fm_files_reset();

    dirs[ndirs].path = "/docs";
    fm_add(&dirs[ndirs], "a.txt", 10, 2, 0);
    fm_add(&dirs[ndirs], "big.bin", ZD_FM_COPY_MAX + 1U, 3, 0);
    ndirs++;
    dirs[ndirs].path = "/dest";
    ndirs++;

    fm_file_add("/docs/a.txt", "hello wor");   /* 9 bytes on purpose */
}

static void test_rename_copy_move_peek(void) {
    struct zd_fm fm;
    char preview[64];
    uint32_t got = 0;

    zd_fm_init(&fm, fm_source, 0);
    fm_xfer_fixture();
    ZD_CHECK_OK(zd_fm_open(&fm, "/docs"));

    /* no ops installed -> -22, and nothing counted as an op error */
    ZD_CHECK_EQ(zd_fm_rename(&fm, ZD_FM_PERM_WRITE, "a.txt", "b.txt"), -22);
    ZD_CHECK_EQ(zd_fm_peek(&fm, ZD_FM_PERM_READ, "a.txt", preview,
                           sizeof(preview), &got), -22);
    ZD_CHECK_EQ(zd_fm_copy(&fm, ZD_FM_PERM_WRITE, "a.txt", "/dest",
                           "c.txt"), -22);
    ZD_CHECK_EQ(zd_fm_move(&fm, ZD_FM_PERM_WRITE, "a.txt", "/dest",
                           "c.txt"), -22);
    ZD_CHECK_EQ(fm.stats.op_errors, 0U);
    fm.ops.rename = fm_rename_fn;
    fm.ops.read_file = fm_read_fn;
    fm.ops.write_file = fm_write_fn;
    fm.ops.remove = fm_remove_fn; /* cross-directory move removes the source */

    /* an actor without the needed bit is refused before any op runs */
    ZD_CHECK_EQ(zd_fm_rename(&fm, ZD_FM_PERM_READ, "a.txt", "b.txt"), -1);
    ZD_CHECK_EQ(zd_fm_peek(&fm, 0, "a.txt", preview, sizeof(preview),
                           &got), -1);
    ZD_CHECK_EQ(zd_fm_copy(&fm, ZD_FM_PERM_READ, "a.txt", "/dest",
                           "c.txt"), -1);
    ZD_CHECK_EQ(fm.stats.ops_perm_denied, 3U);
    ZD_CHECK_EQ(op_len, 0);

    /* bad arguments: separators, traversal, relative target dir */
    ZD_CHECK_EQ(zd_fm_rename(&fm, ZD_FM_PERM_WRITE, "a/b", "b.txt"), -22);
    ZD_CHECK_EQ(zd_fm_rename(&fm, ZD_FM_PERM_WRITE, "a.txt", "../b"), -22);
    ZD_CHECK_EQ(zd_fm_copy(&fm, ZD_FM_PERM_WRITE, "a.txt", "relative",
                           "c.txt"), -22);
    ZD_CHECK_EQ(zd_fm_move(&fm, ZD_FM_PERM_WRITE, "a.txt", "", "c.txt"), -22);
    ZD_CHECK(fm.stats.rejected >= 4U);

    /* preview reads real content and terminates it */
    memset(preview, 'x', sizeof(preview));
    ZD_CHECK_OK(zd_fm_peek(&fm, ZD_FM_PERM_READ, "a.txt", preview,
                           sizeof(preview), &got));
    ZD_CHECK_EQ(got, 9U);
    ZD_CHECK(strcmp(preview, "hello wor") == 0);
    ZD_CHECK_EQ(fm.stats.peeks, 1U);

    /* a read failure propagates the errno and is counted */
    op_fail = -5;
    ZD_CHECK_EQ(zd_fm_peek(&fm, ZD_FM_PERM_READ, "a.txt", preview,
                           sizeof(preview), &got), -5);
    ZD_CHECK_EQ(fm.stats.op_errors, 1U);
    op_fail = 0;

    /* rename joins both names against the current directory */
    ZD_CHECK_OK(zd_fm_rename(&fm, ZD_FM_PERM_WRITE, "a.txt", "renamed.txt"));
    ZD_CHECK(strstr(op_log, "N:/docs/a.txt>/docs/renamed.txt;") != 0);
    ZD_CHECK_EQ(fm.stats.renamed, 1U);

    /* copy refuses an unlisted name and an oversized one: no half files */
    ZD_CHECK_EQ(zd_fm_copy(&fm, ZD_FM_PERM_WRITE, "ghost.txt", "/dest",
                           "c.txt"), -27);
    ZD_CHECK_EQ(zd_fm_copy(&fm, ZD_FM_PERM_WRITE, "big.bin", "/dest",
                           "c.txt"), -27);
    ZD_CHECK_EQ(fm.stats.refusals, 2U);
    ZD_CHECK_EQ(fm.stats.copied, 0U);

    /* a real copy: content lands at the joined destination path */
    ZD_CHECK_OK(zd_fm_copy(&fm, ZD_FM_PERM_WRITE, "a.txt", "/dest",
                           "c.txt"));
    ZD_CHECK(strstr(op_log, "D:/docs/a.txt;") != 0);
    ZD_CHECK(strstr(op_log, "W:/dest/c.txt;") != 0);
    ZD_CHECK_EQ(fm.stats.copied, 1U);
    ZD_CHECK(fm_file("/dest/c.txt") != 0);
    ZD_CHECK(strcmp(fm_file("/dest/c.txt")->data, "hello wor") == 0);

    /* a failing write leaves the source alone and counts the error */
    op_fail = -28;
    ZD_CHECK_EQ(zd_fm_copy(&fm, ZD_FM_PERM_WRITE, "a.txt", "/dest",
                           "d.txt"), -28);
    ZD_CHECK_EQ(fm.stats.op_errors, 2U);
    ZD_CHECK_EQ(fm.stats.copied, 1U);
    op_fail = 0;

    /* move inside the current directory is a plain rename */
    ZD_CHECK_OK(zd_fm_move(&fm, ZD_FM_PERM_WRITE, "a.txt", "/docs",
                           "moved.txt"));
    ZD_CHECK(strstr(op_log, "N:/docs/a.txt>/docs/moved.txt;") != 0);
    ZD_CHECK_EQ(fm.stats.moved, 1U);
    ZD_CHECK_EQ(fm.stats.renamed, 2U);

    /* move across directories copies first, then removes the source */
    op_len = 0;
    op_log[0] = 0;
    ZD_CHECK_OK(zd_fm_move(&fm, ZD_FM_PERM_WRITE, "a.txt", "/dest",
                           "e.txt"));
    ZD_CHECK(strstr(op_log, "D:/docs/a.txt;") != 0);
    ZD_CHECK(strstr(op_log, "W:/dest/e.txt;") != 0);
    ZD_CHECK(strstr(op_log, "R:/docs/a.txt;") != 0);
    ZD_CHECK_EQ(fm.stats.moved, 2U);
    /* the remove must come after the write, never before */
    ZD_CHECK(strstr(op_log, "W:/dest/e.txt;") < strstr(op_log,
                                                       "R:/docs/a.txt;"));
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

    test_rename_copy_move_peek();
}
