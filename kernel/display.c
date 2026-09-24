#include "display.h"
#include "memory.h"
#include "vmm.h"

static struct spinlock display_lock;
static struct zeroos_display_device displays[ZEROOS_DISPLAY_MAX_DEVICES];
static uint64_t next_display_id;

int display_system_init(void) {
    spinlock_init(&display_lock);
    next_display_id=1;
    for (uint32_t i=0;i<ZEROOS_DISPLAY_MAX_DEVICES;++i) {
        displays[i].used=0;
        displays[i].generation=0;
        displays[i].state=ZEROOS_DISPLAY_STOPPED;
        displays[i].mode_count=0;
        displays[i].connected=0;
        displays[i].enabled=0;
        spinlock_init(&displays[i].lock);
    }
    return 0;
}

int display_device_register(enum zeroos_display_type type, const char *name,
                            uint64_t fb_phys, uint64_t fb_size,
                            uint64_t *device_id_out) {
    if (!name || !device_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&display_lock);
    for (uint32_t i=0;i<ZEROOS_DISPLAY_MAX_DEVICES;++i) {
        if (displays[i].used) continue;
        if (displays[i].generation==0xffffffffU) continue;
        displays[i].generation++;
        if (displays[i].generation==0) continue;
        displays[i].used=1;
        displays[i].type=type;
        displays[i].state=ZEROOS_DISPLAY_DORMANT;
        displays[i].framebuffer_phys=fb_phys;
        displays[i].framebuffer_size=fb_size;
        displays[i].framebuffer_virt=0;
        displays[i].current_mode_index=0;
        displays[i].mode_count=0;
        displays[i].connected=1;
        displays[i].enabled=0;
        displays[i].gpu_memory_budget=256*1024*1024;
        displays[i].vsync_count=0;
        uint32_t n=0;
        while (n<ZEROOS_DISPLAY_MAX_NAME-1 && name[n]) { displays[i].name[n]=name[n]; n++; }
        displays[i].name[n]=0;
        displays[i].id = ((uint64_t)displays[i].generation<<16) | (uint64_t)(i+1);
        *device_id_out=displays[i].id;
        spin_unlock_irqrestore(&display_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&display_lock,flags);
    return -1;
}

int display_device_add_mode(uint64_t device_id, uint32_t width, uint32_t height,
                            uint32_t refresh, uint32_t bpp, uint64_t pitch) {
    if (width==0 || height==0 || bpp==0) return -1;
    uint64_t flags=spin_lock_irqsave(&display_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    if (slot==0 || slot>ZEROOS_DISPLAY_MAX_DEVICES || gen==0) { spin_unlock_irqrestore(&display_lock,flags); return -1; }
    struct zeroos_display_device *dev=&displays[slot-1];
    if (!dev->used || dev->generation!=gen) { spin_unlock_irqrestore(&display_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&dev->lock);
    if (dev->mode_count>=ZEROOS_DISPLAY_MAX_MODES) { spin_unlock_irqrestore(&dev->lock,dflags); spin_unlock_irqrestore(&display_lock,flags); return -1; }
    struct zeroos_display_mode *m=&dev->modes[dev->mode_count++];
    m->width=width;
    m->height=height;
    m->refresh_hz=refresh;
    m->bpp=bpp;
    m->pitch=pitch ? pitch : (uint64_t)width*bpp/8;
    m->valid=1;
    spin_unlock_irqrestore(&dev->lock,dflags);
    spin_unlock_irqrestore(&display_lock,flags);
    return 0;
}

int display_device_set_mode(uint64_t device_id, uint32_t mode_index) {
    uint64_t flags=spin_lock_irqsave(&display_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    if (slot==0 || slot>ZEROOS_DISPLAY_MAX_DEVICES || gen==0) { spin_unlock_irqrestore(&display_lock,flags); return -1; }
    struct zeroos_display_device *dev=&displays[slot-1];
    if (!dev->used || dev->generation!=gen) { spin_unlock_irqrestore(&display_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&dev->lock);
    if (mode_index>=dev->mode_count || !dev->modes[mode_index].valid) { spin_unlock_irqrestore(&dev->lock,dflags); spin_unlock_irqrestore(&display_lock,flags); return -1; }
    dev->current_mode_index=mode_index;
    dev->state=ZEROOS_DISPLAY_ACTIVE;
    dev->enabled=1;
    spin_unlock_irqrestore(&dev->lock,dflags);
    spin_unlock_irqrestore(&display_lock,flags);
    return 0;
}

int display_device_set_state(uint64_t device_id, enum zeroos_display_state state) {
    struct zeroos_display_device *dev=0;
    uint64_t flags=spin_lock_irqsave(&display_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    if (slot && slot<=ZEROOS_DISPLAY_MAX_DEVICES && gen) {
        struct zeroos_display_device *d=&displays[slot-1];
        if (d->used && d->generation==gen) dev=d;
    }
    spin_unlock_irqrestore(&display_lock,flags);
    if (!dev) return -1;
    uint64_t dflags=spin_lock_irqsave(&dev->lock);
    dev->state=state;
    spin_unlock_irqrestore(&dev->lock,dflags);
    return 0;
}

struct zeroos_display_device *display_device_lookup(uint64_t device_id) {
    uint64_t flags=spin_lock_irqsave(&display_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    struct zeroos_display_device *dev=0;
    if (slot && slot<=ZEROOS_DISPLAY_MAX_DEVICES && gen) {
        struct zeroos_display_device *d=&displays[slot-1];
        if (d->used && d->generation==gen) dev=d;
    }
    spin_unlock_irqrestore(&display_lock,flags);
    return dev;
}

int display_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&display_lock);
    for (uint32_t i=0;i<ZEROOS_DISPLAY_MAX_DEVICES;++i) {
        if (!displays[i].used) continue;
        if (displays[i].mode_count>ZEROOS_DISPLAY_MAX_MODES) { spin_unlock_irqrestore(&display_lock,flags); return -1; }
    }
    spin_unlock_irqrestore(&display_lock,flags);
    return 0;
}
