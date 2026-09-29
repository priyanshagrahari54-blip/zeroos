#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

/* Declared locally: the desktop core is freestanding-clean and these tests
 * compile both hosted and freestanding, so no <stdint.h> limit macros are
 * assumed to be exposed. */
#define TEST_INT64_MAX 9223372036854775807LL
#define TEST_INT64_MIN (-TEST_INT64_MAX - 1LL)

static int change_events;
static char last_changed_key[48];
static uint32_t last_restart_flag;

static void on_change(void *context, const char *key, int64_t old_value,
                      int64_t new_value, uint32_t requires_restart) {
    (void)context;
    (void)old_value;
    (void)new_value;
    ++change_events;
    snprintf(last_changed_key, sizeof(last_changed_key), "%s", key);
    last_restart_flag = requires_restart;
}

static void register_common(struct zd_settings *settings) {
    struct zd_setting_def def;
    zd_settings_init(settings);
    ZD_CHECK_OK(zd_settings_add_listener(settings, on_change, 0));

    memset(&def, 0, sizeof(def));
    def.key = "ui.scale_percent";
    def.type = ZD_SETTING_INT;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 100;
    def.min_value = 100;
    def.max_value = 300;
    def.group = "display";
    def.description = "interface scale percentage";
    ZD_CHECK_OK(zd_settings_register(settings, &def));

    memset(&def, 0, sizeof(def));
    def.key = "ui.reduced_motion";
    def.type = ZD_SETTING_BOOL;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 0;
    def.group = "accessibility";
    ZD_CHECK_OK(zd_settings_register(settings, &def));

    memset(&def, 0, sizeof(def));
    def.key = "system.hostname";
    def.type = ZD_SETTING_STRING;
    def.scope = ZD_SCOPE_SYSTEM;
    def.default_string = "zeroos";
    def.permissions_required = 0;
    ZD_CHECK_OK(zd_settings_register(settings, &def));

    memset(&def, 0, sizeof(def));
    def.key = "theme.accent";
    def.type = ZD_SETTING_ENUM;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 1;
    def.enum_count = 3;
    def.enum_values[0] = 1;
    def.enum_values[1] = 2;
    def.enum_values[2] = 3;
    ZD_CHECK_OK(zd_settings_register(settings, &def));

    /* Night light depends on reduced_motion being off: dependency demo. */
    memset(&def, 0, sizeof(def));
    def.key = "display.night_light";
    def.type = ZD_SETTING_BOOL;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 0;
    def.dep_count = 1;
    strcpy(def.deps[0].key, "ui.reduced_motion");
    def.deps[0].required_value = 0;
    ZD_CHECK_OK(zd_settings_register(settings, &def));
}

static void test_defaults_and_reset(void) {
    struct zd_settings settings;
    int64_t value = -1;
    char text[48];
    uint32_t changed = 0;
    register_common(&settings);

    ZD_CHECK_OK(zd_settings_get(&settings, "ui.scale_percent", &value, 0, 0));
    ZD_CHECK_EQ(value, 100);
    ZD_CHECK_OK(zd_settings_get(&settings, "system.hostname", 0, text,
                                sizeof(text)));
    ZD_CHECK(strcmp(text, "zeroos") == 0);
    ZD_CHECK_ERR(zd_settings_get(&settings, "nope", &value, 0, 0), ZD_ENOENT);

    ZD_CHECK_OK(zd_settings_set_number(&settings, "ui.scale_percent", 150,
                                       ZD_PERM_SETTINGS_USER, &changed));
    ZD_CHECK_EQ(changed, 1U);
    ZD_CHECK_OK(zd_settings_get(&settings, "ui.scale_percent", &value, 0, 0));
    ZD_CHECK_EQ(value, 150);
    /* Unchanged write reports changed=0 and skips the listener. */
    change_events = 0;
    ZD_CHECK_OK(zd_settings_set_number(&settings, "ui.scale_percent", 150,
                                       ZD_PERM_SETTINGS_USER, &changed));
    ZD_CHECK_EQ(changed, 0U);
    ZD_CHECK_EQ(change_events, 0);

    ZD_CHECK_OK(zd_settings_reset(&settings, "ui.scale_percent",
                                  ZD_PERM_SETTINGS_USER));
    ZD_CHECK_OK(zd_settings_get(&settings, "ui.scale_percent", &value, 0, 0));
    ZD_CHECK_EQ(value, 100);
    ZD_CHECK(settings.stats.resets >= 1U);
}

static void test_permissions_and_scope(void) {
    struct zd_settings settings;
    uint32_t changed = 0;
    register_common(&settings);
    change_events = 0;

    /* System scope requires ADMIN. */
    ZD_CHECK_ERR(zd_settings_set(&settings, "system.hostname", 0, "other",
                                 ZD_PERM_SETTINGS_USER, &changed),
                 ZD_EPERM);
    ZD_CHECK_ERR(zd_settings_set(&settings, "system.hostname", 0, "other",
                                 ZD_PERM_NONE, &changed),
                 ZD_EPERM);
    ZD_CHECK_OK(zd_settings_set(&settings, "system.hostname", 0, "other",
                                ZD_PERM_ADMIN, &changed));
    ZD_CHECK_EQ(change_events, 1);
    ZD_CHECK(settings.stats.denied >= 2U);

    /* Type/range violations. */
    ZD_CHECK_ERR(zd_settings_set_number(&settings, "ui.scale_percent", 999,
                                        ZD_PERM_SETTINGS_USER, &changed),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_set_number(&settings, "ui.scale_percent", 99,
                                        ZD_PERM_SETTINGS_USER, &changed),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_set_number(&settings, "ui.reduced_motion", 7,
                                        ZD_PERM_SETTINGS_USER, &changed),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_set_number(&settings, "theme.accent", 9,
                                        ZD_PERM_SETTINGS_USER, &changed),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_set(&settings, "ui.scale_percent", 0, "text",
                                 ZD_PERM_SETTINGS_USER, &changed),
                 ZD_EINVAL); /* string value on int key */
}

static void test_dependencies(void) {
    struct zd_settings settings;
    uint32_t changed = 0;
    register_common(&settings);

    /* Dependency satisfied by default (reduced_motion == 0). */
    ZD_CHECK(zd_settings_dependencies_met(&settings, "display.night_light"));
    ZD_CHECK_OK(zd_settings_set_number(&settings, "display.night_light", 1,
                                       ZD_PERM_SETTINGS_USER, &changed));
    /* Flip the dependency: further night_light writes blocked. */
    ZD_CHECK_OK(zd_settings_set_number(&settings, "ui.reduced_motion", 1,
                                       ZD_PERM_SETTINGS_USER, &changed));
    ZD_CHECK(!zd_settings_dependencies_met(&settings, "display.night_light"));
    ZD_CHECK_ERR(zd_settings_set_number(&settings, "display.night_light", 0,
                                        ZD_PERM_SETTINGS_USER, &changed),
                 ZD_ESTATE);
    /* Restore dependency: writes allowed again. */
    ZD_CHECK_OK(zd_settings_set_number(&settings, "ui.reduced_motion", 0,
                                       ZD_PERM_SETTINGS_USER, &changed));
    ZD_CHECK_OK(zd_settings_set_number(&settings, "display.night_light", 0,
                                       ZD_PERM_SETTINGS_USER, &changed));
}

static void test_search_provider(void) {
    struct zd_settings settings;
    const char *keys[8];
    uint32_t count;
    register_common(&settings);
    count = zd_settings_search(&settings, "scale", keys, 8);
    ZD_CHECK_EQ(count, 1U);
    ZD_CHECK(strcmp(keys[0], "ui.scale_percent") == 0);
    count = zd_settings_search(&settings, "accessibility", keys, 8);
    ZD_CHECK(count >= 1U);
    count = zd_settings_search(&settings, "zzz-no-match", keys, 8);
    ZD_CHECK_EQ(count, 0U);
}

static void test_export_import_roundtrip(void) {
    struct zd_settings settings;
    struct zd_settings restored;
    char blob[2048];
    uint32_t length = 0;
    uint32_t version = 0;
    int64_t value = 0;
    char text[48];

    register_common(&settings);
    ZD_CHECK_OK(zd_settings_set_number(&settings, "ui.scale_percent", 175,
                                       ZD_PERM_SETTINGS_USER, 0));
    ZD_CHECK_OK(zd_settings_set(&settings, "system.hostname", 0, "atlas",
                                ZD_PERM_ADMIN, 0));
    ZD_CHECK_OK(zd_settings_export(&settings, blob, sizeof(blob), &length,
                                   &version));
    ZD_CHECK_EQ(version, ZD_SETTINGS_CURRENT_VERSION);
    ZD_CHECK(length > 0);

    register_common(&restored);
    ZD_CHECK_OK(zd_settings_import(&restored, blob, ZD_PERM_ADMIN));
    ZD_CHECK_OK(zd_settings_get(&restored, "ui.scale_percent", &value, 0, 0));
    ZD_CHECK_EQ(value, 175);
    ZD_CHECK_OK(zd_settings_get(&restored, "system.hostname", 0, text,
                                sizeof(text)));
    ZD_CHECK(strcmp(text, "atlas") == 0);
    /* Session-scope keys are never exported: register one and check. */
}

static void test_import_negatives(void) {
    struct zd_settings settings;
    int64_t value = -1;
    register_common(&settings);
    ZD_CHECK_OK(zd_settings_set_number(&settings, "ui.scale_percent", 150,
                                       ZD_PERM_SETTINGS_USER, 0));

    /* Malformed blobs fail atomically: live store untouched. */
    ZD_CHECK_ERR(zd_settings_import(&settings, "v2\nui.scale_percent=abc\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_import(&settings, "v2\nnoline-terminator",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_import(&settings, "v9\n", ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_import(&settings, "garbage", ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_import(&settings, "v2\n=orphan\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_import(0, "v2\n", 0), ZD_EINVAL);
    ZD_CHECK(settings.stats.import_failures >= 4U);
    ZD_CHECK_OK(zd_settings_get(&settings, "ui.scale_percent", &value, 0, 0));
    ZD_CHECK_EQ(value, 150); /* unchanged after failed imports */
}

static void test_v1_migration(void) {
    struct zd_settings settings;
    int64_t value = 0;
    register_common(&settings);
    /* v1 blob stores ui.scale 1..4; import migrates to ui.scale_percent. */
    ZD_CHECK_OK(zd_settings_import(&settings, "v1\nui.scale=2\n",
                                   ZD_PERM_SETTINGS_USER));
    ZD_CHECK_OK(zd_settings_get(&settings, "ui.scale_percent", &value, 0, 0));
    ZD_CHECK_EQ(value, 200);
    ZD_CHECK(settings.stats.migrated_keys >= 1U);
    /* Out-of-range v1 scale rejected atomically. */
    ZD_CHECK_ERR(zd_settings_import(&settings, "v1\nui.scale=9\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_OK(zd_settings_get(&settings, "ui.scale_percent", &value, 0, 0));
    ZD_CHECK_EQ(value, 200);
    /* Unknown keys dropped and counted, not fatal. */
    ZD_CHECK_OK(zd_settings_import(&settings,
                                   "v2\nunknown.key=1\nui.scale_percent=125\n",
                                   ZD_PERM_SETTINGS_USER));
    ZD_CHECK(settings.stats.unknown_keys_dropped >= 1U);
    ZD_CHECK_OK(zd_settings_get(&settings, "ui.scale_percent", &value, 0, 0));
    ZD_CHECK_EQ(value, 125);
}

/* The blob parser must reject integer text it cannot represent, and it must
 * do so *before* the accumulator wraps.  The old form multiplied first and
 * compared afterwards: "36893488147419103240" accumulated to the (legal)
 * 3689348814741910324, wrapped to 8 on the next multiply, and was then
 * happily accepted as 8.  A settings import that silently stores 8 for a
 * number the user wrote as 3.6e19 is worse than a rejected import, because
 * the caller treats a rejection as a transactional no-op.
 *
 * The wide key exists so the *parser* decides these cases: its declared
 * range covers the whole of int64, so range validation cannot mask a
 * parser defect (or vice versa). */
static void test_import_int64_bounds(void) {
    struct zd_settings settings;
    struct zd_setting_def def;
    int64_t value = 0;
    uint32_t failures_before;

    register_common(&settings);
    memset(&def, 0, sizeof(def));
    def.key = "test.wide_int";
    def.type = ZD_SETTING_INT;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 0;
    def.min_value = TEST_INT64_MIN;
    def.max_value = TEST_INT64_MAX;
    ZD_CHECK_OK(zd_settings_register(&settings, &def));

    /* Exact positive bound: accepted, stored verbatim. */
    ZD_CHECK_OK(zd_settings_import(&settings,
                                   "v2\ntest.wide_int=9223372036854775807\n",
                                   ZD_PERM_SETTINGS_USER));
    ZD_CHECK_OK(zd_settings_get(&settings, "test.wide_int", &value, 0, 0));
    ZD_CHECK_EQ(value, TEST_INT64_MAX);

    /* One past the positive bound: rejected, value untouched. */
    failures_before = settings.stats.import_failures;
    ZD_CHECK_ERR(zd_settings_import(&settings,
                                    "v2\ntest.wide_int=9223372036854775808\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK(settings.stats.import_failures == failures_before + 1U);
    ZD_CHECK_OK(zd_settings_get(&settings, "test.wide_int", &value, 0, 0));
    ZD_CHECK_EQ(value, TEST_INT64_MAX);

    /* Exact negative bound (one further than the positive). */
    ZD_CHECK_OK(zd_settings_import(&settings,
                                   "v2\ntest.wide_int=-9223372036854775808\n",
                                   ZD_PERM_SETTINGS_USER));
    ZD_CHECK_OK(zd_settings_get(&settings, "test.wide_int", &value, 0, 0));
    ZD_CHECK_EQ(value, TEST_INT64_MIN);

    /* One past the negative bound: rejected. */
    ZD_CHECK_ERR(zd_settings_import(&settings,
                                    "v2\ntest.wide_int=-9223372036854775809\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_OK(zd_settings_get(&settings, "test.wide_int", &value, 0, 0));
    ZD_CHECK_EQ(value, TEST_INT64_MIN);

    /* Regression: the wrap case. 20 digits whose first 19 are in range. */
    ZD_CHECK_OK(zd_settings_import(&settings,
                                   "v2\ntest.wide_int=3689348814741910324\n",
                                   ZD_PERM_SETTINGS_USER));
    ZD_CHECK_OK(zd_settings_get(&settings, "test.wide_int", &value, 0, 0));
    ZD_CHECK_EQ(value, 3689348814741910324LL);
    ZD_CHECK_ERR(zd_settings_import(&settings,
                                    "v2\ntest.wide_int=36893488147419103240\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_OK(zd_settings_get(&settings, "test.wide_int", &value, 0, 0));
    ZD_CHECK_EQ(value, 3689348814741910324LL);

    /* Regression: uint64 wrap that lands back on a small positive value. */
    ZD_CHECK_ERR(zd_settings_import(&settings,
                                    "v2\ntest.wide_int=18446744073709551617\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_OK(zd_settings_get(&settings, "test.wide_int", &value, 0, 0));
    ZD_CHECK_EQ(value, 3689348814741910324LL);

    /* Leading zeros are not digits of magnitude: the bound still applies to
     * the value, not to the text length. */
    ZD_CHECK_OK(zd_settings_import(
        &settings, "v2\ntest.wide_int=0000009223372036854775807\n",
        ZD_PERM_SETTINGS_USER));
    ZD_CHECK_OK(zd_settings_get(&settings, "test.wide_int", &value, 0, 0));
    ZD_CHECK_EQ(value, TEST_INT64_MAX);
    ZD_CHECK_ERR(zd_settings_import(&settings,
                                    "v2\ntest.wide_int=0000009223372036854775808\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);

    /* Malformed numeric text on a known key fails the whole import. */
    ZD_CHECK_ERR(zd_settings_import(&settings, "v2\ntest.wide_int=12a\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_import(&settings, "v2\ntest.wide_int=-\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_import(&settings, "v2\ntest.wide_int=\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_ERR(zd_settings_import(&settings, "v2\ntest.wide_int=1.5\n",
                                    ZD_PERM_SETTINGS_USER),
                 ZD_EINVAL);
    ZD_CHECK_OK(zd_settings_get(&settings, "test.wide_int", &value, 0, 0));
    ZD_CHECK_EQ(value, TEST_INT64_MAX);

    /* The wrap also matters for a key whose range would have rejected the
     * wrapped value anyway: the import must be *reported* as failed, not
     * silently drop the key. */
    failures_before = settings.stats.import_failures;
    ZD_CHECK_ERR(
        zd_settings_import(&settings, "v2\nui.scale_percent=36893488147419103240\n",
                           ZD_PERM_SETTINGS_USER),
        ZD_EINVAL);
    ZD_CHECK(settings.stats.import_failures == failures_before + 1U);
    ZD_CHECK_OK(zd_settings_get(&settings, "ui.scale_percent", &value, 0, 0));
    ZD_CHECK_EQ(value, 100); /* default survives the rejected import */
}

static void test_string_default_is_store_owned(void) {
    struct zd_settings settings;
    struct zd_setting_def def;
    char default_text[ZD_SETTINGS_STRING_CAP];
    char out[ZD_SETTINGS_STRING_CAP];

    zd_settings_init(&settings);
    snprintf(default_text, sizeof(default_text), "original");
    memset(&def, 0, sizeof(def));
    def.key = "string.default";
    def.type = ZD_SETTING_STRING;
    def.scope = ZD_SCOPE_USER;
    def.default_string = default_text;
    ZD_CHECK_OK(zd_settings_register(&settings, &def));

    /* The schema may be stack-built. Mutating the caller's buffer after
     * registration must not change what reset() considers the default. */
    snprintf(default_text, sizeof(default_text), "mutated");
    ZD_CHECK_OK(zd_settings_set(&settings, "string.default", 0, "custom",
                                0, 0));
    ZD_CHECK_OK(zd_settings_reset(&settings, "string.default", 0));
    ZD_CHECK_OK(zd_settings_get(&settings, "string.default", 0, out,
                                sizeof(out)));
    ZD_CHECK(strcmp(out, "original") == 0);
    ZD_CHECK(strcmp(settings.defs[0].default_string, "original") == 0);
}

static void test_two_stores_keep_their_own_keys(void) {
    struct zd_settings first;
    struct zd_settings second;
    struct zd_setting_def def;
    int64_t value = 0;

    /* Two stores alive at once. Registering in the second must not
     * rename what the first is holding, so the key storage has to
     * belong to a store rather than to the module: one array indexed by
     * slot is shared by every store in the process. */
    zd_settings_init(&first);
    zd_settings_init(&second);

    memset(&def, 0, sizeof(def));
    def.key = "store.one";
    def.type = ZD_SETTING_INT;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 1;
    def.min_value = 0;
    def.max_value = 10;
    ZD_CHECK_OK(zd_settings_register(&first, &def));

    memset(&def, 0, sizeof(def));
    def.key = "store.two";
    def.type = ZD_SETTING_INT;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 2;
    def.min_value = 0;
    def.max_value = 10;
    ZD_CHECK_OK(zd_settings_register(&second, &def));

    /* Each store still answers for its own key, by name. */
    ZD_CHECK_OK(zd_settings_get(&first, "store.one", &value, 0, 0));
    ZD_CHECK_EQ(value, 1);
    ZD_CHECK_OK(zd_settings_get(&second, "store.two", &value, 0, 0));
    ZD_CHECK_EQ(value, 2);
    ZD_CHECK(zd_settings_def(&first, "store.one") != 0);
    ZD_CHECK(zd_settings_def(&second, "store.two") != 0);
    /* And neither store claims the other's key. */
    ZD_CHECK(zd_settings_def(&first, "store.two") == 0);
    ZD_CHECK(zd_settings_def(&second, "store.one") == 0);
    /* A write through the first store lands on the first store's key. */
    ZD_CHECK_OK(zd_settings_set_number(&first, "store.one", 7, 0, 0));
    ZD_CHECK_OK(zd_settings_get(&first, "store.one", &value, 0, 0));
    ZD_CHECK_EQ(value, 7);
    ZD_CHECK_OK(zd_settings_get(&second, "store.two", &value, 0, 0));
    ZD_CHECK_EQ(value, 2);
}

static void test_register_negatives(void) {
    struct zd_settings settings;
    struct zd_setting_def def;
    zd_settings_init(&settings);
    memset(&def, 0, sizeof(def));
    def.key = "";
    ZD_CHECK_ERR(zd_settings_register(&settings, &def), ZD_EINVAL);
    def.key = "x";
    ZD_CHECK_OK(zd_settings_register(&settings, &def));
    ZD_CHECK_ERR(zd_settings_register(&settings, &def), ZD_EBUSY);
    ZD_CHECK(zd_settings_def(&settings, "missing") == 0);
    ZD_CHECK(zd_settings_def(&settings, "x") != 0);

    /* The store owns exact keys. A key that has to be truncated cannot
     * be looked up or exported under the name the caller registered, so
     * it is refused rather than accepted under a different name. */
    {
        char long_key[ZD_SETTINGS_KEY_CAP + 4];
        uint32_t i;
        for (i = 0; i + 1 < sizeof(long_key); ++i)
            long_key[i] = 'k';
        long_key[sizeof(long_key) - 1] = 0;
        def.key = long_key;
        ZD_CHECK_ERR(zd_settings_register(&settings, &def), ZD_EOVERFLOW);
    }
    memset(&def, 0, sizeof(def));
    def.key = "long.default";
    def.type = ZD_SETTING_STRING;
    def.scope = ZD_SCOPE_USER;
    {
        char long_default[ZD_SETTINGS_STRING_CAP + 1];
        uint32_t i;
        for (i = 0; i + 1 < sizeof(long_default); ++i)
            long_default[i] = 'd';
        long_default[sizeof(long_default) - 1] = 0;
        def.default_string = long_default;
        ZD_CHECK_ERR(zd_settings_register(&settings, &def), ZD_EOVERFLOW);
    }

    memset(&def, 0, sizeof(def));
    def.key = "bad.bool.default";
    def.type = ZD_SETTING_BOOL;
    def.scope = ZD_SCOPE_USER;
    def.default_value = 2;
    ZD_CHECK_ERR(zd_settings_register(&settings, &def), ZD_EINVAL);

    memset(&def, 0, sizeof(def));
    def.key = "bad.int.default";
    def.type = ZD_SETTING_INT;
    def.scope = ZD_SCOPE_USER;
    def.min_value = 10;
    def.max_value = 20;
    def.default_value = 9;
    ZD_CHECK_ERR(zd_settings_register(&settings, &def), ZD_EINVAL);

    memset(&def, 0, sizeof(def));
    def.key = "bad.enum.default";
    def.type = ZD_SETTING_ENUM;
    def.scope = ZD_SCOPE_USER;
    def.enum_count = 2;
    def.enum_values[0] = 1;
    def.enum_values[1] = 2;
    def.default_value = 3;
    ZD_CHECK_ERR(zd_settings_register(&settings, &def), ZD_EINVAL);
}

void zd_test_settings_suite(void) {
    printf(" suite: settings\n");
    ZD_RUN(test_defaults_and_reset);
    ZD_RUN(test_permissions_and_scope);
    ZD_RUN(test_dependencies);
    ZD_RUN(test_search_provider);
    ZD_RUN(test_export_import_roundtrip);
    ZD_RUN(test_import_negatives);
    ZD_RUN(test_v1_migration);
    ZD_RUN(test_import_int64_bounds);
    ZD_RUN(test_register_negatives);
    ZD_RUN(test_two_stores_keep_their_own_keys);
    ZD_RUN(test_string_default_is_store_owned);
}
