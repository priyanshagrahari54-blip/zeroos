/* Strict URL parser.  See url.h for the contract. */
#include <zeroos/desktop/url.h>

static uint32_t u_len(const char *s) {
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}
static void u_copy(char *d, uint32_t cap, const char *s, uint32_t n) {
    uint32_t i = 0;
    if (!d || !cap)
        return;
    while (i < n && i + 1 < cap) {
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
}
static int u_ci(const char *a, const char *b) {
    /* case-insensitive prefix match: does a start with b?  (a may
     * continue — e.g. "https://host" vs "https://") */
    uint32_t i = 0;
    for (;;) {
        char x = a[i], y = b[i];
        if (y >= 'A' && y <= 'Z')
            y = (char)(y - 'A' + 'a');
        if (!y)
            return 1; /* b exhausted: prefix matched */
        if (x >= 'A' && x <= 'Z')
            x = (char)(x - 'A' + 'a');
        if (x != y || !x)
            return 0;
        ++i;
    }
}
static int u_hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int u_bad_ctrl(const char *s, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f)
            return 1;
    }
    return 0;
}

int zd_url_scheme_allowed(int scheme) {
    return scheme == ZD_URL_SCHEME_HTTP || scheme == ZD_URL_SCHEME_HTTPS ||
           scheme == ZD_URL_SCHEME_ABOUT;
}

int zd_url_parse(const char *input, struct zd_url *out) {
    uint32_t n, i;
    const char *colon, *host_start, *p;
    uint32_t host_len;

    if (!out)
        return -22;
    out->scheme = ZD_URL_SCHEME_NONE;
    out->host[0] = 0;
    out->port = 0;
    out->path[0] = 0;
    out->is_secure = 0;
    out->reject_reason = ZD_URL_R_NULL;

    if (!input)
        return -22;
    n = u_len(input);
    if (n == 0) {
        out->reject_reason = ZD_URL_R_MALFORMED;
        return -22;
    }
    if (n > ZD_URL_MAX) {
        out->reject_reason = ZD_URL_R_TOO_LONG;
        return -22;
    }
    if (u_bad_ctrl(input, n)) {
        out->reject_reason = ZD_URL_R_MALFORMED;
        return -22;
    }

    /* scheme = *ALPHA "://"  (we require the hierarchical part) */
    colon = 0;
    for (i = 0; i < n; ++i) {
        char c = input[i];
        if (c == ':') {
            colon = input + i;
            break;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))
            break;
    }
    if (!colon || colon == input) {
        out->reject_reason = ZD_URL_R_BAD_SCHEME;
        return -22;
    }

    if (u_ci(input, "about:")) { /* exact "about:blank" family */
        uint32_t alen = (uint32_t)(colon - input) + 1; /* include ':' */
        if (u_len(input) != alen + 5 /* ":blank" minus colon len math */ ||
            u_ci(input + alen, "blank") == 0) {
            out->reject_reason = ZD_URL_R_BAD_SCHEME;
            return -22;
        }
        out->scheme = ZD_URL_SCHEME_ABOUT;
        u_copy(out->path, sizeof(out->path), "", 0);
        return 0;
    }

    if (u_ci(input, "http://")) {
        out->scheme = ZD_URL_SCHEME_HTTP;
        p = input + 7;
    } else if (u_ci(input, "https://")) {
        out->scheme = ZD_URL_SCHEME_HTTPS;
        out->is_secure = 1;
        p = input + 8;
    } else {
        /* javascript:, data:, file:, unknown: — never navigable */
        out->reject_reason = ZD_URL_R_BAD_SCHEME;
        return -22;
    }

    /* authority = host[:port] ; no userinfo (anti-phishing), no
     * IPv6 literal in this slice (rejected as bad host).  The host
     * ends at the first colon (port separator). */
    host_start = p;
    i = 0;
    while (p[i] && p[i] != '/' && p[i] != '?' && p[i] != '#')
        ++i;
    /* userinfo (user@host) is rejected outright — anti-phishing */
    {
        uint32_t k;
        for (k = 0; k < i; ++k) {
            if (host_start[k] == '@') {
                out->reject_reason = ZD_URL_R_BAD_HOST;
                return -22;
            }
        }
    }
    /* host ends at the first colon (port separator) */
    {
        uint32_t k;
        for (k = 0; k < i; ++k) {
            if (host_start[k] == ':')
                break;
        }
        i = k;
    }
    host_len = i;
    if (host_len == 0) {
        out->reject_reason = ZD_URL_R_NO_HOST;
        return -22;
    }
    if (host_len > ZD_URL_HOST_MAX) {
        out->reject_reason = ZD_URL_R_BAD_HOST;
        return -22;
    }
    if (host_start[0] == '@' ||
        (host_len > 1 && host_start[0] == '[')) {
        out->reject_reason = ZD_URL_R_BAD_HOST;
        return -22;
    }
    {
        uint32_t h;
        int label_len = 0, saw_digit = 0;
        for (h = 0; h < host_len; ++h) {
            char c = host_start[h];
            if (c == ' ' || c == '\\' || c == '@' || c == '[' || c == ']' ||
                (unsigned char)c < 0x21) {
                out->reject_reason = ZD_URL_R_BAD_HOST;
                return -22;
            }
            if (c == '.') {
                if (label_len == 0) {
                    out->reject_reason = ZD_URL_R_BAD_HOST;
                    return -22;
                }
                label_len = 0;
                continue;
            }
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '%')) {
                out->reject_reason = ZD_URL_R_BAD_HOST;
                return -22;
            }
            if (c >= '0' && c <= '9')
                saw_digit = 1;
            if (c == '%') { /* percent-encoding in host: need 2 hex */
                if (h + 2 >= host_len || u_hex(host_start[h + 1]) < 0 ||
                    u_hex(host_start[h + 2]) < 0) {
                    out->reject_reason = ZD_URL_R_BAD_HOST;
                    return -22;
                }
                h += 2;
                continue;
            }
            if (c == '-') {
                if (label_len == 0) {
                    out->reject_reason = ZD_URL_R_BAD_HOST;
                    return -22;
                }
                ++label_len;
                continue;
            }
            ++label_len;
            if (label_len > 63) {
                out->reject_reason = ZD_URL_R_BAD_HOST;
                return -22;
            }
        }
        if (label_len == 0) { /* trailing dot label empty */
            out->reject_reason = ZD_URL_R_BAD_HOST;
            return -22;
        }
        (void)saw_digit;
    }
    u_copy(out->host, sizeof(out->host), host_start, host_len);

    p = host_start + host_len;
    if (*p == ':') {
        uint32_t port = 0, digits = 0;
        ++p;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (uint32_t)(*p - '0');
            ++p;
            ++digits;
            if (digits > 5 || port > 65535) {
                out->reject_reason = ZD_URL_R_BAD_PORT;
                return -22;
            }
        }
        if (digits == 0 || (*p && *p != '/' && *p != '?' && *p != '#')) {
            out->reject_reason = ZD_URL_R_BAD_PORT;
            return -22;
        }
        out->port = (uint16_t)port;
    } else if (*p && *p != '/' && *p != '?' && *p != '#') {
        out->reject_reason = ZD_URL_R_BAD_HOST;
        return -22;
    }

    if (!*p) {
        out->path[0] = 0;
        return 0;
    }
    if (*p == '?' || *p == '#') {
        out->path[0] = '/';
        out->path[1] = 0;
        return 0;
    }
    /* path with query: validate escapes and control chars */
    {
        uint32_t plen = u_len(p), k;
        for (k = 0; k < plen; ++k) {
            unsigned char c = (unsigned char)p[k];
            if (c < 0x20 || c == 0x7f) {
                out->reject_reason = ZD_URL_R_BAD_PATH;
                return -22;
            }
            if (c == '%') {
                int hi = (k + 2 < plen) ? u_hex(p[k + 1]) : -1;
                int lo = (k + 2 < plen) ? u_hex(p[k + 2]) : -1;
                if (hi < 0 || lo < 0) {
                    out->reject_reason = ZD_URL_R_BAD_PATH;
                    return -22;
                }
                if (hi * 16 + lo < 0x20 || hi * 16 + lo == 0x7f) {
                    /* escapes decoding to control bytes (%00, %0A…) */
                    out->reject_reason = ZD_URL_R_BAD_PATH;
                    return -22;
                }
                k += 2;
            }
        }
        u_copy(out->path, sizeof(out->path), p, plen);
    }
    return 0;
}

const char *zd_url_reject_str(uint32_t reason) {
    switch (reason) {
    case ZD_URL_OK_REJECT: return "ok";
    case ZD_URL_R_NULL: return "null input";
    case ZD_URL_R_TOO_LONG: return "url too long";
    case ZD_URL_R_BAD_SCHEME: return "scheme not allowed";
    case ZD_URL_R_NO_HOST: return "missing host";
    case ZD_URL_R_BAD_HOST: return "invalid host";
    case ZD_URL_R_BAD_PORT: return "invalid port";
    case ZD_URL_R_BAD_PATH: return "invalid path";
    case ZD_URL_R_MALFORMED: return "malformed url";
    default: return "unknown";
    }
}
