/* Formula engine + OCR contract tests (part G) */
#include "test_harness.h"
#include <zeroos/desktop/desktop.h>
#include <string.h>

#define FEQ(a, b) (((a) - (b) < 0.0001) && ((b) - (a) < 0.0001))

static void test_formula(void) {
    struct zd_formula f;
    double v = 0;

    zd_formula_init(&f);

    /* arithmetic with precedence */
    ZD_CHECK_OK(zd_formula_parse(&f, "2+3*4"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 14.0));
    ZD_CHECK_OK(zd_formula_parse(&f, "(2+3)*4"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 20.0));
    ZD_CHECK_OK(zd_formula_parse(&f, "10/4"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 2.5));
    ZD_CHECK_OK(zd_formula_parse(&f, "1.5+0.25"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 1.75));
    ZD_CHECK_OK(zd_formula_parse(&f, "1.55"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 1.55));

    /* unary minus and precedence of power */
    ZD_CHECK_OK(zd_formula_parse(&f, "-3+10"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 7.0));
    ZD_CHECK_OK(zd_formula_parse(&f, "2^3^2")); /* right assoc */
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 512.0));
    ZD_CHECK_OK(zd_formula_parse(&f, "-2^2"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, -4.0)); /* unary binds looser than ^ base */

    /* \frac and \sqrt */
    ZD_CHECK_OK(zd_formula_parse(&f, "\\frac{1}{4}"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 0.25));
    ZD_CHECK_OK(zd_formula_parse(&f, "\\frac{2+3}{1+1}"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 2.5));
    ZD_CHECK_OK(zd_formula_parse(&f, "\\sqrt{9}"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 3.0));
    ZD_CHECK_OK(zd_formula_parse(&f, "\\sqrt{2}"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 1.41421356));

    /* variables */
    ZD_CHECK_OK(zd_formula_parse(&f, "x*y+z"));
    ZD_CHECK_EQ(zd_formula_eval(&f, &v), -2); /* unknown var */
    ZD_CHECK_OK(zd_formula_set_var(&f, "x", 3));
    ZD_CHECK_OK(zd_formula_set_var(&f, "y", 4));
    ZD_CHECK_OK(zd_formula_set_var(&f, "z", 1));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 13.0));
    /* update in place */
    ZD_CHECK_OK(zd_formula_set_var(&f, "y", 5));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 16.0));
    /* invalid names */
    ZD_CHECK_EQ(zd_formula_set_var(&f, "1bad", 1), -22);
    ZD_CHECK_EQ(zd_formula_set_var(&f, "", 1), -22);
    ZD_CHECK_EQ(zd_formula_set_var(&f, "toolongname", 1), -22);
    ZD_CHECK(f.stats.var_rejected >= 3);

    /* explicit errors */
    ZD_CHECK_OK(zd_formula_parse(&f, "1/0"));
    ZD_CHECK_EQ(zd_formula_eval(&f, &v), -22); /* never Inf */
    ZD_CHECK_OK(zd_formula_parse(&f, "2^0.5"));
    ZD_CHECK_EQ(zd_formula_eval(&f, &v), -22); /* non-integer exp */
    ZD_CHECK_OK(zd_formula_parse(&f, "\\sqrt{-4}"));
    ZD_CHECK_EQ(zd_formula_eval(&f, &v), -22); /* never NaN */
    ZD_CHECK_OK(zd_formula_parse(&f, "\\frac{1}{0}"));
    ZD_CHECK_EQ(zd_formula_eval(&f, &v), -22);

    /* parse errors with position */
    ZD_CHECK_EQ(zd_formula_parse(&f, "2+"), -22);
    ZD_CHECK(f.err_pos > 0);
    ZD_CHECK_EQ(zd_formula_parse(&f, "(1+2"), -22);
    ZD_CHECK_OK(zd_formula_parse(&f, "1 + + 2")); /* unary plus */
    ZD_CHECK_EQ(zd_formula_parse(&f, "2 3"), -22); /* juxtaposition */
    ZD_CHECK_EQ(zd_formula_parse(&f, ""), -22);
    ZD_CHECK_EQ(zd_formula_parse(&f, NULL), -22);
    ZD_CHECK_EQ(zd_formula_parse(&f, "\\frac{1}{2"), -22);
    ZD_CHECK(f.stats.parse_errors >= 6);
    /* eval without parse */
    ZD_CHECK_EQ(zd_formula_eval(&f, &v), -22);
    ZD_CHECK_EQ(zd_formula_eval(&f, NULL), -22);

    /* overlong input */
    {
        char big[ZD_FORMULA_INPUT + 16];
        memset(big, '9', sizeof(big) - 1);
        big[0] = '1';
        big[sizeof(big) - 1] = 0;
        ZD_CHECK_EQ(zd_formula_parse(&f, big), -22);
    }

    /* deep nesting rejected (depth bound) */
    {
        char deep[80];
        uint32_t i, n = 0;
        for (i = 0; i < 20 && n + 2 < sizeof(deep); ++i)
            deep[n++] = '(';
        deep[n++] = '1';
        for (i = 0; i < 20 && n + 1 < sizeof(deep); ++i)
            deep[n++] = ')';
        deep[n] = 0;
        ZD_CHECK_EQ(zd_formula_parse(&f, deep), -22);
    }

    /* arena bound: many terms blow the node cap cleanly */
    ZD_CHECK_EQ(zd_formula_parse(
                    &f,
                    "1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1"
                    "+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1"
                    "+1+1+1+1+1+1+1+1+1+1+1+1"),
                -22);

    /* vars persist across parses on the same object */
    ZD_CHECK_OK(zd_formula_parse(&f, "x+1"));
    ZD_CHECK_OK(zd_formula_eval(&f, &v));
    ZD_CHECK(FEQ(v, 4.0)); /* x still 3 */
    ZD_CHECK(f.stats.parses >= 5);
    ZD_CHECK(f.stats.evals >= 5);
}

/* ---- OCR contract ---- */

static int ocr_fixture_engine(void *ctx, const uint8_t *img,
                              uint32_t w, uint32_t h,
                              const char *lang, char *out,
                              uint32_t cap) {
    (void)img;
    (void)w;
    (void)h;
    (void)ctx;
    if (cap < 6)
        return -28;
    if (lang && lang[0] == 'x')
        return -2;
    out[0] = 'o';
    out[1] = 'k';
    out[2] = 0;
    return 0;
}

static void test_ocr(void) {
    static uint8_t img[16] = {1, 2, 3, 4};
    char out[32];

    /* production build: no engine */
    ZD_CHECK_EQ(zd_ocr_available(), 0);
    ZD_CHECK_EQ(zd_ocr_recognize(img, 4, 4, "eng", out, sizeof(out)),
                -95);
    ZD_CHECK_EQ(out[0], 0); /* nothing fabricated */
    ZD_CHECK(strcmp(zd_ocr_status_name(-95), "no-engine") == 0);

    /* argument validation runs before the capability gate */
    ZD_CHECK_EQ(zd_ocr_recognize(NULL, 4, 4, 0, out, sizeof(out)),
                -22);
    ZD_CHECK_EQ(zd_ocr_recognize(img, 0, 4, 0, out, sizeof(out)),
                -22);
    ZD_CHECK_EQ(zd_ocr_recognize(img, 4, 4, 0, out, 0), -22);
    ZD_CHECK_EQ(zd_ocr_recognize(img, ZD_OCR_MAX_W + 1, 4, 0, out,
                                 sizeof(out)), -22);
    ZD_CHECK(strcmp(zd_ocr_status_name(-22), "bad-arguments") == 0);
    ZD_CHECK(strcmp(zd_ocr_status_name(0), "ok") == 0);

    /* contract plumbing: a registered engine drives the same path */
    {
        static struct zd_ocr_engine eng;
        struct zd_ocr_engine *prev;
        eng.recognize = ocr_fixture_engine;
        eng.ctx = 0;
        prev = zd_ocr_set_engine(&eng);
        ZD_CHECK(prev == NULL);
        ZD_CHECK_EQ(zd_ocr_available(), 1);
        ZD_CHECK_OK(zd_ocr_recognize(img, 4, 4, "eng", out,
                                     sizeof(out)));
        ZD_CHECK(strcmp(out, "ok") == 0);
        /* engine errno propagates */
        ZD_CHECK_EQ(zd_ocr_recognize(img, 4, 4, "x", out,
                                     sizeof(out)), -2);
        /* unlink -> back to explicit unsupported */
        prev = zd_ocr_set_engine(prev);
        ZD_CHECK(prev == &eng);
        ZD_CHECK_EQ(zd_ocr_available(), 0);
        ZD_CHECK_EQ(zd_ocr_recognize(img, 4, 4, 0, out, sizeof(out)),
                    -95);
    }
}

void zd_test_formula_ocr_suite(void) {
    test_formula();
    test_ocr();
}
