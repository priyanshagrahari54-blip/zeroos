/* Built-in Universal Search providers (Stage 5 part A).
 *
 * commands + diagnostics are self-contained.  files + settings are the
 * shell-layer bindings: files walks the *same* directory source the file
 * manager uses (the session binds `zeroos_readdir`), and settings reads the
 * schema-driven settings registry.  Neither provider scans a disk: files
 * only lists the explicitly registered roots within a bounded depth and a
 * per-query directory budget, so `full_scans` stays 0. */
#ifndef ZEROOS_DESKTOP_PROVIDERS_H
#define ZEROOS_DESKTOP_PROVIDERS_H

#include <zeroos/desktop/search.h>
#include <zeroos/desktop/filemgr.h>
#include <zeroos/desktop/settings.h>

#define ZD_CMD_MAX 16
#define ZD_CMD_NAME 24
#define ZD_CMD_DESC 64

struct zd_command {
    char name[ZD_CMD_NAME];
    char description[ZD_CMD_DESC];
    int (*run)(void *ctx);
    void *ctx;
    uint8_t in_use;
};

struct zd_cmd_provider {
    struct zd_search_provider provider; /* embeds as a provider */
    struct zd_command commands[ZD_CMD_MAX];
    uint32_t count;
};

/* Diagnostics provider: one health line per index — wired by the
 * shell to real counters.  Returns 1 while lines exist, 0 after. */
typedef uint32_t (*zd_diag_line_fn)(void *ctx, uint32_t index,
                                    char *out, uint32_t cap);
struct zd_diag_provider {
    struct zd_search_provider provider;
    zd_diag_line_fn line;   /* produce one health line per call index */
    void *ctx;
};

int zd_commands_init(struct zd_cmd_provider *cp);
int zd_commands_register(struct zd_cmd_provider *cp, const char *name,
                         const char *desc,
                         int (*run)(void *), void *ctx);
int zd_commands_execute(struct zd_cmd_provider *cp, const char *name);

void zd_diagnostics_init(struct zd_diag_provider *dp,
                         zd_diag_line_fn line, void *ctx);

/* ---- files provider (directory source shared with the file manager) ---- */

#define ZD_FILES_MAX_ROOTS 8
#define ZD_FILES_DEFAULT_DEPTH 4
#define ZD_FILES_DIR_BUDGET 32   /* directory listings allowed per query */
#define ZD_FILES_BATCH 32        /* entries requested per listing */
#define ZD_FILES_QUEUE 32        /* bounded traversal work list */

struct zd_files_provider {
    struct zd_search_provider provider; /* embeds as a provider */
    zd_fm_source_fn source;             /* NULL => unavailable, never faked */
    void *ctx;
    char roots[ZD_FILES_MAX_ROOTS][ZD_FM_PATH];
    uint32_t root_count;
    uint32_t max_depth;                 /* 0 treated as 1 (roots only) */
    struct {
        uint64_t queries;
        uint64_t dirs_listed;
        uint64_t matches;
        uint64_t source_errors;         /* source errno, isolated per dir */
        uint64_t budget_exhausted;      /* stopped by the directory budget */
        uint64_t queue_dropped;         /* list full or path unaddressable */
        uint64_t truncated_dirs;        /* listing hit ZD_FILES_BATCH */
        uint64_t canceled;
        uint64_t full_scans;            /* must stay 0 */
    } stats;
};

int zd_files_provider_init(struct zd_files_provider *fp,
                           zd_fm_source_fn source, void *ctx);
/* Absolute, bounded, traversal-free roots only (-ZD_EINVAL otherwise). */
int zd_files_provider_add_root(struct zd_files_provider *fp,
                               const char *root);

/* ---- settings provider (schema-driven registry) ---- */

#define ZD_SETT_P_MAX_KEYS 16

struct zd_settings_provider {
    struct zd_search_provider provider; /* embeds as a provider */
    const struct zd_settings *settings;
    struct {
        uint64_t queries;
        uint64_t matches;
        uint64_t unavailable;           /* empty/absent registry */
        uint64_t canceled;
    } stats;
};

void zd_settings_provider_init(struct zd_settings_provider *sp,
                               const struct zd_settings *settings);

#endif /* ZEROOS_DESKTOP_PROVIDERS_H */
