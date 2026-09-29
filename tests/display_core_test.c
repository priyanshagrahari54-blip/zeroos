#include <assert.h>
#include <string.h>
#include "../kernel/display_core.h"

#define TEST_FRAMEBYTES (16 * 1024 * 1024)

static void test_mode_valid_null_and_zero(void) {
    struct display_mode m = {1920, 1080, 60000, 1};

    /* NULL mode rejected */
    assert(!display_mode_valid(0, TEST_FRAMEBYTES));

    m.width = 0;
    assert(!display_mode_valid(&m, TEST_FRAMEBYTES));
    m.width = 1920;

    m.height = 0;
    assert(!display_mode_valid(&m, TEST_FRAMEBYTES));
    m.height = 1080;

    /* Zero refresh rejected */
    m.refresh_millihz = 0;
    assert(!display_mode_valid(&m, TEST_FRAMEBYTES));
    m.refresh_millihz = 60000;

    assert(display_mode_valid(&m, TEST_FRAMEBYTES));
}

static void test_mode_valid_dimension_bounds(void) {
    struct display_mode m = {1920, 1080, 60000, 1};

    /* 16384 is the inclusive ceiling */
    m.width = 16384;
    m.height = 16384;
    m.pixel_format = 1;
    assert(display_mode_valid(&m, 16384ULL * 16384ULL * 4ULL));

    /* Width above the ceiling rejected */
    m.width = 16385;
    assert(!display_mode_valid(&m, 16384ULL * 16384ULL * 4ULL));
    m.width = 0xFFFFFFFFU;
    assert(!display_mode_valid(&m, 16384ULL * 16384ULL * 4ULL));
    m.width = 16384;

    /* Height above the ceiling rejected */
    m.height = 16385;
    assert(!display_mode_valid(&m, 16384ULL * 16384ULL * 4ULL));
    m.height = 0xFFFFFFFFU;
    assert(!display_mode_valid(&m, 16384ULL * 16384ULL * 4ULL));
}

static void test_mode_valid_pixel_format(void) {
    struct display_mode m = {1920, 1080, 60000, 1};

    /* Format 1 -> 4 bytes per pixel, format 2 -> 2 bytes per pixel */
    m.pixel_format = 1;
    assert(display_mode_valid(&m, TEST_FRAMEBYTES));
    m.pixel_format = 2;
    assert(display_mode_valid(&m, TEST_FRAMEBYTES));

    /* Anything else has no byte-per-pixel mapping and is rejected */
    m.pixel_format = 0;
    assert(!display_mode_valid(&m, TEST_FRAMEBYTES));
    m.pixel_format = 3;
    assert(!display_mode_valid(&m, TEST_FRAMEBYTES));
    m.pixel_format = 0xFFFFFFFFU;
    assert(!display_mode_valid(&m, TEST_FRAMEBYTES));
}

static void test_mode_valid_frame_limit(void) {
    struct display_mode m = {1920, 1080, 60000, 1};

    /* pixels = 1920*1080 = 2073600; *4 = 8294400 bytes */
    assert(display_mode_valid(&m, 8294400ULL));
    /* One byte short of the requirement is rejected */
    assert(!display_mode_valid(&m, 8294399ULL));
    /* A zero limit cannot hold any mode */
    assert(!display_mode_valid(&m, 0));

    /* Format 2 halves the requirement */
    m.pixel_format = 2;
    assert(display_mode_valid(&m, 4147200ULL));
    assert(!display_mode_valid(&m, 4147199ULL));

    /* The largest accepted mode needs 16384*16384*4 = 1 GiB exactly, and the
     * pixels*bytes-per-pixel product must be evaluated without overflowing
     * the comparison: one byte below the requirement is rejected. */
    m.width = 16384;
    m.height = 16384;
    m.pixel_format = 1;
    assert(display_mode_valid(&m, 1073741824ULL));
    assert(!display_mode_valid(&m, 1073741823ULL));
    assert(!display_mode_valid(&m, 1));
}

static void test_caps_add_basics(void) {
    struct display_caps c = {0};
    struct display_mode m = {1920, 1080, 60000, 1};

    /* NULL arguments rejected */
    assert(display_caps_add(0, &m, TEST_FRAMEBYTES) == -1);
    assert(display_caps_add(&c, 0, TEST_FRAMEBYTES) == -1);

    /* First insert succeeds */
    assert(display_caps_add(&c, &m, TEST_FRAMEBYTES) == 0);
    assert(c.count == 1);
    assert(c.modes[0].width == 1920 && c.modes[0].height == 1080);

    /* Exact duplicate rejected */
    assert(display_caps_add(&c, &m, TEST_FRAMEBYTES) == -1);
    assert(c.count == 1);

    /* Same geometry, different refresh: accepted */
    m.refresh_millihz = 30000;
    assert(display_caps_add(&c, &m, TEST_FRAMEBYTES) == 0);
    assert(c.count == 2);

    /* Same geometry and refresh, different format: accepted */
    m.pixel_format = 2;
    assert(display_caps_add(&c, &m, TEST_FRAMEBYTES) == 0);
    assert(c.count == 3);

    /* An invalid mode is rejected without touching the table */
    m.width = 0;
    assert(display_caps_add(&c, &m, TEST_FRAMEBYTES) == -1);
    assert(c.count == 3);
}

static void test_caps_add_capacity(void) {
    struct display_caps c = {0};
    struct display_mode m = {640, 480, 60000, 1};

    memset(&c, 0, sizeof(c));
    for (uint32_t i = 0; i < DISPLAY_MAX_MODES; ++i) {
        m.width = 640U + i;
        assert(display_caps_add(&c, &m, TEST_FRAMEBYTES) == 0);
    }
    assert(c.count == DISPLAY_MAX_MODES);

    /* Table is full: a further distinct mode is rejected */
    m.width = 4096;
    assert(display_caps_add(&c, &m, TEST_FRAMEBYTES) == -1);
    assert(c.count == DISPLAY_MAX_MODES);

    /* A duplicate is also rejected (checked before capacity) */
    m.width = 640;
    assert(display_caps_add(&c, &m, TEST_FRAMEBYTES) == -1);
}

static void test_present_request_valid(void) {
    /* Pixel-mapping present validation: happy paths per depth. */
    assert(display_present_request_valid(1024, 768, 32, 3, 0, 0, 4, 4, 16));
    assert(display_present_request_valid(1024, 768, 32, 3, 1020, 764, 4, 4, 16));
    assert(display_present_request_valid(1024, 768, 16, 1, 0, 0, 4, 4, 8));
    assert(display_present_request_valid(1024, 768, 24, 2, 0, 0, 4, 4, 12));
    assert(display_present_request_valid(1024, 768, 32, 3, 0, 0, 1024, 768, 4096));
    /* Padded source stride above the minimum row is legal. */
    assert(display_present_request_valid(1024, 768, 32, 3, 0, 0, 4, 4, 4096));

    /* Bounds and overflow negatives. */
    assert(!display_present_request_valid(0, 0, 32, 3, 0, 0, 1, 1, 4));
    assert(!display_present_request_valid(1024, 768, 32, 3, 1023, 0, 4, 4, 16));
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 767, 4, 4, 16));
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0xffffffffU, 4, 4, 16));
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0, 0, 4, 16));
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0, 4, 0, 16));

    /* Format/bpp agreement and stride floor. */
    assert(!display_present_request_valid(1024, 768, 32, 1, 0, 0, 4, 4, 16));
    assert(!display_present_request_valid(1024, 768, 16, 3, 0, 0, 4, 4, 8));
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0, 4, 4, 12));
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0, 4, 4, 0));
    assert(!display_present_request_valid(1024, 768, 8, 0, 0, 0, 4, 4, 4));
    /* Stride cap keeps height*stride arithmetic bounded. */
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0, 4, 4, (1U << 21)));
    /* A full-scanout present with exact pitch must remain valid. */
    assert(display_present_request_valid(1024, 768, 32, 3, 0, 0, 1024, 768, 4096));
}

static void test_present_request_zero_framebuffer(void) {
    /* A zero-sized framebuffer cannot host any rectangle. */
    assert(!display_present_request_valid(0, 768, 32, 3, 0, 0, 1, 1, 4));
    assert(!display_present_request_valid(1024, 0, 32, 3, 0, 0, 1, 1, 4));

    /* Unsupported bit depths are rejected before geometry is considered. */
    assert(!display_present_request_valid(1024, 768, 8, 0, 0, 0, 4, 4, 4));
    assert(!display_present_request_valid(1024, 768, 4, 0, 0, 0, 4, 4, 4));
    assert(!display_present_request_valid(1024, 768, 64, 3, 0, 0, 4, 4, 32));
    assert(!display_present_request_valid(1024, 768, 0, 0, 0, 0, 4, 4, 0));
}

static void test_present_request_exact_fits(void) {
    /* A rectangle whose right/bottom edge lands exactly on the framebuffer
     * edge is legal: x + width == fb_width is in range. */
    assert(display_present_request_valid(1024, 768, 32, 3, 0, 0, 1024, 1, 4096));
    assert(display_present_request_valid(1024, 768, 32, 3, 0, 0, 1, 768, 4));
    assert(display_present_request_valid(1024, 768, 32, 3, 1023, 767, 1, 1, 4));

    /* One past the edge is not. */
    assert(!display_present_request_valid(1024, 768, 32, 3, 1, 0, 1024, 1, 4096));
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 1, 1, 768, 4));

    /* Zero-area rectangles are rejected. */
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0, 0, 1, 4));
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0, 1, 0, 4));
}

static void test_present_request_stride_bounds(void) {
    /* Stride exactly at the row minimum is accepted at every depth. */
    assert(display_present_request_valid(1024, 768, 32, 3, 0, 0, 1024, 1, 4096));
    assert(display_present_request_valid(1024, 768, 24, 2, 0, 0, 1024, 1, 3072));
    assert(display_present_request_valid(1024, 768, 16, 1, 0, 0, 1024, 1, 2048));

    /* One byte below the row minimum is rejected. */
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0, 1024, 1, 4095));
    assert(!display_present_request_valid(1024, 768, 24, 2, 0, 0, 1024, 1, 3071));
    assert(!display_present_request_valid(1024, 768, 16, 1, 0, 0, 1024, 1, 2047));

    /* Stride at the cap is accepted; one byte above it is rejected. */
    assert(display_present_request_valid(1024, 768, 32, 3, 0, 0, 4, 4,
                                        1024U * 1024U));
    assert(!display_present_request_valid(1024, 768, 32, 3, 0, 0, 4, 4,
                                          (1024U * 1024U) + 1U));
}

int main(void) {
    test_mode_valid_null_and_zero();
    test_mode_valid_dimension_bounds();
    test_mode_valid_pixel_format();
    test_mode_valid_frame_limit();
    test_caps_add_basics();
    test_caps_add_capacity();
    test_present_request_valid();
    test_present_request_zero_framebuffer();
    test_present_request_exact_fits();
    test_present_request_stride_bounds();
    return 0;
}
