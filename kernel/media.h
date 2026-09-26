#ifndef ZEROOS_MEDIA_H
#define ZEROOS_MEDIA_H
#include "types.h"
#include "sync.h"
#define ZEROOS_MEDIA_MAX_TRACKS 128U
#define ZEROOS_MEDIA_MAX_PLAYLISTS 16U
enum zeroos_media_state { ZEROOS_MEDIA_STOPPED=0,ZEROOS_MEDIA_DORMANT,ZEROOS_MEDIA_WARM,ZEROOS_MEDIA_ACTIVE,ZEROOS_MEDIA_THROTTLED,ZEROOS_MEDIA_SUSPENDED };
struct zeroos_media_track { uint64_t id; uint32_t gen; uint8_t used; char title[64]; char artist[64]; uint64_t duration_ms; uint32_t sample_rate; uint8_t has_artwork; struct spinlock lock; };
struct zeroos_media_playlist { uint64_t id; uint32_t gen; uint8_t used; char name[64]; uint64_t track_ids[ZEROOS_MEDIA_MAX_TRACKS]; uint32_t count; struct spinlock lock; };
int media_system_init(void);
int media_track_add(const char *title,const char *artist,uint64_t duration,uint64_t *id_out);
int media_playlist_create(const char *name,uint64_t *id_out);
int media_playlist_add_track(uint64_t playlist_id,uint64_t track_id);
int media_debug_validate(void);
#endif
