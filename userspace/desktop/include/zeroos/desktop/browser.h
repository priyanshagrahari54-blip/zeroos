#ifndef ZEROOS_DESKTOP_BROWSER_H
#define ZEROOS_DESKTOP_BROWSER_H
/* Browser tab lifecycle (Stage 5 part E): ACTIVE -> IDLE -> FROZEN ->
 * DISCARDED with crash recovery.  Fixed-capacity, zero-heap, clock-
 * injected so hosts and tests drive time explicitly.
 *
 * Contract:
 *  - ACTIVE:   focused or recently used; document resident.
 *  - IDLE:     no focus for idle_ticks; document still resident.
 *  - FROZEN:   idle for freeze_ticks further; timers/scripts suspended
 *              by the browser engine, document still resident.
 *  - DISCARDED: idle for discard_ticks further; document released,
 *              metadata (id, counters) retained — reopen reloads.
 *  - CRASHED:  renderer died from any state; RELOAD recovers to ACTIVE
 *              with the document restored (or an explicit error the UI
 *              surface must show).  Crash counts are never erased.
 * Transitions are monotonic down the ladder except focus/reload/crash
 * jumps; illegal moves fail with the desktop error codes. */
#include <zeroos/desktop/common.h>

enum zd_tab_state {
    ZD_TAB_ACTIVE = 0,
    ZD_TAB_IDLE = 1,
    ZD_TAB_FROZEN = 2,
    ZD_TAB_DISCARDED = 3,
    ZD_TAB_CRASHED = 4
};

enum zd_tab_event {
    ZD_TAB_EV_FOCUS = 0,        /* user focused the tab */
    ZD_TAB_EV_BLUR = 1,         /* user left the tab */
    ZD_TAB_EV_TICK = 2,         /* clock advanced to `now` */
    ZD_TAB_EV_RENDERER_CRASH = 3,
    ZD_TAB_EV_RELOAD = 4,       /* reopen/reload request (UI or crash) */
    ZD_TAB_EV_DISCARD_NOW = 5   /* explicit memory pressure discard */
};

#define ZD_BROWSER_MAX_TABS 16u
#define ZD_BROWSER_IDLE_TICKS_DEFAULT 100u
#define ZD_BROWSER_FREEZE_TICKS_DEFAULT 300u
#define ZD_BROWSER_DISCARD_TICKS_DEFAULT 900u

struct zd_tab {
    uint32_t id;
    enum zd_tab_state state;
    uint64_t last_focus_tick;   /* last tick the tab was ACTIVE-focused */
    uint32_t content_bytes;     /* resident document size (0 when discarded) */
    uint32_t reloads;
    uint32_t crashes;
    uint8_t has_document;       /* 1 while a document is resident */
    uint8_t used;               /* slot allocated */
};

struct zd_browser_stats {
    uint32_t opened;
    uint32_t closed;
    uint32_t freezes;
    uint32_t discards;
    uint32_t crashes;
    uint32_t crash_recoveries;
    uint32_t reloads;
    uint32_t rejected_events;
};

struct zd_browser {
    struct zd_tab tabs[ZD_BROWSER_MAX_TABS];
    struct zd_browser_stats stats;
    uint32_t tab_count;
    uint32_t active_id;         /* 0 = none focused */
    uint32_t idle_ticks;        /* ACTIVE -> IDLE */
    uint32_t freeze_ticks;      /* extra idle -> FROZEN */
    uint32_t discard_ticks;     /* extra idle -> DISCARDED */
    uint64_t now;
};

/* Lifecycle counts: 0 or all three ladder values (ladder values must
 * satisfy idle <= freeze <= discard in *ticks from focus* — init folds
 * the stage deltas into absolute thresholds). */
void zd_browser_init(struct zd_browser *browser, uint32_t idle_ticks,
                     uint32_t freeze_ticks, uint32_t discard_ticks);

/* Open a tab with `content_bytes` of document; id is assigned (1..N).
 * Returns 0, or -ZD_ENOSPC at capacity / -ZD_EINVAL on bad args. */
int zd_browser_open(struct zd_browser *browser, uint32_t content_bytes,
                    uint32_t *id_out);

/* Close (unload) a tab: frees the slot and document.  Closing the
 * focused tab clears focus.  -ZD_ENOENT if unknown. */
int zd_browser_close(struct zd_browser *browser, uint32_t id);

/* Apply one lifecycle event at `now` (monotonic tick).  Returns 0 on
 * acceptance, -ZD_ENOENT unknown id, -ZD_EINVAL on impossible move
 * (e.g. reload of a healthy active tab), -ZD_ESTATE for events that
 * cannot apply in the current state. */
int zd_browser_event(struct zd_browser *browser, uint32_t id,
                     enum zd_tab_event event, uint64_t now);

/* Convenience: current state lookup; returns -ZD_ENOENT for unknown. */
int zd_browser_state(const struct zd_browser *browser, uint32_t id,
                     enum zd_tab_state *state_out);

#endif
