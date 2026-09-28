#include <stdio.h>
#include <string.h>
#include "mouse_core.h"

static int failures;

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        failures++;                                                 \
    }                                                               \
} while (0)

static void reset(struct mouse_decoder *d) {
    mouse_decoder_init(d);
}

static void feed(struct mouse_decoder *d, uint8_t a, uint8_t b, uint8_t c) {
    (void)mouse_decoder_feed(d, a);
    (void)mouse_decoder_feed(d, b);
    (void)mouse_decoder_feed(d, c);
}

static void test_motion_basic(void) {
    struct mouse_decoder d;
    struct mouse_event ev;
    reset(&d);
    /* sync only, +3 right, +2 device-up */
    feed(&d, 0x08, 0x03, 0x02);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.kind == MOUSE_EV_MOTION);
    CHECK(ev.button == POINTER_MOTION);
    CHECK(ev.dx == 3 && ev.dy == 2);
    CHECK(ev.buttons == 0);
    CHECK(mouse_decoder_next(&d, &ev) == 0);
}

static void test_motion_sign(void) {
    struct mouse_decoder d;
    struct mouse_event ev;
    reset(&d);
    /* X sign set: -1; Y sign clear, data 0xFE: +254 */
    feed(&d, 0x18, 0xFF, 0xFE);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.kind == MOUSE_EV_MOTION);
    CHECK(ev.dx == -1);
    CHECK(ev.dy == 254);
    reset(&d);
    /* Y sign set: 0x00 -> -256 (9-bit edge), X 0 */
    feed(&d, 0x48, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.dx == 0 && ev.dy == -256);
    reset(&d);
    /* X sign + data 0x80: 128-256 = -128 */
    feed(&d, 0x18, 0x80, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.dx == -128 && ev.dy == 0);
}

static void test_overflow_discard(void) {
    struct mouse_decoder d;
    struct mouse_event ev;
    reset(&d);
    /* X overflow set: packet consumed, nothing emitted */
    feed(&d, 0x28, 0x7F, 0x7F);
    CHECK(mouse_decoder_next(&d, &ev) == 0);
    /* next packet decodes normally */
    feed(&d, 0x08, 0x01, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.kind == MOUSE_EV_MOTION && ev.dx == 1 && ev.dy == 0);
}

static void test_resync(void) {
    struct mouse_decoder d;
    struct mouse_event ev;
    reset(&d);
    /* stray byte without sync bit is dropped; assembly recovers */
    (void)mouse_decoder_feed(&d, 0x00);
    (void)mouse_decoder_feed(&d, 0x42);
    feed(&d, 0x08, 0x02, 0x02);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.kind == MOUSE_EV_MOTION && ev.dx == 2 && ev.dy == 2);
    CHECK(mouse_decoder_next(&d, &ev) == 0);
}

static void test_button_transitions(void) {
    struct mouse_decoder d;
    struct mouse_event ev;
    reset(&d);
    /* left press: one button event, no motion */
    feed(&d, 0x09, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.kind == MOUSE_EV_BUTTON);
    CHECK(ev.button == POINTER_BTN_LEFT);
    CHECK(ev.pressed == 1);
    CHECK(ev.buttons == 0x01);
    CHECK(mouse_decoder_next(&d, &ev) == 0);

    /* middle press while left held: only middle transitions */
    feed(&d, 0x0D, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.button == POINTER_BTN_MIDDLE && ev.pressed == 1);
    CHECK(ev.buttons == 0x05);
    CHECK(mouse_decoder_next(&d, &ev) == 0);

    /* release all: two transitions, left first then middle */
    feed(&d, 0x08, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.button == POINTER_BTN_LEFT && ev.pressed == 0);
    CHECK(ev.buttons == 0x04);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.button == POINTER_BTN_MIDDLE && ev.pressed == 0);
    CHECK(ev.buttons == 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 0);

    /* right button code maps to POINTER_BTN_RIGHT (ABI numbering) */
    feed(&d, 0x0A, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.button == POINTER_BTN_RIGHT && ev.pressed == 1);
}

static void test_button_plus_motion(void) {
    struct mouse_decoder d;
    struct mouse_event ev;
    reset(&d);
    /* left press and +1 X in one packet: button first, then motion */
    feed(&d, 0x09, 0x01, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.kind == MOUSE_EV_BUTTON && ev.button == POINTER_BTN_LEFT);
    CHECK(ev.buttons == 0x01);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.kind == MOUSE_EV_MOTION && ev.dx == 1 && ev.dy == 0);
    CHECK(ev.buttons == 0x01);
    CHECK(mouse_decoder_next(&d, &ev) == 0);
}

static void test_queue_bound(void) {
    struct mouse_decoder d;
    struct mouse_event ev;
    reset(&d);
    /* three simultaneous button transitions + motion = 4 events (cap) */
    feed(&d, 0x0F, 0x05, 0x05);
    int count = 0;
    while (mouse_decoder_next(&d, &ev))
        count++;
    CHECK(count == 4);
    /* cap respected: decoder stays usable after a full queue */
    feed(&d, 0x08, 0x00, 0x00);
    count = 0;
    while (mouse_decoder_next(&d, &ev))
        count++;
    CHECK(count == 3); /* all three release */
    CHECK(ev.buttons == 0x00);
}

static void test_null_safety(void) {
    struct mouse_decoder d;
    struct mouse_event ev;

    /* init tolerates NULL rather than faulting */
    mouse_decoder_init(0);

    /* feed tolerates NULL and reports no completed packet */
    CHECK(mouse_decoder_feed(0, 0x08) == 0);

    /* next tolerates both NULL arguments */
    reset(&d);
    (void)mouse_decoder_feed(&d, 0x08);
    CHECK(mouse_decoder_next(0, &ev) == 0);
    CHECK(mouse_decoder_next(&d, 0) == 0);
    CHECK(mouse_decoder_next(0, 0) == 0);
}

static void test_repeated_sync_byte(void) {
    struct mouse_decoder d;
    struct mouse_event ev;

    reset(&d);
    /* Only byte 0 of a packet can resynchronize the stream; once byte 0 has
     * been accepted the next two bytes are taken as X/Y data whatever their
     * value, so a 0x08 in those positions is motion of +8, not a new packet. */
    CHECK(mouse_decoder_feed(&d, 0x08) == 0);
    CHECK(mouse_decoder_feed(&d, 0x08) == 0);
    CHECK(mouse_decoder_feed(&d, 0x08) == 1);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.kind == MOUSE_EV_MOTION);
    CHECK(ev.dx == 8 && ev.dy == 8);

    /* Index wraps back to 0 after a completed packet. */
    CHECK(mouse_decoder_feed(&d, 0x09) == 0);
    CHECK(mouse_decoder_feed(&d, 0x00) == 0);
    CHECK(mouse_decoder_feed(&d, 0x00) == 1);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.kind == MOUSE_EV_BUTTON && ev.button == POINTER_BTN_LEFT);
    CHECK(ev.pressed == 1 && ev.buttons == 0x01);
}

static void test_idle_packet_emits_nothing(void) {
    struct mouse_decoder d;
    struct mouse_event ev;

    reset(&d);
    /* No buttons held, no buttons pressed, zero motion: nothing queues. */
    feed(&d, 0x08, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 0);
    /* Buttons already held and still held: no repeated transition. */
    feed(&d, 0x09, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    feed(&d, 0x09, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 0);

    /* Re-init clears the held button mask. */
    reset(&d);
    feed(&d, 0x09, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.button == POINTER_BTN_LEFT && ev.pressed == 1);
}

static void test_y_overflow_discard(void) {
    struct mouse_decoder d;
    struct mouse_event ev;

    reset(&d);
    /* Hold a button, then take an overflow packet: the discard must not
     * emit a spurious release and must not lose the held button. */
    feed(&d, 0x09, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.button == POINTER_BTN_LEFT && ev.pressed == 1);

    feed(&d, 0x88, 0xFF, 0xFF); /* Y overflow */
    CHECK(mouse_decoder_next(&d, &ev) == 0);

    feed(&d, 0x09, 0x00, 0x00); /* still held: no transition */
    CHECK(mouse_decoder_next(&d, &ev) == 0);

    feed(&d, 0x08, 0x00, 0x00); /* release */
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.button == POINTER_BTN_LEFT && ev.pressed == 0);
    CHECK(ev.buttons == 0x00);
}

static void test_motion_extremes(void) {
    struct mouse_decoder d;
    struct mouse_event ev;

    reset(&d);
    /* Maximum positive 9-bit values. */
    feed(&d, 0x08, 0xFF, 0xFF);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.dx == 255 && ev.dy == 255);

    /* Both signs set with zero data: -256 on each axis. */
    feed(&d, 0x58, 0x00, 0x00);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.dx == -256 && ev.dy == -256);

    /* Both signs set with 0xFF data: -1 on each axis. */
    feed(&d, 0x58, 0xFF, 0xFF);
    CHECK(mouse_decoder_next(&d, &ev) == 1);
    CHECK(ev.dx == -1 && ev.dy == -1);
}

int main(void) {
    test_motion_basic();
    test_motion_sign();
    test_motion_extremes();
    test_overflow_discard();
    test_y_overflow_discard();
    test_resync();
    test_repeated_sync_byte();
    test_button_transitions();
    test_button_plus_motion();
    test_queue_bound();
    test_idle_packet_emits_nothing();
    test_null_safety();
    if (failures) {
        printf("mouse_core_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("mouse_core_test: all checks passed\n");
    return 0;
}
