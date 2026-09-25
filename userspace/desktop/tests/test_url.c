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

    /* reject strings are stable */
    ZD_CHECK(zd_url_reject_str(ZD_URL_R_BAD_SCHEME) != 0);
    ZD_CHECK(zd_url_reject_str(999) != 0);
    ZD_CHECK(zd_url_parse(0, 0) == -22);
}
