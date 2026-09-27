#ifndef ZEROOS_DESKTOP_WINDOW_H
#define ZEROOS_DESKTOP_WINDOW_H

/* Window system core: surfaces, ownership, focus, stacking, geometry,
 * workspaces, snapping, multi-monitor layout, DPI scaling, input routing,
 * accessibility labels, buffer lifetime and client-crash isolation.
 *
 * Ownership: every window belongs to exactly one client id. Client death is
 * an explicit event that destroys the client's windows, invalidates buffer
 * generations and reassigns focus through the ordinary stacking rules. The
 * kernel never sees any of this policy. */

#include <zeroos/desktop/common.h>

typedef uint32_t zd_window_id;
typedef uint32_t zd_client_id;
typedef uint32_t zd_buffer_generation;

#define ZD_MAX_MONITORS 4
#define ZD_MAX_WORKSPACES 8
#define ZD_MAX_WINDOWS 64
#define ZD_WINDOW_TITLE_CAP 32
#define ZD_WINDOW_LABEL_CAP 40
#define ZD_MAX_WM_LISTENERS 4
#define ZD_INVALID_WINDOW ((zd_window_id)0)

enum zd_window_state {
    ZD_WINDOW_NORMAL = 0,
    ZD_WINDOW_MINIMIZED = 1,
    ZD_WINDOW_MAXIMIZED = 2
};

enum zd_snap_region {
    ZD_SNAP_NONE = 0,
    ZD_SNAP_LEFT = 1,
    ZD_SNAP_RIGHT = 2,
    ZD_SNAP_TOP_LEFT = 3,
    ZD_SNAP_TOP_RIGHT = 4,
    ZD_SNAP_BOTTOM_LEFT = 5,
    ZD_SNAP_BOTTOM_RIGHT = 6,
    ZD_SNAP_FULLSCREEN = 7,
    ZD_SNAP_COUNT = 8
};

enum zd_a11y_role {
    ZD_ROLE_UNKNOWN = 0,
    ZD_ROLE_WINDOW = 1,
    ZD_ROLE_APPLICATION = 2,
    ZD_ROLE_DIALOG = 3,
    ZD_ROLE_BUTTON = 4,
    ZD_ROLE_TEXT = 5,
    ZD_ROLE_ENTRY = 6,
    ZD_ROLE_LIST = 7,
    ZD_ROLE_MENU = 8,
    ZD_ROLE_BAR = 9,
    ZD_ROLE_CHECKBOX = 10,
    ZD_ROLE_SLIDER = 11,
    ZD_ROLE_IMAGE = 12,
    ZD_ROLE_CONTAINER = 13
};

enum zd_buffer_state {
    ZD_BUFFER_FREE = 0,       /* client may render into it */
    ZD_BUFFER_QUEUED = 1,     /* submitted, awaiting composition */
    ZD_BUFFER_COMPOSITED = 2, /* presented; client may release */
    ZD_BUFFER_INVALID = 3     /* owner crashed or surface destroyed */
};

struct zd_monitor {
    uint32_t id;
    struct zd_rect bounds;     /* physical pixels, virtual desktop space */
    uint32_t scale_percent;    /* 100, 125, 150, 200 ... */
    uint32_t primary;
    uint32_t enabled;
    char name[16];
};

struct zd_window_create_info {
    zd_client_id client;
    const char *title;
    struct zd_rect logical_rect; /* logical (DPI-scaled) coordinates */
    int32_t min_width;
    int32_t min_height;
    int32_t max_width;           /* 0 = unbounded */
    int32_t max_height;
    uint32_t monitor_id;
    const char *a11y_label;
    enum zd_a11y_role role;
    uint32_t resizable;
};

struct zd_window {
    zd_window_id id;
    zd_client_id client;
    uint32_t monitor_id;
    uint32_t workspace;      /* zero-based */
    struct zd_rect logical;  /* position/size in logical units */
    int32_t min_width;
    int32_t min_height;
    int32_t max_width;
    int32_t max_height;
    uint32_t resizable;
    enum zd_window_state state;
    enum zd_snap_region snap;
    uint32_t mapped;
    uint32_t keyboard_focused;
    char title[ZD_WINDOW_TITLE_CAP];
    char a11y_label[ZD_WINDOW_LABEL_CAP];
    enum zd_a11y_role role;
    zd_buffer_generation buffer_generation;
    enum zd_buffer_state buffer_state;
    uint32_t in_use;
};

struct zd_wm_stats {
    uint32_t windows_created;
    uint32_t windows_destroyed;
    uint32_t focus_changes;
    uint32_t workspace_switches;
    uint32_t client_crashes_handled;
    uint32_t buffer_rejects;
    uint32_t invalid_route_attempts;
};

struct zd_wm_listener_context;

struct zd_wm_listener {
    void (*on_window_created)(void *context, zd_window_id id);
    void (*on_window_destroyed)(void *context, zd_window_id id);
    void (*on_window_geometry)(void *context, zd_window_id id);
    void (*on_window_state)(void *context, zd_window_id id);
    void (*on_focus_changed)(void *context, zd_window_id id);
    void (*on_workspace_changed)(void *context, uint32_t workspace);
    void (*on_stacking_changed)(void *context);
    void *context;
};

struct zd_wm {
    struct zd_monitor monitors[ZD_MAX_MONITORS];
    uint32_t monitor_count;
    uint32_t active_workspace;
    uint32_t workspace_count;
    zd_window_id stacking[ZD_MAX_WINDOWS]; /* bottom -> top */
    uint32_t stacking_count;
    zd_window_id focus_stack[ZD_MAX_WINDOWS]; /* [0] = most recent */
    uint32_t focus_count;
    zd_window_id pointer_focus;
    struct zd_window windows[ZD_MAX_WINDOWS];
    uint32_t window_count;
    zd_window_id next_id;
    zd_client_id next_client;
    struct zd_wm_stats stats;
    uint32_t listener_count;
    struct zd_wm_listener listeners[ZD_MAX_WM_LISTENERS];
};

void zd_wm_init(struct zd_wm *wm);
/* Monitors are registered before windows; the first registered is primary. */
int zd_wm_add_monitor(struct zd_wm *wm, const struct zd_monitor *monitor);
const struct zd_monitor *zd_wm_monitor(const struct zd_wm *wm, uint32_t id);
int zd_wm_add_listener(struct zd_wm *wm, const struct zd_wm_listener *listener);
zd_client_id zd_wm_register_client(struct zd_wm *wm);
int zd_wm_create_window(struct zd_wm *wm, const struct zd_window_create_info *info,
                        zd_window_id *out_id);
int zd_wm_destroy_window(struct zd_wm *wm, zd_window_id id);
struct zd_window *zd_wm_window(struct zd_wm *wm, zd_window_id id);
const struct zd_window *zd_wm_window_const(const struct zd_wm *wm, zd_window_id id);
int zd_wm_set_title(struct zd_wm *wm, zd_window_id id, const char *title);
int zd_wm_move_resize(struct zd_wm *wm, zd_window_id id,
                      struct zd_rect logical);
int zd_wm_minimize(struct zd_wm *wm, zd_window_id id);
int zd_wm_maximize(struct zd_wm *wm, zd_window_id id);
int zd_wm_restore(struct zd_wm *wm, zd_window_id id);
int zd_wm_snap(struct zd_wm *wm, zd_window_id id, enum zd_snap_region region);
int zd_wm_raise(struct zd_wm *wm, zd_window_id id);
int zd_wm_lower(struct zd_wm *wm, zd_window_id id);
int zd_wm_focus(struct zd_wm *wm, zd_window_id id);
int zd_wm_set_workspace(struct zd_wm *wm, zd_window_id id, uint32_t workspace);
int zd_wm_switch_workspace(struct zd_wm *wm, uint32_t workspace);
int zd_wm_assign_monitor(struct zd_wm *wm, zd_window_id id, uint32_t monitor_id);
/* Pointer routing: topmost mapped, non-minimized window of the active
 * workspace under the physical point, or 0 for shell/desktop. */
zd_window_id zd_wm_hit_test(const struct zd_wm *wm, int32_t physical_x,
                            int32_t physical_y);
/* Keyboard routing target: the focused window of the active workspace. */
zd_window_id zd_wm_keyboard_target(const struct zd_wm *wm);
/* Stacking query: index 0 = bottom. Returns count copied. */
uint32_t zd_wm_stacking_order(const struct zd_wm *wm, zd_window_id *out,
                              uint32_t capacity);
int32_t zd_wm_scale_x(const struct zd_monitor *monitor, int32_t logical_x);
int32_t zd_wm_scale_y(const struct zd_monitor *monitor, int32_t logical_y);
int32_t zd_wm_unscale_x(const struct zd_monitor *monitor, int32_t physical_x);
int32_t zd_wm_unscale_y(const struct zd_monitor *monitor, int32_t physical_y);
struct zd_rect zd_wm_logical_to_physical(const struct zd_monitor *monitor,
                                         struct zd_rect logical);
struct zd_rect zd_wm_workarea(const struct zd_monitor *monitor);
/* Buffer lifetime: a client queues its latest rendered generation; the
 * compositor releases it after presentation. Stale or cross-client
 * operations fail without mutating state. */
int zd_wm_queue_buffer(struct zd_wm *wm, zd_window_id id,
                       zd_buffer_generation generation);
int zd_wm_release_buffer(struct zd_wm *wm, zd_window_id id,
                         zd_buffer_generation generation);
enum zd_buffer_state zd_wm_buffer_state(const struct zd_wm *wm, zd_window_id id,
                                        zd_buffer_generation *generation);
/* Crash isolation: destroy every window owned by the client, invalidate
 * buffers, reassign focus. Returns number of windows destroyed. */
uint32_t zd_wm_client_crashed(struct zd_wm *wm, zd_client_id client);
uint32_t zd_wm_windows_for_client(const struct zd_wm *wm, zd_client_id client,
                                  zd_window_id *out, uint32_t capacity);
/* Snap geometry resolution (logical units, monitor workarea). */
struct zd_rect zd_wm_snap_geometry(const struct zd_monitor *monitor,
                                   enum zd_snap_region region);
/* Ordering helpers used by tests and the shell overview. */
int zd_wm_is_above(const struct zd_wm *wm, zd_window_id upper,
                   zd_window_id lower);

#endif
