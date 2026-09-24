#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

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
    ZD_RUN(test_register_negatives);
}
