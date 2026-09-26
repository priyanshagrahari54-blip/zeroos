#ifndef ZEROOS_DESKTOP_FILEMGR_H
#define ZEROOS_DESKTOP_FILEMGR_H

/* File manager core (Stage 5 shell surface): directory listing with
 * injected source (the session binds `zeroos_readdir`; tests bind an
 * in-memory source — never faked as a real filesystem), sorting,
 * hidden-file filtering, multi-selection, bounded path history with
 * back/forward, and permission-gated file operations.
 *
 * The source callback fills entries for ONE directory; it returns 0
 * or a negative errno.  Capacity truncation is reported, not hidden:
 * `truncated` is set whenever the source fills the capacity (it may
 * have had more entries — sources never exceed cap).
 */

#include <stdint.h>

#define ZD_FM_MAX 64
#define ZD_FM_NAME 48
#define ZD_FM_PATH 96
#define ZD_FM_HISTORY 16

/* entry kind flags */
#define ZD_FM_DIR    (1u << 0)
#define ZD_FM_HIDDEN (1u << 1)

struct zd_fm_entry {
    char name[ZD_FM_NAME];
    uint64_t size;
    int64_t mtime;
    uint32_t flags;          /* ZD_FM_* */
};

/* source: write up to `cap` entries for `path` into out; *out_n set
 * to the number produced (must not exceed cap).  Returns 0 or
 * -errno. */
typedef int (*zd_fm_source_fn)(void *ctx, const char *path,
                               struct zd_fm_entry *out, uint32_t cap,
                               uint32_t *out_n);

/* operation gates: actor must hold this bit for the operation */
#define ZD_FM_PERM_READ  (1u << 0)
#define ZD_FM_PERM_WRITE (1u << 1)

/* ops injected by the session over the VFS; NULL ops = read-only */
struct zd_fm_ops {
    int (*remove)(void *ctx, const char *path);
    int (*mkdir)(void *ctx, const char *path);
    void *ctx;
};

enum zd_fm_sort {
    ZD_FM_SORT_NAME = 0,
    ZD_FM_SORT_SIZE,
    ZD_FM_SORT_MTIME
};

struct zd_fm {
    char path[ZD_FM_PATH];             /* current directory */
    struct zd_fm_entry entries[ZD_FM_MAX];
    uint32_t count;
    uint8_t truncated;                 /* source hit capacity */
    uint64_t selected[(ZD_FM_MAX + 63) / 64];
    uint32_t sort_mode;                /* enum zd_fm_sort */
    uint32_t sort_desc;
    uint32_t show_hidden;
    /* path history (back/forward), mirrors nav discipline */
    char history[ZD_FM_HISTORY][ZD_FM_PATH];
    uint32_t hist_count, hist_pos;
    int hist_state;                    /* 0 empty, 1 loading, 2 ok, 3 failed */
    zd_fm_source_fn source;
    void *source_ctx;
    struct zd_fm_ops ops;
    struct {
        uint32_t refreshes, source_errors, truncations, rejected,
                 navigations, backs, forwards, history_dropped,
                 selects, ops_perm_denied, removed, mkdirs, op_errors;
    } stats;
};

void zd_fm_init(struct zd_fm *fm, zd_fm_source_fn source, void *ctx);
/* cd to path (absolute required: leading '/'), re-list.  Source
 * errno propagates (hist_state=failed); overlong paths rejected. */
int zd_fm_open(struct zd_fm *fm, const char *path);
/* Re-list the current path. */
int zd_fm_refresh(struct zd_fm *fm);
int zd_fm_back(struct zd_fm *fm);
int zd_fm_forward(struct zd_fm *fm);
/* Entries after hidden filter + sort, indexed 0..visible_count-1. */
uint32_t zd_fm_visible_count(const struct zd_fm *fm);
const struct zd_fm_entry *zd_fm_visible(const struct zd_fm *fm,
                                        uint32_t idx);
int zd_fm_set_sort(struct zd_fm *fm, uint32_t mode, uint32_t desc);
int zd_fm_set_show_hidden(struct zd_fm *fm, uint32_t show);
/* Selection over VISIBLE indices; -2 out of range. */
int zd_fm_select(struct zd_fm *fm, uint32_t idx);
int zd_fm_toggle(struct zd_fm *fm, uint32_t idx);
void zd_fm_clear_selection(struct zd_fm *fm);
uint32_t zd_fm_selected_count(const struct zd_fm *fm);
/* remove/mkdir under `name` in current dir: -1 permission denied
 * (counted), -22 bad args/no ops, source ops' errno otherwise. */
int zd_fm_remove(struct zd_fm *fm, uint32_t actor_perms,
                 const char *name);
int zd_fm_mkdir(struct zd_fm *fm, uint32_t actor_perms,
                const char *name);

#endif /* ZEROOS_DESKTOP_FILEMGR_H */
