#include <assert.h>
#include "../kernel/input_core.h"
int main(void){
 struct input_queue q; input_queue_init(&q); struct input_event in={0},out={0}; in.kind=INPUT_KEY; in.code=30;
 assert(input_queue_pop(&q,&out)==1); assert(input_queue_push(&q,&in)==0); assert(input_queue_pop(&q,&out)==0&&out.code==30);
 for(unsigned i=0;i<INPUT_QUEUE_CAPACITY-1;i++)assert(input_queue_push(&q,&in)==0);
 assert(input_queue_push(&q,&in)==-2&&q.dropped==1);
 struct input_registry r; input_registry_init(&r); unsigned id=0; assert(input_device_add(&r,&id)==0&&id!=0); assert(input_device_remove(&r,id)==0); assert(input_device_remove(&r,id)==-1);
 return 0;
}
