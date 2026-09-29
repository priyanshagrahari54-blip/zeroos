#include <assert.h>
#include <string.h>
#include "../kernel/scancode_core.h"

static int feed(struct scancode_decoder *d, unsigned char byte,
                struct zeroos_input_event *ev) {
    memset(ev, 0, sizeof(*ev));
    return scancode_feed(d, byte, ev);
}

/*
 * Regression for the extended-key down-state overrun: the index used for
 * per-key bookkeeping is (e0 << 7) | make_code, i.e. the whole byte range,
 * while the down-state table used to be 128 entries long.  Every E0-prefixed
 * key (arrows, numpad enter, right ctrl/alt, super, ...) therefore read and
 * wrote past the array — inside the decoder it clobbered prefix/modifier
 * state, and past it whatever followed the object in memory (ASan reported
 * the stack-buffer-overflow in scancode_feed).  The canary pins the
 * "nothing after the decoder is touched" invariant, and the assertions pin
 * the observable behaviour: extended keys must not alias base keys and must
 * not disturb unrelated decoder state.
 */
struct guarded_decoder {
    struct scancode_decoder decoder;
    unsigned char canary[64];
};

static void extended_key_space_and_neighbour_safety(
    struct scancode_decoder *d, struct zeroos_input_event *ev) {
    struct guarded_decoder g;
    unsigned char byte;

    /* Table must cover the whole (e0, code) index space. */
    assert(SCANCODER_TABLE_SIZE == 256U);

    memset(&g, 0, sizeof(g));
    for (unsigned i = 0; i < sizeof(g.canary); ++i)
        g.canary[i] = (unsigned char)(0xA5U + i);
    scancode_decoder_init(&g.decoder);

    /* Hold a base modifier so an extended-key write into the neighbouring
     * state would show up as a lost/changed modifier. */
    assert(feed(&g.decoder, 0x2a, ev) == 1);    /* left shift down */

    /* Sweep every extended code: E0+make then E0+break.  The prefix is
     * consumed by exactly one byte, so the break needs its own E0 —
     * otherwise the break decodes as a base key and legitimately releases
     * the base shift held above. */
    for (unsigned i = 0; i < 256U; ++i) {
        byte = (unsigned char)i;
        assert(feed(&g.decoder, 0xe0, ev) == 0);
        (void)feed(&g.decoder, byte, ev);                       /* make */
        assert(feed(&g.decoder, 0xe0, ev) == 0);
        (void)feed(&g.decoder, (unsigned char)(byte | 0x80), ev); /* break */
        for (unsigned j = 0; j < sizeof(g.canary); ++j)
            assert(g.canary[j] == (unsigned char)(0xA5U + j));
    }
    /* Nothing may remain held down after the sweep: an orphan break must be
     * ignored for every extended code, and the still-held left shift must
     * have survived. */
    for (unsigned i = 0; i < 256U; ++i) {
        assert(feed(&g.decoder, 0xe0, ev) == 0);
        assert(feed(&g.decoder, (unsigned char)(i | 0x80), ev) == 0);
    }
    assert(feed(&g.decoder, 0x02, ev) == 1 && ev->code == '!'); /* shift alive */
    assert(feed(&g.decoder, 0x82, ev) == 1 && ev->code == '!');
    assert(feed(&g.decoder, 0xaa, ev) == 1 && ev->code == ZEROOS_KEY_SHIFT_L);
    assert(ev->flags == 0);

    /* Same scancode, different E0 state -> different keys, no aliasing. */
    scancode_decoder_init(d);
    assert(feed(d, 0x1d, ev) == 1 && ev->code == ZEROOS_KEY_CTRL_L);
    assert(feed(d, 0xe0, ev) == 0);
    assert(feed(d, 0x1d, ev) == 1 && ev->code == ZEROOS_KEY_CTRL_R);
    /* Releasing the extended ctrl must not release the base one. */
    assert(feed(d, 0xe0, ev) == 0);
    assert(feed(d, 0x9d, ev) == 1 && ev->code == ZEROOS_KEY_CTRL_R);
    assert(ev->flags == 0);
    assert(feed(d, 0x9d, ev) == 1 && ev->code == ZEROOS_KEY_CTRL_L);
    assert(ev->flags == 0);
    /* The same split holds for the scancode shared by alt / right alt. */
    assert(feed(d, 0x38, ev) == 1 && ev->code == ZEROOS_KEY_ALT_L);
    assert(feed(d, 0xe0, ev) == 0);
    assert(feed(d, 0x38, ev) == 1 && ev->code == ZEROOS_KEY_ALT_R);
    assert(feed(d, 0xe0, ev) == 0);
    assert(feed(d, 0xb8, ev) == 1 && ev->code == ZEROOS_KEY_ALT_R);
    assert(feed(d, 0xb8, ev) == 1 && ev->code == ZEROOS_KEY_ALT_L);

    /* Highest extended index (0xFF with E0 -> index 0xFF) is in range. */
    assert(feed(d, 0xe0, ev) == 0);
    assert(feed(d, 0xff, ev) == 0);             /* unknown extended code */
    assert(feed(d, 0xe0, ev) == 0);
    assert(feed(d, 0x7f, ev) == 0);
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

    extended_key_space_and_neighbour_safety(&d, &ev);

    return 0;
}
