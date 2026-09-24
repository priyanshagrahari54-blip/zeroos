#include "input_core.h"
void input_queue_init(struct input_queue *q) { if(!q)return; q->head=q->tail=q->dropped=0; }
int input_queue_push(struct input_queue *q,const struct input_event *e) {
    if(!q||!e||e->kind<INPUT_KEY||e->kind>INPUT_DEVICE_GONE)return -1;
    uint32_t next=(q->head+1U)%INPUT_QUEUE_CAPACITY;
    if(next==q->tail) { q->dropped++; return -2; }
    q->events[q->head]=*e; q->head=next; return 0;
}
int input_queue_pop(struct input_queue *q,struct input_event *e) {
    if(!q||!e)return -1;
    if(q->tail==q->head)return 1;
    *e=q->events[q->tail]; q->tail=(q->tail+1U)%INPUT_QUEUE_CAPACITY; return 0;
}
void input_registry_init(struct input_registry *r) { if(!r)return; r->next_id=1; for(uint32_t i=0;i<INPUT_MAX_DEVICES;i++){r->devices[i].id=0;r->devices[i].active=0;} }
int input_device_add(struct input_registry *r,uint32_t *id) {
    if(!r||!id||!r->next_id)return -1;
    for(uint32_t i=0;i<INPUT_MAX_DEVICES;i++)if(!r->devices[i].active){uint32_t n=r->next_id++; if(!r->next_id)return -1; r->devices[i].id=n;r->devices[i].active=1;*id=n;return 0;}
    return -2;
}
int input_device_remove(struct input_registry *r,uint32_t id) {
    if(!r||!id)return -1;
    for(uint32_t i=0;i<INPUT_MAX_DEVICES;i++)if(r->devices[i].active&&r->devices[i].id==id){r->devices[i].active=0;r->devices[i].id=0;return 0;}
    return -1;
}
