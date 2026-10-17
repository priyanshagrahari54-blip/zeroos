/*
 * Host gate for userspace/session/font8x8.h.
 *
 * The session renders this font onto the panel, so a corrupt glyph is a defect
 * a person sees, not a test detail. This checks the structural invariants that
 * catch table corruption (copy/paste rows, duplicated glyphs, an out-of-range
 * index reading past the table) and pins a few glyphs to their exact bitmaps so
 * a silent edit to a used glyph fails the build rather than the user's eyes.
 */
#include <stdio.h>
#include <string.h>

#include <zeroos/font8x8.h>

static int checks = 0;
static int failures = 0;

static void expect(int condition, const char *what) {
    ++checks;
    if (!condition) {
        ++failures;
        printf("font8x8: FAIL %s\n", what);
    }
}

static int glyph_popcount(const unsigned char *g) {
    int total = 0;
    for (int row = 0; row < FONT8X8_ROWS; ++row) {
        unsigned char b = g[row];
        while (b) {
            total += (int)(b & 1U);
            b >>= 1;
        }
    }
    return total;
}

static int same_glyph(const unsigned char *a, const unsigned char *b) {
    return memcmp(a, b, FONT8X8_ROWS) == 0;
}

int main(void) {
    /* The table covers exactly 0x20..0x7E. */
    expect(FONT8X8_GLYPHS == 95, "table covers the 95 printable code points");
    expect(FONT8X8_ROWS == 8, "glyphs are 8 rows tall");

    /* Space is blank; every other printable glyph puts ink on the panel. An
     * all-zero row set anywhere else means the glyph was lost. */
    expect(glyph_popcount(font8x8_glyph(' ')) == 0, "space renders blank");
    for (unsigned char c = FONT8X8_FIRST; c <= FONT8X8_LAST; ++c) {
        if (c == ' ')
            continue;
        if (glyph_popcount(font8x8_glyph(c)) == 0) {
            ++checks;
            ++failures;
            printf("font8x8: FAIL glyph 0x%02x renders blank\n", c);
        } else {
            ++checks;
        }
    }

    /* No two glyphs may share a bitmap: a duplicated row is the classic way a
     * hand-written font table goes wrong, and it is invisible to the compiler.
     * '_' (0x5F) is exempt only in that it is a single underline row, so it is
     * compared like everything else but against the whole table. */
    for (unsigned char a = FONT8X8_FIRST; a <= FONT8X8_LAST; ++a) {
        for (unsigned char b = (unsigned char)(a + 1); b <= FONT8X8_LAST; ++b) {
            if (same_glyph(font8x8_glyph(a), font8x8_glyph(b))) {
                ++checks;
                ++failures;
                printf("font8x8: FAIL glyphs 0x%02x and 0x%02x are identical\n",
                       a, b);
            } else {
                ++checks;
            }
        }
    }

    /* Out-of-range input must fall back to '?', never index past the table. */
    expect(same_glyph(font8x8_glyph(0x00), font8x8_glyph('?')),
           "NUL falls back to the '?' glyph");
    expect(same_glyph(font8x8_glyph(0x7F), font8x8_glyph('?')),
           "DEL falls back to the '?' glyph");
    expect(same_glyph(font8x8_glyph(0xFF), font8x8_glyph('?')),
           "0xFF falls back to the '?' glyph");

    /* Pinned bitmaps for the glyphs the session actually draws. */
    {
        static const unsigned char zero_expected[8] = {
            0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00
        };
        static const unsigned char x_expected[8] = {
            0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00
        };
        static const unsigned char z_upper_expected[8] = {
            0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00
        };
        expect(same_glyph(font8x8_glyph('0'), zero_expected),
               "'0' bitmap matches the pinned value");
        expect(same_glyph(font8x8_glyph('x'), x_expected),
               "'x' bitmap matches the pinned value");
        expect(same_glyph(font8x8_glyph('Z'), z_upper_expected),
               "'Z' bitmap matches the pinned value");
    }

    /* The build revision is hexadecimal, so every hex digit must be distinct
     * and legible; a collision here makes two builds look identical. */
    {
        const char *hex = "0123456789abcdef";
        for (int i = 0; hex[i]; ++i) {
            for (int j = i + 1; hex[j]; ++j) {
                if (same_glyph(font8x8_glyph((unsigned char)hex[i]),
                               font8x8_glyph((unsigned char)hex[j]))) {
                    ++checks;
                    ++failures;
                    printf("font8x8: FAIL hex digits '%c' and '%c' collide\n",
                           hex[i], hex[j]);
                } else {
                    ++checks;
                }
            }
        }
    }

    printf("font8x8-check: checks=%d failures=%d RESULT: %s\n", checks,
           failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
