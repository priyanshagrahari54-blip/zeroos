/* OCR contract (Stage 5 part G — Study Center).
 *
 * Honest capability surface: the engine is pluggable and NOT linked
 * in this build, so availability reports 0 and every recognize()
 * call fails explicitly with -95 (unsupported) after argument
 * validation.  Nothing here fabricates text.  When a licensed
 * engine lands, it registers through zd_ocr_set_engine() and the
 * same call path returns real results — the contract does not
 * change.  Claims stay at "contract + capability gate"; OCR
 * accuracy claims require engine evidence.
 */
#ifndef ZEROOS_DESKTOP_OCR_H
#define ZEROOS_DESKTOP_OCR_H

#include <stdint.h>

#define ZD_OCR_MAX_W 4096
#define ZD_OCR_MAX_H 4096
#define ZD_OCR_LANG 8

struct zd_ocr_engine {
    /* returns 0 with NUL-terminated text in out, or -errno */
    int (*recognize)(void *ctx, const uint8_t *image, uint32_t w,
                     uint32_t h, const char *lang, char *out,
                     uint32_t cap);
    void *ctx;
};

/* 1 when an engine is registered, else 0. */
uint32_t zd_ocr_available(void);
/* Register (NULL = unlink).  Returns previous engine pointer. */
struct zd_ocr_engine *zd_ocr_set_engine(struct zd_ocr_engine *e);
/* Argument validation always runs: null image/zero-or-huge
 * dimensions/empty out/overlong lang -> -22.  Without an engine ->
 * -95 (explicit unsupported, never fabricated text). */
int zd_ocr_recognize(const uint8_t *image, uint32_t w, uint32_t h,
                     const char *lang, char *out, uint32_t cap);
const char *zd_ocr_status_name(int rc);

#endif /* ZEROOS_DESKTOP_OCR_H */
