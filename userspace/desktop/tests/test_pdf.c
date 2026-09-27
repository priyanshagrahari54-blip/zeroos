/* PDF contract tests — hand-authored minimal fixtures (part G) */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <string.h>

/* two pages, uncompressed content streams, /Contents references */
static const char pdf_two_pages[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
    "2 0 obj << /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >> endobj\n"
    "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
    "4 0 obj << /Length 20 >> stream\n"
    "(ZEROOS page one) Tj\n"
    "endstream endobj\n"
    "5 0 obj << /Type /Page /Parent 2 0 R /Contents 6 0 R >> endobj\n"
    "6 0 obj << /Length 20 >> stream\n"
    "[ (Hel) 3 (lo) ] TJ\n"
    "endstream endobj\n"
    "trailer << /Size 7 /Root 1 0 R >>\n"
    "startxref\n0\n%%EOF\n";

/* same but the content stream declares a filter */
static const char pdf_filtered[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
    "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
    "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
    "4 0 obj << /Length 10 /Filter /FlateDecode >> stream\n"
    "xxxxxxxxxx\n"
    "endstream endobj\n"
    "trailer << /Size 5 /Root 1 0 R /Encrypt 9 0 R >>\n"
    "%%EOF\n";

/* valid structure but an encryption dictionary is present */
static const char pdf_encrypted[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
    "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
    "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
    "4 0 obj << /Length 10 >> stream\n"
    "(locked!!!!) Tj\n"
    "endstream endobj\n"
    "trailer << /Size 6 /Root 1 0 R /Encrypt 5 0 R >>\n"
    "%%EOF\n";

/* header but no page tree */
static const char pdf_no_pages[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog >> endobj\n"
    "trailer << /Size 2 /Root 1 0 R >>\n"
    "%%EOF\n";

/* page object without a /Contents reference */
static const char pdf_no_contents[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
    "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
    "3 0 obj << /Type /Page /Parent 2 0 R >> endobj\n"
    "trailer << /Size 4 /Root 1 0 R >>\n"
    "%%EOF\n";

/* escaped parens inside a literal string */
static const char pdf_escapes[] =
    "%PDF-1.7\n"
    "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
    "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
    "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
    "4 0 obj << /Length 24 >> stream\n"
    "(line\\(one\\)) Tj\n"
    "endstream endobj\n"
    "trailer << /Size 5 /Root 1 0 R >>\n"
    "%%EOF\n";

void zd_test_pdf_suite(void) {
    struct zd_pdf doc;
    int rc;

    /* ---- two-page happy path ---- */
    rc = zd_pdf_open((const uint8_t *)pdf_two_pages,
                     (uint32_t)(sizeof(pdf_two_pages) - 1), &doc);
    ZD_CHECK_OK(rc);
    ZD_CHECK(strcmp(doc.version, "1.7") == 0);
    ZD_CHECK_EQ(doc.page_count, 2);
    ZD_CHECK_EQ(doc.encrypted, 0);
    ZD_CHECK_EQ(doc.filtered_streams, 0);
    ZD_CHECK_OK(zd_pdf_extract(&doc));
    ZD_CHECK_EQ(doc.pages[0].obj_num, 3);
    ZD_CHECK_EQ(doc.pages[1].obj_num, 5);
    ZD_CHECK(strcmp(doc.pages[0].text, "ZEROOS page one\n") == 0);
    ZD_CHECK(strcmp(doc.pages[1].text, "Hello") == 0);
    ZD_CHECK_EQ(doc.pages[0].text_len, 16);

    /* ---- filtered streams: explicit unsupported ---- */
    rc = zd_pdf_open((const uint8_t *)pdf_filtered,
                     (uint32_t)(sizeof(pdf_filtered) - 1), &doc);
    ZD_CHECK_EQ(rc, -95);
    ZD_CHECK(doc.encrypted == 1 || doc.filtered_streams >= 1);
    ZD_CHECK(strcmp(zd_pdf_status_name(rc),
                    "unsupported-feature") == 0);
    ZD_CHECK_EQ(zd_pdf_extract(&doc), -95);

    /* ---- encryption: explicit unsupported, structure still known -- */
    rc = zd_pdf_open((const uint8_t *)pdf_encrypted,
                     (uint32_t)(sizeof(pdf_encrypted) - 1), &doc);
    ZD_CHECK_EQ(rc, -95);
    ZD_CHECK_EQ(doc.encrypted, 1);
    ZD_CHECK_EQ(doc.page_count, 1);
    ZD_CHECK_EQ(zd_pdf_extract(&doc), -95);

    /* ---- malformed / truncated / headerless ---- */
    ZD_CHECK_EQ(zd_pdf_open((const uint8_t *)"garbage", 7, &doc), -22);
    ZD_CHECK_EQ(zd_pdf_open((const uint8_t *)"%PDF-1.7 short", 14, &doc),
                -22);
    ZD_CHECK_EQ(zd_pdf_open(NULL, 100, &doc), -22);
    ZD_CHECK_EQ(zd_pdf_open((const uint8_t *)pdf_two_pages, 100, &doc),
                -22); /* truncated mid-file before pages */
    ZD_CHECK_EQ(zd_pdf_open((const uint8_t *)pdf_no_pages,
                            (uint32_t)(sizeof(pdf_no_pages) - 1),
                            &doc), -22);
    ZD_CHECK(strcmp(zd_pdf_status_name(-22), "malformed") == 0);
    ZD_CHECK(strcmp(zd_pdf_status_name(0), "ok") == 0);
    ZD_CHECK(strcmp(zd_pdf_status_name(-5), "error") == 0);

    /* ---- page without contents: tolerated with warning ---- */
    rc = zd_pdf_open((const uint8_t *)pdf_no_contents,
                     (uint32_t)(sizeof(pdf_no_contents) - 1), &doc);
    ZD_CHECK_OK(rc);
    ZD_CHECK_OK(zd_pdf_extract(&doc));
    ZD_CHECK_EQ(doc.parse_warnings, 1);
    ZD_CHECK_EQ(doc.pages[0].text_len, 0);

    /* ---- escapes unescaped ---- */
    rc = zd_pdf_open((const uint8_t *)pdf_escapes,
                     (uint32_t)(sizeof(pdf_escapes) - 1), &doc);
    ZD_CHECK_OK(rc);
    ZD_CHECK_OK(zd_pdf_extract(&doc));
    ZD_CHECK(strcmp(doc.pages[0].text, "line(one)\n") == 0);

    /* ---- text cap: truncation counted as a warning ---- */
    {
        static char big[4200];
        static char built[6000];
        uint32_t i = 0, off = 0;
        memcpy(big, "%PDF-1.7\n", 9);
        off = 9;
        memcpy(big + off,
               "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n", 50);
        off += 50;
        memcpy(big + off,
               "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n",
               58);
        off += 58;
        memcpy(big + off,
               "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n",
               63);
        off += 63;
        memcpy(big + off, "4 0 obj << /Length 3000 >> stream\n", 34);
        off += 34;
        big[off++] = '(';
        for (i = 0; i < 3000; ++i)
            big[off++] = (char)('A' + (i % 26));
        big[off++] = ')';
        big[off++] = ' ';
        big[off++] = 'T';
        big[off++] = 'j';
        big[off++] = '\n';
        memcpy(big + off, "endstream endobj\n", 17);
        off += 17;
        memcpy(big + off, "trailer << /Size 5 >>\n%%EOF\n", 27);
        off += 27;
        rc = zd_pdf_open((const uint8_t *)big, off, &doc);
        ZD_CHECK_OK(rc);
        ZD_CHECK_OK(zd_pdf_extract(&doc));
        ZD_CHECK_EQ(doc.pages[0].text_len, ZD_PDF_TEXT_CAP - 1);
        ZD_CHECK(doc.parse_warnings >= 1);
        (void)built;
    }

    /* ---- extract without open ---- */
    ZD_CHECK_EQ(zd_pdf_extract(NULL), -22);
    memset(&doc, 0, sizeof(doc));
    ZD_CHECK_EQ(zd_pdf_extract(&doc), -22);
}
