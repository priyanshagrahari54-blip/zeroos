/* ZERO Bar — ZEROOS shell status/controls strip.
 * Host-testable core: applet layout, click routing, quick toggles,
 * notification badges, DPI scaling and keyboard focus.  Rendering and
 * event integration live in the shell service. */
#ifndef ZEROOS_DESKTOP_BAR_H
#define ZEROOS_DESKTOP_BAR_H

#include <stdint.h>

enum zd_bar_applet {
    ZD_BAR_APPLET_MENU = 0,   /* launcher button */
    ZD_BAR_APPLET_WORKSPACES, /* workspace indicator */
    ZD_BAR_APPLET_TITLE,      /* focused window title (flexible) */
    ZD_BAR_APPLET_NOTIFICATIONS,
    ZD_BAR_APPLET_CLOCK,
    ZD_BAR_APPLET_QUICK,      /* quick controls cluster */
    ZD_BAR_APPLET_COUNT
};

enum zd_bar_toggle {
    ZD_BAR_TOGGLE_WIFI = 0,
    ZD_BAR_TOGGLE_BLUETOOTH,
    ZD_BAR_TOGGLE_DND,
    ZD_BAR_TOGGLE_BRIGHTNESS,
    ZD_BAR_TOGGLE_COUNT
};

enum zd_bar_action {          /* what a click resolved to */
    ZD_BAR_ACT_NONE = 0,
    ZD_BAR_ACT_OPEN_LAUNCHER,
    ZD_BAR_ACT_SWITCH_WORKSPACE,
    ZD_BAR_ACT_OPEN_NOTIFICATIONS,
    ZD_BAR_ACT_OPEN_CLOCK,
    ZD_BAR_ACT_TOGGLE,
    ZD_BAR_ACT_CYCLE_FOCUS    /* keyboard: activate focused applet */
};

struct zd_bar_applet_metrics {
    uint32_t x;                /* layout position within the bar */
    uint32_t w;                /* layout width in logical px */
};

/* ops: providers injected by the shell.  toggle returns <0 when
 * refused (bar records the denial and leaves state unchanged). */
struct zd_bar_ops {
    int (*toggle)(void *ctx, int toggle_id, int on);
    void *ctx;
};

struct zd_bar {
    struct zd_caps *caps;      /* optional privilege gate */
    uint32_t width_logical;    /* configured logical width (>0) */
    uint32_t dpi_scale;        /* 1 = 100%, 2 = 200% (>0) */
    struct zd_bar_ops ops;
    struct zd_bar_applet_metrics applets[ZD_BAR_APPLET_COUNT];
    uint32_t focus_idx;        /* keyboard focus over applets */
    uint8_t toggle_on[ZD_BAR_TOGGLE_COUNT];
    uint8_t toggle_denied[ZD_BAR_TOGGLE_COUNT];
    uint8_t dnd_active;        /* do-not-disturb mirrors toggle DND */
    uint32_t notif_count;      /* badge count (0 hides the applet) */
    uint32_t workspace;        /* 0-based active workspace */
    uint32_t workspace_count;
    char title[64];            /* focused window title ("" = none) */
    struct { uint32_t opens, toggles, denied_toggles, focus_moves; } stats;
};

int zd_bar_init(struct zd_bar *b, uint32_t logical_width, uint32_t dpi_scale);
void zd_bar_set_ops(struct zd_bar *b, const struct zd_bar_ops *ops);
/* Optional: gate quick toggles on ZD_SVC_BAR/ZD_CAP_SETTINGS_WRITE. */
void zd_bar_set_caps(struct zd_bar *b, struct zd_caps *caps);
/* Fill applet metrics from the logical width: fixed applets first,
 * title absorbs the slack, so the last applet ends exactly at the
 * configured width when there is room.  Returns 0. */
int zd_bar_layout(struct zd_bar *b);
/* Physical pixels covered by the bar (= logical * dpi_scale). */
uint32_t zd_bar_physical_width(const struct zd_bar *b);

/* Pointer click at logical x -> action + arg (workspace index for
 * SWITCH_WORKSPACE, toggle id for TOGGLE).  Empty/gap: -ENOENT. */
int zd_bar_click(struct zd_bar *b, uint32_t x, int *action_out, int *arg_out);
/* Keyboard: rotate focus across applets. */
int zd_bar_focus_next(struct zd_bar *b);
/* Activate the focused applet (same actions as a click). */
int zd_bar_activate_focused(struct zd_bar *b, int *action_out, int *arg_out);

int zd_bar_toggle_set(struct zd_bar *b, int toggle_id, int on);
int zd_bar_set_workspace(struct zd_bar *b, uint32_t idx); /* -22 if OOR */
void zd_bar_set_title(struct zd_bar *b, const char *title);
void zd_bar_set_notif_count(struct zd_bar *b, uint32_t n);

#endif /* ZEROOS_DESKTOP_BAR_H */
