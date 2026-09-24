#include "input.h"
#include "timer.h"

static struct spinlock input_lock;
static struct zeroos_input_device input_devices[ZEROOS_INPUT_MAX_DEVICES];

int input_system_init(void) {
    spinlock_init(&input_lock);
    for (uint32_t i=0;i<ZEROOS_INPUT_MAX_DEVICES;++i) {
        input_devices[i].used=0;
        input_devices[i].generation=0;
        input_devices[i].state=ZEROOS_INPUT_STOPPED;
        input_devices[i].head=input_devices[i].tail=input_devices[i].count=0;
        spinlock_init(&input_devices[i].lock);
        wait_queue_init(&input_devices[i].event_waiters);
    }
    return 0;
}

int input_device_register(enum zeroos_input_device_type type, const char *name, uint64_t *device_id_out) {
    if (!name || !device_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&input_lock);
    for (uint32_t i=0;i<ZEROOS_INPUT_MAX_DEVICES;++i) {
        if (input_devices[i].used) continue;
        if (input_devices[i].generation==0xffffffffU) continue;
        input_devices[i].generation++;
        if (input_devices[i].generation==0) continue;
        input_devices[i].used=1;
        input_devices[i].type=type;
        input_devices[i].state=ZEROOS_INPUT_DORMANT;
        input_devices[i].head=input_devices[i].tail=input_devices[i].count=0;
        input_devices[i].event_count=0;
        input_devices[i].dropped_count=0;
        uint32_t n=0;
        while (n<31 && name[n]) { input_devices[i].name[n]=name[n]; n++; }
        input_devices[i].name[n]=0;
        input_devices[i].id = ((uint64_t)input_devices[i].generation<<16) | (uint64_t)(i+1);
        *device_id_out=input_devices[i].id;
        spin_unlock_irqrestore(&input_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&input_lock,flags);
    return -1;
}

int input_device_push_event(uint64_t device_id, struct zeroos_input_event *ev) {
    if (!ev) return -1;
    uint64_t flags=spin_lock_irqsave(&input_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    if (slot==0 || slot>ZEROOS_INPUT_MAX_DEVICES || gen==0) { spin_unlock_irqrestore(&input_lock,flags); return -1; }
    struct zeroos_input_device *dev=&input_devices[slot-1];
    if (!dev->used || dev->generation!=gen) { spin_unlock_irqrestore(&input_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&dev->lock);
    if (dev->count>=ZEROOS_INPUT_MAX_EVENTS) {
        dev->dropped_count++;
        spin_unlock_irqrestore(&dev->lock,dflags);
        spin_unlock_irqrestore(&input_lock,flags);
        return -1;
    }
    dev->ring[dev->tail]=*ev;
    dev->tail=(dev->tail+1)%ZEROOS_INPUT_MAX_EVENTS;
    dev->count++;
    dev->event_count++;
    spin_unlock_irqrestore(&dev->lock,dflags);
    (void)wait_queue_wake_all(&dev->event_waiters);
    spin_unlock_irqrestore(&input_lock,flags);
    return 0;
}

int input_device_read_events(uint64_t device_id, struct zeroos_input_event *buf, uint32_t cap,
                              uint32_t *read_out, uint64_t timeout_ticks) {
    if (!buf || !read_out || cap==0) return -1;
    uint64_t flags=spin_lock_irqsave(&input_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    if (slot==0 || slot>ZEROOS_INPUT_MAX_DEVICES || gen==0) { spin_unlock_irqrestore(&input_lock,flags); return -1; }
    struct zeroos_input_device *dev=&input_devices[slot-1];
    if (!dev->used || dev->generation!=gen) { spin_unlock_irqrestore(&input_lock,flags); return -1; }
    spin_unlock_irqrestore(&input_lock,flags);
    /* Non-blocking read for now; timeout path would integrate scheduler block */
    uint64_t dflags=spin_lock_irqsave(&dev->lock);
    uint32_t to_read = dev->count < cap ? dev->count : cap;
    for (uint32_t i=0;i<to_read;++i) {
        buf[i]=dev->ring[dev->head];
        dev->head=(dev->head+1)%ZEROOS_INPUT_MAX_EVENTS;
    }
    dev->count-=to_read;
    *read_out=to_read;
    spin_unlock_irqrestore(&dev->lock,dflags);
    (void)timeout_ticks;
    return 0;
}

int input_device_set_state(uint64_t device_id, enum zeroos_input_state state) {
    uint64_t flags=spin_lock_irqsave(&input_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    if (slot==0 || slot>ZEROOS_INPUT_MAX_DEVICES || gen==0) { spin_unlock_irqrestore(&input_lock,flags); return -1; }
    struct zeroos_input_device *dev=&input_devices[slot-1];
    if (!dev->used || dev->generation!=gen) { spin_unlock_irqrestore(&input_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&dev->lock);
    dev->state=state;
    spin_unlock_irqrestore(&dev->lock,dflags);
    spin_unlock_irqrestore(&input_lock,flags);
    return 0;
}

struct zeroos_input_device *input_device_lookup(uint64_t device_id) {
    uint64_t flags=spin_lock_irqsave(&input_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    struct zeroos_input_device *dev=0;
    if (slot && slot<=ZEROOS_INPUT_MAX_DEVICES && gen) {
        struct zeroos_input_device *d=&input_devices[slot-1];
        if (d->used && d->generation==gen) dev=d;
    }
    spin_unlock_irqrestore(&input_lock,flags);
    return dev;
}

int input_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&input_lock);
    for (uint32_t i=0;i<ZEROOS_INPUT_MAX_DEVICES;++i) if (input_devices[i].used && input_devices[i].count>ZEROOS_INPUT_MAX_EVENTS) { spin_unlock_irqrestore(&input_lock,flags); return -1; }
    spin_unlock_irqrestore(&input_lock,flags);
    return 0;
}
