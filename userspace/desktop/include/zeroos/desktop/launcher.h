/* ZEROOS launcher — app registry, query filtering, launch requests.
 * Host-testable: ranking, busy dedup, empty/loading/error states. */
#ifndef ZEROOS_DESKTOP_LAUNCHER_H
#define ZEROOS_DESKTOP_LAUNCHER_H

#include <stdint.h>

#define ZD_LAUNCHER_MAX_APPS 32
#define ZD_LAUNCHER_NAME 32
#define ZD_LAUNCHER_KEYWORDS 64
#define ZD_LAUNCHER_QUERY 48

enum zd_launch_state {
    ZD_LAUNCH_IDLE = 0,
    ZD_LAUNCH_PENDING,
    ZD_LAUNCH_RUNNING,
    ZD_LAUNCH_FAILED
};

struct zd_app {
    char name[ZD_LAUNCHER_NAME];
    char keywords[ZD_LAUNCHER_KEYWORDS]; /* space-separated, lowercase */
    int visible;                          /* 0 = hidden from results */
    int state;                            /* enum zd_launch_state */
    int fail_errno;                       /* why FAILED, 0 otherwise */
    int in_recents;                       /* launch recency slot (0=none) */
};

struct zd_launcher;

/* launch hook: starts the app, returns 0 (accepted) or <0 (errno). */
typedef int (*zd_launch_fn)(void *ctx, const char *name);

struct zd_launcher {
    struct zd_caps *caps;                      /* optional privilege gate */
    struct zd_app apps[ZD_LAUNCHER_MAX_APPS];
    int app_count;
    int recents_counter;                  /* monotonically increasing */
    zd_launch_fn launch;
    void *launch_ctx;
    struct {
        uint32_t adds, launches, launch_rejected, launch_failures,
                 queries, empty_queries, cap_denied;
    } stats;
};

int zd_launcher_init(struct zd_launcher *l, zd_launch_fn launch, void *ctx);
/* Optional: gate launches on ZD_SVC_LAUNCHER/ZD_CAP_LAUNCH_APPS. */
void zd_launcher_set_caps(struct zd_launcher *l, struct zd_caps *caps);
/* Register; duplicate name -> -17 (EEXIST), full -> -28 (ENOSPC). */
int zd_launcher_add(struct zd_launcher *l, const char *name,
                    const char *keywords, int visible);
struct zd_app *zd_launcher_find(struct zd_launcher *l, const char *name);

/* Query: empty -> recents (in_recents ordered, visible only);
 * otherwise visible apps whose name/keywords contain the query
 * (case-insensitive), recents-first among equals.  Results are
 * pointers into the registry, up to max results.  Returns count. */
int zd_launcher_query(struct zd_launcher *l, const char *query,
                      struct zd_app **out, int max_out);

/* Request a launch.  PENDING overwrites FAILED (retry).  RUNNING or
 * PENDING -> -EBUSY (dedup).  Hook refusal -> FAILED with errno. */
int zd_launcher_launch(struct zd_launcher *l, const char *name);
/* Shell reports the process result; running app -> recents bump. */
void zd_launcher_report(struct zd_launcher *l, const char *name,
                        int running, int fail_errno);

#endif /* ZEROOS_DESKTOP_LAUNCHER_H */
