#ifndef ZEROOS_SCANCODE_CORE_H
#define ZEROOS_SCANCODE_CORE_H

#include "syscall.h"

/*
 * PS/2 scancode set-1 decoder (i8042 translation mode).
 *
 * Pure state machine: no port I/O, no locks, no allocations — the kernel
 * input driver feeds it bytes from IRQ context and the host test suite
 * feeds it recorded streams. Tracks the E0 extended prefix, left/right
 * modifier state, caps-lock, typematic repeat and per-key down bookkeeping
 * so releases always report the code that was pressed.
 *
 * Emitted events carry kind/code/value/flags only; the caller fills
 * timestamp and device_id. Printable keys use their (shift/caps-adjusted)
 * ASCII value; non-printable keys use ZEROOS_KEY_* above 0xFF.
 */

#define SCANCODER_TABLE_SIZE 128U

struct scancode_decoder {
    /* (e0 << 7 | scancode) -> code emitted at make; 0 = not down. */
    uint16_t down_codes[SCANCODER_TABLE_SIZE];
    uint8_t prefix_e0;
    uint8_t shift_l, shift_r;
    uint8_t ctrl_l, ctrl_r;
    uint8_t alt_l, alt_r;
    uint8_t caps_on;
};

void scancode_decoder_init(struct scancode_decoder *decoder);

/*
 * Decode one scancode byte. Returns 1 when *out holds a completed event,
 * 0 when the byte changed internal state only (or was unrecognized).
 */
int scancode_feed(struct scancode_decoder *decoder, uint8_t byte,
                  struct zeroos_input_event *out);

#endif
