/* Browser URL parser host tests (part E) — security negatives. */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/url.h>

void zd_test_url_suite(void) {
    struct zd_url u;
    int rc;

    /* valid https */
    rc = zd_url_parse("https://example.com/path?x=1", &u);
    ZD_CHECK(rc == 0);
    ZD_CHECK(u.scheme == ZD_URL_SCHEME_HTTPS && u.is_secure == 1);
    ZD_CHECK(strcmp(u.host, "example.com") == 0);
    ZD_CHECK(strcmp(u.path, "/path?x=1") == 0);
    ZD_CHECK(u.port == 0);
    /* success leaves no rejection reason behind for the UI to show */
    ZD_CHECK(u.reject_reason == ZD_URL_OK_REJECT);

    /* http with explicit port */
    rc = zd_url_parse("http://localhost:8080/x", &u);
    ZD_CHECK(rc == 0);
    ZD_CHECK(u.scheme == ZD_URL_SCHEME_HTTP && u.is_secure == 0);
    ZD_CHECK(u.port == 8080 && strcmp(u.host, "localhost") == 0);

    /* scheme case-insensitive */
    ZD_CHECK(zd_url_parse("HTTP://a.example/", &u) == 0);
    ZD_CHECK(zd_url_parse("Https://a.example/", &u) == 0);

    /* bare host with no path */
    ZD_CHECK(zd_url_parse("https://example.com", &u) == 0);
    ZD_CHECK(u.path[0] == 0);

    /* about:blank only */
    ZD_CHECK(zd_url_parse("about:blank", &u) == 0);
    ZD_CHECK(u.scheme == ZD_URL_SCHEME_ABOUT);
    ZD_CHECK(zd_url_parse("about:config", &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_SCHEME);

    /* dangerous schemes never navigate */
    ZD_CHECK(zd_url_parse("javascript:alert(1)", &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_SCHEME);
    ZD_CHECK(zd_url_parse("data:text/html,<script>x</script>", &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_SCHEME);
    ZD_CHECK(zd_url_parse("vbscript:x", &u) == -22);
    ZD_CHECK(zd_url_parse("file:///etc/passwd", &u) == -22);
    ZD_CHECK(zd_url_scheme_allowed(u.scheme) == 0);
    ZD_CHECK(zd_url_scheme_allowed(ZD_URL_SCHEME_HTTPS) == 1);

    /* null/empty/overlong */
    ZD_CHECK(zd_url_parse(0, &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_NULL);
    ZD_CHECK(zd_url_parse("", &u) == -22);
    {
        char big[ZD_URL_MAX + 8];
        memset(big, 'a', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        memcpy(big, "https://example.com/", 20);
        ZD_CHECK(zd_url_parse(big, &u) == -22);
        ZD_CHECK(u.reject_reason == ZD_URL_R_TOO_LONG);
    }

    /* control characters and embedded newline injection */
    ZD_CHECK(zd_url_parse("https://exa\tmple.com/", &u) == -22);
    ZD_CHECK(zd_url_parse("https://example.com/a\nb", &u) == -22);

    /* host rules */
    ZD_CHECK(zd_url_parse("https://", &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_NO_HOST);
    ZD_CHECK(zd_url_parse("https://bad host/", &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_HOST);
    ZD_CHECK(zd_url_parse("https://user:pass@evil.com/", &u) == -22); /* userinfo */
    ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_HOST);
    ZD_CHECK(zd_url_parse("https://-lead.com/", &u) == -22);  /* label rule */
    ZD_CHECK(zd_url_parse("https://trail.-com/", &u) == -22);
    ZD_CHECK(zd_url_parse("https://double..dot/", &u) == -22);
    ZD_CHECK(zd_url_parse("https://[::1]/", &u) == -22); /* not in slice */

    /* ports */
    ZD_CHECK(zd_url_parse("https://example.com:0/", &u) == 0);
    ZD_CHECK(zd_url_parse("https://example.com:65535/", &u) == 0);
    ZD_CHECK(zd_url_parse("https://example.com:65536/", &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_PORT);
    ZD_CHECK(zd_url_parse("https://example.com:99999999/", &u) == -22);
    ZD_CHECK(zd_url_parse("https://example.com:abc/", &u) == -22);

    /* percent-encoding validation in path */
    ZD_CHECK(zd_url_parse("https://example.com/a%20b", &u) == 0);
    ZD_CHECK(zd_url_parse("https://example.com/a%2", &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_PATH);
    ZD_CHECK(zd_url_parse("https://example.com/a%zz", &u) == -22);
    ZD_CHECK(zd_url_parse("https://example.com/a%00", &u) == -22);

    /* Boundaries. A single 253-byte label is not a legal DNS name -- the
     * label rule is 63 -- so the honest host boundary is four legal
     * labels, and everything here pins that the parser keeps what it
     * accepted instead of quietly truncating it. */
    {
        char big[ZD_URL_MAX + 16];
        unsigned at, lab;

        /* host of exactly ZD_URL_HOST_MAX: accepted, not truncated */
        memset(big, 0, sizeof(big));
        memcpy(big, "https://", 8);
        at = 8;
        for (lab = 0; lab < 3; ++lab) {
            memset(big + at, 'a', 63);
            at += 63;
            big[at++] = '.';
        }
        memset(big + at, 'b', ZD_URL_HOST_MAX - (at - 8));
        at += ZD_URL_HOST_MAX - (at - 8);
        big[at++] = '/';
        big[at] = 0;
        ZD_CHECK(strlen(big + 8) == (size_t)ZD_URL_HOST_MAX + 1);
        ZD_CHECK(zd_url_parse(big, &u) == 0);
        ZD_CHECK(strlen(u.host) == (size_t)ZD_URL_HOST_MAX);
        /* one more host byte and the host is too long, not silently cut */
        at = (unsigned)strlen(big);
        big[at - 1] = 'a';
        big[at] = '/';
        big[at + 1] = 0;
        ZD_CHECK(zd_url_parse(big, &u) == -22);
        ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_HOST);
    }
    {
        char lab[80];
        memset(lab, 0, sizeof(lab));
        memcpy(lab, "https://", 8);
        memset(lab + 8, 'b', 63);
        memcpy(lab + 71, ".com/", 5);
        ZD_CHECK(zd_url_parse(lab, &u) == 0); /* 63-byte label is legal */
        memset(lab + 8, 'b', 64);
        memcpy(lab + 72, ".com/", 5);
        ZD_CHECK(zd_url_parse(lab, &u) == -22); /* 64 is not */
        ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_HOST);
    }
    {
        /* total length: exactly ZD_URL_MAX parses and the path keeps
         * every byte of it; one more byte is refused as too long. */
        char big[ZD_URL_MAX + 16];
        unsigned n, i;
        memset(big, 0, sizeof(big));
        memcpy(big, "https://a.co/", 13);
        for (i = 13; i < ZD_URL_MAX; ++i)
            big[i] = 'p';
        big[ZD_URL_MAX] = 0;
        n = (unsigned)strlen(big);
        ZD_CHECK(n == ZD_URL_MAX);
        ZD_CHECK(zd_url_parse(big, &u) == 0);
        ZD_CHECK(strlen(u.path) == n - 12); /* "https://a.co" is 12 bytes */
        big[n] = 'p';
        big[n + 1] = 0;
        ZD_CHECK(zd_url_parse(big, &u) == -22);
        ZD_CHECK(u.reject_reason == ZD_URL_R_TOO_LONG);
    }

    /* escapes that run off the end, in the path and in the host */
    ZD_CHECK(zd_url_parse("https://a.co/x%4", &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_PATH);
    ZD_CHECK(zd_url_parse("https://a.co%2", &u) == -22);
    ZD_CHECK(zd_url_parse("https://a%41.co/", &u) == 0);

    /* port: empty is not "default", the maximum is kept */
    ZD_CHECK(zd_url_parse("https://a.co:/", &u) == -22);
    ZD_CHECK(u.reject_reason == ZD_URL_R_BAD_PORT);
    ZD_CHECK(zd_url_parse("https://a.co:65535/", &u) == 0);
    ZD_CHECK(u.port == 65535);

    /* the about: family is exact, case-insensitive, and nothing else */
    ZD_CHECK(zd_url_parse("ABOUT:BLANK", &u) == 0);
    ZD_CHECK(u.scheme == ZD_URL_SCHEME_ABOUT);
    ZD_CHECK(zd_url_parse("about:blankx", &u) == -22);
    ZD_CHECK(zd_url_parse("about:", &u) == -22);
    ZD_CHECK(zd_url_parse("about", &u) == -22);
    ZD_CHECK(zd_url_parse(":", &u) == -22);
    ZD_CHECK(zd_url_parse("//a.co/", &u) == -22); /* scheme-relative */
    ZD_CHECK(zd_url_parse("https://a.co", &u) == 0);
    ZD_CHECK(u.path[0] == 0 && u.port == 0);

    /* reject strings are stable */
    ZD_CHECK(zd_url_reject_str(ZD_URL_R_BAD_SCHEME) != 0);
    ZD_CHECK(zd_url_reject_str(999) != 0);
    ZD_CHECK(zd_url_parse(0, 0) == -22);
}
