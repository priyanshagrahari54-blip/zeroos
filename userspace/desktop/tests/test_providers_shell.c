/* Shell-layer search provider tests: files (directory source shared with the
 * file manager) and settings (schema-driven registry).  The directory source
 * here is an in-memory fixture; the session binds the same callback shape to
 * `zeroos_readdir`, so these suites cover the contract that binding uses. */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <zeroos/syscall.h>
#include <string.h>

/* ---- in-memory directory fixture -------------------------------------- */

struct pv_dir {
    char path[ZD_FM_PATH];     /* copied: callers may build paths locally */
    struct zd_fm_entry entries[40];
    uint32_t count;
    int force_err;             /* negative errno returned for this dir */
};

#define PV_DIRS 40
static struct pv_dir pv_dirs[PV_DIRS];
static uint32_t pv_ndirs;
static uint32_t pv_listings;   /* how often the source was called */

static struct pv_dir *pv_dir(const char *path) {
    uint32_t i;
    for (i = 0; i < pv_ndirs; ++i)
        if (strcmp(pv_dirs[i].path, path) == 0)
            return &pv_dirs[i];
    return 0;
}

static void pv_reset(void) {
    memset(pv_dirs, 0, sizeof(pv_dirs));
    pv_ndirs = 0;
    pv_listings = 0;
}

static void pv_mkdir(const char *path) {
    struct pv_dir *d;
    ZD_CHECK(pv_ndirs < PV_DIRS);
    if (pv_ndirs >= PV_DIRS)
        return;
    d = &pv_dirs[pv_ndirs++];
    memset(d, 0, sizeof(*d));
    snprintf(d->path, sizeof(d->path), "%s", path);
}

static void pv_add(const char *dir, const char *name, uint64_t size,
                   uint32_t flags) {
    struct pv_dir *d = pv_dir(dir);
    struct zd_fm_entry *e;
    ZD_CHECK(d != 0);
    if (!d)
        return;
    e = &d->entries[d->count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->size = size;
    e->flags = flags;
}

static int pv_source(void *ctx, const char *path, struct zd_fm_entry *out,
                     uint32_t cap, uint32_t *out_n) {
    struct pv_dir *d;
    uint32_t i;
    (void)ctx;
    ++pv_listings;
    *out_n = 0;
    d = pv_dir(path);
    if (!d)
        return -2;                       /* ZEROOS_ENOENT */
    if (d->force_err)
        return d->force_err;
    for (i = 0; i < d->count && i < cap; ++i)
        out[i] = d->entries[i];
    *out_n = (d->count < cap) ? d->count : cap;
    return 0;
}

static uint32_t pv_result_has(const struct zd_search_result *results,
                              uint32_t count, const char *path,
                              enum zd_search_kind kind) {
    uint32_t i;
    for (i = 0; i < count; ++i)
        if (results[i].kind == kind && strcmp(results[i].path, path) == 0)
            return 1;
    return 0;
}

static uint32_t pv_score_of(const struct zd_search_result *results,
                            uint32_t count, const char *path) {
    uint32_t i;
    for (i = 0; i < count; ++i)
        if (strcmp(results[i].path, path) == 0)
            return results[i].score;
    return 0;
}

/* ---- files provider ---------------------------------------------------- */

static void test_files_provider_roots(void) {
    struct zd_files_provider fp;
    uint32_t i;

    ZD_CHECK_ERR(zd_files_provider_init(0, pv_source, 0), ZD_EINVAL);
    ZD_CHECK_OK(zd_files_provider_init(&fp, pv_source, 0));
    ZD_CHECK_EQ(fp.provider.available(&fp), 0u);  /* no roots yet */
    ZD_CHECK_ERR(zd_files_provider_add_root(0, "/ram"), ZD_EINVAL);
    ZD_CHECK_ERR(zd_files_provider_add_root(&fp, "ram"), ZD_EINVAL);
    ZD_CHECK_ERR(zd_files_provider_add_root(&fp, "/ram/../etc"), ZD_EINVAL);
    ZD_CHECK_ERR(zd_files_provider_add_root(&fp, ""), ZD_EINVAL);
    ZD_CHECK_EQ(fp.root_count, 0u);
    ZD_CHECK_OK(zd_files_provider_add_root(&fp, "/ram/shell"));
    ZD_CHECK_OK(zd_files_provider_add_root(&fp, "/ram/shell")); /* dup: ok */
    ZD_CHECK_EQ(fp.root_count, 1u);
    ZD_CHECK_EQ(fp.provider.available(&fp), 1u);
    for (i = 0; i < ZD_FILES_MAX_ROOTS; ++i) {
        char path[ZD_FM_PATH];
        snprintf(path, sizeof(path), "/ram/root%u", i);
        if (i == 0)
            continue;                    /* /ram/shell already occupies 0 */
        ZD_CHECK_EQ(zd_files_provider_add_root(&fp, path),
                    (i < ZD_FILES_MAX_ROOTS) ? 0 : -ZD_ENOSPC);
    }
    ZD_CHECK_ERR(zd_files_provider_add_root(&fp, "/ram/overflow"),
                 ZD_ENOSPC);

    /* A provider without a source is unavailable, never fake-available. */
    ZD_CHECK_OK(zd_files_provider_init(&fp, 0, 0));
    ZD_CHECK_EQ(fp.provider.available(&fp), 0u);
    ZD_CHECK_OK(zd_files_provider_add_root(&fp, "/ram"));
    ZD_CHECK_EQ(fp.provider.available(&fp), 0u);
}

static void test_files_provider_query(void) {
    struct zd_files_provider fp;
    struct zd_search_result results[8];
    struct zd_intent intent;
    uint32_t got = 0;

    pv_reset();
    pv_mkdir("/ram/shell");
    pv_add("/ram/shell", "notes.txt", 64, 0);
    pv_add("/ram/shell", "notepad.md", 128, 0);
    pv_add("/ram/shell", "inbox", 0, ZD_FM_DIR);
    pv_add("/ram/shell", ".hidden", 8, 0);
    pv_mkdir("/ram/shell/inbox");
    pv_add("/ram/shell/inbox", "todo-notes.txt", 32, 0);
    pv_add("/ram/shell/inbox", "deep", 0, ZD_FM_DIR);
    pv_mkdir("/ram/shell/inbox/deep");
    pv_add("/ram/shell/inbox/deep", "buried-notes.txt", 4, 0);
    pv_add("/ram/shell/inbox/deep", "deeper", 0, ZD_FM_DIR);
    pv_mkdir("/ram/shell/inbox/deep/deeper");
    pv_add("/ram/shell/inbox/deep/deeper", "abyss-notes.txt", 4, 0);

    ZD_CHECK_OK(zd_files_provider_init(&fp, pv_source, 0));
    ZD_CHECK_OK(zd_files_provider_add_root(&fp, "/ram/shell"));
    fp.max_depth = ZD_FILES_DEFAULT_DEPTH;

    memset(&intent, 0, sizeof(intent));
    intent.kind = ZD_SEARCH_ANY;
    strcpy(intent.raw, "notes");
    intent.token_count = 1;
    strcpy(intent.tokens[0], "notes");

    got = (uint32_t)fp.provider.query(&fp, &intent, results, 8, 0, 1);
    /* Four levels are within max_depth=4: notes.txt, todo-notes.txt,
     * buried-notes.txt and abyss-notes.txt (notepad.md does not contain
     * the token). */
    ZD_CHECK_EQ(got, 4u);
    ZD_CHECK(pv_result_has(results, got, "/ram/shell/notes.txt",
                           ZD_SEARCH_FILE));
    ZD_CHECK(pv_result_has(results, got, "/ram/shell/inbox/todo-notes.txt",
                           ZD_SEARCH_FILE));
    ZD_CHECK(pv_result_has(results, got,
                           "/ram/shell/inbox/deep/buried-notes.txt",
                           ZD_SEARCH_FILE));
    ZD_CHECK(pv_result_has(results, got,
                           "/ram/shell/inbox/deep/deeper/abyss-notes.txt",
                           ZD_SEARCH_FILE));
    /* Prefix match outranks a mid-name match. */
    ZD_CHECK_EQ(pv_score_of(results, got, "/ram/shell/notes.txt"), 100u);
    ZD_CHECK_EQ(pv_score_of(results, got,
                            "/ram/shell/inbox/todo-notes.txt"), 60u);
    ZD_CHECK_EQ(fp.stats.full_scans, 0u);
    ZD_CHECK(fp.stats.dirs_listed > 0u);
    ZD_CHECK_EQ(fp.stats.source_errors, 0u);

    /* Kind filtering: file: never returns the folder, folder: only it. */
    intent.kind = ZD_SEARCH_FILE;
    got = (uint32_t)fp.provider.query(&fp, &intent, results, 8, 0, 1);
    ZD_CHECK(pv_result_has(results, got, "/ram/shell/notes.txt",
                           ZD_SEARCH_FILE));
    ZD_CHECK(!pv_result_has(results, got, "/ram/shell/inbox",
                            ZD_SEARCH_FOLDER));
    intent.kind = ZD_SEARCH_FOLDER;
    strcpy(intent.tokens[0], "in");
    strcpy(intent.raw, "in");
    got = (uint32_t)fp.provider.query(&fp, &intent, results, 8, 0, 1);
    ZD_CHECK_EQ(got, 1u);
    ZD_CHECK(pv_result_has(results, got, "/ram/shell/inbox",
                           ZD_SEARCH_FOLDER));

    /* A kind this provider does not own yields nothing, not an error. */
    intent.kind = ZD_SEARCH_APP;
    ZD_CHECK_EQ(fp.provider.query(&fp, &intent, results, 8, 0, 1), 0);

    /* Capacity is honoured exactly. */
    intent.kind = ZD_SEARCH_ANY;
    strcpy(intent.tokens[0], "notes");
    strcpy(intent.raw, "notes");
    ZD_CHECK_EQ(fp.provider.query(&fp, &intent, results, 1, 0, 1), 1);

    /* max_depth=3 stops the walk before the fourth level. */
    fp.max_depth = 3;
    got = (uint32_t)fp.provider.query(&fp, &intent, results, 8, 0, 1);
    ZD_CHECK_EQ(got, 3u);
    ZD_CHECK(!pv_result_has(results, got,
                            "/ram/shell/inbox/deep/deeper/abyss-notes.txt",
                            ZD_SEARCH_FILE));

    /* depth 1 = roots only */
    fp.max_depth = 1;
    got = (uint32_t)fp.provider.query(&fp, &intent, results, 8, 0, 1);
    ZD_CHECK_EQ(got, 1u);
    ZD_CHECK(pv_result_has(results, got, "/ram/shell/notes.txt",
                           ZD_SEARCH_FILE));
    fp.max_depth = 0;                    /* 0 is treated as 1 */
    ZD_CHECK_EQ(fp.provider.query(&fp, &intent, results, 8, 0, 1), 1);

    /* Empty needle: nothing, and no directory listing performed. */
    intent.tokens[0][0] = 0;
    intent.raw[0] = 0;
    {
        uint32_t before = pv_listings;
        ZD_CHECK_EQ(fp.provider.query(&fp, &intent, results, 8, 0, 1), 0);
        ZD_CHECK_EQ(pv_listings, before);
    }

    /* Invalid arguments. */
    ZD_CHECK_ERR(fp.provider.query(0, &intent, results, 8, 0, 1), ZD_EINVAL);
    ZD_CHECK_ERR(fp.provider.query(&fp, 0, results, 8, 0, 1), ZD_EINVAL);
    ZD_CHECK_ERR(fp.provider.query(&fp, &intent, 0, 8, 0, 1), ZD_EINVAL);
}

static void test_files_provider_bounds(void) {
    struct zd_files_provider fp;
    struct zd_search_result results[4];
    struct zd_intent intent;
    uint32_t i, got, cancel;

    /* A wide tree must be stopped by the per-query directory budget. */
    pv_reset();
    pv_mkdir("/wide");
    for (i = 0; i < 39; ++i) {
        char name[ZD_FM_NAME];
        char path[ZD_FM_PATH];
        snprintf(name, sizeof(name), "d%02u", i);
        snprintf(path, sizeof(path), "/wide/d%02u", i);
        pv_add("/wide", name, 0, ZD_FM_DIR);
        pv_mkdir(path);
        pv_add(path, "needle.txt", i, 0);
    }
    ZD_CHECK_OK(zd_files_provider_init(&fp, pv_source, 0));
    ZD_CHECK_OK(zd_files_provider_add_root(&fp, "/wide"));
    fp.max_depth = 2;

    memset(&intent, 0, sizeof(intent));
    intent.kind = ZD_SEARCH_ANY;
    strcpy(intent.raw, "needle");
    intent.token_count = 1;
    strcpy(intent.tokens[0], "needle");

    got = (uint32_t)fp.provider.query(&fp, &intent, results, 4, 0, 1);
    ZD_CHECK_EQ(got, 4u);                        /* capacity wins */
    ZD_CHECK(fp.stats.dirs_listed <= ZD_FILES_DIR_BUDGET);

    /* A 35-deep chain: the work list never fills (one child per directory),
     * so only the per-query directory budget can stop the walk — never a
     * whole-tree rescan. */
    {
        struct zd_search_result wide[64];
        char path[ZD_FM_PATH];
        char child[ZD_FM_PATH];
        uint32_t level;

        pv_reset();
        pv_mkdir("/chain");
        snprintf(path, sizeof(path), "/chain");
        for (level = 0; level < 34; ++level) {
            uint32_t pl = (uint32_t)strlen(path);
            ZD_CHECK(pl + 3U < sizeof(child));
            memcpy(child, path, pl);
            child[pl] = '/';
            child[pl + 1] = 'a';
            child[pl + 2] = 0;
            pv_add(path, "needle.txt", level, 0);
            pv_add(path, "a", 0, ZD_FM_DIR);
            pv_mkdir(child);
            memcpy(path, child, pl + 3U);
        }
        pv_add(path, "needle.txt", 99, 0);

        ZD_CHECK_OK(zd_files_provider_init(&fp, pv_source, 0));
        ZD_CHECK_OK(zd_files_provider_add_root(&fp, "/chain"));
        fp.max_depth = 40;
        got = (uint32_t)fp.provider.query(&fp, &intent, wide, 64, 0, 1);
        ZD_CHECK_EQ(fp.stats.dirs_listed, (uint64_t)ZD_FILES_DIR_BUDGET);
        ZD_CHECK_EQ(fp.stats.budget_exhausted, 1u);
        ZD_CHECK_EQ(fp.stats.full_scans, 0u);
        ZD_CHECK_EQ(got, (uint32_t)ZD_FILES_DIR_BUDGET);
    }

    /* Cancellation stops the walk immediately. */
    fp.stats.canceled = 0;
    fp.stats.dirs_listed = 0;
    cancel = 7;                                  /* provider runs at gen 1 */
    ZD_CHECK_EQ(fp.provider.query(&fp, &intent, results, 4, &cancel, 1), 0);
    ZD_CHECK_EQ(fp.stats.canceled, 1u);
    ZD_CHECK_EQ(fp.stats.dirs_listed, 0u);
    cancel = 1;
    ZD_CHECK(fp.provider.query(&fp, &intent, results, 4, &cancel, 1) > 0);

    /* One unreadable directory is isolated, never fatal for the query. */
    pv_reset();
    pv_mkdir("/mixed");
    pv_add("/mixed", "ok-needle.txt", 1, 0);
    pv_add("/mixed", "locked", 0, ZD_FM_DIR);
    pv_mkdir("/mixed/locked");
    pv_dir("/mixed/locked")->force_err = -13;    /* ZEROOS_EACCES */
    ZD_CHECK_OK(zd_files_provider_init(&fp, pv_source, 0));
    ZD_CHECK_OK(zd_files_provider_add_root(&fp, "/mixed"));
    fp.max_depth = 3;
    got = (uint32_t)fp.provider.query(&fp, &intent, results, 4, 0, 1);
    ZD_CHECK_EQ(got, 1u);
    ZD_CHECK(pv_result_has(results, got, "/mixed/ok-needle.txt",
                           ZD_SEARCH_FILE));
    ZD_CHECK_EQ(fp.stats.source_errors, 1u);

    /* A directory that fills the batch is reported as truncated. */
    pv_reset();
    pv_mkdir("/full");
    for (i = 0; i < ZD_FILES_BATCH; ++i) {
        char name[ZD_FM_NAME];
        snprintf(name, sizeof(name), "notes-%02u.txt", i);
        pv_add("/full", name, i, 0);
    }
    ZD_CHECK_OK(zd_files_provider_init(&fp, pv_source, 0));
    ZD_CHECK_OK(zd_files_provider_add_root(&fp, "/full"));
    (void)fp.provider.query(&fp, &intent, results, 4, 0, 1);
    ZD_CHECK_EQ(fp.stats.truncated_dirs, 1u);
}

/* ---- settings provider ------------------------------------------------- */

static const struct zd_setting_def pv_def_scale = {
    .key = "ui.scale_percent",
    .type = ZD_SETTING_INT,
    .scope = ZD_SCOPE_USER,
    .permissions_required = ZD_PERM_SETTINGS_USER,
    .default_value = 100,
    .min_value = 50,
    .max_value = 300,
    .group = "display",
    .description = "Interface scaling percentage"
};
static const struct zd_setting_def pv_def_motion = {
    .key = "ui.reduced_motion",
    .type = ZD_SETTING_BOOL,
    .scope = ZD_SCOPE_USER,
    .permissions_required = ZD_PERM_SETTINGS_USER,
    .default_value = 0,
    .min_value = 0,
    .max_value = 1,
    .group = "display",
    .description = "Reduce animation"
};
/* Depends on reduced_motion == 1, so it can be reported unavailable. */
static const struct zd_setting_def pv_def_caption = {
    .key = "a11y.caption_scale",
    .type = ZD_SETTING_INT,
    .scope = ZD_SCOPE_USER,
    .permissions_required = ZD_PERM_SETTINGS_USER,
    .default_value = 100,
    .min_value = 50,
    .max_value = 300,
    .group = "accessibility",
    .description = "Caption scale"
};

static void test_settings_provider(void) {
    struct zd_settings settings;
    struct zd_settings_provider sp;
    struct zd_search_result results[8];
    struct zd_intent intent;
    struct zd_setting_def caption = pv_def_caption;
    uint32_t got, cancel, changed = 0;

    zd_settings_init(&settings);
    zd_settings_provider_init(&sp, &settings);
    ZD_CHECK_EQ(sp.provider.name ? strcmp(sp.provider.name, "settings") : 1,
                0);
    ZD_CHECK_EQ(sp.provider.available(&sp), 0u);   /* empty registry */

    memset(&intent, 0, sizeof(intent));
    intent.kind = ZD_SEARCH_ANY;
    strcpy(intent.raw, "ui");
    intent.token_count = 1;
    strcpy(intent.tokens[0], "ui");
    ZD_CHECK_EQ(sp.provider.query(&sp, &intent, results, 8, 0, 1), 0);
    ZD_CHECK_EQ(sp.stats.unavailable, 1u);

    ZD_CHECK_OK(zd_settings_register(&settings, &pv_def_scale));
    ZD_CHECK_OK(zd_settings_register(&settings, &pv_def_motion));
    caption.dep_count = 1;
    strcpy(caption.deps[0].key, "ui.reduced_motion");
    caption.deps[0].required_value = 1;
    ZD_CHECK_OK(zd_settings_register(&settings, &caption));
    ZD_CHECK_EQ(sp.provider.available(&sp), 1u);

    /* key + description + group matching (case-insensitive in the core). */
    got = (uint32_t)sp.provider.query(&sp, &intent, results, 8, 0, 1);
    ZD_CHECK_EQ(got, 2u);
    ZD_CHECK_EQ((int)results[0].kind, (int)ZD_SEARCH_SETTING);
    ZD_CHECK(strcmp(results[0].path, "set:ui.scale_percent") == 0 ||
             strcmp(results[0].path, "set:ui.reduced_motion") == 0);
    ZD_CHECK_EQ(pv_score_of(results, got, "set:ui.scale_percent"), 100u);

    /* description-only match scores below a key prefix match */
    strcpy(intent.tokens[0], "scaling");
    strcpy(intent.raw, "scaling");
    got = (uint32_t)sp.provider.query(&sp, &intent, results, 8, 0, 1);
    ZD_CHECK_EQ(got, 1u);
    ZD_CHECK_EQ(pv_score_of(results, got, "set:ui.scale_percent"), 70u);

    /* group match */
    strcpy(intent.tokens[0], "accessibility");
    strcpy(intent.raw, "accessibility");
    got = (uint32_t)sp.provider.query(&sp, &intent, results, 8, 0, 1);
    ZD_CHECK_EQ(got, 1u);
    /* Unmet dependency: reported, but flagged not-actionable. */
    ZD_CHECK_EQ(results[0].available, 0u);
    ZD_CHECK_OK(zd_settings_set_number(&settings, "ui.reduced_motion", 1,
                                       ZD_PERM_SETTINGS_USER |
                                       ZD_PERM_ADMIN, &changed));
    got = (uint32_t)sp.provider.query(&sp, &intent, results, 8, 0, 1);
    ZD_CHECK_EQ(got, 1u);
    ZD_CHECK_EQ(results[0].available, 1u);

    /* Kind filtering and cancellation. */
    intent.kind = ZD_SEARCH_FILE;
    ZD_CHECK_EQ(sp.provider.query(&sp, &intent, results, 8, 0, 1), 0);
    intent.kind = ZD_SEARCH_SETTING;
    ZD_CHECK(sp.provider.query(&sp, &intent, results, 8, 0, 1) > 0);
    cancel = 9;
    ZD_CHECK_EQ(sp.provider.query(&sp, &intent, results, 8, &cancel, 1), 0);
    ZD_CHECK_EQ(sp.stats.canceled, 1u);
    cancel = 1;
    ZD_CHECK(sp.provider.query(&sp, &intent, results, 8, &cancel, 1) > 0);

    /* Capacity + invalid arguments. */
    ZD_CHECK_EQ(sp.provider.query(&sp, &intent, results, 1, 0, 1), 1);
    ZD_CHECK_ERR(sp.provider.query(0, &intent, results, 8, 0, 1), ZD_EINVAL);
    ZD_CHECK_ERR(sp.provider.query(&sp, 0, results, 8, 0, 1), ZD_EINVAL);
    ZD_CHECK_ERR(sp.provider.query(&sp, &intent, 0, 8, 0, 1), ZD_EINVAL);
    zd_settings_provider_init(0, &settings);       /* null safe */
}

/* ---- pipeline integration --------------------------------------------- */

static void test_shell_providers_in_pipeline(void) {
    struct zd_search search;
    struct zd_files_provider files;
    struct zd_settings_provider settings_provider;
    struct zd_settings settings;
    struct zd_cmd_provider commands;
    struct zd_search_result results[16];
    uint32_t count = 0, i, saw_file = 0, saw_setting = 0, saw_command = 0;

    pv_reset();
    pv_mkdir("/ram/shell");
    pv_add("/ram/shell", "notes.txt", 64, 0);
    pv_add("/ram/shell", "docs", 0, ZD_FM_DIR);
    pv_mkdir("/ram/shell/docs");
    pv_add("/ram/shell/docs", "study-notes.pdf", 1024, 0);

    zd_search_init(&search);
    ZD_CHECK_OK(zd_files_provider_init(&files, pv_source, 0));
    ZD_CHECK_OK(zd_files_provider_add_root(&files, "/ram/shell"));
    zd_settings_init(&settings);
    ZD_CHECK_OK(zd_settings_register(&settings, &pv_def_scale));
    zd_settings_provider_init(&settings_provider, &settings);
    ZD_CHECK_OK(zd_commands_init(&commands));

    ZD_CHECK_OK(zd_search_add_provider(&search, &files.provider));
    ZD_CHECK_OK(zd_search_add_provider(&search,
                                       &settings_provider.provider));
    ZD_CHECK_OK(zd_search_add_provider(&search, &commands.provider));

    ZD_CHECK_OK(zd_search_query(&search, "file:notes", results, 16, &count));
    for (i = 0; i < count; ++i) {
        if (results[i].kind == ZD_SEARCH_FILE)
            ++saw_file;
        if (results[i].kind == ZD_SEARCH_SETTING)
            ++saw_setting;
        if (results[i].kind == ZD_SEARCH_COMMAND)
            ++saw_command;
    }
    ZD_CHECK(saw_file >= 2u);
    ZD_CHECK_EQ(saw_setting, 0u);        /* file: excludes settings */

    count = 0;
    ZD_CHECK_OK(zd_search_query(&search, "set:scale", results, 16, &count));
    saw_setting = 0;
    for (i = 0; i < count; ++i)
        if (results[i].kind == ZD_SEARCH_SETTING)
            ++saw_setting;
    ZD_CHECK_EQ(saw_setting, 1u);

    count = 0;
    ZD_CHECK_OK(zd_search_query(&search, "notes", results, 16, &count));
    ZD_CHECK(count > 0u);

    /* The index core never rescans, and neither do the shell providers. */
    ZD_CHECK_EQ(search.index_stats.full_scans, 0u);
    ZD_CHECK_EQ(files.stats.full_scans, 0u);
    /* Results are ordered by descending score after the merge. */
    for (i = 1; i < count; ++i)
        ZD_CHECK(results[i - 1].score >= results[i].score);
}

/* ---- replay of the guest session sequence ------------------------------ */

/* The session process binds this exact source/ops shape to the file
 * syscalls and asserts these same counts as boot milestones, so the
 * sequence is replayed here against a mutable in-memory directory: a
 * miscounted milestone fails on the host instead of panicking the guest. */

static struct zd_fm_entry sb_entries[8];
static uint32_t sb_count;
static uint32_t sb_removed, sb_made;

static void sb_reset(void) {
    memset(sb_entries, 0, sizeof(sb_entries));
    sb_count = 0;
    sb_removed = 0;
    sb_made = 0;
}

static void sb_add(const char *name, uint64_t size, uint32_t flags) {
    struct zd_fm_entry *e = &sb_entries[sb_count++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->size = size;
    e->flags = flags;
}

static int sb_source(void *ctx, const char *path, struct zd_fm_entry *out,
                     uint32_t cap, uint32_t *out_n) {
    uint32_t i;
    (void)ctx;
    *out_n = 0;
    if (strcmp(path, "/ram/shell") != 0)
        return -2;                     /* ZEROOS_ENOENT */
    for (i = 0; i < sb_count && i < cap; ++i)
        out[i] = sb_entries[i];
    *out_n = (sb_count < cap) ? sb_count : cap;
    return 0;
}

static int sb_remove(void *ctx, const char *path) {
    uint32_t i;
    (void)ctx;
    if (strcmp(path, "/ram/shell/notes.txt") != 0)
        return -2;
    for (i = 0; i < sb_count; ++i) {
        if (strcmp(sb_entries[i].name, "notes.txt") == 0) {
            sb_entries[i] = sb_entries[sb_count - 1];
            --sb_count;
            ++sb_removed;
            return 0;
        }
    }
    return -2;
}

static int sb_mkdir(void *ctx, const char *path) {
    (void)ctx;
    if (strcmp(path, "/ram/shell/inbox") != 0)
        return -13;                    /* ZEROOS_EACCES */
    sb_add("inbox", 0, ZD_FM_DIR);
    ++sb_made;
    return 0;
}

static void test_session_binding_sequence(void) {
    struct zd_fm fm;
    int rc;

    /* The session derives ZD_FM_DIR from the readdir type nibble; pin the
     * encoding so a header change cannot silently make every directory a
     * regular file (it did: folder search then found nothing). */
    ZD_CHECK_EQ(ZEROOS_DT_DIR, ZEROOS_S_IFDIR >> 12);
    ZD_CHECK_EQ(ZEROOS_DT_REG, ZEROOS_S_IFREG >> 12);
    ZD_CHECK_EQ(ZEROOS_DT_DIR, 4u);
    ZD_CHECK_EQ(ZEROOS_DT_REG, 8u);
    ZD_CHECK_NE(ZEROOS_DT_DIR, ZEROOS_DT_REG);

    sb_reset();
    sb_add("notes.txt", 64, 0);
    /* The session's VFS source marks dotfiles ZD_FM_HIDDEN; the fixture
     * mirrors that so the replay exercises the same filter path. */
    sb_add(".hidden", 8, ZD_FM_HIDDEN);

    zd_fm_init(&fm, sb_source, 0);
    fm.ops.remove = sb_remove;
    fm.ops.mkdir = sb_mkdir;
    fm.ops.ctx = 0;

    ZD_CHECK_OK(zd_fm_open(&fm, "/ram/shell"));
    ZD_CHECK_EQ(zd_fm_visible_count(&fm), 1u);      /* dotfile filtered */
    ZD_CHECK(zd_fm_visible(&fm, 0) != 0);
    if (zd_fm_visible(&fm, 0)) {
        ZD_CHECK(strcmp(zd_fm_visible(&fm, 0)->name, "notes.txt") == 0);
        ZD_CHECK_EQ(zd_fm_visible(&fm, 0)->size, 64u);
    }
    ZD_CHECK_OK(zd_fm_select(&fm, 0));
    ZD_CHECK_EQ(zd_fm_selected_count(&fm), 1u);

    /* Permission gate: read-only actor cannot mutate, nothing is removed. */
    ZD_CHECK_EQ(zd_fm_remove(&fm, ZD_FM_PERM_READ, "notes.txt"), -1);
    ZD_CHECK_EQ(sb_removed, 0u);

    ZD_CHECK_OK(zd_fm_mkdir(&fm, ZD_FM_PERM_READ | ZD_FM_PERM_WRITE,
                            "inbox"));
    ZD_CHECK_EQ(sb_made, 1u);
    ZD_CHECK_EQ(zd_fm_visible_count(&fm), 2u);

    ZD_CHECK_OK(zd_fm_remove(&fm, ZD_FM_PERM_WRITE, "notes.txt"));
    ZD_CHECK_EQ(sb_removed, 1u);
    ZD_CHECK_EQ(zd_fm_visible_count(&fm), 1u);

    ZD_CHECK_OK(zd_fm_set_show_hidden(&fm, 1));
    ZD_CHECK_EQ(zd_fm_visible_count(&fm), 2u);

    /* Missing directory: VFS errno, failed state, EMPTY UI condition. */
    rc = zd_fm_open(&fm, "/ram/no-such-dir");
    ZD_CHECK_EQ(rc, -2);
    ZD_CHECK_EQ(fm.hist_state, 3);
    ZD_CHECK_EQ((int)zd_ui_condition_from_vfs_rc(rc), (int)ZD_UI_EMPTY);
    ZD_CHECK_OK(zd_fm_open(&fm, "/ram/shell"));
    ZD_CHECK_EQ(zd_fm_visible_count(&fm), 2u);
}

/* ---- step-9 platform activation replay ---------------------------------
 * The session activates the shell platform services on real kernel state
 * (monotonic clock, topology, live scanout).  This replays the same call
 * sequence against the real cores with injected fakes so a mis-ordered or
 * mis-stamped call fails here instead of only in guest CI. */

#define PA_PACING_NS 20000000ULL

struct pa_display {
    struct zeroos_display_info info;
    uint32_t presents;
};

static uint64_t pa_clock;

static int pa_query_info(void *context, struct zeroos_display_info *info) {
    struct pa_display *display = (struct pa_display *)context;
    *info = display->info;
    return 0;
}

static int pa_present(void *context, uint32_t x, uint32_t y, uint32_t width,
                      uint32_t height, uint32_t stride, const void *pixels) {
    struct pa_display *display = (struct pa_display *)context;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)stride;
    (void)pixels;
    ++display->presents;
    return 0;
}

static uint64_t pa_ticks(void *context) {
    (void)context;
    return pa_clock;
}

static struct zd_settings pa_settings;
static const struct zd_setting_def pa_scale_setting = {
    .key = "shell.scale_percent",
    .type = ZD_SETTING_INT,
    .scope = ZD_SCOPE_USER,
    .permissions_required = ZD_PERM_SETTINGS_USER,
    .default_value = 100,
    .min_value = 50,
    .max_value = 300,
    .group = "shell",
    .description = "Shell interface scaling"
};

static uint32_t pa_notify_posts;
static uint32_t pa_restarts;
static uint32_t pa_transitions;

static int pa_notify(void *context, const char *target) {
    (void)context;
    if (!target || !target[0])
        return -ZD_EINVAL;
    ++pa_notify_posts;
    return 0;
}

static int pa_set_setting(void *context, const char *target, int32_t value) {
    uint32_t changed = 0;
    (void)context;
    if (!target)
        return -ZD_EINVAL;
    return zd_settings_set_number(&pa_settings, target, (int64_t)value,
                                  ZD_PERM_SETTINGS_USER, &changed);
}

static const struct zd_automation_ops pa_auto_ops = {
    .notify = pa_notify,
    .set_setting = pa_set_setting,
    .callback = 0,
    .log = 0,
    .context = 0
};

static void pa_on_change(void *context, enum zd_lifecycle_state previous,
                         enum zd_lifecycle_state next,
                         enum zd_lifecycle_event cause) {
    (void)context;
    (void)previous;
    (void)next;
    (void)cause;
    ++pa_transitions;
}

static void pa_on_decision(void *context, uint32_t service_id,
                           enum zd_watchdog_decision decision,
                           uint64_t delay_ns) {
    (void)context;
    (void)service_id;
    (void)delay_ns;
    if (decision == ZD_WD_RESTART)
        ++pa_restarts;
}

static void pa_on_degraded(void *context) {
    (void)context;
}

static void test_session_platform_activation_sequence(void) {
    struct pa_display fake;
    struct zd_display_ops ops;
    struct zd_display_service service;
    struct zd_capabilities caps;
    struct zd_governor governor;
    struct zd_lifecycle lifecycle;
    struct zd_watchdog watchdog;
    struct zd_watchdog_result result;
    struct zd_automation automation;
    struct zd_automation_rule rule;
    struct zd_automation_audit_entry audit[4];
    uint8_t frame[16];
    uint32_t frame_budget = 0;
    uint32_t effects = 0;
    uint32_t free_percent;
    uint32_t consumed = 0;
    uint32_t audit_count;
    uint32_t fired;
    uint32_t rule_id = 0;
    uint32_t service_id = 0;
    int64_t setting_value = 0;
    enum zd_pressure_level pressure;

    /* 1. Present pacing on a stamped baseline: the session must stamp the
     * baseline present with the same clock it later compares against. */
    memset(&fake, 0, sizeof(fake));
    fake.info.width = 1024;
    fake.info.height = 768;
    fake.info.pitch = 4096;
    fake.info.bpp = 32;
    fake.info.byte_size = 4096ULL * 768ULL;
    fake.info.flags = ZEROOS_DISPLAY_FLAG_PRESENT;
    ops.query_info = pa_query_info;
    ops.present = pa_present;
    ops.ticks = pa_ticks;
    ops.context = &fake;
    pa_clock = 1000000000ULL;
    ZD_CHECK_EQ(zd_display_service_init(&service, &ops, 0), 0);
    ZD_CHECK_EQ(zd_display_service_attach(&service), 0);
    service.min_present_interval_ticks = PA_PACING_NS;
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){10, 10, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 4096, pa_clock),
                0);
    ZD_CHECK_EQ(zd_display_service_damage(&service,
                                          (struct zd_rect){10, 10, 4, 4}), 0);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 4096,
                                           pa_clock + 1000ULL),
                -ZD_EAGAIN);
    ZD_CHECK_EQ(service.stats.presents_refused_paced, 1U);
    ZD_CHECK_EQ(fake.presents, 1U);
    /* A zero-stamped baseline would look already elapsed: pin the guard. */
    ZD_CHECK(pa_clock + 1000ULL - service.last_present_tick <
             PA_PACING_NS);
    ZD_CHECK_EQ(zd_display_service_present(&service, frame, 4096,
                                           pa_clock + PA_PACING_NS + 1ULL),
                0);
    ZD_CHECK_EQ(fake.presents, 2U);

    /* 2. Governor over the same topology the guest reports (2 CPUs, 128 MB,
     * software compositing): tier floor, real budget, measured pressure. */
    memset(&caps, 0, sizeof(caps));
    caps.cpu_count = 2;
    caps.ram_mb = 128;
    caps.display_width = 1024;
    caps.display_height = 768;
    zd_governor_init(&governor, &caps);
    ZD_CHECK_EQ((int)governor.tier, (int)ZD_TIER_MINIMAL);
    zd_governor_effects_for_tier(governor.tier, &frame_budget, &effects);
    ZD_CHECK(frame_budget != 0U);
    free_percent = 90U; /* 90% of managed memory free, as the guest reports */
    pressure = free_percent < 10U ? ZD_PRESSURE_CRITICAL :
               free_percent < 25U ? ZD_PRESSURE_HIGH :
               free_percent < 50U ? ZD_PRESSURE_MODERATE :
               free_percent < 75U ? ZD_PRESSURE_LOW : ZD_PRESSURE_NONE;
    ZD_CHECK_EQ((int)pressure, (int)ZD_PRESSURE_NONE);
    ZD_CHECK_EQ(zd_governor_set_pressure(&governor, pressure, 0), 0);
    ZD_CHECK(governor.active_actions ==
             zd_governor_actions_for_level(pressure, 0));

    /* 3. Lifecycle: the session's transition order, the illegal event and
     * the heavy-work gate. */
    zd_lifecycle_init(&lifecycle);
    ZD_CHECK_EQ(zd_lifecycle_add_listener(&lifecycle, pa_on_change, 0), 0);
    ZD_CHECK_EQ(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_START, 1), 0);
    ZD_CHECK_EQ(zd_lifecycle_dispatch(&lifecycle,
                                      ZD_LIFECYCLE_CONTROLLERS_UP, 2), 0);
    ZD_CHECK_EQ(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_ACTIVATE, 3),
                0);
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), (int)ZD_LIFECYCLE_ACTIVE);
    ZD_CHECK(zd_lifecycle_allows_heavy_work(&lifecycle));
    ZD_CHECK_ERR(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_START, 4),
                 ZD_EINVAL);
    ZD_CHECK_EQ(zd_lifecycle_dispatch(&lifecycle, ZD_LIFECYCLE_PRESSURE, 5),
                0);
    ZD_CHECK(!zd_lifecycle_allows_heavy_work(&lifecycle));
    ZD_CHECK_EQ(zd_lifecycle_dispatch(&lifecycle,
                                      ZD_LIFECYCLE_PRESSURE_RELEASED, 6), 0);
    ZD_CHECK_EQ(zd_lifecycle_state(&lifecycle), (int)ZD_LIFECYCLE_ACTIVE);
    ZD_CHECK_EQ(pa_transitions, 5U);

    /* 4. Watchdog: healthy evaluation, failure -> restart decision,
     * recovery, healthy again. */
    zd_watchdog_init(&watchdog);
    ZD_CHECK_EQ(zd_watchdog_set_listener(&watchdog, pa_on_decision,
                                         pa_on_degraded, 0), 0);
    ZD_CHECK_EQ(zd_watchdog_register(&watchdog, "search", ZD_WD_ON_FAILURE,
                                     3, 0, PA_PACING_NS,
                                     PA_PACING_NS * 8, &service_id), 0);
    ZD_CHECK_EQ(zd_watchdog_set_heartbeat(&watchdog, service_id,
                                          PA_PACING_NS * 4), 0);
    ZD_CHECK_EQ(zd_watchdog_note_start(&watchdog, service_id, 100), 0);
    ZD_CHECK_EQ(zd_watchdog_note_heartbeat(&watchdog, service_id, 100), 0);
    ZD_CHECK_EQ(zd_watchdog_evaluate(&watchdog, service_id, 100, &result), 0);
    ZD_CHECK_EQ((int)result.decision, (int)ZD_WD_CONTINUE);
    ZD_CHECK_EQ(zd_watchdog_note_exit(&watchdog, service_id, 101, 1,
                                      &result), 0);
    ZD_CHECK_EQ((int)result.decision, (int)ZD_WD_RESTART);
    ZD_CHECK_EQ(pa_restarts, 1U);
    ZD_CHECK_EQ(zd_watchdog_note_start(&watchdog, service_id, 102), 0);
    ZD_CHECK_EQ(zd_watchdog_evaluate(&watchdog, service_id, 102, &result), 0);
    ZD_CHECK_EQ((int)result.decision, (int)ZD_WD_CONTINUE);

    /* 5. Automation: permission gate, real settings write, cooldown and
     * the audit trail the privacy centre reads. */
    zd_settings_init(&pa_settings);
    ZD_CHECK_EQ(zd_settings_register(&pa_settings, &pa_scale_setting), 0);
    pa_notify_posts = 0;
    ZD_CHECK_EQ(zd_automation_init(&automation, &pa_auto_ops), 0);
    memset(&rule, 0, sizeof(rule));
    rule.enabled = 1;
    rule.event = ZD_AUTO_EV_SERVICE_FAILED;
    rule.action = ZD_AUTO_ACT_NOTIFY;
    snprintf(rule.target, sizeof(rule.target), "shell");
    rule.permission = 0;
    ZD_CHECK_EQ(zd_automation_add(&automation, &rule, &rule_id), 0);
    ZD_CHECK_EQ(zd_automation_fire(&automation, ZD_AUTO_EV_SERVICE_FAILED,
                                   200), 0U);
    ZD_CHECK_EQ(pa_notify_posts, 0U);
    ZD_CHECK_EQ(automation.stats.denied_permission, 1U);
    ZD_CHECK_EQ(zd_automation_set_permission(&automation, rule_id, 1), 0);
    ZD_CHECK_EQ(zd_automation_fire(&automation, ZD_AUTO_EV_SERVICE_FAILED,
                                   201), 1U);
    ZD_CHECK_EQ(pa_notify_posts, 1U);

    rule.event = ZD_AUTO_EV_CALLER_TICK;
    rule.action = ZD_AUTO_ACT_SETTING;
    snprintf(rule.target, sizeof(rule.target), "shell.scale_percent");
    rule.setting_value = 150;
    rule.cooldown_ticks = PA_PACING_NS;
    rule.permission = 1;
    ZD_CHECK_EQ(zd_automation_add(&automation, &rule, &rule_id), 0);
    fired = zd_automation_fire(&automation, ZD_AUTO_EV_CALLER_TICK,
                               1000000000ULL);
    ZD_CHECK_EQ(fired, 1U);
    ZD_CHECK_EQ(zd_settings_get(&pa_settings, "shell.scale_percent",
                                &setting_value, 0, 0), 0);
    ZD_CHECK_EQ(setting_value, 150);
    fired = zd_automation_fire(&automation, ZD_AUTO_EV_CALLER_TICK,
                               1000000001ULL);
    ZD_CHECK_EQ(fired, 0U);
    ZD_CHECK_EQ(automation.stats.rate_limited, 1U);
    fired = zd_automation_fire(&automation, ZD_AUTO_EV_CALLER_TICK,
                               1000000000ULL + PA_PACING_NS + 1ULL);
    ZD_CHECK_EQ(fired, 1U);
    ZD_CHECK_EQ(automation.stats.fires, 3U);
    audit_count = zd_automation_drain_audit(&automation, audit,
                                            ZD_ARRAY_COUNT(audit),
                                            &consumed);
    ZD_CHECK(audit_count != 0U);
    ZD_CHECK_EQ(consumed, audit_count);
}

static void test_notes_provider(void) {
    struct zd_notes notes;
    struct zd_notes_provider np;
    struct zd_search_result results[8];
    struct zd_intent intent;
    uint32_t id_a = 0, id_b = 0, id_c = 0, got, cancel;
    char expect[32];

    zd_notes_init(&notes);
    zd_notes_provider_init(0, &notes, 0, 0, 0);          /* must not crash */
    zd_notes_provider_init(&np, 0, 0, 0, 0);
    ZD_CHECK(np.provider.name && strcmp(np.provider.name, "notes") == 0);
    ZD_CHECK_EQ(np.provider.kind_mask, 1u << ZD_SEARCH_DOCUMENT);
    ZD_CHECK_EQ(np.provider.available(&np), 0u);  /* no store bound */

    memset(&intent, 0, sizeof(intent));
    intent.kind = ZD_SEARCH_ANY;
    strcpy(intent.raw, "kernel");
    intent.token_count = 1;
    strcpy(intent.tokens[0], "kernel");

    /* No store: unavailable is counted, nothing is fabricated. */
    ZD_CHECK_EQ(np.provider.query(&np, &intent, results, 8, 0, 1), 0);
    ZD_CHECK_EQ(np.stats.unavailable, 1u);
    ZD_CHECK_EQ(np.stats.queries, 0u);

    /* Bad arguments are rejected. */
    ZD_CHECK_ERR(np.provider.query(0, &intent, results, 8, 0, 1), ZD_EINVAL);
    ZD_CHECK_ERR(np.provider.query(&np, 0, results, 8, 0, 1), ZD_EINVAL);
    ZD_CHECK_ERR(np.provider.query(&np, &intent, 0, 8, 0, 1), ZD_EINVAL);

    ZD_CHECK_OK(zd_notes_create(&notes, "kernel notes", "scheduler and vmm",
                                100, &id_a));
    ZD_CHECK_OK(zd_notes_create(&notes, "study plan",
                                "revise kernel scheduling", 200, &id_b));
    ZD_CHECK_OK(zd_notes_create(&notes, "shopping", "milk and bread",
                                300, &id_c));
    ZD_CHECK_OK(zd_notes_set_pinned(&notes, id_b, 1));
    zd_notes_provider_init(&np, &notes, zd_notes_search,
                           zd_notes_get, zd_notes_snippet);
    ZD_CHECK_EQ(np.provider.available(&np), 1u);

    /* Title and body both match, case-insensitively, via the note store's
     * own matcher; emission order is note order and ranking is by score. */
    got = (uint32_t)np.provider.query(&np, &intent, results, 8, 0, 1);
    ZD_CHECK_EQ(got, 2u);
    ZD_CHECK_EQ(np.stats.queries, 1u);
    ZD_CHECK_EQ(np.stats.matches, 2u);
    ZD_CHECK_EQ(results[0].document_id, (uint64_t)id_a);
    ZD_CHECK_EQ(results[0].kind, ZD_SEARCH_DOCUMENT);
    /* Title match: the label is the title itself. */
    ZD_CHECK(strcmp(results[0].label, "kernel notes") == 0);
    ZD_CHECK_EQ(results[0].score, 70u);
    ZD_CHECK_EQ(results[0].recency_score, 100u);
    ZD_CHECK_EQ(results[0].available, 1u);
    snprintf(expect, sizeof(expect), "note:%u", id_a);
    ZD_CHECK(strcmp(results[0].path, expect) == 0);
    /* A pinned note scores higher: that is a product rule, not a guess. */
    ZD_CHECK_EQ(results[1].document_id, (uint64_t)id_b);
    ZD_CHECK_EQ(results[1].score, 90u);
    ZD_CHECK(results[1].score > results[0].score);

    /* A second matching token strengthens the existing result instead of
     * duplicating it. */
    intent.token_count = 2;
    strcpy(intent.tokens[1], "study");
    got = (uint32_t)np.provider.query(&np, &intent, results, 8, 0, 1);
    ZD_CHECK_EQ(got, 2u);
    ZD_CHECK_EQ(results[0].document_id, (uint64_t)id_a);
    ZD_CHECK_EQ(results[0].score, 70u);
    ZD_CHECK_EQ(results[1].document_id, (uint64_t)id_b);
    ZD_CHECK_EQ(results[1].score, 105u);
    intent.token_count = 1;

    /* A kind filter that excludes documents yields nothing and does not
     * touch the counters. */
    {
        uint64_t before = np.stats.queries;
        intent.kind = ZD_SEARCH_FILE;
        ZD_CHECK_EQ(np.provider.query(&np, &intent, results, 8, 0, 1), 0);
        intent.kind = ZD_SEARCH_ANY;
        ZD_CHECK_EQ(np.stats.queries, before);
    }

    /* Capacity is respected and the drop is counted, not hidden. */
    got = (uint32_t)np.provider.query(&np, &intent, results, 1, 0, 1);
    ZD_CHECK_EQ(got, 1u);
    ZD_CHECK_EQ(np.stats.truncated, 1u);

    /* An empty query is not a query. */
    memset(&intent, 0, sizeof(intent));
    intent.kind = ZD_SEARCH_ANY;
    ZD_CHECK_EQ(np.provider.query(&np, &intent, results, 8, 0, 1), 0);

    /* A stale cancel token stops the provider before it reads the store. */
    intent.token_count = 1;
    strcpy(intent.tokens[0], "kernel");
    cancel = 99;
    ZD_CHECK_EQ(np.provider.query(&np, &intent, results, 8, &cancel, 1), 0);
    ZD_CHECK_EQ(np.stats.canceled, 1u);
    (void)id_c;
}

static void test_notes_provider_in_pipeline(void) {
    struct zd_search search;
    struct zd_notes notes;
    struct zd_notes_provider np;
    struct zd_search_result results[16];
    uint32_t count = 0, i, saw_doc = 0, id = 0;

    zd_notes_init(&notes);
    ZD_CHECK_OK(zd_notes_create(&notes, "kernel notes", "scheduler and vmm",
                                100, &id));
    zd_search_init(&search);
    zd_notes_provider_init(&np, &notes, zd_notes_search,
                           zd_notes_get, zd_notes_snippet);
    ZD_CHECK_OK(zd_search_add_provider(&search, &np.provider));

    /* doc: restricts the intent to documents. */
    ZD_CHECK_OK(zd_search_query(&search, "doc:kernel", results, 16, &count));
    for (i = 0; i < count; ++i)
        if (results[i].kind == ZD_SEARCH_DOCUMENT)
            ++saw_doc;
    ZD_CHECK_EQ(saw_doc, 1u);
    ZD_CHECK_EQ(results[0].document_id, (uint64_t)id);

    /* An unprefixed query reaches the same provider through ANY. The match
     * is in the body only, so the label carries the matched window -- the
     * ranking core scores label and path, and would drop it otherwise. */
    count = 0;
    saw_doc = 0;
    ZD_CHECK_OK(zd_search_query(&search, "vmm", results, 16, &count));
    for (i = 0; i < count; ++i)
        if (results[i].kind == ZD_SEARCH_DOCUMENT)
            ++saw_doc;
    ZD_CHECK_EQ(saw_doc, 1u);
    ZD_CHECK(results[0].score > 0u);
    ZD_CHECK(strstr(results[0].label, "vmm") != 0);
    ZD_CHECK(strncmp(results[0].label, "kernel notes ", 13) == 0);

    /* A miss produces no document results and no invented match. */
    count = 0;
    saw_doc = 0;
    ZD_CHECK_OK(zd_search_query(&search, "nothingmatchesthis", results, 16,
                                &count));
    for (i = 0; i < count; ++i)
        if (results[i].kind == ZD_SEARCH_DOCUMENT)
            ++saw_doc;
    ZD_CHECK_EQ(saw_doc, 0u);
}

void zd_test_providers_shell_suite(void) {
    printf(" suite: shell search providers\n");
    ZD_RUN(test_files_provider_roots);
    ZD_RUN(test_files_provider_query);
    ZD_RUN(test_files_provider_bounds);
    ZD_RUN(test_settings_provider);
    ZD_RUN(test_notes_provider);
    ZD_RUN(test_notes_provider_in_pipeline);
    ZD_RUN(test_shell_providers_in_pipeline);
    ZD_RUN(test_session_binding_sequence);
    ZD_RUN(test_session_platform_activation_sequence);
}
