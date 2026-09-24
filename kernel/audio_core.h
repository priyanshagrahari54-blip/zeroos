#ifndef ZEROOS_AUDIO_CORE_H
#define ZEROOS_AUDIO_CORE_H
#include "types.h"
#define AUDIO_RING_MAX_FRAMES 4096
struct audio_ring { int16_t *samples; uint32_t capacity,read_at,write_at,used,underruns,overruns; };
int audio_ring_init(struct audio_ring *r,int16_t *storage,uint32_t frames);
uint32_t audio_ring_write(struct audio_ring *r,const int16_t *samples,uint32_t frames);
uint32_t audio_ring_read(struct audio_ring *r,int16_t *samples,uint32_t frames);
#endif
