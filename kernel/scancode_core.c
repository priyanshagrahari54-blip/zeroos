#include "scancode_core.h"

/* Scancode set-1 make codes with translation enabled (XT subset). */
#define SC_PREFIX     0xe0U
#define SC_BREAK      0x80U
#define SC_ESCAPE     0x01U
#define SC_BACKSPACE  0x0eU
#define SC_TAB        0x0fU
#define SC_ENTER      0x1cU
#define SC_CTRL       0x1dU
#define SC_LSHIFT     0x2aU
#define SC_RSHIFT     0x36U
#define SC_ALT        0x38U
#define SC_CAPS       0x3aU
#define SC_RCTRL_E0   0x1dU
#define SC_RALT_E0    0x38U
#define SC_SUPER_L_E0 0x5bU
#define SC_SUPER_R_E0 0x5cU
#define SC_HOME_E0    0x47U
#define SC_UP_E0      0x48U
#define SC_PGUP_E0    0x49U
#define SC_LEFT_E0    0x4bU
#define SC_RIGHT_E0   0x4dU
#define SC_END_E0     0x4fU
#define SC_DOWN_E0    0x50U
#define SC_PGDN_E0    0x51U
#define SC_INSERT_E0  0x52U
#define SC_DELETE_E0  0x53U

void scancode_decoder_init(struct scancode_decoder *decoder) {
    if (!decoder)
        return;
    for (uint32_t i = 0; i < SCANCODER_TABLE_SIZE; ++i)
        decoder->down_codes[i] = 0;
    decoder->prefix_e0 = 0;
    decoder->shift_l = 0;
    decoder->shift_r = 0;
    decoder->ctrl_l = 0;
    decoder->ctrl_r = 0;
    decoder->alt_l = 0;
    decoder->alt_r = 0;
    decoder->caps_on = 0;
}

static uint16_t base_key(uint8_t code) {
    /* Non-extended base map: ASCII for printable, ZEROOS_KEY_* above. */
    static const uint16_t table[0x54] = {
        0,
        ZEROOS_KEY_ESCAPE, '1', '2', '3', '4', '5', '6', '7', '8', '9',
        '0', '-', '=', ZEROOS_KEY_BACKSPACE,
        ZEROOS_KEY_TAB, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
        '[', ']', ZEROOS_KEY_ENTER, ZEROOS_KEY_CTRL_L,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        ZEROOS_KEY_SHIFT_L, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm',
        ',', '.', '/', ZEROOS_KEY_SHIFT_R,
        '*', ZEROOS_KEY_ALT_L, ' ', ZEROOS_KEY_CAPS_LOCK,
        ZEROOS_KEY_F1, ZEROOS_KEY_F2, ZEROOS_KEY_F3, ZEROOS_KEY_F4,
        ZEROOS_KEY_F5, ZEROOS_KEY_F6, ZEROOS_KEY_F7, ZEROOS_KEY_F8,
        ZEROOS_KEY_F9, ZEROOS_KEY_F10, ZEROOS_KEY_F11, ZEROOS_KEY_F12,
        /* 0x47..0x53 keypad (numlock-on digit mapping). */
        '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
    };
    if (code >= sizeof(table) / sizeof(table[0]))
        return 0;
    return table[code];
}

static uint16_t extended_key(uint8_t code) {
    switch (code) {
    case 0x1c: return ZEROOS_KEY_ENTER;          /* keypad enter */
    case SC_RCTRL_E0: return ZEROOS_KEY_CTRL_R;
    case SC_RALT_E0: return ZEROOS_KEY_ALT_R;
    case SC_SUPER_L_E0: return ZEROOS_KEY_SUPER_L;
    case SC_SUPER_R_E0: return ZEROOS_KEY_SUPER_R;
    case SC_HOME_E0: return ZEROOS_KEY_HOME;
    case SC_UP_E0: return ZEROOS_KEY_UP;
    case SC_PGUP_E0: return ZEROOS_KEY_PAGE_UP;
    case SC_LEFT_E0: return ZEROOS_KEY_LEFT;
    case SC_RIGHT_E0: return ZEROOS_KEY_RIGHT;
    case SC_END_E0: return ZEROOS_KEY_END;
    case SC_DOWN_E0: return ZEROOS_KEY_DOWN;
    case SC_PGDN_E0: return ZEROOS_KEY_PAGE_DOWN;
    case SC_INSERT_E0: return ZEROOS_KEY_INSERT;
    case SC_DELETE_E0: return ZEROOS_KEY_DELETE;
    case 0x35: return '/';
    default: return 0;
    }
}

static uint16_t apply_shift(uint16_t code, uint8_t shifted) {
    if (!shifted)
        return code;
    switch (code) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case '`': return '~';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    default: return code;
    }
}

static uint16_t apply_letter_case(uint16_t code, uint8_t upper) {
    if (code >= 'a' && code <= 'z')
        return upper ? (uint16_t)(code - 'a' + 'A') : code;
    return code;
}

static int emit(struct zeroos_input_event *out, uint16_t code,
                uint8_t flags, int32_t value) {
    out->timestamp = 0;
    out->device_id = 0;
    out->x = 0;
    out->y = 0;
    out->value = value;
    out->code = code;
    out->kind = ZEROOS_INPUT_KIND_KEY;
    out->flags = flags;
    return 1;
}

static void update_modifier(struct scancode_decoder *d, uint16_t code,
                            uint8_t down) {
    switch (code) {
    case ZEROOS_KEY_SHIFT_L: d->shift_l = down; break;
    case ZEROOS_KEY_SHIFT_R: d->shift_r = down; break;
    case ZEROOS_KEY_CTRL_L: d->ctrl_l = down; break;
    case ZEROOS_KEY_CTRL_R: d->ctrl_r = down; break;
    case ZEROOS_KEY_ALT_L: d->alt_l = down; break;
    case ZEROOS_KEY_ALT_R: d->alt_r = down; break;
    default: break;
    }
}

int scancode_feed(struct scancode_decoder *decoder, uint8_t byte,
                  struct zeroos_input_event *out) {
    uint8_t extended;
    uint8_t release;
    uint8_t index;
    uint16_t key;
    uint8_t shifted;
    uint8_t was_down;

    if (!decoder || !out)
        return 0;

    if (byte == SC_PREFIX) {
        decoder->prefix_e0 = 1;
        return 0;
    }
    extended = decoder->prefix_e0;
    decoder->prefix_e0 = 0;
    release = (byte & SC_BREAK) != 0;
    byte &= (uint8_t)~SC_BREAK;
    index = (uint8_t)((extended ? 0x80U : 0U) | byte);

    key = extended ? extended_key(byte) : base_key(byte);
    if (!key)
        return 0;

    if (byte == SC_CAPS && !extended) {
        /* Caps lock toggles on make; both edges emit key events. */
        if (!release)
            decoder->caps_on = (uint8_t)!decoder->caps_on;
        return emit(out, key,
                    release ? 0 : ZEROOS_INPUT_FLAG_DOWN,
                    release ? 0 : 1);
    }

    was_down = decoder->down_codes[index] != 0;
    if (release) {
        uint16_t pressed = decoder->down_codes[index];
        if (!pressed)
            return 0; /* orphan break: nothing to release */
        decoder->down_codes[index] = 0;
        update_modifier(decoder, pressed, 0);
        return emit(out, pressed, 0, 0);
    }

    /* Make. Modifiers keep their identity regardless of shift/caps. */
    if (key == ZEROOS_KEY_SHIFT_L || key == ZEROOS_KEY_SHIFT_R ||
        key == ZEROOS_KEY_CTRL_L || key == ZEROOS_KEY_CTRL_R ||
        key == ZEROOS_KEY_ALT_L || key == ZEROOS_KEY_ALT_R) {
        decoder->down_codes[index] = key;
        update_modifier(decoder, key, 1);
        return emit(out, key, ZEROOS_INPUT_FLAG_DOWN, 1);
    }

    shifted = (decoder->shift_l || decoder->shift_r) ? 1 : 0;
    key = apply_shift(key, shifted);
    if (!shifted && decoder->caps_on)
        key = apply_letter_case(key, 1);
    else if (shifted && decoder->caps_on)
        key = apply_letter_case(key, 0);

    decoder->down_codes[index] = key;
    return emit(out, key,
                ZEROOS_INPUT_FLAG_DOWN |
                    (was_down ? ZEROOS_INPUT_FLAG_REPEAT : 0),
                1);
}
