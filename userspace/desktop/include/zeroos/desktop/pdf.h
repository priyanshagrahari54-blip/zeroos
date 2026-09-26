/* PDF document contract (Stage 5 part G — Study Center).
 *
 * Honest subset parser over caller-owned bytes, zero heap:
 *   - header/version check, page-tree walk (/Pages /Kids /Count),
 *     per-page content streams, text extraction for Tj/TJ operators
 *     in uncompressed streams.
 *   - Explicit unsupported failures (never silent): FlateDecode and
 *     other filters (-ZD_EOPNOTSUPP-style: -95), encryption dict
 *     (-95), malformed structure (-22), truncated input (-22).
 *   - Bounds: ZD_PDF_MAX_PAGES 64, ZD_PDF_TEXT_CAP per page 1024.
 * Fixtures in the test suite are hand-authored minimal PDFs.
 */
#ifndef ZEROOS_DESKTOP_PDF_H
#define ZEROOS_DESKTOP_PDF_H

#include <stdint.h>

#define ZD_PDF_MAX_PAGES 64
#define ZD_PDF_TEXT_CAP 1024
#define ZD_PDF_LABEL_CAP 48

enum zd_pdf_status {
    ZD_PDF_OK = 0,
    ZD_PDF_UNSUPPORTED_FILTER = -95,  /* e.g. FlateDecode */
    ZD_PDF_UNSUPPORTED_ENCRYPT = -95,
    ZD_PDF_MALFORMED = -22
};

struct zd_pdf_page {
    uint32_t obj_num;                 /* page object number */
    uint32_t text_len;                /* extracted chars */
    char text[ZD_PDF_TEXT_CAP];
};

struct zd_pdf {
    const uint8_t *data;              /* borrowed, not copied */
    uint32_t size;
    char version[8];                  /* e.g. "1.7" */
    uint32_t page_count;
    uint32_t encrypted;               /* 1 = /Encrypt present */
    uint32_t filtered_streams;        /* streams with a /Filter */
    struct zd_pdf_page pages[ZD_PDF_MAX_PAGES];
    uint32_t parse_warnings;          /* tolerated oddities */
};

/* Parse structure (page tree, encryption, filters) without extracting
 * text.  bad args/truncated/no-header/no-page-tree -> -22;
 * encryption or filter present -> -95 (caller may still not proceed
 * without a decoder, but knows WHY). */
int zd_pdf_open(const uint8_t *data, uint32_t size, struct zd_pdf *out);
/* open() + extract Tj/TJ text for every page.  Same errors as open;
 * page text is NUL-terminated, truncated text counted in warnings. */
int zd_pdf_extract(struct zd_pdf *doc);
/* Human-readable failure name for UI/diagnostic surfaces. */
const char *zd_pdf_status_name(int rc);

#endif /* ZEROOS_DESKTOP_PDF_H */
