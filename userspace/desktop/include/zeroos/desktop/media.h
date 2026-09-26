/* Media core (Stage 5 part H): lawful sources only, no DRM bypass.
 *
 * Policy engine, not a decoder: every play goes through origin
 * registration + rights + an explicit DRM gate.  Default is DENY for
 * unregistered origins and for DRM-protected content — the contract
 * provides no code path that strips or bypasses a protection
 * scheme; such items are refused with a counted, visible reason.
 */
#ifndef ZEROOS_DESKTOP_MEDIA_H
#define ZEROOS_DESKTOP_MEDIA_H

#include <stdint.h>

#define ZD_MEDIA_SOURCES 8
#define ZD_MEDIA_ITEMS 16
#define ZD_MEDIA_ORIGIN 64
#define ZD_MEDIA_TITLE 48

/* rights bits */
#define ZD_MEDIA_RIGHT_PLAY  (1u << 0)
#define ZD_MEDIA_RIGHT_CACHE (1u << 1)
#define ZD_MEDIA_RIGHT_EXPORT (1u << 2)
#define ZD_MEDIA_RIGHT_SYNC  (1u << 3)
#define ZD_MEDIA_RIGHT_ALL   0xFu

struct zd_media_source {
    char origin[ZD_MEDIA_ORIGIN];   /* "https://host" or "file:/path" */
    uint32_t rights;                /* ZD_MEDIA_RIGHT_* mask */
    uint32_t in_use;
    uint32_t plays;
    uint32_t refusals;
};

struct zd_media_item {
    char origin[ZD_MEDIA_ORIGIN];
    char title[ZD_MEDIA_TITLE];
    uint32_t source_idx;            /* resolved origin slot */
    uint8_t drm;                    /* 1 = protected content */
    uint8_t in_use;
};

struct zd_media {
    struct zd_media_source sources[ZD_MEDIA_SOURCES];
    struct zd_media_item items[ZD_MEDIA_ITEMS];
    uint32_t source_count;
    uint32_t item_count;
    struct {
        uint32_t plays, refusals_drm, refusals_rights,
                 refusals_origin, rejected, registered, items_added;
    } stats;
};

void zd_media_init(struct zd_media *m);
/* Register/replace an origin's rights.  empty/overlong origin -> -22
 * +rejected; full -> -28; rights outside mask -> -22. */
int zd_media_register_source(struct zd_media *m, const char *origin,
                             uint32_t rights);
int zd_media_unregister_source(struct zd_media *m, const char *origin);
/* Add an item under an origin (any rights — listing != playing). */
int zd_media_add_item(struct zd_media *m, const char *origin,
                      const char *title, uint32_t drm);
/* Attempt to play: unregistered origin -> -1 (refusals_origin),
 * missing PLAY right -> -1 (refusals_rights), drm -> -1
 * (refusals_drm — never bypassed).  Success -> 0, plays++. */
int zd_media_play(struct zd_media *m, uint32_t item_id);
/* Cache/export/sync gates with the same origin+rights discipline. */
int zd_media_request(struct zd_media *m, uint32_t item_id,
                     uint32_t right);   /* one ZD_MEDIA_RIGHT_* bit */

#endif /* ZEROOS_DESKTOP_MEDIA_H */
