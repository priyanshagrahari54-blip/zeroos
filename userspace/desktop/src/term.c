/* Terminal core.  See term.h for the supported-sequence contract. */
#include <zeroos/desktop/term.h>

static void t_clear_cell(struct zd_term_cell *c, uint8_t fg,
                         uint8_t bg) {
    c->ch[0] = ' ';
    c->ch[1] = 0;
    c->ch[2] = 0;
    c->ch[3] = 0;
    c->fg = fg;
    c->bg = bg;
    c->attrs = 0;
}

static void t_clear_row(struct zd_term *t, uint32_t y) {
    uint32_t x;
    for (x = 0; x < t->cols; ++x)
        t_clear_cell(&t->grid[y][x], 7, 0);
}

static void t_scroll_back(struct zd_term *t) {
    uint32_t x, y;
    for (x = 0; x < t->cols; ++x)
        t->lines[t->sb_head][x] = t->grid[0][x];
    t->sb_head = (t->sb_head + 1) % ZD_TERM_SCROLLBACK;
    if (t->sb_count < ZD_TERM_SCROLLBACK)
        t->sb_count++;
    for (y = 0; y + 1 < t->rows; ++y)
        for (x = 0; x < t->cols; ++x)
            t->grid[y][x] = t->grid[y + 1][x];
    t_clear_row(t, t->rows - 1);
    t->stats.scrolls++;
}

static void t_linefeed(struct zd_term *t) {
    if (t->cur_y + 1 >= t->rows)
        t_scroll_back(t);
    else
        t->cur_y++;
}

static void t_put(struct zd_term *t, const char *bytes, uint32_t n) {
    struct zd_term_cell *c;
    uint32_t i;
    if (t->cur_x >= t->cols) {
        t->cur_x = 0;
        t_linefeed(t);
    }
    if (t->cur_y >= t->rows)
        t->cur_y = t->rows - 1;
    c = &t->grid[t->cur_y][t->cur_x];
    for (i = 0; i < 4; ++i)
        c->ch[i] = 0;
    for (i = 0; i < n && i < 4; ++i)
        c->ch[i] = bytes[i];
    c->fg = t->fg;
    c->bg = t->bg;
    c->attrs = t->attrs;
    t->cur_x++;
}

static void t_erase_cells(struct zd_term *t, uint32_t y, uint32_t x0,
                          uint32_t x1) {
    uint32_t x;
    if (y >= t->rows)
        return;
    for (x = x0; x < x1 && x < t->cols; ++x)
        t_clear_cell(&t->grid[y][x], t->fg, t->bg);
}

static uint32_t t_param(const char *s, uint32_t len, uint32_t idx,
                        uint32_t defv, int *ok) {
    uint32_t i = 0, cur = 0, which = 0;
    int any = 0;
    *ok = 1;
    if (len == 0)
        return defv;
    if (s[0] == '?' || s[0] == '>' || s[0] == '<') {
        *ok = 0;
        return defv;
    }
    for (i = 0; i < len; ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + (uint32_t)(c - '0');
            any = 1;
        } else if (c == ';') {
            if (which == idx)
                return any ? cur : defv;
            which++;
            cur = 0;
            any = 0;
        } else {
            *ok = 0;
            return defv;
        }
    }
    if (which == idx)
        return any ? cur : defv;
    return defv;
}

static void t_sgr(struct zd_term *t, const char *s, uint32_t len) {
    uint32_t i = 0;
    if (len == 0) {
        t->fg = 7;
        t->bg = 0;
        t->attrs = 0;
        return;
    }
    for (;;) {
        uint32_t v = 0;
        int any = 0;
        while (i < len && s[i] != ';') {
            if (s[i] >= '0' && s[i] <= '9') {
                v = v * 10 + (uint32_t)(s[i] - '0');
                any = 1;
            }
            ++i;
        }
        if (any) {
            switch (v) {
            case 0:
                t->fg = 7;
                t->bg = 0;
                t->attrs = 0;
                break;
            case 1:
                t->attrs |= ZD_TERM_BOLD;
                break;
            case 4:
                t->attrs |= ZD_TERM_UNDERLINE;
                break;
            case 7:
                t->attrs |= ZD_TERM_INVERSE;
                break;
            case 39:
                t->fg = 7;
                break;
            case 49:
                t->bg = 0;
                break;
            default:
                if (v >= 30 && v <= 37)
                    t->fg = (uint8_t)(v - 30);
                else if (v >= 90 && v <= 97)
                    t->fg = (uint8_t)(v - 90 + 8);
                else if (v >= 40 && v <= 47)
                    t->bg = (uint8_t)(v - 40);
                else if (v >= 100 && v <= 107)
                    t->bg = (uint8_t)(v - 100 + 8);
                break;
            }
        }
        if (i >= len)
            break;
        ++i; /* ';' */
    }
}

static void t_csi(struct zd_term *t) {
    char cmd;
    uint32_t len = t->seq_len;
    int ok = 1;
    if (len == 0 || len > sizeof(t->seq)) {
        t->stats.unknown_seqs++;
        return;
    }
    cmd = t->seq[len - 1];
    len--;
    switch (cmd) {
    case 'A': {
        uint32_t n = t_param(t->seq, len, 0, 1, &ok);
        if (ok)
            t->cur_y = (n >= t->cur_y) ? 0 : t->cur_y - n;
        break;
    }
    case 'B': {
        uint32_t n = t_param(t->seq, len, 0, 1, &ok);
        if (ok) {
            t->cur_y += n;
            if (t->cur_y >= t->rows)
                t->cur_y = t->rows - 1;
        }
        break;
    }
    case 'C': {
        uint32_t n = t_param(t->seq, len, 0, 1, &ok);
        if (ok) {
            t->cur_x += n;
            if (t->cur_x >= t->cols)
                t->cur_x = t->cols - 1;
        }
        break;
    }
    case 'D': {
        uint32_t n = t_param(t->seq, len, 0, 1, &ok);
        if (ok)
            t->cur_x = (n >= t->cur_x) ? 0 : t->cur_x - n;
        break;
    }
    case 'H':
    case 'f': {
        uint32_t row = t_param(t->seq, len, 0, 1, &ok);
        uint32_t col = t_param(t->seq, len, 1, 1, &ok);
        if (ok) {
            t->cur_y = row ? row - 1 : 0;
            t->cur_x = col ? col - 1 : 0;
            if (t->cur_y >= t->rows)
                t->cur_y = t->rows - 1;
            if (t->cur_x >= t->cols)
                t->cur_x = t->cols - 1;
        }
        break;
    }
    case 'J': {
        uint32_t mode = t_param(t->seq, len, 0, 0, &ok);
        uint32_t y;
        if (!ok)
            break;
        if (mode == 0) {
            t_erase_cells(t, t->cur_y, t->cur_x, t->cols);
            for (y = t->cur_y + 1; y < t->rows; ++y)
                t_erase_cells(t, y, 0, t->cols);
        } else if (mode == 1) {
            for (y = 0; y < t->cur_y; ++y)
                t_erase_cells(t, y, 0, t->cols);
            t_erase_cells(t, t->cur_y, 0, t->cur_x + 1);
        } else if (mode == 2) {
            for (y = 0; y < t->rows; ++y)
                t_erase_cells(t, y, 0, t->cols);
        }
        break;
    }
    case 'K': {
        uint32_t mode = t_param(t->seq, len, 0, 0, &ok);
        if (!ok)
            break;
        if (mode == 0)
            t_erase_cells(t, t->cur_y, t->cur_x, t->cols);
        else if (mode == 1)
            t_erase_cells(t, t->cur_y, 0, t->cur_x + 1);
        else if (mode == 2)
            t_erase_cells(t, t->cur_y, 0, t->cols);
        break;
    }
    case 'm':
        t_sgr(t, t->seq, len);
        break;
    default:
        t->stats.unknown_seqs++;
        break;
    }
}

void zd_term_init(struct zd_term *t, uint32_t rows, uint32_t cols) {
    uint32_t y, x;
    if (!t)
        return;
    t->rows = rows ? rows : ZD_TERM_ROWS;
    t->cols = cols ? cols : ZD_TERM_COLS;
    if (t->rows > ZD_TERM_MAX_ROWS)
        t->rows = ZD_TERM_MAX_ROWS;
    if (t->cols > ZD_TERM_MAX_COLS)
        t->cols = ZD_TERM_MAX_COLS;
    for (y = 0; y < ZD_TERM_MAX_ROWS; ++y)
        for (x = 0; x < ZD_TERM_MAX_COLS; ++x)
            t_clear_cell(&t->grid[y][x], 7, 0);
    for (y = 0; y < ZD_TERM_SCROLLBACK; ++y)
        for (x = 0; x < ZD_TERM_MAX_COLS; ++x)
            t_clear_cell(&t->lines[y][x], 7, 0);
    t->cur_x = t->cur_y = 0;
    t->fg = 7;
    t->bg = 0;
    t->attrs = 0;
    t->pstate = ZD_TERM_S_GROUND;
    t->seq_len = 0;
    t->seq_overflow = 0;
    t->utf8_len = t->utf8_need = 0;
    t->sb_head = t->sb_count = 0;
    t->stats.writes = t->stats.bytes = 0;
    t->stats.unknown_seqs = t->stats.osc_seen = 0;
    t->stats.scrolls = t->stats.truncated_utf8 = 0;
}

static void t_byte(struct zd_term *t, uint8_t b) {
    /* pending UTF-8 continuation (ground only) */
    if (t->pstate == ZD_TERM_S_GROUND && t->utf8_need) {
        if ((b & 0xC0) == 0x80 && t->utf8_len < 4) {
            t->utf8[t->utf8_len++] = (char)b;
            if (t->utf8_len == t->utf8_need) {
                t_put(t, t->utf8, t->utf8_len);
                t->utf8_len = t->utf8_need = 0;
            }
            return;
        }
        t->stats.truncated_utf8++;
        t->utf8_len = t->utf8_need = 0;
        /* fall through: handle this byte normally */
    }
    switch (t->pstate) {
    case ZD_TERM_S_GROUND:
        if (b == 0x1B) {
            t->pstate = ZD_TERM_S_ESC;
        } else if (b == '\n') {
            t_linefeed(t);
        } else if (b == '\r') {
            t->cur_x = 0;
        } else if (b == '\b') {
            if (t->cur_x)
                t->cur_x--;
        } else if (b == '\t') {
            uint32_t nx = ((t->cur_x / 8) + 1) * 8;
            t->cur_x = (nx < t->cols) ? nx : t->cols - 1;
        } else if (b >= 0x20 && b < 0x7F) {
            char c = (char)b;
            t_put(t, &c, 1);
        } else if (b >= 0x80) {
            uint32_t need = 0;
            if ((b & 0xE0) == 0xC0)
                need = 2;
            else if ((b & 0xF0) == 0xE0)
                need = 3;
            else if ((b & 0xF8) == 0xF0)
                need = 4;
            if (need) {
                t->utf8[0] = (char)b;
                t->utf8_len = 1;
                t->utf8_need = need;
            } else {
                t->stats.truncated_utf8++;
            }
        }
        /* other C0 controls (BEL...) ignored */
        break;
    case ZD_TERM_S_ESC:
        if (b == '[') {
            t->pstate = ZD_TERM_S_CSI;
            t->seq_len = 0;
            t->seq_overflow = 0;
        } else if (b == ']') {
            t->pstate = ZD_TERM_S_OSC;
            t->seq_len = 0;
            t->seq_overflow = 0;
        } else if (b >= 0x20 && b <= 0x2F) {
            /* intermediate (charset selects like ESC ( B) — keep
             * consuming until the final byte so nothing leaks as
             * text */
            t->pstate = ZD_TERM_S_ESC_INT;
            t->seq_len = 1;
        } else {
            t->stats.unknown_seqs++;
            t->pstate = ZD_TERM_S_GROUND;
        }
        break;
    case ZD_TERM_S_ESC_INT:
        if (b >= 0x20 && b <= 0x2F) {
            if (t->seq_len < sizeof(t->seq))
                t->seq_len++;
        } else if (b >= 0x30 && b <= 0x7E) {
            t->stats.unknown_seqs++; /* Fe/Fs with intermediates */
            t->pstate = ZD_TERM_S_GROUND;
            t->seq_len = 0;
        } else {
            /* malformed: resync */
            t->stats.unknown_seqs++;
            t->pstate = ZD_TERM_S_GROUND;
            t->seq_len = 0;
            t_byte(t, b);
        }
        break;
    case ZD_TERM_S_CSI:
        if (b >= 0x40 && b <= 0x7E) {
            if (t->seq_overflow) {
                /* oversized: drained, never parsed as text */
                t->stats.unknown_seqs++;
            } else {
                if (t->seq_len < sizeof(t->seq))
                    t->seq[t->seq_len] = (char)b;
                t->seq_len++;
                t_csi(t);
            }
            t->pstate = ZD_TERM_S_GROUND;
            t->seq_len = 0;
            t->seq_overflow = 0;
        } else if (t->seq_overflow) {
            /* keep draining */
        } else if (t->seq_len < sizeof(t->seq)) {
            t->seq[t->seq_len++] = (char)b;
        } else {
            t->seq_overflow = 1;
            t->stats.unknown_seqs++;
        }
        break;
    case ZD_TERM_S_OSC:
        if (b == 0x07) {
            t->stats.osc_seen++;
            t->pstate = ZD_TERM_S_GROUND;
            t->seq_len = 0;
        } else if (b == 0x1B) {
            t->pstate = ZD_TERM_S_OSC_ESC;
        } else if (t->seq_len < ZD_TERM_OSC_MAX) {
            t->seq_len++;
        }
        break;
    case ZD_TERM_S_OSC_ESC:
        if (b == '\\') {
            t->stats.osc_seen++;
            t->pstate = ZD_TERM_S_GROUND;
            t->seq_len = 0;
        } else {
            t->pstate = ZD_TERM_S_OSC;
            t->seq_len = 0;
        }
        break;
    default:
        t->pstate = ZD_TERM_S_GROUND;
        break;
    }
}

int zd_term_write(struct zd_term *t, const uint8_t *data,
                  uint32_t len) {
    uint32_t i;
    if (!t || (!data && len))
        return -22;
    t->stats.writes++;
    t->stats.bytes += len;
    for (i = 0; i < len; ++i)
        t_byte(t, data[i]);
    return 0;
}

int zd_term_scrollback_line(const struct zd_term *t, uint32_t idx,
                            struct zd_term_cell *out, uint32_t cap) {
    uint32_t x, n = 0, slot;
    if (!t || !out || !cap)
        return -22;
    if (idx >= t->sb_count)
        return -2;
    slot = (t->sb_head + ZD_TERM_SCROLLBACK - t->sb_count + idx) %
           ZD_TERM_SCROLLBACK;
    for (x = 0; x < t->cols && x < cap; ++x) {
        out[x] = t->lines[slot][x];
        n++;
    }
    return (int)n;
}

const struct zd_term_cell *zd_term_cell(const struct zd_term *t,
                                        uint32_t x, uint32_t y) {
    if (!t || x >= t->cols || y >= t->rows)
        return 0;
    return &t->grid[y][x];
}

uint32_t zd_term_row_text(const struct zd_term *t, uint32_t y,
                          char *out, uint32_t cap) {
    uint32_t x, n = 0, last = 0;
    if (!t || !out || !cap)
        return 0;
    out[0] = 0;
    if (y >= t->rows)
        return 0;
    /* trailing blanks are not emitted (explicit contract: cells are
     * joined without padding) */
    for (x = t->cols; x > 0; --x) {
        const struct zd_term_cell *c = &t->grid[y][x - 1];
        if (!(c->ch[0] == ' ' && c->ch[1] == 0)) {
            last = x;
            break;
        }
    }
    for (x = 0; x < last && n + 1 < cap; ++x) {
        const struct zd_term_cell *c = &t->grid[y][x];
        if (c->ch[0]) {
            uint32_t k = 0;
            while (k < 4 && c->ch[k])
                out[n++] = c->ch[k++];
        } else {
            out[n++] = ' ';
        }
    }
    out[n] = 0;
    return n;
}
