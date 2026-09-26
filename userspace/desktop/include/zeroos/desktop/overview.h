/* Shell overview / expose model (Stage 5 part A): uniform thumbnail
 * grid over a work area with gap, hit-testing and keyboard focus
 * cycling.  Pure geometry + selection — no rendering here. */
#ifndef ZEROOS_DESKTOP_OVERVIEW_H
#define ZEROOS_DESKTOP_OVERVIEW_H

#include <stdint.h>
#include <zeroos/desktop/common.h>

#define ZD_OVERVIEW_MAX 32

struct zd_overview_item {
    uint32_t win_id;
    struct zd_rect thumb;
};

struct zd_overview {
    struct zd_overview_item items[ZD_OVERVIEW_MAX];
    uint32_t count;
    uint32_t focus;                 /* selected index (wraps) */
    struct zd_rect area;            /* work area, logical units */
    uint32_t gap;                   /* px between thumbnails + edges */
    uint32_t cols, rows;            /* current grid */
    struct {
        uint32_t opens, closes, relayouts, selections, rejected;
    } stats;
};

/* Validate area (w/h > 0) and gap; count=0 until windows are set.
 * Cells that would drop below 1px -> -22 (area too small for N). */
int zd_overview_open(struct zd_overview *o, struct zd_rect area,
                     uint32_t gap);
void zd_overview_close(struct zd_overview *o);
/* Replace the window set (ids, n <= ZD_OVERVIEW_MAX) and re-layout.
 * n > cap -> -28 +rejected; dup ids tolerated (caller's business). */
int zd_overview_set_windows(struct zd_overview *o, const uint32_t *ids,
                            uint32_t n);
/* Hit-test logical point -> item index, -2 outside any thumbnail
 * (including gaps), -22 when empty/unopened. */
int zd_overview_hit(const struct zd_overview *o, int32_t x,
                    int32_t y);
/* Focus cycling (wraps): 0 ok, -22 empty. */
int zd_overview_focus_next(struct zd_overview *o);
int zd_overview_focus_prev(struct zd_overview *o);
/* Selected window id: 0 when empty (ids are 1-based by contract of
 * callers; 0 never valid). */
uint32_t zd_overview_focused_id(const struct zd_overview *o);
/* Remove window by id: relayout; -2 unknown. */
int zd_overview_remove(struct zd_overview *o, uint32_t win_id);

#endif /* ZEROOS_DESKTOP_OVERVIEW_H */
