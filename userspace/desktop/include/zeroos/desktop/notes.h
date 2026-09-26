/* Study notes (Stage 5 part G): bounded notebook with pinned notes
 * and substring search.  Bodies are copied in (caller owns nothing
 * afterwards); no file persistence here — storage binding is a
 * separate integration step. */
#ifndef ZEROOS_DESKTOP_NOTES_H
#define ZEROOS_DESKTOP_NOTES_H

#include <stdint.h>

#define ZD_NOTES_MAX 64
#define ZD_NOTES_TITLE 32
#define ZD_NOTES_BODY 256

struct zd_note {
    uint32_t id;
    uint8_t pinned;
    uint32_t body_len;
    int64_t mtime;
    char title[ZD_NOTES_TITLE];
    char body[ZD_NOTES_BODY];
};

struct zd_notes {
    struct zd_note items[ZD_NOTES_MAX];
    uint32_t count;
    uint32_t next_id;
    struct {
        uint32_t created, updated, deleted, pinned, unpinned;
        uint32_t searches, search_hits, rejected;
    } stats;
};

void zd_notes_init(struct zd_notes *n);
/* Create: empty/NULL title or overlong (>=TITLE) -> -22 (+rejected);
 * empty body allowed; full -> -28.  mtime stamped by caller args to
 * stay deterministic in tests (pass 0 or a clock value). */
int zd_notes_create(struct zd_notes *n, const char *title,
                    const char *body, int64_t mtime, uint32_t *out_id);
/* Update body (and title when non-NULL); -2 unknown, -22 overlong. */
int zd_notes_update(struct zd_notes *n, uint32_t id, const char *title,
                    const char *body, int64_t mtime);
int zd_notes_delete(struct zd_notes *n, uint32_t id);
/* -2 unknown, -22 bad arg; pin first match only. */
int zd_notes_set_pinned(struct zd_notes *n, uint32_t id, int pinned);
const struct zd_note *zd_notes_get(const struct zd_notes *n,
                                   uint32_t id);
/* Case-insensitive ASCII substring search over title+body ("" matches
 * all).  With out != NULL the return is min(hits, cap) and up to cap
 * ids are written (caller-sized); with out == NULL it is the full
 * hit count.  -22 on bad args. */
int zd_notes_search(const struct zd_notes *n, const char *sub,
                    uint32_t *out, uint32_t cap);

#endif /* ZEROOS_DESKTOP_NOTES_H */
