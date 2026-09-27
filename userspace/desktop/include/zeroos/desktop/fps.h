/* FPS monitor and performance profiles (Stage 5 part I).
 * Clock-injected sliding window: frames are recorded with the app's
 * monotonic time source; FPS, frame-time percentiles and budget
 * breaches are derived — never guessed.  Cooperative: profiles publish
 * a target frame budget the resource governor can honor. */
#ifndef ZEROOS_DESKTOP_FPS_H
#define ZEROOS_DESKTOP_FPS_H

#include <stdint.h>

#define ZD_FPS_RING 128
#define ZD_FPS_BUCKETS 8 /* <=4,8,12,16,20,25,33,over ms */

enum zd_perf_profile {
    ZD_PERF_LOW_LATENCY = 0, /* 8 ms budget  (~120 fps) */
    ZD_PERF_BALANCED,        /* 16 ms budget (~60 fps) */
    ZD_PERF_QUALITY          /* 33 ms budget (~30 fps) */
};

struct zd_fps {
    uint64_t stamp[ZD_FPS_RING];   /* last frame timestamps (ns) */
    uint64_t frame_ms[ZD_FPS_RING];/* last frame durations (ms) */
    uint32_t head;                 /* next write slot */
    uint32_t count;                /* samples stored (<= ring) */
    int profile;
    uint32_t window_ms;            /* fps window, >0 */
    uint32_t budget_breaches;      /* frames slower than budget */
    uint64_t frames_total;
};

int zd_fps_init(struct zd_fps *f, uint32_t window_ms, int profile);
int zd_fps_set_profile(struct zd_fps *f, int profile);
uint32_t zd_fps_budget_ms(int profile); /* 0 for bad profile */
/* Record a frame ending at now_ms with duration frame_ms. */
int zd_fps_record(struct zd_fps *f, uint64_t now_ms, uint64_t frame_ms);
/* FPS over the trailing window: frames in (now-window, now] scaled to
 * per-second.  0 when no sample in the window. */
uint32_t zd_fps_current(const struct zd_fps *f, uint64_t now_ms);
/* Frame-duration percentile p (0..100) over stored samples; 0 if none. */
uint32_t zd_fps_percentile(const struct zd_fps *f, uint32_t p);
uint32_t zd_fps_avg_frame_ms(const struct zd_fps *f);

#endif /* ZEROOS_DESKTOP_FPS_H */
