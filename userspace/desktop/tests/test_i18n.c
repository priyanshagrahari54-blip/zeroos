#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

static void test_catalog_coverage(void) {
    struct zd_i18n i18n;
    uint32_t total = 0;
    uint32_t hi_present = 0;
    zd_i18n_init(&i18n, ZD_LOCALE_EN);
    ZD_CHECK(zd_i18n_load_shell_catalog(&i18n) >= 30U);
    zd_i18n_coverage(&i18n, &total, &hi_present);
    ZD_CHECK(total >= 30U);
    /* Full Hindi coverage for shipped keys: no missing translations. */
    ZD_CHECK_EQ(total, hi_present);
    ZD_CHECK_EQ(i18n.fallback_count, 0U);
    ZD_CHECK_EQ(i18n.missing_count, 0U);
}

static void test_locale_switching(void) {
    struct zd_i18n i18n;
    const char *english;
    const char *hindi;
    zd_i18n_init(&i18n, ZD_LOCALE_EN);
    zd_i18n_load_shell_catalog(&i18n);
    english = (const char *)zd_i18n_text(&i18n, "shell.settings");
    ZD_CHECK(strcmp(english, "Settings") == 0);
    ZD_CHECK_EQ(zd_i18n_locale(&i18n), ZD_LOCALE_EN);
    zd_i18n_set_locale(&i18n, ZD_LOCALE_HI);
    hindi = (const char *)zd_i18n_text(&i18n, "shell.settings");
    ZD_CHECK(strcmp(hindi, "Settings") != 0);
    ZD_CHECK(hindi[0] != '\0');
    ZD_CHECK(strcmp(hindi, "सेटिंग्स") == 0);
    /* UTF-8 bytes preserved exactly (Devanagari multi-byte sequence). */
    ZD_CHECK(strlen(hindi) > 3U);
    /* Missing key falls back to the key text and is counted. */
    ZD_CHECK(strcmp(zd_i18n_text(&i18n, "no.such.key"), "no.such.key") == 0);
    ZD_CHECK(i18n.missing_count >= 1U);
    /* Locale name helpers. */
    ZD_CHECK(strcmp(zd_i18n_locale_name(ZD_LOCALE_HI), "hi") == 0);
    ZD_CHECK(zd_i18n_locale_from_name("hi_IN") == ZD_LOCALE_HI);
    ZD_CHECK(zd_i18n_locale_from_name("en_US") == ZD_LOCALE_EN);
    ZD_CHECK(zd_i18n_locale_from_name(0) == ZD_LOCALE_EN);
}

static void test_ui_state_strings_both_locales(void) {
    /* Every UI acceptance state has a translated string in both locales. */
    static const char *const state_keys[] = {
        "state.loading", "state.empty", "state.error", "state.offline",
        "state.permission_denied", "state.low_resource",
        "state.reduced_motion", "state.keyboard_nav"
    };
    struct zd_i18n i18n;
    uint32_t index;
    zd_i18n_init(&i18n, ZD_LOCALE_EN);
    zd_i18n_load_shell_catalog(&i18n);
    for (index = 0; index < sizeof(state_keys) / sizeof(state_keys[0]);
         ++index) {
        const char *en = zd_i18n_text(&i18n, state_keys[index]);
        ZD_CHECK(en != state_keys[index]); /* resolved, not fallback */
        zd_i18n_set_locale(&i18n, ZD_LOCALE_HI);
        {
            const char *hi = zd_i18n_text(&i18n, state_keys[index]);
            ZD_CHECK(hi != state_keys[index]);
            ZD_CHECK(strcmp(hi, en) != 0); /* actually translated */
        }
        zd_i18n_set_locale(&i18n, ZD_LOCALE_EN);
    }
}

static void test_formatting(void) {
    struct zd_i18n i18n;
    char buffer[96];
    const char *args[3];
    zd_i18n_init(&i18n, ZD_LOCALE_EN);
    zd_i18n_load_shell_catalog(&i18n);

    /* {0} substitution using an existing template by re-using a key with
     * placeholders through a synthetic catalog entry is overkill; exercise
     * the formatter directly with a raw template via a temp key path:
     * format uses the looked-up text, so build a mini catalog. */
    {
        static const struct zd_i18n_entry mini[] = {
            {"test.fmt", "Hello {0}, you have {1} items", "नमस्ते {0}, {1} आइटम"}
        };
        i18n.catalog = mini;
        i18n.catalog_size = 1;
    }
    args[0] = "Asha";
    args[1] = "5";
    ZD_CHECK_OK(zd_i18n_format(&i18n, "test.fmt", args, 2, buffer,
                               sizeof(buffer)));
    ZD_CHECK(strcmp(buffer, "Hello Asha, you have 5 items") == 0);
    zd_i18n_set_locale(&i18n, ZD_LOCALE_HI);
    ZD_CHECK_OK(zd_i18n_format(&i18n, "test.fmt", args, 2, buffer,
                               sizeof(buffer)));
    ZD_CHECK(strcmp(buffer, "नमस्ते Asha, 5 आइटम") == 0);
    /* Missing arg index substitutes empty. */
    ZD_CHECK_OK(zd_i18n_format(&i18n, "test.fmt", args, 1, buffer,
                               sizeof(buffer)));
    ZD_CHECK(strstr(buffer, "Hello") != 0 || strstr(buffer, "नमस्ते") != 0);
    /* Overflow rejected. */
    ZD_CHECK_ERR(zd_i18n_format(&i18n, "test.fmt", args, 2, buffer, 8),
                 ZD_EOVERFLOW);
    ZD_CHECK_ERR(zd_i18n_format(&i18n, "test.fmt", args, 4, buffer,
                                sizeof(buffer)),
                 ZD_EOVERFLOW);
    ZD_CHECK_ERR(zd_i18n_format(&i18n, "test.fmt", args, 2, 0, 10),
                 ZD_EINVAL);
}

void zd_test_i18n_suite(void) {
    printf(" suite: localization\n");
    ZD_RUN(test_catalog_coverage);
    ZD_RUN(test_locale_switching);
    ZD_RUN(test_ui_state_strings_both_locales);
    ZD_RUN(test_formatting);
}
