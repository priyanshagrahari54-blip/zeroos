#ifndef ZEROOS_DESKTOP_COMPOSITOR_H
#define ZEROOS_DESKTOP_COMPOSITOR_H

/* Compositor core: retained scene, damage tracking, occlusion culling,
 * frame scheduling/pacing, buffer lifecycle, resource caching, software
 * raster presentation with damage accounting, low-power mode and
 * reduced-motion policy.
 *
 * The core never polls. The session loop wakes on timer/vsync/input events
 * and calls zd_compositor_begin_frame(); if nothing is dirty or the pacing
 * deadline has not elapsed the call returns -ZD_EAGAIN and no pixel is
 * touched. Unchanged content is never redrawn. */

#include <zeroos/desktop/common.h>
#include <zeroos/desktop/window.h>

#define ZD_MAX_DAMAGE_RECTS 32
#define ZD_MAX_SCENE_NODES (ZD_MAX_WINDOWS + 8)
#define ZD_COMPOSITOR_MAX_LISTENERS 4

typedef uint32_t zd_node_id;

struct zd_bitmap {
    uint32_t *pixels;   /* XRGB8888, row-major */
    int32_t width;
    int32_t height;
    int32_t stride_px;  /* pixels per row */
};

struct zd_compositor_stats {
    uint64_t frames_presented;
    uint64_t frames_skipped_not_dirty;
    uint64_t frames_skipped_paced;
    uint64_t damage_rects_submitted;
    uint64_t damage_pixels_visible;
    uint64_t pixels_written;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t cache_evictions;
};

struct zd_scene_node {
    zd_node_id id;              /* window id or shell node id */
    struct zd_rect physical;    /* physical pixels on virtual desktop */
    uint32_t enabled;
    uint32_t is_window;
    uint32_t workspace;
    uint32_t focused;
    uint8_t opacity;            /* 255 = opaque */
    uint32_t resource_key;      /* cache key (buffer generation) */
};

/* Pixel source: the compositor asks the owner for a window's rendered
 * pixels at present time. Returning NULL means the buffer is gone (client
 * crashed); the node is skipped and marked stale. */
typedef const uint32_t *(*zd_pixel_source_fn)(void *context, zd_window_id id,
                                              int32_t *width, int32_t *height,
                                              int32_t *stride_px);

struct zd_frame_policy {
    uint32_t refresh_mhz;       /* display refresh in millihertz */
    uint32_t low_power;         /* 1 = halve target frame rate */
    uint32_t reduced_motion;    /* 1 = shell must not animate */
    uint32_t max_fps_low_power;
};

struct zd_compositor {
    struct zd_wm *wm;
    struct zd_frame_policy policy;
    struct zd_scene_node nodes[ZD_MAX_SCENE_NODES];
    uint32_t node_count;
    /* Damage entries are attributed to the node that produced them so
     * occlusion culling can subtract only the windows stacked above. */
    struct {
        zd_window_id owner;
        struct zd_rect rect;
    } damage[ZD_MAX_DAMAGE_RECTS];
    uint32_t damage_count;
    uint64_t damage_pixels_pending;
    uint64_t frame_interval_ns;
    uint64_t next_present_ns;
    uint64_t current_frame;
    uint32_t scene_valid;
    zd_pixel_source_fn pixel_source;
    void *pixel_context;
    /* Resource cache (LRU byte-budgeted; stores node resource keys). */
    struct {
        zd_node_id owner;
        uint32_t resource_key;
        uint64_t bytes;
        uint64_t last_used;
        uint32_t valid;
    } cache[ZD_MAX_SCENE_NODES];
    uint64_t cache_bytes;
    uint64_t cache_budget_bytes;
    struct zd_compositor_stats stats;
    uint32_t listener_count;
    struct {
        void (*on_present)(void *context, uint64_t frame,
                           struct zd_rect damaged_area);
        void *context;
    } listeners[ZD_COMPOSITOR_MAX_LISTENERS];
};

void zd_compositor_init(struct zd_compositor *compositor, struct zd_wm *wm,
                        const struct zd_frame_policy *policy);
int zd_compositor_set_pixel_source(struct zd_compositor *compositor,
                                   zd_pixel_source_fn source, void *context);
int zd_compositor_add_listener(struct zd_compositor *compositor,
                               void (*on_present)(void *context,
                                                  uint64_t frame,
                                                  struct zd_rect damaged_area),
                               void *context);
void zd_compositor_set_policy(struct zd_compositor *compositor,
                              const struct zd_frame_policy *policy);
void zd_compositor_set_cache_budget(struct zd_compositor *compositor,
                                    uint64_t bytes);
/* Retained scene rebuild from the window manager (explicit, not polled). */
void zd_compositor_sync_scene(struct zd_compositor *compositor);
/* Damage submission; rects are physical, clipped at present time. */
int zd_compositor_damage(struct zd_compositor *compositor, zd_window_id id,
                         struct zd_rect physical);
int zd_compositor_damage_full(struct zd_compositor *compositor, zd_window_id id);
/* Frame scheduling: 0 when a frame must be produced, -ZD_EAGAIN when
 * skipped (not dirty or still pacing), -ZD_EINVAL on bad input. */
int zd_compositor_begin_frame(struct zd_compositor *compositor, uint64_t now_ns);
/* Present the current damage into the target. Returns pixels written (>=0)
 * or negative error. Second call with no new damage writes 0 pixels. */
int zd_compositor_present(struct zd_compositor *compositor,
                          struct zd_bitmap target, uint64_t now_ns);
uint64_t zd_compositor_time_to_present(const struct zd_compositor *compositor,
                                       uint64_t now_ns);
/* Occlusion: compute visible damage rectangles after subtracting stacking
 * occluders above the damaged window. Returns count written. */
uint32_t zd_compositor_visible_damage(struct zd_compositor *compositor,
                                      struct zd_rect *out, uint32_t capacity);
/* Buffer invalidation when a client crashes. */
void zd_compositor_drop_client(struct zd_compositor *compositor,
                               zd_client_id client);
uint64_t zd_compositor_frame_interval_ns(const struct zd_compositor *compositor);

#endif
