/* Terminal core (Stage 5 shell surface): a bounded screen buffer
 * with a streaming escape-sequence parser.
 *
 * v1 support (explicit, no pretence): UTF-8 text (0x00-0x7F single
 * byte; multi-byte collected across writes), CSI cursor movement
 * (A/B/C/D/H), erase display/line (J/K), SGR colors+attrs (0,1,4,7,
 * 30-37/90-97, 40-47/100-107), LF/CR/BS/TAB, alternate-screen-free
 * single buffer, bounded scrollback.  Unknown/unsupported sequences
 * are consumed and counted (term->stats.unknown_seqs) — never
 * misparsed as text, never unbounded.  No PTY here: the child side
 * binds later through zd_term_write/read.
 */
#ifndef ZEROOS_DESKTOP_TERM_H
#define ZEROOS_DESKTOP_TERM_H

#include <stdint.h>

#define ZD_TERM_COLS 80
#define ZD_TERM_ROWS 24
#define ZD_TERM_MAX_COLS 240
#define ZD_TERM_MAX_ROWS 120
#define ZD_TERM_SCROLLBACK 128
#define ZD_TERM_OSC_MAX 64   /* ignored payload cap (title etc.) */

/* cell attributes */
#define ZD_TERM_BOLD     (1u << 0)
#define ZD_TERM_UNDERLINE (1u << 1)
#define ZD_TERM_INVERSE  (1u << 2)

struct zd_term_cell {
    char ch[4];      /* UTF-8 bytes, NUL-terminated (" " when blank) */
    uint8_t fg;      /* 0-15 palette (SGR 30-37/90-97 mapped) */
    uint8_t bg;
    uint8_t attrs;   /* ZD_TERM_* mask */
};

struct zd_term {
    uint32_t rows, cols;
    struct zd_term_cell grid[ZD_TERM_MAX_ROWS][ZD_TERM_MAX_COLS];
    uint32_t cur_x, cur_y;          /* 0-based cursor */
    uint8_t fg, bg, attrs;           /* active pen */
    /* streaming parser state */
    int pstate;                      /* enum zd_term_pstate */
    char seq[32];                    /* CSI/OSC accumulator */
    uint32_t seq_len;
    uint8_t seq_overflow;            /* oversized CSI: drain, no parse */
    char utf8[4];                    /* partial UTF-8 collect */
    uint32_t utf8_len, utf8_need;
    /* bounded scrollback: newest at index (head-1) mod CAP */
    struct zd_term_cell lines[ZD_TERM_SCROLLBACK][ZD_TERM_MAX_COLS];
    uint32_t sb_head;                /* next write slot */
    uint32_t sb_count;
    struct {
        uint32_t writes, bytes, unknown_seqs, osc_seen,
                 scrolls, truncated_utf8;
    } stats;
};

enum zd_term_pstate {
    ZD_TERM_S_GROUND = 0,
    ZD_TERM_S_ESC,
    ZD_TERM_S_ESC_INT,     /* ESC intermediates: ( ) # * + ... */
    ZD_TERM_S_CSI,
    ZD_TERM_S_OSC,
    ZD_TERM_S_OSC_ESC      /* ESC \ terminator pending */
};

void zd_term_init(struct zd_term *t, uint32_t rows, uint32_t cols);
/* Feed output bytes (from a PTY later; tests feed literals).
 * rows/cols validated at init: 0 -> defaults, > maxima -> -22. */
int zd_term_write(struct zd_term *t, const uint8_t *data,
                  uint32_t len);
/* Read a scrollback line: 0-based from oldest kept (0) to newest
 * (sb_count-1); copies up to cap cells, returns line length in
 * chars, -22 bad args, -2 out of range. */
int zd_term_scrollback_line(const struct zd_term *t, uint32_t idx,
                            struct zd_term_cell *out, uint32_t cap);
const struct zd_term_cell *zd_term_cell(const struct zd_term *t,
                                        uint32_t x, uint32_t y);
/* Plain-text row for UI/tests: NUL-terminates into out (cap bytes,
 * cells joined without padding); returns rows char count. */
uint32_t zd_term_row_text(const struct zd_term *t, uint32_t y,
                          char *out, uint32_t cap);

/* Line discipline (interactive input path).  The terminal above renders
 * output; this is the editing half of an interactive shell line: bytes
 * arrive one at a time, printable bytes are appended, BS/DEL erases, and
 * CR/LF completes the line.  It never writes to the screen itself -- it
 * reports the exact echo bytes the caller must send down its output path
 * (in the session, a real kernel pipe), so what the user sees is produced
 * by the same parser that renders program output.  Nothing is silently
 * truncated: a byte that does not fit is refused and counted. */
#define ZD_TERM_LINE_MAX 256

struct zd_term_line {
    char buf[ZD_TERM_LINE_MAX + 1];
    uint32_t len;          /* bytes currently in the line */
    uint32_t completions;  /* CR/LF accepted */
    uint32_t erased;       /* BS/DEL applied to a non-empty line */
    uint32_t overflow;     /* printable bytes refused (line full) */
    uint32_t ignored;      /* control bytes other than BS/DEL/CR/LF */
};

void zd_term_line_init(struct zd_term_line *l);
/* Feed one input byte.  *echo / *echo_len receive the bytes to send to the
 * display path (may be empty).  Returns 1 when the line is complete, 0
 * when more input is needed, -22 on bad arguments.  A completed line stays
 * in the buffer until the next input byte or zd_term_line_init. */
int zd_term_line_input(struct zd_term_line *l, uint8_t byte,
                       const char **echo, uint32_t *echo_len);
/* Copy the current line, NUL-terminated, into out (cap bytes); returns the
 * full line length even when the copy was truncated. */
uint32_t zd_term_line_copy(const struct zd_term_line *l, char *out,
                           uint32_t cap);

#endif /* ZEROOS_DESKTOP_TERM_H */
