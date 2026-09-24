#include <assert.h>
#include "../kernel/audio_core.h"
int main(void){int16_t store[4],in[]={1,2,3,4,5},out[6]={0};struct audio_ring r;assert(audio_ring_init(&r,store,4)==0);assert(audio_ring_write(&r,in,5)==4&&r.overruns==1);assert(audio_ring_read(&r,out,6)==4&&r.underruns==1);assert(out[0]==1&&out[3]==4&&r.used==0);return 0;}
