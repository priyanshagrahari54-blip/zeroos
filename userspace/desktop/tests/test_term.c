/* Terminal core tests (shell surface) */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <string.h>

static void wstr(struct zd_term *t, const char *s) {
    ZD_CHECK_OK(zd_term_write(t, (const uint8_t *)s,
                              (uint32_t)strlen(s)));
}

void zd_test_term_suite(void) {
    struct zd_term t;
    char line[160];
    struct zd_term_cell cells[80];
    const struct zd_term_cell *c;

    zd_term_init(&t, 0, 0); /* defaults 24x80 */
    ZD_CHECK_EQ(t.rows, 24);
    ZD_CHECK_EQ(t.cols, 80);
    ZD_CHECK_EQ(zd_term_write(&t, NULL, 5), -22);
    ZD_CHECK_EQ(zd_term_write(NULL, (const uint8_t *)"x", 1), -22);

    /* plain text + row extraction */
    wstr(&t, "ZEROOS term");
    ZD_CHECK_EQ(zd_term_row_text(&t, 0, line, sizeof(line)), 11);
    ZD_CHECK(strcmp(line, "ZEROOS term") == 0);
    ZD_CHECK_EQ(t.cur_x, 11);
    ZD_CHECK_EQ(t.cur_y, 0);

    /* CR / BS / TAB */
    wstr(&t, "\rX");
    ZD_CHECK_EQ(zd_term_row_text(&t, 0, line, sizeof(line)), 11);
    ZD_CHECK(strncmp(line, "XEROOS term", 11) == 0);
    wstr(&t, "\b\bY");
    ZD_CHECK(strncmp(line, "XEROOS term", 11) == 0); /* CR->0, BS x2 */
    zd_term_init(&t, 6, 16);
    wstr(&t, "a\tb");
    ZD_CHECK_EQ(t.cur_x, 9); /* a, tab to col 8, then b -> col 9 */

    /* CSI cursor movement */
    zd_term_init(&t, 6, 16);
    wstr(&t, "\033[3;5H");
    ZD_CHECK_EQ(t.cur_y, 2);
    ZD_CHECK_EQ(t.cur_x, 4);
    wstr(&t, "\033[A"); /* up 1 */
    ZD_CHECK_EQ(t.cur_y, 1);
    wstr(&t, "\033[10A"); /* clamp at 0 */
    ZD_CHECK_EQ(t.cur_y, 0);
    wstr(&t, "\033[2B"); /* down 2 */
    ZD_CHECK_EQ(t.cur_y, 2);
    wstr(&t, "\033[C"); /* right */
    ZD_CHECK_EQ(t.cur_x, 5);
    wstr(&t, "\033[99D"); /* clamp left */
    ZD_CHECK_EQ(t.cur_x, 0);
    wstr(&t, "\033[99C"); /* clamp right */
    ZD_CHECK_EQ(t.cur_x, 15);
    wstr(&t, "\033[5;1H");

    /* wrap at edge: 16-wide row, cursor starts at (4,0) after H */
    wstr(&t, "0123456789ABCDE"); /* 15 chars -> cols 0..14 */
    wstr(&t, "F");               /* fills col 15 */
    wstr(&t, "G");               /* wraps to next line */
    ZD_CHECK_EQ(t.cur_x, 1);
    ZD_CHECK_EQ(t.cur_y, 5);

    /* SGR colors + attrs */
    zd_term_init(&t, 4, 16);
    wstr(&t, "\033[1;4;7;31;42mZ");
    c = zd_term_cell(&t, 0, 0);
    ZD_CHECK(c != NULL);
    ZD_CHECK_EQ(c->ch[0], 'Z');
    ZD_CHECK_EQ(c->fg, 1);
    ZD_CHECK_EQ(c->bg, 2);
    ZD_CHECK(c->attrs & ZD_TERM_BOLD);
    ZD_CHECK(c->attrs & ZD_TERM_UNDERLINE);
    ZD_CHECK(c->attrs & ZD_TERM_INVERSE);
    wstr(&t, "\033[93mA"); /* bright fg */
    c = zd_term_cell(&t, 1, 0);
    ZD_CHECK_EQ(c->fg, 11);
    wstr(&t, "\033[0mB"); /* reset */
    c = zd_term_cell(&t, 2, 0);
    ZD_CHECK_EQ(c->fg, 7);
    ZD_CHECK_EQ(c->bg, 0);
    ZD_CHECK_EQ(c->attrs, 0);

    /* erase line */
    zd_term_init(&t, 3, 8);
    wstr(&t, "abcdefgh");
    wstr(&t, "\033[1;5H\033[K"); /* to end of line */
    ZD_CHECK(zd_term_row_text(&t, 0, line, sizeof(line)) > 0);
    ZD_CHECK(strcmp(line, "abcd") == 0);
    wstr(&t, "\033[2K"); /* whole line */
    ZD_CHECK_EQ(zd_term_row_text(&t, 0, line, sizeof(line)), 0);

    /* erase display 2 */
    zd_term_init(&t, 3, 8);
    wstr(&t, "one\r\ntwo\r\nthr");
    wstr(&t, "\033[2J");
    ZD_CHECK_EQ(zd_term_row_text(&t, 0, line, sizeof(line)), 0);
    ZD_CHECK_EQ(zd_term_row_text(&t, 2, line, sizeof(line)), 0);

    /* scrolling + scrollback order */
    zd_term_init(&t, 3, 8);
    wstr(&t, "L1\r\nL2\r\nL3\r\nL4"); /* L1 scrolls out */
    ZD_CHECK_EQ(t.stats.scrolls, 1);
    ZD_CHECK_EQ(t.sb_count, 1);
    ZD_CHECK_EQ(zd_term_scrollback_line(&t, 0, cells, 80), 8);
    ZD_CHECK_EQ(cells[0].ch[0], 'L');
    ZD_CHECK_EQ(cells[1].ch[0], '1'); /* one char per cell */
    ZD_CHECK_EQ(zd_term_row_text(&t, 0, line, sizeof(line)), 2);
    ZD_CHECK(strcmp(line, "L2") == 0);
    /* OOB reads */
    ZD_CHECK_EQ(zd_term_scrollback_line(&t, 9, cells, 80), -2);
    ZD_CHECK_EQ(zd_term_scrollback_line(&t, 0, NULL, 80), -22);
    ZD_CHECK(zd_term_cell(&t, 99, 99) == NULL);
    ZD_CHECK_EQ(zd_term_row_text(&t, 0, line, 0), 0);

    /* scrollback ring wrap: 128 cap on a 2-row screen */
    zd_term_init(&t, 2, 8);
    {
        uint32_t i;
        for (i = 0; i < 135; ++i) {
            char buf[16];
            buf[0] = 'A' + (char)(i % 26);
            buf[1] = '0' + (char)(i % 10);
            buf[2] = 0;
            wstr(&t, buf);
            wstr(&t, "\r\n");
        }
    }
    ZD_CHECK_EQ(t.sb_count, ZD_TERM_SCROLLBACK);
    /* 135 iterations on a 2-row screen scroll 134 times; cap is 128
     * so 6 oldest (A0..A5) were evicted -> oldest kept is A6 */
    ZD_CHECK_EQ(zd_term_scrollback_line(&t, 0, cells, 80), 8);
    /* i=6: letter 'A'+6%26 = 'G', digit '0'+6 = '6' */
    ZD_CHECK_EQ(cells[0].ch[0], 'G');
    ZD_CHECK_EQ(cells[1].ch[0], '6');

    /* OSC via BEL and via ESC \ */
    zd_term_init(&t, 4, 16);
    wstr(&t, "\033]0;my title\007");
    wstr(&t, "\033]2;t2\033\\");
    ZD_CHECK_EQ(t.stats.osc_seen, 2);
    ZD_CHECK_EQ(t.stats.unknown_seqs, 0);

    /* unsupported sequences consumed, never shown as text */
    zd_term_init(&t, 4, 16);
    wstr(&t, "\033[?25l"); /* private mode */
    wstr(&t, "\033[2J");   /* supported: not unknown */
    wstr(&t, "\033[10;20r"); /* scroll region: unsupported */
    wstr(&t, "\033(B");    /* charset select: unsupported */
    wstr(&t, "ok");
    ZD_CHECK_EQ(t.stats.unknown_seqs, 3);
    ZD_CHECK_EQ(zd_term_row_text(&t, 0, line, sizeof(line)), 2);
    ZD_CHECK(strcmp(line, "ok") == 0);

    /* oversized CSI drained (overflow counted once) */
    zd_term_init(&t, 4, 16);
    {
        uint32_t i;
        wstr(&t, "\033[");
        for (i = 0; i < 40; ++i)
            wstr(&t, "1;");
        wstr(&t, "1H");
    }
    ZD_CHECK(t.stats.unknown_seqs >= 1);
    wstr(&t, "z");
    ZD_CHECK_EQ(zd_term_row_text(&t, 0, line, sizeof(line)), 1);
    ZD_CHECK(strcmp(line, "z") == 0);

    /* partial sequences across writes */
    zd_term_init(&t, 4, 16);
    wstr(&t, "\033");
    ZD_CHECK_EQ(t.pstate, ZD_TERM_S_ESC);
    wstr(&t, "[3");
    ZD_CHECK_EQ(t.pstate, ZD_TERM_S_CSI);
    wstr(&t, "1mQ");
    c = zd_term_cell(&t, 0, 0);
    ZD_CHECK(c && c->fg == 1);

    /* UTF-8, split across writes; invalid bytes counted */
    zd_term_init(&t, 4, 16);
    {
        static const uint8_t euro[3] = {0xE2, 0x82, 0xAC};
        ZD_CHECK_OK(zd_term_write(&t, euro, 1));
        ZD_CHECK_OK(zd_term_write(&t, euro + 1, 2));
        c = zd_term_cell(&t, 0, 0);
        ZD_CHECK(c != NULL);
        ZD_CHECK_EQ((uint8_t)c->ch[0], 0xE2);
        ZD_CHECK_EQ((uint8_t)c->ch[1], 0x82);
        ZD_CHECK_EQ((uint8_t)c->ch[2], 0xAC);
        /* stray continuation */
        wstr(&t, "\200");
        /* invalid lead */
        wstr(&t, "\377");
        ZD_CHECK_EQ(t.stats.truncated_utf8, 2);
        /* pending sequence interrupted by ESC: dropped + counted */
        wstr(&t, "\342");
        wstr(&t, "\033[1m");
        ZD_CHECK_EQ(t.stats.truncated_utf8, 3);
        wstr(&t, "s");
        c = zd_term_cell(&t, 1, 0);
        ZD_CHECK(c && c->ch[0] == 's');
    }

    /* stats + byte accounting */
    ZD_CHECK(t.stats.writes >= 5); /* since the last init */
    ZD_CHECK(t.stats.bytes > 0);
}
