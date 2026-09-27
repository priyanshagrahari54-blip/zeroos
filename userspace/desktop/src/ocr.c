/* OCR capability gate.  See ocr.h. */
#include <zeroos/desktop/ocr.h>

static struct zd_ocr_engine *ocr_engine;

uint32_t zd_ocr_available(void) {
    return ocr_engine && ocr_engine->recognize;
}

struct zd_ocr_engine *zd_ocr_set_engine(struct zd_ocr_engine *e) {
    struct zd_ocr_engine *prev = ocr_engine;
    ocr_engine = e;
    return prev;
}

int zd_ocr_recognize(const uint8_t *image, uint32_t w, uint32_t h,
                     const char *lang, char *out, uint32_t cap) {
    uint32_t n = 0;
    if (!image || !out || !cap)
        return -22;
    if (w == 0 || h == 0 || w > ZD_OCR_MAX_W || h > ZD_OCR_MAX_H)
        return -22;
    if (lang) {
        while (lang[n]) {
            if (n + 1 >= ZD_OCR_LANG)
                return -22;
            ++n;
        }
    }
    out[0] = 0;
    if (!zd_ocr_available())
        return -95; /* explicit: no engine in this build */
    return ocr_engine->recognize(ocr_engine->ctx, image, w, h, lang,
                                 out, cap);
}

const char *zd_ocr_status_name(int rc) {
    switch (rc) {
    case 0:
        return "ok";
    case -95:
        return "no-engine";
    case -22:
        return "bad-arguments";
    default:
        return "error";
    }
}
