/* URL parsing for the browser (Stage 5 part E).
 * Strict validation with security defaults: only http/https (and
 * explicit internal pages), no javascript:/data: scheme smuggling,
 * no control characters, bounded components, port range checks.
 * Percent-encoding validated; canonical rebuild available. */
#ifndef ZEROOS_DESKTOP_URL_H
#define ZEROOS_DESKTOP_URL_H

#include <stdint.h>

#define ZD_URL_MAX 2048
#define ZD_URL_HOST_MAX 253

enum zd_url_scheme {
    ZD_URL_SCHEME_NONE = 0,
    ZD_URL_SCHEME_HTTP,
    ZD_URL_SCHEME_HTTPS,
    ZD_URL_SCHEME_ABOUT      /* about:blank — internal page only */
};

struct zd_url {
    int scheme;                       /* enum zd_url_scheme */
    char host[ZD_URL_HOST_MAX + 1];
    uint16_t port;                    /* 0 = scheme default */
    char path[ZD_URL_MAX];            /* begins with '/' or "" */
    uint32_t reject_reason;           /* enum zd_url_reject */
    uint8_t is_secure;
};

enum zd_url_reject {
    ZD_URL_OK_REJECT = 0,
    ZD_URL_R_NULL = 1,
    ZD_URL_R_TOO_LONG = 2,
    ZD_URL_R_BAD_SCHEME = 3,      /* javascript:, data:, vbscript:, … */
    ZD_URL_R_NO_HOST = 4,
    ZD_URL_R_BAD_HOST = 5,        /* spaces, control chars, bad labels */
    ZD_URL_R_BAD_PORT = 6,
    ZD_URL_R_BAD_PATH = 7,        /* control chars / bad percent-escapes */
    ZD_URL_R_MALFORMED = 8        /* structurally broken */
};

/* Parse a NUL-terminated input.  Returns 0 on success (out filled),
 * -22 with out->reject_reason set on rejection (out still filled for
 * diagnostics where possible). */
int zd_url_parse(const char *input, struct zd_url *out);
/* 1 if the scheme is safe to navigate (http/https/about). */
int zd_url_scheme_allowed(int scheme);
const char *zd_url_reject_str(uint32_t reason);

#endif /* ZEROOS_DESKTOP_URL_H */
