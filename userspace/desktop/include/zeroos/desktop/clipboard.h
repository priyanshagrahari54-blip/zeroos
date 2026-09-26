/* Clipboard service core (Stage 5 shell surface).
 * Bounded history of text clippings with explicit formats, copy/cycle
 * semantics and privacy controls (sensitive entries never persist in
 * history).  Host-testable. */
#ifndef ZEROOS_DESKTOP_CLIPBOARD_H
#define ZEROOS_DESKTOP_CLIPBOARD_H

#include <stdint.h>

#define ZD_CLIP_MAX 12
#define ZD_CLIP_TEXT 256
#define ZD_CLIP_APP 24

enum zd_clip_format {
    ZD_CLIP_FMT_TEXT = 0,
    ZD_CLIP_FMT_URI
};

struct zd_clip {
    char text[ZD_CLIP_TEXT];
    char app[ZD_CLIP_APP];     /* owner app id, "" = shell */
    uint32_t format;           /* enum zd_clip_format */
    uint32_t seq;              /* creation stamp */
    uint8_t sensitive;         /* 1 = excluded from history */
    uint8_t in_use;
};

struct zd_clipboard {
    struct zd_clip slots[ZD_CLIP_MAX];
    struct zd_clip *current;   /* active clipping (NULL = empty) */
    uint32_t next_seq;
    struct {
        uint32_t copies, pastes, sensitive_kept, rejected, cycles;
    } stats;
};

void zd_clipboard_init(struct zd_clipboard *cb);
/* Copy text from app ("" = shell).  sensitive=1 sets current but
 * pushes nothing to history.  Overlong/NULL -> -22 (counted). */
int zd_clipboard_copy(struct zd_clipboard *cb, const char *app,
                      const char *text, uint32_t format, int sensitive);
/* Current clipping as text ("" when empty); NULL out -> -22. */
int zd_clipboard_paste(struct zd_clipboard *cb, char *out,
                       uint32_t out_cap);
/* Cycle history: 0 = current, 1..n-1 = older entries (most recent
 * first).  Sensitive entries are never returned by the cycle. */
int zd_clipboard_cycle(struct zd_clipboard *cb, uint32_t depth,
                       char *out, uint32_t out_cap);
/* Clear current + history (privacy action). */
void zd_clipboard_clear(struct zd_clipboard *cb);
uint32_t zd_clipboard_history_count(const struct zd_clipboard *cb);

#endif /* ZEROOS_DESKTOP_CLIPBOARD_H */
