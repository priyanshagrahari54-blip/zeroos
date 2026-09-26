/* Clipboard service host tests. */
#include <string.h>
#include "test_harness.h"
#include <zeroos/desktop/clipboard.h>

void zd_test_clipboard_suite(void) {
    struct zd_clipboard cb;
    char out[ZD_CLIP_TEXT];

    zd_clipboard_init(&cb);
    ZD_CHECK_EQ(zd_clipboard_history_count(&cb), 0U);
    ZD_CHECK(zd_clipboard_paste(&cb, out, sizeof(out)) == -1);
    ZD_CHECK(zd_clipboard_cycle(&cb, 0, out, sizeof(out)) == -1);

    /* empty/bad copies rejected and counted */
    ZD_CHECK(zd_clipboard_copy(&cb, "app", 0, 0, 0) == -22);
    ZD_CHECK(zd_clipboard_copy(&cb, "app", "", 0, 0) == -22);
    ZD_CHECK(zd_clipboard_copy(&cb, "app", "x", 99, 0) == -22);
    ZD_CHECK(zd_clipboard_copy(&cb, 0, "hello", ZD_CLIP_FMT_TEXT, 0) == 0);
    ZD_CHECK_EQ(cb.stats.rejected, 3U);

    /* copy/paste roundtrip */
    ZD_CHECK(zd_clipboard_paste(&cb, out, sizeof(out)) == 0);
    ZD_CHECK(strcmp(out, "hello") == 0);
    ZD_CHECK(zd_clipboard_copy(&cb, "editor", "world",
                               ZD_CLIP_FMT_TEXT, 0) == 0);
    ZD_CHECK(zd_clipboard_paste(&cb, out, sizeof(out)) == 0);
    ZD_CHECK(strcmp(out, "world") == 0);

    /* history cycling: newest first, current at depth 0 */
    ZD_CHECK(zd_clipboard_copy(&cb, "browser", "page-snippet",
                               ZD_CLIP_FMT_TEXT, 0) == 0);
    ZD_CHECK_EQ(zd_clipboard_history_count(&cb), 3U);
    ZD_CHECK(zd_clipboard_cycle(&cb, 0, out, sizeof(out)) == 0);
    ZD_CHECK(strcmp(out, "page-snippet") == 0);
    ZD_CHECK(zd_clipboard_cycle(&cb, 1, out, sizeof(out)) == 0);
    ZD_CHECK(strcmp(out, "world") == 0);
    ZD_CHECK(zd_clipboard_cycle(&cb, 2, out, sizeof(out)) == 0);
    ZD_CHECK(strcmp(out, "hello") == 0);
    ZD_CHECK(zd_clipboard_cycle(&cb, 3, out, sizeof(out)) == -1);

    /* sensitive: becomes current, never enters history */
    ZD_CHECK(zd_clipboard_copy(&cb, "vault", "hunter2",
                               ZD_CLIP_FMT_TEXT, 1) == 0);
    ZD_CHECK_EQ(cb.stats.sensitive_kept, 1U);
    ZD_CHECK(zd_clipboard_paste(&cb, out, sizeof(out)) == 0);
    ZD_CHECK(strcmp(out, "hunter2") == 0);
    ZD_CHECK_EQ(zd_clipboard_history_count(&cb), 3U); /* unchanged */
    ZD_CHECK(zd_clipboard_cycle(&cb, 1, out, sizeof(out)) == 0);
    ZD_CHECK(strcmp(out, "page-snippet") == 0); /* newest history */
    {
        uint32_t i;
        for (i = 0; i < ZD_CLIP_MAX; ++i)
            ZD_CHECK(strcmp(cb.slots[i].text, "hunter2") != 0 || i == ZD_CLIP_MAX - 1);
    }

    /* overlong text rejected */
    {
        char big[ZD_CLIP_TEXT + 8];
        memset(big, 'a', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        ZD_CHECK(zd_clipboard_copy(&cb, 0, big, 0, 0) == -22);
    }

    /* capacity: fill past 11 history slots, oldest evicted */
    {
        uint32_t k;
        char t[16];
        for (k = 0; k < ZD_CLIP_MAX + 4; ++k) {
            int j = 0;
            t[j++] = 't';
            t[j++] = (char)('0' + k / 10);
            t[j++] = (char)('0' + k % 10);
            t[j] = 0;
            ZD_CHECK_OK(zd_clipboard_copy(&cb, "x", t, ZD_CLIP_FMT_TEXT, 0));
        }
        ZD_CHECK_EQ(zd_clipboard_history_count(&cb), ZD_CLIP_MAX - 1);
        /* oldest evicted; current is t15, so depth 1 = next distinct
         * entry (t14) — current is never double-counted */
        ZD_CHECK(zd_clipboard_cycle(&cb, 1, out, sizeof(out)) == 0);
        ZD_CHECK(strcmp(out, "t14") == 0);
    }

    /* bad cycle args */
    ZD_CHECK(zd_clipboard_cycle(&cb, 1, out, 0) == -22);
    ZD_CHECK(zd_clipboard_paste(&cb, 0, 4) == -22);

    /* clear wipes current + history (privacy) */
    zd_clipboard_clear(&cb);
    ZD_CHECK_EQ(zd_clipboard_history_count(&cb), 0U);
    ZD_CHECK(zd_clipboard_paste(&cb, out, sizeof(out)) == -1);
}
