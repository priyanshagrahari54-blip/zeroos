#ifndef ZEROOS_MOUSE_CORE_H
#define ZEROOS_MOUSE_CORE_H

#include "types.h"

/* Portable 3-byte PS/2 mouse packet decoder (i8042 auxiliary device
 * protocol). Host-testable with no I/O: the driver feeds raw bytes and
 * drains pointer events. Device Y grows upward (hardware convention);
 * screen-space translation is desktop policy, in userspace.
 *
 * Pointer button codes mirror enum zeroos_pointer_code in the public ABI
 * (drift-gated by userspace/tests/abi_consistency.py): motion = 0,
 * left/right/middle/side/extra = 1..5 — the same numbering the desktop
 * input router uses for zd_input_pointer_button. */
enum mouse_event_kind { MOUSE_EV_MOTION = 0, MOUSE_EV_BUTTON = 1 };
enum pointer_code {
    POINTER_MOTION = 0,
    POINTER_BTN_LEFT = 1,
    POINTER_BTN_RIGHT = 2,
    POINTER_BTN_MIDDLE = 3,
    POINTER_BTN_SIDE = 4,
    POINTER_BTN_EXTRA = 5
};

#define MOUSE_EVENT_QUEUE_CAP 4

struct mouse_event {
    uint8_t kind;       /* enum mouse_event_kind */
    uint8_t button;     /* BUTTON: enum pointer_code */
    uint8_t pressed;    /* BUTTON: 1 = press, 0 = release */
    uint8_t buttons;    /* bitmask after this event: 1 = L, 2 = R, 4 = M */
    int16_t dx;         /* MOTION: relative X (9-bit two's complement) */
    int16_t dy;         /* MOTION: relative Y, device space (up positive) */
};

struct mouse_decoder {
    uint8_t index;                       /* next packet byte (0..2) */
    uint8_t packet[3];
    uint8_t buttons;                     /* last reported button mask */
    uint8_t skip_packet;                 /* byte0 had overflow — discard */
    uint8_t pending_count;
    struct mouse_event pending[MOUSE_EVENT_QUEUE_CAP];
};

void mouse_decoder_init(struct mouse_decoder *decoder);

/* Feed one raw byte. Returns 1 when the byte completed a packet, else 0.
 * Completed packets may queue up to four events (per-button transitions
 * plus one motion); drain them with mouse_decoder_next. Packets with
 * overflow flags are consumed without events; bytes without the sync bit
 * resynchronize the stream without corrupting packet assembly. */
int mouse_decoder_feed(struct mouse_decoder *decoder, uint8_t byte);

/* Pop one queued event: 1 = event returned, 0 = queue empty. */
int mouse_decoder_next(struct mouse_decoder *decoder,
                       struct mouse_event *event);

#endif
