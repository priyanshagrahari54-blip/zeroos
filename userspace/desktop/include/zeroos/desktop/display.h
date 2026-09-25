#ifndef ZEROOS_DESKTOP_DISPLAY_H
#define ZEROOS_DESKTOP_DISPLAY_H

/* Display service: owns the scanout presentation path between compositor
 * output and the DISPLAY_INFO/DISPLAY_PRESENT kernel syscalls.
 *
 * Split of responsibilities (Stage 5A graphics service):
 *   kernel  — validates geometry/stride/bounds and streams pixels
 *             (display_core rules, fb lock, ENOENT on degraded boots)
 *   service — adopts the scanout geometry, gates every submission
 *             (degraded / paced / suspended / no-damage), accumulates
 *             damage into one bounded present per frame, and keeps
 *             the failure/suspension state machine
 *   compositor — decides WHAT is dirty and when a frame is due
 *
 * The service is freestanding and policy-only: the syscalls are reached
 * through injected ops so host tests exercise the exact production
 * gating logic, while the on-target shell binds the ops to the real
 * zeroos_display_info()/zeroos_display_present() wrappers. No polling:
 * callers present explicitly when damage exists. */

#include <zeroos/desktop/common.h>
#include <zeroos/syscall.h>

enum zd_display_state {
    ZD_DISPLAY_ATTACHING = 0,
    ZD_DISPLAY_LIVE = 1,      /* scanout present flag set; submits allowed */
    ZD_DISPLAY_DEGRADED = 2,  /* serial-only/info unavailable: refuses */
    ZD_DISPLAY_SUSPENDED = 3  /* failure limit hit: circuit open */
};

struct zd_display_ops {
    /* Kernel DISPLAY_INFO wrapper: 0 or negative -ZD-style errno from the
     * syscall layer. Must fill info on success. */
    int (*query_info)(void *context, struct zeroos_display_info *info);
    /* Kernel DISPLAY_PRESENT wrapper: x/y/width/height in scanout pixels,
     * stride_bytes row pitch of the SOURCE, pixels = rect top-left. */
    int (*present)(void *context, uint32_t x, uint32_t y, uint32_t width,
                   uint32_t height, uint32_t stride_bytes,
                   const void *pixels);
    /* Monotonic pacing clock (scheduler ticks). */
    uint64_t (*ticks)(void *context);
    void *context;
};

struct zd_metrics; /* forward */

struct zd_display_stats {
    uint64_t frames_presented;
    uint64_t pixels_submitted;
    uint64_t presents_refused_degraded;
    uint64_t presents_refused_paced;
    uint64_t presents_refused_suspended;
    uint64_t presents_refused_empty;
    uint64_t present_failures;
    uint64_t damage_rects_accepted;
};

struct zd_display_service {
    struct zd_display_ops ops;
    struct zeroos_display_info info;
    enum zd_display_state state;
    uint32_t width;
    uint32_t height;
    uint64_t min_present_interval_ticks; /* 0 = unpaced */
    uint64_t last_present_tick;
    uint32_t has_presented;
    uint32_t consecutive_failures;
    uint32_t failure_limit;              /* suspend after N in a row */
    struct zd_rect pending_damage;       /* merged bounding box */
    uint32_t pending_valid;
    struct zd_display_stats stats;
    struct zd_metrics *metrics; /* optional part-L recorder */
};

/* ops must provide all three hooks. failure_limit 0 selects the default
 * (3). Returns -ZD_EINVAL on bad input. */
int zd_display_service_init(struct zd_display_service *service,
                            const struct zd_display_ops *ops,
                            uint32_t failure_limit);

/* Query and adopt scanout geometry. Valid info with the PRESENT flag
 * enters LIVE; valid info without it, or a query error, enters DEGRADED
 * (returns the query error or -ZD_ENOENT) — never a crash, never a
 * silent lie about hardware. Re-entering LIVE from SUSPENDED resets the
 * failure counters (explicit recovery, never automatic). Pending damage
 * is discarded: geometry may have changed, so callers re-damage after a
 * successful attach. */
int zd_display_service_attach(struct zd_display_service *service);

/* Record damage (physical, clipped to the scanout) for the next present.
 * Multiple calls merge into one bounding box. Returns -ZD_EINVAL only
 * when the service has no adopted geometry. */
int zd_display_service_damage(struct zd_display_service *service,
                              struct zd_rect rect);

/* Present accumulated damage from a full-frame pixel base.
 * 0                  — submitted (frame counted, pending cleared)
 * -ZD_EAGAIN         — paced, or nothing pending (no redundant submits)
 * -ZD_ENOENT         — degraded display (matches kernel contract)
 * -ZD_ESTATE         — suspended after repeated failures
 * other negative     — forwarded present failure (counted, may suspend)
 * now_tick comes from ops.ticks(); pass it through to keep time sources
 * single. */
void zd_display_service_set_metrics(struct zd_display_service *service,
                                   struct zd_metrics *metrics);

int zd_display_service_present(struct zd_display_service *service,
                               const void *frame_base,
                               uint32_t stride_bytes, uint64_t now_tick);

#endif
