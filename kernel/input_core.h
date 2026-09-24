#ifndef ZEROOS_INPUT_CORE_H
#define ZEROOS_INPUT_CORE_H
#include "types.h"
#define INPUT_QUEUE_CAPACITY 64
#define INPUT_MAX_DEVICES 32
enum input_kind { INPUT_KEY=1, INPUT_POINTER=2, INPUT_TOUCH=3, INPUT_DEVICE_GONE=4 };
struct input_event { uint64_t timestamp; uint32_t device_id; int32_t x,y,value; uint16_t code; uint8_t kind,flags; };
struct input_queue { struct input_event events[INPUT_QUEUE_CAPACITY]; uint32_t head,tail,dropped; };
struct input_device_slot { uint32_t id; uint8_t active; };
struct input_registry { struct input_device_slot devices[INPUT_MAX_DEVICES]; uint32_t next_id; };
void input_queue_init(struct input_queue *q);
int input_queue_push(struct input_queue *q,const struct input_event *event);
int input_queue_pop(struct input_queue *q,struct input_event *event);
void input_registry_init(struct input_registry *r);
int input_device_add(struct input_registry *r,uint32_t *id);
int input_device_remove(struct input_registry *r,uint32_t id);
#endif
