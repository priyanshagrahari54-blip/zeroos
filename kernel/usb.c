#include "usb.h"

static struct spinlock usb_lock;
static struct zeroos_usb_controller controllers[ZEROOS_USB_MAX_CONTROLLERS];
static struct zeroos_usb_device devices[ZEROOS_USB_MAX_DEVICES];

int usb_system_init(void) {
    spinlock_init(&usb_lock);
    for (uint32_t i=0;i<ZEROOS_USB_MAX_CONTROLLERS;++i) {
        controllers[i].used=0;
        controllers[i].generation=0;
        controllers[i].state=ZEROOS_USB_CTRL_STOPPED;
        spinlock_init(&controllers[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_USB_MAX_DEVICES;++i) {
        devices[i].used=0;
        devices[i].generation=0;
        devices[i].state=ZEROOS_USB_DEV_DETACHED;
        spinlock_init(&devices[i].lock);
        wait_queue_init(&devices[i].transfer_waiters);
    }
    return 0;
}

int usb_controller_register(uint64_t mmio_phys, uint64_t mmio_size, uint8_t irq, uint64_t *ctrl_id_out) {
    if (!ctrl_id_out || mmio_phys==0 || mmio_size==0) return -1;
    if (mmio_phys & 0xfffULL) return -1;
    uint64_t flags=spin_lock_irqsave(&usb_lock);
    for (uint32_t i=0;i<ZEROOS_USB_MAX_CONTROLLERS;++i) {
        if (controllers[i].used) continue;
        if (controllers[i].generation==0xffffffffU) continue;
        controllers[i].generation++;
        if (controllers[i].generation==0) continue;
        controllers[i].used=1;
        controllers[i].state=ZEROOS_USB_CTRL_DORMANT;
        controllers[i].mmio_phys=mmio_phys;
        controllers[i].mmio_virt=0;
        controllers[i].mmio_size=mmio_size;
        controllers[i].irq=irq;
        controllers[i].port_count=0;
        controllers[i].device_count=0;
        controllers[i].id = ((uint64_t)controllers[i].generation<<16) | (uint64_t)(i+1);
        *ctrl_id_out=controllers[i].id;
        spin_unlock_irqrestore(&usb_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&usb_lock,flags);
    return -1;
}

int usb_controller_set_state(uint64_t ctrl_id, enum zeroos_usb_controller_state state) {
    uint64_t flags=spin_lock_irqsave(&usb_lock);
    uint32_t slot=(uint32_t)(ctrl_id & 0xffffULL);
    uint32_t gen=(uint32_t)(ctrl_id>>16);
    if (slot==0 || slot>ZEROOS_USB_MAX_CONTROLLERS || gen==0) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    struct zeroos_usb_controller *c=&controllers[slot-1];
    if (!c->used || c->generation!=gen) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    uint64_t cflags=spin_lock_irqsave(&c->lock);
    c->state=state;
    spin_unlock_irqrestore(&c->lock,cflags);
    spin_unlock_irqrestore(&usb_lock,flags);
    return 0;
}

int usb_device_register(uint64_t controller_id, uint8_t port, enum zeroos_usb_speed speed,
                        uint16_t vendor, uint16_t product, uint8_t class_code,
                        uint64_t *device_id_out) {
    if (!device_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&usb_lock);
    uint32_t cslot=(uint32_t)(controller_id & 0xffffULL);
    uint32_t cgen=(uint32_t)(controller_id>>16);
    if (cslot==0 || cslot>ZEROOS_USB_MAX_CONTROLLERS || cgen==0) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    struct zeroos_usb_controller *ctrl=&controllers[cslot-1];
    if (!ctrl->used || ctrl->generation!=cgen) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    for (uint32_t i=0;i<ZEROOS_USB_MAX_DEVICES;++i) {
        if (devices[i].used) continue;
        if (devices[i].generation==0xffffffffU) continue;
        devices[i].generation++;
        if (devices[i].generation==0) continue;
        devices[i].used=1;
        devices[i].state=ZEROOS_USB_DEV_ATTACHED;
        devices[i].speed=speed;
        devices[i].controller_id=controller_id;
        devices[i].port=port;
        devices[i].vendor_id=vendor;
        devices[i].product_id=product;
        devices[i].class_code=class_code;
        devices[i].address=0;
        devices[i].endpoint_count=0;
        devices[i].id = ((uint64_t)devices[i].generation<<16) | (uint64_t)(i+1);
        *device_id_out=devices[i].id;
        ctrl->device_count++;
        spin_unlock_irqrestore(&usb_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&usb_lock,flags);
    return -1;
}

int usb_device_set_address(uint64_t device_id, uint8_t address) {
    if (address==0 || address>127) return -1;
    uint64_t flags=spin_lock_irqsave(&usb_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    if (slot==0 || slot>ZEROOS_USB_MAX_DEVICES || gen==0) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    struct zeroos_usb_device *d=&devices[slot-1];
    if (!d->used || d->generation!=gen) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&d->lock);
    d->address=address;
    d->state=ZEROOS_USB_DEV_ADDRESS;
    spin_unlock_irqrestore(&d->lock,dflags);
    spin_unlock_irqrestore(&usb_lock,flags);
    return 0;
}

int usb_device_configure(uint64_t device_id) {
    uint64_t flags=spin_lock_irqsave(&usb_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    if (slot==0 || slot>ZEROOS_USB_MAX_DEVICES || gen==0) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    struct zeroos_usb_device *d=&devices[slot-1];
    if (!d->used || d->generation!=gen) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&d->lock);
    d->state=ZEROOS_USB_DEV_CONFIGURED;
    spin_unlock_irqrestore(&d->lock,dflags);
    spin_unlock_irqrestore(&usb_lock,flags);
    return 0;
}

int usb_device_add_endpoint(uint64_t device_id, uint8_t ep_addr,
                            enum zeroos_usb_transfer_type type, uint16_t max_packet) {
    if (max_packet==0) return -1;
    uint64_t flags=spin_lock_irqsave(&usb_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    if (slot==0 || slot>ZEROOS_USB_MAX_DEVICES || gen==0) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    struct zeroos_usb_device *d=&devices[slot-1];
    if (!d->used || d->generation!=gen) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&d->lock);
    if (d->endpoint_count>=ZEROOS_USB_MAX_ENDPOINTS) { spin_unlock_irqrestore(&d->lock,dflags); spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    struct zeroos_usb_endpoint *ep=&d->endpoints[d->endpoint_count++];
    ep->address=ep_addr;
    ep->type=type;
    ep->max_packet_size=max_packet;
    ep->used=1;
    spin_unlock_irqrestore(&d->lock,dflags);
    spin_unlock_irqrestore(&usb_lock,flags);
    return 0;
}

struct zeroos_usb_device *usb_device_lookup(uint64_t device_id) {
    uint64_t flags=spin_lock_irqsave(&usb_lock);
    uint32_t slot=(uint32_t)(device_id & 0xffffULL);
    uint32_t gen=(uint32_t)(device_id>>16);
    struct zeroos_usb_device *dev=0;
    if (slot && slot<=ZEROOS_USB_MAX_DEVICES && gen) {
        struct zeroos_usb_device *d=&devices[slot-1];
        if (d->used && d->generation==gen) dev=d;
    }
    spin_unlock_irqrestore(&usb_lock,flags);
    return dev;
}

int usb_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&usb_lock);
    for (uint32_t i=0;i<ZEROOS_USB_MAX_DEVICES;++i) if (devices[i].used && devices[i].endpoint_count>ZEROOS_USB_MAX_ENDPOINTS) { spin_unlock_irqrestore(&usb_lock,flags); return -1; }
    spin_unlock_irqrestore(&usb_lock,flags);
    return 0;
}
