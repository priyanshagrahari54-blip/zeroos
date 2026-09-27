#include <assert.h>
#include <string.h>
#include "../kernel/scancode_core.h"

static int feed(struct scancode_decoder *d, unsigned char byte,
                struct zeroos_input_event *ev) {
    memset(ev, 0, sizeof(*ev));
    return scancode_feed(d, byte, ev);
}

int main(void) {
    struct scancode_decoder d;
    struct zeroos_input_event ev;

    scancode_decoder_init(&d);

    /* Plain letter make/break. */
    assert(feed(&d, 0x1e, &ev) == 1);           /* 'a' make */
    assert(ev.code == 'a' && ev.kind == ZEROOS_INPUT_KIND_KEY);
    assert(ev.flags == ZEROOS_INPUT_FLAG_DOWN && ev.value == 1);
    assert(feed(&d, 0x9e, &ev) == 1);           /* 'a' break */
    assert(ev.code == 'a' && ev.flags == 0 && ev.value == 0);

    /* Typematic repeat carries the REPEAT flag. */
    assert(feed(&d, 0x1e, &ev) == 1);
    assert(feed(&d, 0x1e, &ev) == 1);
    assert(ev.flags == (ZEROOS_INPUT_FLAG_DOWN | ZEROOS_INPUT_FLAG_REPEAT));
    assert(feed(&d, 0x9e, &ev) == 1);
    assert(feed(&d, 0x9e, &ev) == 0);           /* orphan break ignored */

    /* Shifted digits. */
    assert(feed(&d, 0x2a, &ev) == 1);           /* LSHIFT down */
    assert(ev.code == ZEROOS_KEY_SHIFT_L);
    assert(feed(&d, 0x02, &ev) == 1);           /* '1' -> '!' */
    assert(ev.code == '!');
    assert(feed(&d, 0x82, &ev) == 1);
    assert(ev.code == '!');
    assert(feed(&d, 0xaa, &ev) == 1);           /* LSHIFT up */
    assert(ev.code == ZEROOS_KEY_SHIFT_L && ev.flags == 0);
    assert(feed(&d, 0x02, &ev) == 1);
    assert(ev.code == '1');

    /* Caps lock affects letters only, then shift inverts. */
    assert(feed(&d, 0x3a, &ev) == 1);           /* caps make toggles */
    assert(ev.code == ZEROOS_KEY_CAPS_LOCK);
    assert(feed(&d, 0xba, &ev) == 1);           /* caps break */
    assert(feed(&d, 0x1e, &ev) == 1);
    assert(ev.code == 'A');
    assert(feed(&d, 0x9e, &ev) == 1);
    assert(feed(&d, 0x2a, &ev) == 1);           /* shift held */
    assert(feed(&d, 0x1e, &ev) == 1);
    assert(ev.code == 'a');                     /* caps xor shift */
    assert(feed(&d, 0x9e, &ev) == 1);
    assert(feed(&d, 0xaa, &ev) == 1);
    assert(feed(&d, 0x3a, &ev) == 1);           /* caps off */
    assert(feed(&d, 0xba, &ev) == 1);

    /* Extended arrows and navigation. */
    assert(feed(&d, 0xe0, &ev) == 0);
    assert(feed(&d, 0x48, &ev) == 1);
    assert(ev.code == ZEROOS_KEY_UP);
    assert(feed(&d, 0xe0, &ev) == 0);
    assert(feed(&d, 0xc8, &ev) == 1);
    assert(ev.code == ZEROOS_KEY_UP && ev.flags == 0);
    assert(feed(&d, 0xe0, &ev) == 0);
    assert(feed(&d, 0x4b, &ev) == 1);
    assert(ev.code == ZEROOS_KEY_LEFT);
    assert(feed(&d, 0xe0, &ev) == 0);
    assert(feed(&d, 0xcb, &ev) == 1);
    assert(ev.code == ZEROOS_KEY_LEFT);

    /* Right-hand modifiers are distinct from left. */
    assert(feed(&d, 0xe0, &ev) == 0);
    assert(feed(&d, 0x1d, &ev) == 1);
    assert(ev.code == ZEROOS_KEY_CTRL_R);
    assert(feed(&d, 0xe0, &ev) == 0);
    assert(feed(&d, 0x9d, &ev) == 1);
    assert(ev.code == ZEROOS_KEY_CTRL_R && ev.flags == 0);

    /* Enter/specials and rollover: release reports the pressed code even
     * when modifier state changed between make and break. */
    assert(feed(&d, 0x2a, &ev) == 1);           /* shift down */
    assert(feed(&d, 0x1c, &ev) == 1);
    assert(ev.code == ZEROOS_KEY_ENTER);        /* Enter not shift-mapped */
    assert(feed(&d, 0xaa, &ev) == 1);           /* shift released first */
    assert(feed(&d, 0x9c, &ev) == 1);
    assert(ev.code == ZEROOS_KEY_ENTER && ev.flags == 0);

    /* Unrecognized and empty-input paths. */
    assert(feed(&d, 0x00, &ev) == 0);
    assert(feed(&d, 0xe0, &ev) == 0);
    assert(feed(&d, 0x00, &ev) == 0);           /* extended unknown */
    assert(scancode_feed(NULL, 0x1e, &ev) == 0);
    assert(scancode_feed(&d, 0x1e, NULL) == 0);

    /* Reset clears modifier state. */
    assert(feed(&d, 0x2a, &ev) == 1);
    scancode_decoder_init(&d);
    assert(feed(&d, 0x02, &ev) == 1 && ev.code == '1');

    return 0;
}
