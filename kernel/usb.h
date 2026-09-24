#ifndef ZEROOS_USB_H
#define ZEROOS_USB_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_USB_MAX_CONTROLLERS 4U
#define ZEROOS_USB_MAX_DEVICES 32U
#define ZEROOS_USB_MAX_ENDPOINTS 8U

enum zeroos_usb_controller_state {
    ZEROOS_USB_CTRL_STOPPED = 0,
    ZEROOS_USB_CTRL_DORMANT,
    ZEROOS_USB_CTRL_WARM,
    ZEROOS_USB_CTRL_ACTIVE,
    ZEROOS_USB_CTRL_THROTTLED,
    ZEROOS_USB_CTRL_SUSPENDED
};

enum zeroos_usb_device_state {
    ZEROOS_USB_DEV_DETACHED = 0,
    ZEROOS_USB_DEV_ATTACHED,
    ZEROOS_USB_DEV_POWERED,
    ZEROOS_USB_DEV_DEFAULT,
    ZEROOS_USB_DEV_ADDRESS,
    ZEROOS_USB_DEV_CONFIGURED,
    ZEROOS_USB_DEV_SUSPENDED
};

enum zeroos_usb_speed {
    ZEROOS_USB_SPEED_LOW = 0,
    ZEROOS_USB_SPEED_FULL,
    ZEROOS_USB_SPEED_HIGH,
    ZEROOS_USB_SPEED_SUPER
};

enum zeroos_usb_transfer_type {
    ZEROOS_USB_TRANSFER_CONTROL = 0,
    ZEROOS_USB_TRANSFER_ISOCHRONOUS,
    ZEROOS_USB_TRANSFER_BULK,
    ZEROOS_USB_TRANSFER_INTERRUPT
};

struct zeroos_usb_endpoint {
    uint8_t address;
    enum zeroos_usb_transfer_type type;
    uint16_t max_packet_size;
    uint8_t interval;
    uint8_t used;
};

struct zeroos_usb_controller {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_usb_controller_state state;
    uint64_t mmio_phys;
    uint64_t mmio_virt;
    uint64_t mmio_size;
    uint8_t irq;
    struct spinlock lock;
    uint64_t port_count;
    uint64_t device_count;
};

struct zeroos_usb_device {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_usb_device_state state;
    enum zeroos_usb_speed speed;
    uint64_t controller_id;
    uint8_t address;
    uint8_t port;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t class_code;
    uint8_t subclass_code;
    uint8_t protocol;
    uint8_t num_configurations;
    struct zeroos_usb_endpoint endpoints[ZEROOS_USB_MAX_ENDPOINTS];
    uint32_t endpoint_count;
    struct spinlock lock;
    struct wait_queue transfer_waiters;
};

int usb_system_init(void);
int usb_controller_register(uint64_t mmio_phys, uint64_t mmio_size, uint8_t irq, uint64_t *ctrl_id_out);
int usb_controller_set_state(uint64_t ctrl_id, enum zeroos_usb_controller_state state);
int usb_device_register(uint64_t controller_id, uint8_t port, enum zeroos_usb_speed speed,
                        uint16_t vendor, uint16_t product, uint8_t class_code,
                        uint64_t *device_id_out);
int usb_device_set_address(uint64_t device_id, uint8_t address);
int usb_device_configure(uint64_t device_id);
int usb_device_add_endpoint(uint64_t device_id, uint8_t ep_addr,
                            enum zeroos_usb_transfer_type type, uint16_t max_packet);
struct zeroos_usb_device *usb_device_lookup(uint64_t device_id);
int usb_debug_validate(void);

#endif
