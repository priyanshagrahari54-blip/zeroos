#include "mouse_core.h"

/* Byte 0 layout (standard PS/2): bits 0-2 buttons, bit 3 sync (=1),
 * bit 4 X sign, bit 5 X overflow, bit 6 Y sign, bit 7 Y overflow.
 * Bytes 1/2 are X/Y data; with the sign bits they form 9-bit two's
 * complement values (bit 8 weight -256). */
#define MOUSE_SYNC      0x08U
#define MOUSE_X_SIGN    0x10U
#define MOUSE_X_OVER    0x20U
#define MOUSE_Y_SIGN    0x40U
#define MOUSE_Y_OVER    0x80U

void mouse_decoder_init(struct mouse_decoder *decoder) {
    decoder->index = 0;
    decoder->packet[0] = 0;
    decoder->packet[1] = 0;
    decoder->packet[2] = 0;
    decoder->buttons = 0;
    decoder->skip_packet = 0;
    decoder->pending_count = 0;
    for (uint32_t i = 0; i < MOUSE_EVENT_QUEUE_CAP; ++i) {
        decoder->pending[i].kind = 0;
        decoder->pending[i].button = 0;
        decoder->pending[i].pressed = 0;
        decoder->pending[i].buttons = 0;
        decoder->pending[i].dx = 0;
        decoder->pending[i].dy = 0;
    }
}

static void queue_event(struct mouse_decoder *decoder,
                        const struct mouse_event *event) {
    if (decoder->pending_count < MOUSE_EVENT_QUEUE_CAP)
        decoder->pending[decoder->pending_count++] = *event;
}

static int16_t extend9(uint8_t data, uint8_t sign_mask, uint8_t byte0) {
    int32_t value = data;
    if (byte0 & sign_mask)
        value -= 256;
    return (int16_t)value;
}

static void queue_transition(struct mouse_decoder *decoder, uint8_t bit,
                             uint8_t button, uint8_t old_state,
                             uint8_t new_state, uint8_t *state) {
    if ((old_state & bit) == (new_state & bit))
        return;
    if (new_state & bit)
        *state |= bit;
    else
        *state &= (uint8_t)~bit;
    struct mouse_event event;
    event.kind = MOUSE_EV_BUTTON;
    event.button = button;
    event.pressed = (new_state & bit) ? 1U : 0U;
    event.buttons = *state;
    event.dx = 0;
    event.dy = 0;
    queue_event(decoder, &event);
}

int mouse_decoder_feed(struct mouse_decoder *decoder, uint8_t byte) {
    if (decoder->index == 0) {
        if (!(byte & MOUSE_SYNC))
            return 0; /* resync: consume stray bytes, wait for sync */
        decoder->packet[0] = byte;
        decoder->skip_packet =
            (byte & (MOUSE_X_OVER | MOUSE_Y_OVER)) ? 1U : 0U;
        decoder->index = 1;
        return 0;
    }
    decoder->packet[decoder->index] = byte;
    if (decoder->index < 2) {
        decoder->index = 2;
        return 0;
    }

    decoder->index = 0;
    if (decoder->skip_packet) {
        decoder->skip_packet = 0;
        return 1; /* consumed, deliberately emitted nothing */
    }

    uint8_t byte0 = decoder->packet[0];
    int16_t dx = extend9(decoder->packet[1], MOUSE_X_SIGN, byte0);
    int16_t dy = extend9(decoder->packet[2], MOUSE_Y_SIGN, byte0);
    uint8_t buttons = byte0 & 0x07U;
    uint8_t old_state = decoder->buttons;
    uint8_t state = old_state;

    queue_transition(decoder, 0x01U, POINTER_BTN_LEFT, old_state, buttons,
                     &state);
    queue_transition(decoder, 0x02U, POINTER_BTN_RIGHT, old_state, buttons,
                     &state);
    queue_transition(decoder, 0x04U, POINTER_BTN_MIDDLE, old_state, buttons,
                     &state);

    if (dx || dy) {
        struct mouse_event event;
        event.kind = MOUSE_EV_MOTION;
        event.button = POINTER_MOTION;
        event.pressed = 0;
        event.buttons = state;
        event.dx = dx;
        event.dy = dy;
        queue_event(decoder, &event);
    }

    decoder->buttons = buttons;
    return 1;
}

int mouse_decoder_next(struct mouse_decoder *decoder,
                       struct mouse_event *event) {
    if (!event || decoder->pending_count == 0)
        return 0;
    *event = decoder->pending[0];
    for (uint32_t i = 1; i < decoder->pending_count; ++i)
        decoder->pending[i - 1] = decoder->pending[i];
    decoder->pending_count--;
    return 1;
}
