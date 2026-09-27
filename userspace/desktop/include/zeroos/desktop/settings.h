#ifndef ZEROOS_DESKTOP_SETTINGS_H
#define ZEROOS_DESKTOP_SETTINGS_H

/* Schema-driven settings: value, scope, dependencies, permission, default,
 * reset, persistence and migration. Persistence and permission checks are
 * injected so the core stays freestanding and testable; the store itself
 * never performs I/O. Imports are transactional: a malformed blob changes
 * nothing. */

#include <zeroos/desktop/common.h>

#define ZD_SETTINGS_MAX_KEYS 128
#define ZD_SETTINGS_KEY_CAP 48
#define ZD_SETTINGS_STRING_CAP 48
#define ZD_SETTINGS_MAX_ENUM 8
#define ZD_SETTINGS_MAX_DEPS 2
#define ZD_SETTINGS_MAX_LISTENERS 8
#define ZD_SETTINGS_BLOB_CAP 4096
#define ZD_SETTINGS_CURRENT_VERSION 2U

enum zd_setting_type {
    ZD_SETTING_BOOL = 0,
    ZD_SETTING_INT = 1,
    ZD_SETTING_ENUM = 2,
    ZD_SETTING_STRING = 3
};

enum zd_setting_scope {
    ZD_SCOPE_SYSTEM = 0,
    ZD_SCOPE_USER = 1,
    ZD_SCOPE_SESSION = 2
};

/* Capability bits supplied by the session security context. */
#define ZD_PERM_NONE 0U
#define ZD_PERM_ADMIN (1U << 0)
#define ZD_PERM_SETTINGS_USER (1U << 1)

#define ZD_SETTING_FLAG_RESTART (1U << 0)

struct zd_setting_dep {
    char key[ZD_SETTINGS_KEY_CAP];
    int64_t required_value; /* dependency must equal this value */
};

struct zd_setting_def {
    const char *key;
    enum zd_setting_type type;
    enum zd_setting_scope scope;
    uint32_t permissions_required; /* actor must hold ALL listed bits */
    uint32_t flags;
    int64_t default_value;
    int64_t min_value;             /* INT */
    int64_t max_value;             /* INT */
    int64_t enum_values[ZD_SETTINGS_MAX_ENUM]; /* ENUM */
    uint32_t enum_count;
    const char *default_string;     /* STRING */
    struct zd_setting_dep deps[ZD_SETTINGS_MAX_DEPS];
    uint32_t dep_count;
    const char *group;              /* UI grouping hint */
    const char *description;
};

struct zd_setting_value {
    int64_t number;                 /* BOOL/INT/ENUM */
    char text[ZD_SETTINGS_STRING_CAP]; /* STRING */
    uint32_t overridden;            /* 1 when value != default */
};

typedef void (*zd_settings_change_fn)(void *context, const char *key,
                                      int64_t old_value, int64_t new_value,
                                      uint32_t requires_restart);

struct zd_settings {
    struct zd_setting_def defs[ZD_SETTINGS_MAX_KEYS];
    struct zd_setting_value values[ZD_SETTINGS_MAX_KEYS];
    uint32_t count;
    uint32_t listener_count;
    struct {
        zd_settings_change_fn callback;
        void *context;
    } listeners[ZD_SETTINGS_MAX_LISTENERS];
    struct {
        uint64_t reads;
        uint64_t writes;
        uint64_t denied;
        uint64_t resets;
        uint64_t unchanged;
        uint64_t import_failures;
        uint64_t migrated_keys;
        uint64_t unknown_keys_dropped;
    } stats;
};

void zd_settings_init(struct zd_settings *settings);
int zd_settings_register(struct zd_settings *settings,
                         const struct zd_setting_def *def);
const struct zd_setting_def *zd_settings_def(const struct zd_settings *settings,
                                             const char *key);
int zd_settings_add_listener(struct zd_settings *settings,
                             zd_settings_change_fn callback, void *context);
/* Returns 0 on success; -ZD_EPERM capability/scope denial; -ZD_EINVAL type
 * or range violation; -ZD_ESTATE unmet dependency; -ZD_ENOENT unknown key.
 * *out_changed reports whether the stored value actually changed. */
int zd_settings_set(struct zd_settings *settings, const char *key,
                    int64_t value, const char *string_value,
                    uint32_t actor_permissions, uint32_t *out_changed);
int zd_settings_set_number(struct zd_settings *settings, const char *key,
                           int64_t value, uint32_t actor_permissions,
                           uint32_t *out_changed);
int zd_settings_get(const struct zd_settings *settings, const char *key,
                    int64_t *out_value, char *out_string,
                    uint32_t out_string_capacity);
int zd_settings_reset(struct zd_settings *settings, const char *key,
                      uint32_t actor_permissions);
/* Settings search provider support: fills up to capacity matching keys. */
uint32_t zd_settings_search(const struct zd_settings *settings,
                            const char *query,
                            const char **out_keys, uint32_t capacity);
/* Dependencies satisfied for the current stored values. */
int zd_settings_dependencies_met(const struct zd_settings *settings,
                                 const char *key);
/* Persistence: versioned blob export/import. Import parses into a staging
 * area first; any malformed line fails the whole import (-ZD_EINVAL) and
 * leaves the live store untouched. v1 blobs are migrated (ui.scale 1..4 ->
 * ui.scale_percent 100..300) before commit. Unknown keys are dropped and
 * counted. */
int zd_settings_export(const struct zd_settings *settings, char *blob,
                       uint32_t capacity, uint32_t *out_length,
                       uint32_t *out_version);
int zd_settings_import(struct zd_settings *settings, const char *blob,
                       uint32_t actor_permissions);

#endif
