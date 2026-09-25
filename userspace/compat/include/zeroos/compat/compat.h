#ifndef ZEROOS_COMPAT_H
#define ZEROOS_COMPAT_H
/* Windows compatibility core (Stage 5 part C) — the parts that must be
 * real before any loader work: explicit process lifecycle where
 * INSTALLED != RUNNING, path and registry translation with explicit
 * diagnostics for anything unsupported, and DLL bookkeeping with
 * refcounts and named failures.  Nothing here executes foreign code;
 * the loader (PE parsing, imports, syscalls translation) is a later
 * layer that will consume these contracts.
 *
 * Policy carried by the design:
 *  - INSTALLED != RUNNING: install registers files/registry only;
 *    start/stop move the runtime state; a stopped app holds ZERO
 *    resident resources (dormant when unused).
 *  - Every unsupported construct fails explicitly with a named
 *    diagnostic reason — never silently emulated or dropped.
 *  - Fixed capacity, zero heap, host-tested. */
#include <stdint.h>

#define ZCOMPAT_OK 0
#define ZCOMPAT_ERR (-1)
#define ZCOMPAT_BADARG (-2)
#define ZCOMPAT_NOSPACE (-3)
#define ZCOMPAT_NOTFOUND (-4)
#define ZCOMPAT_BADSTATE (-5)
#define ZCOMPAT_UNSUPPORTED (-6)

#define ZCOMPAT_MAX_APPS 16
#define ZCOMPAT_MAX_DLLS 24
#define ZCOMPAT_MAX_DRIVES 4
#define ZCOMPAT_MAX_HIVES 4
#define ZCOMPAT_NAME 48
#define ZCOMPAT_PATH 96

enum zcompat_app_state {
    ZCOMPAT_APP_INSTALLING = 0,
    ZCOMPAT_APP_INSTALLED = 1,
    ZCOMPAT_APP_RUNNING = 2,
    ZCOMPAT_APP_STOPPED = 3
};

struct zcompat_app {
    char name[ZCOMPAT_NAME];
    enum zcompat_app_state state;
    uint32_t install_files;
    uint32_t resident_bytes;  /* must be 0 unless RUNNING */
    uint32_t crashes;
    uint32_t restarts;
    uint8_t used;
};

/* Drive letters and UNC: explicit translation tables. */
struct zcompat_drive {
    char letter;               /* 'C' etc. */
    char mount[ZCOMPAT_PATH];  /* e.g. "/data/windows-c" */
    uint8_t used;
};

/* Registry: hive -> ZEROOS settings scope; value types validated. */
enum zcompat_reg_type {
    ZCOMPAT_REG_SZ = 1,
    ZCOMPAT_REG_DWORD = 2,
    ZCOMPAT_REG_BINARY = 3
};

struct zcompat_hive {
    char hive[16];             /* "HKLM", "HKCU", ... */
    char scope[ZCOMPAT_PATH];  /* settings scope prefix */
    uint8_t writable;
    uint8_t used;
};

struct zcompat_dll {
    char name[ZCOMPAT_NAME];
    uint32_t refcount;
    uint8_t loaded;
};

struct zcompat_stats {
    uint32_t installed;
    uint32_t started;
    uint32_t stopped;
    uint32_t crashed;
    uint32_t restarted;
    uint32_t path_translated;
    uint32_t path_rejected;
    uint32_t reg_reads;
    uint32_t reg_writes;
    uint32_t reg_rejected;
    uint32_t dll_loads;
    uint32_t dll_unloads;
    uint32_t dll_missing;
};

struct zcompat {
    struct zcompat_app apps[ZCOMPAT_MAX_APPS];
    struct zcompat_drive drives[ZCOMPAT_MAX_DRIVES];
    struct zcompat_hive hives[ZCOMPAT_MAX_HIVES];
    struct zcompat_dll dlls[ZCOMPAT_MAX_DLLS];
    struct zcompat_stats stats;
};

void zcompat_init(struct zcompat *c);

/* Drives/hives are configured by the host (mount policy), not guessed. */
int zcompat_add_drive(struct zcompat *c, char letter, const char *mount);
int zcompat_add_hive(struct zcompat *c, const char *hive,
                     const char *scope, int writable);

/* --- app lifecycle ---------------------------------------------------
 * install:  INSTALLING -> INSTALLED (registers `files` entries)
 * start:    INSTALLED/STOPPED -> RUNNING (allocates residency)
 * stop:     RUNNING -> STOPPED (releases ALL resident bytes)
 * crash:    RUNNING -> STOPPED, crashes++ (running again requires
 *           start; recovery never auto-restarts)
 * All moves are validated: running twice or stopping an installed-but-
 * never-started app returns ZCOMPAT_BADSTATE. */
int zcompat_install(struct zcompat *c, const char *name, uint32_t files);
int zcompat_start(struct zcompat *c, const char *name,
                  uint32_t resident_bytes);
int zcompat_stop(struct zcompat *c, const char *name);
int zcompat_crash(struct zcompat *c, const char *name);
int zcompat_uninstall(struct zcompat *c, const char *name);
int zcompat_state(const struct zcompat *c, const char *name,
                  enum zcompat_app_state *out);

/* --- path translation -------------------------------------------------
 * "C:\dir\file"  -> "<mount>/dir/file"  (backslashes folded, drive
 *                   letter matched case-insensitively)
 * "C:/dir/file"  accepted the same way
 * UNC ("\\srv\share"), NTFS alternate streams ("a.txt:stream") and
 * unknown drives fail with ZCOMPAT_UNSUPPORTED / ZCOMPAT_NOTFOUND and
 * bump the rejection counter. */
int zcompat_translate_path(struct zcompat *c, const char *win_path,
                           char *out, uint32_t out_cap);

/* --- registry ---------------------------------------------------------
 * Key form "HKLM\Software\Vendor\Key".  Unknown hive -> UNSUPPORTED;
 * read on missing key -> NOTFOUND; write on read-only hive -> BADSTATE
 * (and counts reg_rejected). */
int zcompat_reg_resolve(struct zcompat *c, const char *key,
                        char *out, uint32_t out_cap, int *writable_out);
int zcompat_reg_validate_value(const char *name,
                               enum zcompat_reg_type type,
                               const void *data, uint32_t len);

/* --- DLL bookkeeping ---------------------------------------------------
 * load: first use records the dll (refcount 1); later loads bump.
 * unload decrements; at 0 the dll is evicted.  Names are
 * case-insensitive. */
int zcompat_dll_load(struct zcompat *c, const char *name, uint32_t *handle);
int zcompat_dll_unload(struct zcompat *c, const char *name);
int zcompat_dll_refcount(const struct zcompat *c, const char *name,
                         uint32_t *out);

#endif
