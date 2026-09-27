/* Built-in search providers: commands + diagnostics.  providers.h */
#include <zeroos/desktop/providers.h>

static uint32_t p_contains(const char *hay, const char *needle) {
    uint32_t h = 0, n = 0, i, j;
    if (!hay || !needle || !needle[0])
        return 0;
    while (hay[h])
        ++h;
    while (needle[n])
        ++n;
    if (n > h)
        return 0;
    for (i = 0; i + n <= h; ++i) {
        for (j = 0; j < n && hay[i + j] == needle[j]; ++j)
            ;
        if (j == n)
            return 1;
    }
    return 0;
}
static uint32_t p_startswith(const char *s, const char *pfx) {
    uint32_t i = 0;
    if (!s || !pfx)
        return 0;
    while (pfx[i]) {
        if (s[i] != pfx[i])
            return 0;
        ++i;
    }
    return 1;
}

/* ---- commands provider ---- */

static uint32_t p_commands_available(void *context) {
    const struct zd_cmd_provider *cp = context;
    return cp && cp->count > 0;
}

static int p_commands_query(void *context,
                            const struct zd_intent *intent,
                            struct zd_search_result *results,
                            uint32_t capacity,
                            volatile uint32_t *cancel_token,
                            uint32_t query_generation) {
    struct zd_cmd_provider *cp = context;
    const char *needle;
    uint32_t i, n = 0, score;
    if (!cp || !intent || !results)
        return -22;
    if (cancel_token && *cancel_token != query_generation)
        return 0; /* canceled: stop immediately */
    /* match against every token's raw text (usually one) */
    needle = intent->token_count ? intent->tokens[0] : intent->raw;
    for (i = 0; i < ZD_CMD_MAX && n < capacity; ++i) {
        struct zd_command *c = &cp->commands[i];
        if (!c->in_use || !p_contains(c->name, needle))
            continue;
        if (p_startswith(c->name, needle))
            score = 100;
        else
            score = 60;
        results[n].document_id = (uint64_t)(i + 1);
        results[n].kind = ZD_SEARCH_COMMAND;
        {
            uint32_t k = 0;
            while (c->name[k] && k + 1 < ZD_SEARCH_LABEL_CAP) {
                results[n].label[k] = c->name[k];
                ++k;
            }
            results[n].label[k] = 0;
        }
        results[n].path[0] = 'c';
        results[n].path[1] = 'm';
        results[n].path[2] = 'd';
        results[n].path[3] = ':';
        {
            uint32_t k = 0;
            while (c->name[k] && k + 5 < ZD_SEARCH_PATH_CAP) {
                results[n].path[4 + k] = c->name[k];
                ++k;
            }
            results[n].path[4 + k] = 0;
        }
        results[n].score = score;
        results[n].provider_priority = 0;
        results[n].recency_score = 0;
        results[n].use_count = 0;
        results[n].available = 1;
        ++n;
    }
    return (int)n;
}

int zd_commands_init(struct zd_cmd_provider *cp) {
    uint32_t i;
    if (!cp)
        return -22;
    for (i = 0; i < ZD_CMD_MAX; ++i)
        cp->commands[i].in_use = 0;
    cp->count = 0;
    cp->provider.name = "commands";
    cp->provider.kind_mask = 1u << ZD_SEARCH_COMMAND;
    cp->provider.priority = 50;
    cp->provider.available = p_commands_available;
    cp->provider.query = p_commands_query;
    cp->provider.context = cp;
    cp->provider.registered = 0;
    return 0;
}

int zd_commands_register(struct zd_cmd_provider *cp, const char *name,
                         const char *desc,
                         int (*run)(void *), void *ctx) {
    uint32_t i;
    if (!cp || !name || !name[0] || !run)
        return -22;
    for (i = 0; i < ZD_CMD_MAX; ++i) {
        if (cp->commands[i].in_use) {
            uint32_t k = 0, same = 1;
            while (cp->commands[i].name[k] && name[k]) {
                if (cp->commands[i].name[k] != name[k]) {
                    same = 0;
                    break;
                }
                ++k;
            }
            if (same && cp->commands[i].name[k] == 0 &&
                name[k] == 0)
                return -17; /* EEXIST duplicate name */
        }
    }
    if (cp->count >= ZD_CMD_MAX)
        return -28;
    for (i = 0; i < ZD_CMD_MAX; ++i) {
        struct zd_command *c;
        if (cp->commands[i].in_use)
            continue;
        c = &cp->commands[i];
        {
            uint32_t k = 0;
            while (name[k] && k + 1 < ZD_CMD_NAME) {
                c->name[k] = name[k];
                ++k;
            }
            c->name[k] = 0;
        }
        c->description[0] = 0;
        if (desc) {
            uint32_t k = 0;
            while (desc[k] && k + 1 < ZD_CMD_DESC) {
                c->description[k] = desc[k];
                ++k;
            }
            c->description[k] = 0;
        }
        c->run = run;
        c->ctx = ctx;
        c->in_use = 1;
        cp->count++;
        return 0;
    }
    return -28;
}

int zd_commands_execute(struct zd_cmd_provider *cp, const char *name) {
    uint32_t i;
    if (!cp || !name || !name[0])
        return -22;
    for (i = 0; i < ZD_CMD_MAX; ++i) {
        struct zd_command *c = &cp->commands[i];
        uint32_t k = 0, same = 1;
        if (!c->in_use)
            continue;
        while (c->name[k] && name[k]) {
            if (c->name[k] != name[k]) {
                same = 0;
                break;
            }
            ++k;
        }
        if (same && c->name[k] == 0 && name[k] == 0)
            return c->run(c->ctx);
    }
    return -2;
}

/* ---- diagnostics provider ---- */

static uint32_t p_diag_available(void *context) {
    const struct zd_diag_provider *dp = context;
    return dp && dp->line;
}

#define P_DIAG_MAX_LINES 8

static int p_diag_query(void *context, const struct zd_intent *intent,
                        struct zd_search_result *results,
                        uint32_t capacity,
                        volatile uint32_t *cancel_token,
                        uint32_t query_generation) {
    struct zd_diag_provider *dp = context;
    uint32_t idx = 0, n = 0;
    char line[96];
    if (!dp || !intent || !results || !dp->line)
        return -22;
    if (cancel_token && *cancel_token != query_generation)
        return 0;
    while (idx < P_DIAG_MAX_LINES && n < capacity) {
        if (!dp->line(dp->ctx, idx, line, sizeof(line)))
            break;
        {
            uint32_t k = 0;
            while (line[k] && k + 1 < ZD_SEARCH_LABEL_CAP) {
                results[n].label[k] = line[k];
                ++k;
            }
            results[n].label[k] = 0;
        }
        results[n].document_id = 0x60000000ull + idx;
        results[n].kind = ZD_SEARCH_DIAGNOSTIC;
        results[n].path[0] = 0;
        results[n].score = 80 - idx * 10;
        results[n].provider_priority = 0;
        results[n].recency_score = 0;
        results[n].use_count = 0;
        results[n].available = 1;
        ++n;
        ++idx;
    }
    return (int)n;
}

void zd_diagnostics_init(struct zd_diag_provider *dp,
                         zd_diag_line_fn line, void *ctx) {
    if (!dp)
        return;
    dp->line = line;
    dp->ctx = ctx;
    dp->provider.name = "diagnostics";
    dp->provider.kind_mask = 1u << ZD_SEARCH_DIAGNOSTIC;
    dp->provider.priority = 40;
    dp->provider.available = p_diag_available;
    dp->provider.query = p_diag_query;
    dp->provider.context = dp;
    dp->provider.registered = 0;
}

/* ---- files provider -------------------------------------------------- */

/* FNV-1a over the full path: a stable document id for dedup/ranking. */
static uint64_t p_hash_path(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    uint32_t i = 0;
    if (!s)
        return 0;
    while (s[i]) {
        h ^= (uint64_t)(uint8_t)s[i];
        h *= 1099511628211ULL;
        ++i;
    }
    return h;
}

static uint32_t p_strlen(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}

static void p_copy_capped(char *dst, uint32_t cap, const char *src) {
    uint32_t k = 0;
    if (!dst || !cap)
        return;
    while (src && src[k] && k + 1 < cap) {
        dst[k] = src[k];
        ++k;
    }
    dst[k] = 0;
}

/* Absolute, bounded, no ".." traversal — mirrors the file-manager path
 * discipline so a hostile root cannot escape its subtree. */
static int p_root_ok(const char *path) {
    uint32_t i, n;
    if (!path || path[0] != '/')
        return 0;
    n = p_strlen(path);
    if (n == 0 || n >= ZD_FM_PATH)
        return 0;
    for (i = 0; i + 1 < n; ++i)
        if (path[i] == '.' && path[i + 1] == '.')
            return 0;
    return 1;
}

/* Builds "dir/name" into out; -ZD_EOVERFLOW when it would not fit (a
 * truncated path would silently point at the wrong file). */
static int p_join(char *out, uint32_t cap, const char *dir,
                  const char *name) {
    uint32_t dl = p_strlen(dir), nl = p_strlen(name), need;
    if (!out || !dir || !name || !dl || !nl || dl >= cap)
        return -ZD_EINVAL;
    need = dl + nl + 2U;               /* separator + NUL */
    if (dir[dl - 1] == '/')
        --need;
    if (need > cap)
        return -ZD_EOVERFLOW;
    p_copy_capped(out, cap, dir);
    {
        uint32_t k = p_strlen(out);
        if (k && out[k - 1] != '/' && k + 1 < cap) {
            out[k] = '/';
            out[k + 1] = 0;
            ++k;
        }
        p_copy_capped(out + k, cap - k, name);
    }
    return 0;
}

static uint32_t p_files_available(void *context) {
    const struct zd_files_provider *fp = context;
    return fp && fp->source && fp->root_count > 0;
}

static int p_files_query(void *context, const struct zd_intent *intent,
                         struct zd_search_result *results, uint32_t capacity,
                         volatile uint32_t *cancel_token,
                         uint32_t query_generation) {
    struct zd_files_provider *fp = context;
    struct zd_files_work {
        char path[ZD_FM_PATH];
        uint32_t depth;
    } queue[ZD_FILES_QUEUE];
    struct zd_fm_entry batch[ZD_FILES_BATCH];
    /* Ring-buffer work list: consumed slots are reused, so the traversal
     * memory is bounded by ZD_FILES_QUEUE regardless of tree shape. */
    uint32_t head = 0, tail = 0, queued = 0, i, n = 0, depth_limit;
    uint32_t want_file = 1, want_folder = 1;
    const char *needle;

    if (!fp || !intent || !results || !fp->source)
        return -ZD_EINVAL;
    if (!fp->root_count || capacity == 0)
        return 0;
    if (intent->kind == ZD_SEARCH_FILE) {
        want_folder = 0;
    } else if (intent->kind == ZD_SEARCH_FOLDER) {
        want_file = 0;
    } else if (intent->kind != ZD_SEARCH_ANY) {
        return 0;                      /* not this provider's kind */
    }
    needle = intent->token_count ? intent->tokens[0] : intent->raw;
    if (!needle[0])
        return 0;
    ++fp->stats.queries;
    depth_limit = fp->max_depth ? fp->max_depth : 1U;

    for (i = 0; i < fp->root_count && queued < ZD_FILES_QUEUE; ++i) {
        p_copy_capped(queue[tail].path, sizeof(queue[tail].path),
                      fp->roots[i]);
        queue[tail].depth = 0;
        tail = (tail + 1U) % ZD_FILES_QUEUE;
        ++queued;
    }

    while (queued && n < capacity) {
        const struct zd_files_work *dir = &queue[head];
        char child[ZD_FM_PATH];
        uint32_t count = 0;
        int rc;

        if (cancel_token && *cancel_token != query_generation) {
            ++fp->stats.canceled;
            break;
        }
        if (fp->stats.dirs_listed >= ZD_FILES_DIR_BUDGET) {
            ++fp->stats.budget_exhausted;
            break;
        }
        rc = fp->source(fp->ctx, dir->path, batch, ZD_FILES_BATCH, &count);
        ++fp->stats.dirs_listed;
        head = (head + 1U) % ZD_FILES_QUEUE;
        --queued;
        if (rc != 0) {
            /* One unreadable directory never fails the whole query. */
            ++fp->stats.source_errors;
            continue;
        }
        if (count >= ZD_FILES_BATCH)
            ++fp->stats.truncated_dirs;
        for (i = 0; i < count && n < capacity; ++i) {
            const struct zd_fm_entry *e = &batch[i];
            uint32_t is_dir = (e->flags & ZD_FM_DIR) != 0;

            if (p_join(child, sizeof(child), dir->path, e->name) != 0) {
                /* Name too long to address: report it, never guess. */
                ++fp->stats.queue_dropped;
                continue;
            }
            if (is_dir && dir->depth + 1U < depth_limit) {
                if (queued < ZD_FILES_QUEUE) {
                    p_copy_capped(queue[tail].path,
                                  sizeof(queue[tail].path), child);
                    queue[tail].depth = dir->depth + 1U;
                    tail = (tail + 1U) % ZD_FILES_QUEUE;
                    ++queued;
                } else {
                    ++fp->stats.queue_dropped;
                }
            }
            if ((is_dir && !want_folder) || (!is_dir && !want_file))
                continue;
            if (!p_contains(e->name, needle))
                continue;
            results[n].document_id = p_hash_path(child);
            results[n].kind = is_dir ? ZD_SEARCH_FOLDER : ZD_SEARCH_FILE;
            p_copy_capped(results[n].label, ZD_SEARCH_LABEL_CAP, e->name);
            p_copy_capped(results[n].path, ZD_SEARCH_PATH_CAP, child);
            results[n].score = p_startswith(e->name, needle) ? 100U : 60U;
            results[n].provider_priority = 0;
            results[n].recency_score = 0;
            results[n].use_count = 0;
            results[n].available = 1;
            ++n;
        }
    }
    fp->stats.matches += n;
    return (int)n;
}

int zd_files_provider_init(struct zd_files_provider *fp,
                           zd_fm_source_fn source, void *ctx) {
    uint32_t i;
    if (!fp)
        return -ZD_EINVAL;
    for (i = 0; i < ZD_FILES_MAX_ROOTS; ++i)
        fp->roots[i][0] = 0;
    fp->root_count = 0;
    fp->max_depth = ZD_FILES_DEFAULT_DEPTH;
    fp->stats.queries = 0;
    fp->stats.dirs_listed = 0;
    fp->stats.matches = 0;
    fp->stats.source_errors = 0;
    fp->stats.budget_exhausted = 0;
    fp->stats.queue_dropped = 0;
    fp->stats.truncated_dirs = 0;
    fp->stats.canceled = 0;
    fp->stats.full_scans = 0;          /* never a whole-tree rescan */
    fp->source = source;
    fp->ctx = ctx;
    fp->provider.name = "files";
    fp->provider.kind_mask = (1u << ZD_SEARCH_FILE) | (1u << ZD_SEARCH_FOLDER);
    fp->provider.priority = 70;
    fp->provider.available = p_files_available;
    fp->provider.query = p_files_query;
    fp->provider.context = fp;
    fp->provider.registered = 0;
    return 0;
}

int zd_files_provider_add_root(struct zd_files_provider *fp,
                               const char *root) {
    uint32_t i;
    if (!fp)
        return -ZD_EINVAL;
    if (!p_root_ok(root))
        return -ZD_EINVAL;
    for (i = 0; i < fp->root_count; ++i) {
        uint32_t k = 0, same = 1;
        while (fp->roots[i][k] && root[k]) {
            if (fp->roots[i][k] != root[k]) {
                same = 0;
                break;
            }
            ++k;
        }
        if (same && fp->roots[i][k] == 0 && root[k] == 0)
            return 0;                  /* duplicate root: idempotent */
    }
    if (fp->root_count >= ZD_FILES_MAX_ROOTS)
        return -ZD_ENOSPC;
    p_copy_capped(fp->roots[fp->root_count], ZD_FM_PATH, root);
    ++fp->root_count;
    return 0;
}

/* ---- settings provider ----------------------------------------------- */

static uint32_t p_settings_available(void *context) {
    const struct zd_settings_provider *sp = context;
    return sp && sp->settings && sp->settings->count > 0;
}

static int p_settings_query(void *context, const struct zd_intent *intent,
                            struct zd_search_result *results,
                            uint32_t capacity,
                            volatile uint32_t *cancel_token,
                            uint32_t query_generation) {
    struct zd_settings_provider *sp = context;
    const char *keys[ZD_SETT_P_MAX_KEYS];
    uint32_t found, i, n = 0;
    const char *needle;
    char prefixed[ZD_SEARCH_PATH_CAP];

    if (!sp || !intent || !results)
        return -ZD_EINVAL;
    if (intent->kind != ZD_SEARCH_ANY && intent->kind != ZD_SEARCH_SETTING)
        return 0;
    if (!sp->settings || sp->settings->count == 0 || capacity == 0) {
        ++sp->stats.unavailable;
        return 0;
    }
    if (cancel_token && *cancel_token != query_generation) {
        ++sp->stats.canceled;
        return 0;
    }
    needle = intent->token_count ? intent->tokens[0] : intent->raw;
    if (!needle[0])
        return 0;
    ++sp->stats.queries;
    found = zd_settings_search(sp->settings, needle, keys,
                               ZD_SETT_P_MAX_KEYS);
    for (i = 0; i < found && n < capacity; ++i) {
        const struct zd_setting_def *def =
            zd_settings_def(sp->settings, keys[i]);
        results[n].document_id = p_hash_path(keys[i]);
        results[n].kind = ZD_SEARCH_SETTING;
        p_copy_capped(results[n].label, ZD_SEARCH_LABEL_CAP, keys[i]);
        prefixed[0] = 's';
        prefixed[1] = 'e';
        prefixed[2] = 't';
        prefixed[3] = ':';
        p_copy_capped(prefixed + 4, ZD_SEARCH_PATH_CAP - 4U, keys[i]);
        p_copy_capped(results[n].path, ZD_SEARCH_PATH_CAP, prefixed);
        results[n].score = p_startswith(keys[i], needle) ? 100U : 70U;
        results[n].provider_priority = 0;
        results[n].recency_score = 0;
        results[n].use_count = 0;
        /* A setting whose dependency is unmet is still reported, but not
         * as directly actionable. */
        results[n].available =
            (def && zd_settings_dependencies_met(sp->settings, keys[i]))
                ? 1U
                : 0U;
        ++n;
    }
    sp->stats.matches += n;
    return (int)n;
}

void zd_settings_provider_init(struct zd_settings_provider *sp,
                               const struct zd_settings *settings) {
    if (!sp)
        return;
    sp->settings = settings;
    sp->stats.queries = 0;
    sp->stats.matches = 0;
    sp->stats.unavailable = 0;
    sp->stats.canceled = 0;
    sp->provider.name = "settings";
    sp->provider.kind_mask = 1u << ZD_SEARCH_SETTING;
    sp->provider.priority = 60;
    sp->provider.available = p_settings_available;
    sp->provider.query = p_settings_query;
    sp->provider.context = sp;
    sp->provider.registered = 0;
}

/* ---- notes content provider ------------------------------------------ */

static uint32_t p_notes_available(void *context) {
    const struct zd_notes_provider *np = context;
    return np && np->notes && np->search && np->get && np->notes->count > 0;
}

static int p_contains_ci(const char *hay, const char *needle) {
    uint32_t i, k, nl = 0;
    if (!hay || !needle || !needle[0])
        return 0;
    while (needle[nl])
        ++nl;
    for (i = 0; hay[i]; ++i) {
        for (k = 0; k < nl; ++k) {
            char a = hay[i + k];
            char b = needle[k];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b || !a)
                break;
        }
        if (k == nl)
            return 1;
    }
    return 0;
}

static void p_utoa(uint64_t value, char *out, uint32_t cap) {
    char scratch[24];
    uint32_t n = 0, i;
    if (!out || !cap)
        return;
    if (value == 0)
        scratch[n++] = '0';
    while (value && n < sizeof(scratch)) {
        scratch[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    for (i = 0; i < n && i + 1 < cap; ++i)
        out[i] = scratch[n - 1U - i];
    out[i < cap ? i : cap - 1U] = 0;
}

static int p_notes_query(void *context, const struct zd_intent *intent,
                         struct zd_search_result *results, uint32_t capacity,
                         volatile uint32_t *cancel_token,
                         uint32_t query_generation) {
    struct zd_notes_provider *np = context;
    uint32_t ids[ZD_NOTES_P_WINDOW];
    uint32_t emitted[ZD_NOTES_P_WINDOW];
    uint32_t emitted_count = 0;
    uint32_t token_count, t, n = 0;
    char path[ZD_SEARCH_PATH_CAP];

    if (!np || !intent || !results)
        return -ZD_EINVAL;
    if (intent->kind != ZD_SEARCH_ANY && intent->kind != ZD_SEARCH_DOCUMENT)
        return 0;
    if (!np->notes || !np->search || !np->get || np->notes->count == 0 ||
        capacity == 0) {
        ++np->stats.unavailable;
        return 0;
    }
    if (cancel_token && *cancel_token != query_generation) {
        ++np->stats.canceled;
        return 0;
    }
    token_count = intent->token_count ? intent->token_count : 1U;
    if (token_count == 1 && !intent->raw[0])
        return 0;
    ++np->stats.queries;
    for (t = 0; t < token_count; ++t) {
        const char *needle =
            intent->token_count ? intent->tokens[t] : intent->raw;
        uint32_t found, i;
        int hits;
        if (!needle[0])
            continue;
        if (cancel_token && *cancel_token != query_generation) {
            ++np->stats.canceled;
            break;
        }
        /* The note store's own case-insensitive matcher decides what
         * matches; this provider only turns hits into search results. */
        hits = np->search(np->notes, needle, ids, ZD_NOTES_P_WINDOW);
        if (hits < 0)
            continue;
        found = (uint32_t)hits;
        if (found >= ZD_NOTES_P_WINDOW)
            ++np->stats.capped_hits;
        for (i = 0; i < found; ++i) {
            const struct zd_note *note = np->get(np->notes, ids[i]);
            uint32_t k, already = 0;
            if (!note)
                continue;
            for (k = 0; k < emitted_count; ++k) {
                if (emitted[k] != ids[i])
                    continue;
                already = 1;
                break;
            }
            if (already) {
                /* A second matching token strengthens the existing result
                 * instead of duplicating it. */
                for (k = 0; k < n; ++k) {
                    if (results[k].document_id == note->id) {
                        results[k].score += 15U;
                        break;
                    }
                }
                continue;
            }
            if (n >= capacity) {
                ++np->stats.truncated;
                continue;
            }
            if (emitted_count < ZD_NOTES_P_WINDOW)
                emitted[emitted_count++] = ids[i];
            results[n].document_id = note->id;
            results[n].kind = ZD_SEARCH_DOCUMENT;
            /* The ranking core scores the label and the path, so a
             * body-only match has to carry the matched text into the
             * label or it would be dropped as unscored: bounded title,
             * then the body window that actually matched. */
            if (p_contains_ci(note->title, needle)) {
                p_copy_capped(results[n].label, ZD_SEARCH_LABEL_CAP,
                              note->title);
            } else {
                uint32_t k = 0;
                while (k < 12U && note->title[k]) {
                    results[n].label[k] = note->title[k];
                    ++k;
                }
                if (k < ZD_SEARCH_LABEL_CAP - 1U)
                    results[n].label[k++] = ' ';
                results[n].label[k] = 0;
                if (!np->snippet ||
                    np->snippet(note, needle, results[n].label + k,
                                ZD_SEARCH_LABEL_CAP - k) < 0)
                    p_copy_capped(results[n].label, ZD_SEARCH_LABEL_CAP,
                                  note->title);
            }
            p_copy_capped(path, ZD_SEARCH_PATH_CAP, "note:");
            p_utoa(note->id, path + 5, ZD_SEARCH_PATH_CAP - 5U);
            p_copy_capped(results[n].path, ZD_SEARCH_PATH_CAP, path);
            /* Pinned notes rank first; that is a product rule, not a
             * guess about relevance. */
            results[n].score = note->pinned ? 90U : 70U;
            results[n].provider_priority = 0;
            results[n].recency_score =
                note->mtime > 0 ? (uint64_t)note->mtime : 0U;
            results[n].use_count = 0;
            results[n].available = 1U;
            ++n;
        }
    }
    np->stats.matches += n;
    return (int)n;
}

void zd_notes_provider_init(struct zd_notes_provider *np,
                            const struct zd_notes *notes,
                            zd_notes_search_fn search, zd_notes_get_fn get,
                            zd_notes_snippet_fn snippet) {
    if (!np)
        return;
    np->notes = notes;
    np->search = search;
    np->get = get;
    np->snippet = snippet;
    np->stats.queries = 0;
    np->stats.matches = 0;
    np->stats.unavailable = 0;
    np->stats.canceled = 0;
    np->stats.truncated = 0;
    np->stats.capped_hits = 0;
    np->provider.name = "notes";
    np->provider.kind_mask = 1u << ZD_SEARCH_DOCUMENT;
    np->provider.priority = 55;
    np->provider.available = p_notes_available;
    np->provider.query = p_notes_query;
    np->provider.context = np;
    np->provider.registered = 0;
}
