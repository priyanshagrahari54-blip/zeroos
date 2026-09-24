#include "audio_core.h"
int audio_ring_init(struct audio_ring *r,int16_t *s,uint32_t n){if(!r||!s||!n||n>AUDIO_RING_MAX_FRAMES)return -1;r->samples=s;r->capacity=n;r->read_at=r->write_at=r->used=r->underruns=r->overruns=0;return 0;}
uint32_t audio_ring_write(struct audio_ring *r,const int16_t *s,uint32_t n){if(!r||!r->samples||(!s&&n))return 0;uint32_t done=0;while(done<n&&r->used<r->capacity){r->samples[r->write_at]=s[done++];r->write_at=(r->write_at+1U)%r->capacity;r->used++;}if(done<n)r->overruns++;return done;}
uint32_t audio_ring_read(struct audio_ring *r,int16_t *s,uint32_t n){if(!r||!r->samples||(!s&&n))return 0;uint32_t done=0;while(done<n&&r->used){s[done++]=r->samples[r->read_at];r->read_at=(r->read_at+1U)%r->capacity;r->used--;}if(done<n)r->underruns++;return done;}
