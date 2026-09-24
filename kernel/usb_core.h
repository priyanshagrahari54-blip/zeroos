#ifndef ZEROOS_USB_CORE_H
#define ZEROOS_USB_CORE_H
#include "types.h"
#define USB_DESC_LIMIT 4096
struct usb_descriptor_summary { uint16_t count; uint8_t device_seen, configuration_seen; };
/* Validates descriptor framing and lengths only; no controller/device support implied. */
int usb_validate_descriptors(const uint8_t *data,uint32_t length,struct usb_descriptor_summary *out);
#endif
