#include <zeroos/desktop/settings.h>

/* Schema store --------------------------------------------------------- */

void zd_settings_init(struct zd_settings *settings) {
    if (!settings)
        return;
    zd_memset(settings, 0, sizeof(*settings));
}

static int settings_find(const struct zd_settings *settings, const char *key) {
    uint32_t index;
    if (!settings || !key)
        return -1;
    for (index = 0; index < settings->count; ++index)
        if (zd_str_equal(settings->defs[index].key, key))
            return (int)index;
    return -1;
}

int zd_settings_register(struct zd_settings *settings,
                         const struct zd_setting_def *def) {
    struct zd_setting_def *slot;
    if (!settings || !def || !def->key || !*def->key)
        return -ZD_EINVAL;
    if (settings->count >= ZD_SETTINGS_MAX_KEYS)
        return -ZD_ENOSPC;
    if (settings_find(settings, def->key) >= 0)
        return -ZD_EBUSY;
    if ((int)def->type < ZD_SETTING_BOOL || (int)def->type > ZD_SETTING_STRING)
        return -ZD_EINVAL;
    if (def->type == ZD_SETTING_INT && def->min_value > def->max_value)
        return -ZD_EINVAL;
    if (def->type == ZD_SETTING_ENUM &&
        (def->enum_count == 0 || def->enum_count > ZD_SETTINGS_MAX_ENUM))
        return -ZD_EINVAL;
    if (def->type == ZD_SETTING_STRING && !def->default_string)
        return -ZD_EINVAL;
    if (def->dep_count > ZD_SETTINGS_MAX_DEPS)
        return -ZD_EINVAL;

    slot = &settings->defs[settings->count];
    *slot = *def;
    /* Normalize key ownership into the store (defs may be stack copies). */
    {
        static char key_storage[ZD_SETTINGS_MAX_KEYS][ZD_SETTINGS_KEY_CAP];
        zd_str_copy(key_storage[settings->count], ZD_SETTINGS_KEY_CAP, def->key);
        slot->key = key_storage[settings->count];
    }
    zd_memset(&settings->values[settings->count], 0,
              sizeof(settings->values[settings->count]));
    settings->values[settings->count].number = def->default_value;
    if (def->type == ZD_SETTING_STRING)
        zd_str_copy(settings->values[settings->count].text,
                    ZD_SETTINGS_STRING_CAP, def->default_string);
    settings->values[settings->count].overridden = 0;
    ++settings->count;
    return 0;
}

const struct zd_setting_def *zd_settings_def(const struct zd_settings *settings,
                                             const char *key) {
    int index = settings_find(settings, key);
    return index < 0 ? (const struct zd_setting_def *)0 :
           &settings->defs[index];
}

int zd_settings_add_listener(struct zd_settings *settings,
                             zd_settings_change_fn callback, void *context) {
    if (!settings || !callback)
        return -ZD_EINVAL;
    if (settings->listener_count >= ZD_SETTINGS_MAX_LISTENERS)
        return -ZD_ENOSPC;
    settings->listeners[settings->listener_count].callback = callback;
    settings->listeners[settings->listener_count].context = context;
    ++settings->listener_count;
    return 0;
}

int zd_settings_dependencies_met(const struct zd_settings *settings,
                                 const char *key) {
    const struct zd_setting_def *def = zd_settings_def(settings, key);
    uint32_t index;
    if (!def)
        return 0;
    for (index = 0; index < def->dep_count; ++index) {
        int dep_index = settings_find(settings, def->deps[index].key);
        if (dep_index < 0)
            return 0;
        if (settings->values[dep_index].number !=
            def->deps[index].required_value)
            return 0;
    }
    return 1;
}

static int validate_value(const struct zd_setting_def *def, int64_t value,
                          const char *string_value) {
    uint32_t index;
    switch (def->type) {
    case ZD_SETTING_BOOL:
        if (value != 0 && value != 1)
            return -ZD_EINVAL;
        return 0;
    case ZD_SETTING_INT:
        if (value < def->min_value || value > def->max_value)
            return -ZD_EINVAL;
        return 0;
    case ZD_SETTING_ENUM:
        for (index = 0; index < def->enum_count; ++index)
            if (def->enum_values[index] == value)
                return 0;
        return -ZD_EINVAL;
    case ZD_SETTING_STRING:
        if (!string_value)
            return -ZD_EINVAL;
        if (zd_str_length(string_value) >= ZD_SETTINGS_STRING_CAP)
            return -ZD_EOVERFLOW;
        return 0;
    default:
        return -ZD_EINVAL;
    }
}

static void notify_change(struct zd_settings *settings, const char *key,
                          int64_t old_value, int64_t new_value,
                          uint32_t requires_restart) {
    uint32_t index;
    for (index = 0; index < settings->listener_count; ++index)
        settings->listeners[index].callback(
            settings->listeners[index].context, key, old_value, new_value,
            requires_restart);
}

int zd_settings_set(struct zd_settings *settings, const char *key,
                    int64_t value, const char *string_value,
                    uint32_t actor_permissions, uint32_t *out_changed) {
    int index = settings_find(settings, key);
    const struct zd_setting_def *def;
    struct zd_setting_value *stored;
    int64_t old_number;
    char old_string[ZD_SETTINGS_STRING_CAP];
    int validation;

    if (out_changed)
        *out_changed = 0;
    if (!settings || !key)
        return -ZD_EINVAL;
    if (index < 0)
        return -ZD_ENOENT;
    def = &settings->defs[index];

    /* Scope/permission: system-scope keys require ALL declared bits (at
     * minimum ADMIN for SYSTEM scope). */
    if (def->scope == ZD_SCOPE_SYSTEM &&
        !(actor_permissions & ZD_PERM_ADMIN)) {
        ++settings->stats.denied;
        return -ZD_EPERM;
    }
    if (def->permissions_required &&
        (actor_permissions & def->permissions_required) !=
            def->permissions_required) {
        ++settings->stats.denied;
        return -ZD_EPERM;
    }

    validation = validate_value(def, value, string_value);
    if (validation != 0) {
        ++settings->stats.denied;
        return validation;
    }
    if (!zd_settings_dependencies_met(settings, key)) {
        ++settings->stats.denied;
        return -ZD_ESTATE;
    }

    stored = &settings->values[index];
    old_number = stored->number;
    zd_str_copy(old_string, sizeof(old_string), stored->text);

    if (def->type == ZD_SETTING_STRING) {
        if (zd_str_equal(stored->text, string_value)) {
            ++settings->stats.unchanged;
            if (out_changed)
                *out_changed = 0;
            return 0;
        }
        zd_str_copy(stored->text, sizeof(stored->text), string_value);
    } else {
        if (stored->number == value) {
            ++settings->stats.unchanged;
            if (out_changed)
                *out_changed = 0;
            return 0;
        }
        stored->number = value;
    }
    stored->overridden =
        (def->type == ZD_SETTING_STRING) ?
        !zd_str_equal(stored->text, def->default_string ?
                      def->default_string : "") :
        (value != def->default_value);
    ++settings->stats.writes;
    notify_change(settings, def->key, old_number,
                  def->type == ZD_SETTING_STRING ? 0 : value,
                  (def->flags & ZD_SETTING_FLAG_RESTART) ? 1U : 0U);
    (void)old_string;
    if (out_changed)
        *out_changed = 1;
    return 0;
}

int zd_settings_set_number(struct zd_settings *settings, const char *key,
                           int64_t value, uint32_t actor_permissions,
                           uint32_t *out_changed) {
    return zd_settings_set(settings, key, value, (const char *)0,
                           actor_permissions, out_changed);
}

int zd_settings_get(const struct zd_settings *settings, const char *key,
                    int64_t *out_value, char *out_string,
                    uint32_t out_string_capacity) {
    int index = settings_find(settings, key);
    if (!settings || !key)
        return -ZD_EINVAL;
    if (index < 0)
        return -ZD_ENOENT;
    ((struct zd_settings *)settings)->stats.reads++;
    if (out_value)
        *out_value = settings->values[index].number;
    if (out_string && out_string_capacity)
        zd_str_copy(out_string, out_string_capacity,
                    settings->values[index].text);
    return 0;
}

int zd_settings_reset(struct zd_settings *settings, const char *key,
                      uint32_t actor_permissions) {
    int index = settings_find(settings, key);
    const struct zd_setting_def *def;
    uint32_t changed = 0;
    int result;

    if (!settings || !key)
        return -ZD_EINVAL;
    if (index < 0)
        return -ZD_ENOENT;
    def = &settings->defs[index];
    ++settings->stats.resets;
    if (def->type == ZD_SETTING_STRING)
        result = zd_settings_set(settings, key, def->default_value,
                                 def->default_string, actor_permissions,
                                 &changed);
    else
        result = zd_settings_set_number(settings, key, def->default_value,
                                        actor_permissions, &changed);
    if (result == 0) {
        /* Reset always reports overridden=0 even when value equal. */
        settings->values[index].overridden = 0;
    }
    return result;
}

uint32_t zd_settings_search(const struct zd_settings *settings,
                            const char *query, const char **out_keys,
                            uint32_t capacity) {
    uint32_t index;
    uint32_t count = 0;
    if (!settings || !query || !out_keys || capacity == 0)
        return 0;
    for (index = 0; index < settings->count && count < capacity; ++index) {
        if (zd_str_contains_ci(settings->defs[index].key, query) ||
            (settings->defs[index].description &&
             zd_str_contains_ci(settings->defs[index].description, query)) ||
            (settings->defs[index].group &&
             zd_str_contains_ci(settings->defs[index].group, query)))
            out_keys[count++] = settings->defs[index].key;
    }
    return count;
}

/* Persistence ---------------------------------------------------------- */

static uint32_t setting_encoded_length(const struct zd_settings *settings,
                                       uint32_t index) {
    const struct zd_setting_def *def = &settings->defs[index];
    uint32_t length = (uint32_t)zd_str_length(def->key) + 1U;
    if (def->type == ZD_SETTING_STRING)
        length += zd_str_length(settings->values[index].text);
    else {
        /* worst-case int64 text */
        length += 21U;
    }
    return length + 1U; /* '=' + newline accounted loosely */
}

static int append_text(char *blob, uint32_t capacity, uint32_t *offset,
                       const char *text) {
    while (*text) {
        if (*offset + 1U >= capacity)
            return -ZD_EOVERFLOW;
        blob[(*offset)++] = *text++;
    }
    return 0;
}

static int append_number(char *blob, uint32_t capacity, uint32_t *offset,
                         int64_t value) {
    char digits[24];
    int position = 23;
    uint64_t magnitude;
    digits[position] = '\0';
    if (value < 0)
        magnitude = (uint64_t)(-(value + 1)) + 1ULL;
    else
        magnitude = (uint64_t)value;
    if (magnitude == 0)
        digits[--position] = '0';
    while (magnitude) {
        digits[--position] = (char)('0' + (magnitude % 10U));
        magnitude /= 10U;
    }
    if (value < 0)
        digits[--position] = '-';
    return append_text(blob, capacity, offset, &digits[position]);
}

int zd_settings_export(const struct zd_settings *settings, char *blob,
                       uint32_t capacity, uint32_t *out_length,
                       uint32_t *out_version) {
    uint32_t offset = 0;
    uint32_t index;
    char version_line[16];

    if (!settings || !blob || capacity < 8)
        return -ZD_EINVAL;
    zd_str_copy(version_line, sizeof(version_line), "v2\n");
    if (append_text(blob, capacity, &offset, version_line) != 0)
        return -ZD_EOVERFLOW;
    for (index = 0; index < settings->count; ++index) {
        const struct zd_setting_def *def = &settings->defs[index];
        if (def->scope == ZD_SCOPE_SESSION)
            continue; /* session scope is not persisted */
        if (offset + setting_encoded_length(settings, index) >= capacity)
            return -ZD_EOVERFLOW;
        if (append_text(blob, capacity, &offset, def->key) != 0 ||
            append_text(blob, capacity, &offset, "=") != 0)
            return -ZD_EOVERFLOW;
        if (def->type == ZD_SETTING_STRING) {
            if (append_text(blob, capacity, &offset,
                            settings->values[index].text) != 0)
                return -ZD_EOVERFLOW;
        } else {
            if (append_number(blob, capacity, &offset,
                              settings->values[index].number) != 0)
                return -ZD_EOVERFLOW;
        }
        if (append_text(blob, capacity, &offset, "\n") != 0)
            return -ZD_EOVERFLOW;
    }
    if (offset >= capacity)
        return -ZD_EOVERFLOW;
    blob[offset] = '\0';
    if (out_length)
        *out_length = offset;
    if (out_version)
        *out_version = ZD_SETTINGS_CURRENT_VERSION;
    return 0;
}

struct settings_import_staging {
    char keys[ZD_SETTINGS_MAX_KEYS][ZD_SETTINGS_KEY_CAP];
    int64_t values[ZD_SETTINGS_MAX_KEYS];
    char strings[ZD_SETTINGS_MAX_KEYS][ZD_SETTINGS_STRING_CAP];
    int32_t targets[ZD_SETTINGS_MAX_KEYS]; /* schema index at parse time */
    uint32_t count;
    uint32_t unknown_dropped;
};

static int parse_int64(const char *text, size_t length, int64_t *out) {
    size_t index = 0;
    int negative = 0;
    uint64_t magnitude = 0;
    if (length == 0)
        return -1;
    if (text[0] == '-') {
        negative = 1;
        index = 1;
        if (length == 1)
            return -1;
    }
    for (; index < length; ++index) {
        if (text[index] < '0' || text[index] > '9')
            return -1;
        magnitude = magnitude * 10U + (uint64_t)(text[index] - '0');
        if (magnitude > 9223372036854775807ULL)
            return -1;
    }
    *out = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return 0;
}

int zd_settings_import(struct zd_settings *settings, const char *blob,
                       uint32_t actor_permissions) {
    struct settings_import_staging staging;
    const char *cursor = blob;
    uint32_t version = 0;
    uint32_t index;

    if (!settings || !blob)
        return -ZD_EINVAL;
    /* Any parse/migration failure aborts the whole transactional import
     * and is counted for diagnostics; the live store is never touched. */
#define IMPORT_FAIL(code) do { ++settings->stats.import_failures; \
                               return -(code); } while (0)
    zd_memset(&staging, 0, sizeof(staging));

    /* Header: v1 or v2. */
    if (cursor[0] != 'v' || cursor[1] < '0' || cursor[1] > '9' ||
        cursor[2] != '\n')
        IMPORT_FAIL(ZD_EINVAL);
    version = (uint32_t)(cursor[1] - '0');
    if (version != 1 && version != 2)
        IMPORT_FAIL(ZD_EINVAL);
    cursor += 3;

    while (*cursor) {
        const char *line_end = cursor;
        const char *equals = (const char *)0;
        char key[ZD_SETTINGS_KEY_CAP];
        size_t key_length;
        while (*line_end && *line_end != '\n')
            ++line_end;
        if (*line_end != '\n')
            IMPORT_FAIL(ZD_EINVAL); /* every line must be newline-terminated */
        {
            const char *scan;
            for (scan = cursor; scan < line_end; ++scan)
                if (*scan == '=') {
                    equals = scan;
                    break;
                }
        }
        if (!equals || equals == cursor)
            IMPORT_FAIL(ZD_EINVAL);
        key_length = (size_t)(equals - cursor);
        if (key_length >= ZD_SETTINGS_KEY_CAP)
            IMPORT_FAIL(ZD_EINVAL);
        zd_memcpy(key, cursor, key_length);
        key[key_length] = '\0';

        if (staging.count >= ZD_SETTINGS_MAX_KEYS)
            IMPORT_FAIL(ZD_EOVERFLOW);
        zd_str_copy(staging.keys[staging.count], ZD_SETTINGS_KEY_CAP, key);
        {
            size_t value_length = (size_t)(line_end - equals - 1);
            int target = settings_find(settings, key);
            staging.targets[staging.count] = target;
            if (target >= 0 &&
                settings->defs[target].type == ZD_SETTING_STRING) {
                if (value_length >= ZD_SETTINGS_STRING_CAP)
                    IMPORT_FAIL(ZD_EINVAL);
                zd_memcpy(staging.strings[staging.count], equals + 1,
                          value_length);
                staging.strings[staging.count][value_length] = '\0';
                staging.values[staging.count] = 0;
            } else if (parse_int64(equals + 1, value_length,
                                   &staging.values[staging.count]) == 0) {
                staging.strings[staging.count][0] = '\0';
            } else if (target >= 0) {
                /* Known numeric key receiving junk: reject the import. */
                IMPORT_FAIL(ZD_EINVAL);
            } else {
                /* Unknown key with non-numeric value: stage the raw text
                 * so migration can still interpret it, drop at commit. */
                size_t copy_length =
                    value_length < ZD_SETTINGS_STRING_CAP - 1
                        ? value_length
                        : ZD_SETTINGS_STRING_CAP - 1;
                zd_memcpy(staging.strings[staging.count], equals + 1,
                          copy_length);
                staging.strings[staging.count][copy_length] = '\0';
                staging.values[staging.count] = 0;
            }
            ++staging.count;
        }
        cursor = line_end + 1;
    }

    /* v1 migration (staging only): ui.scale (1..4) -> ui.scale_percent
     * (100..300). Stats are applied at commit so a rejected blob leaves no
     * migration footprint. */
    if (version == 1) {
        uint32_t migrated = 0;
        for (index = 0; index < staging.count; ++index) {
            if (!zd_str_equal(staging.keys[index], "ui.scale"))
                continue;
            if (staging.values[index] < 1 || staging.values[index] > 4)
                IMPORT_FAIL(ZD_EINVAL);
            zd_str_copy(staging.keys[index], ZD_SETTINGS_KEY_CAP,
                        "ui.scale_percent");
            staging.values[index] *= 100;
            staging.targets[index] = settings_find(
                settings, staging.keys[index]);
            ++migrated;
        }
        settings->stats.migrated_keys += migrated;
    }

    /* Commit phase: only reached when the whole blob parsed. */
    for (index = 0; index < staging.count; ++index) {
        int target;
        uint32_t changed = 0;
        target = settings_find(settings, staging.keys[index]);
        if (target < 0) {
            ++staging.unknown_dropped;
            continue;
        }
        {
            const struct zd_setting_def *def = &settings->defs[target];
            if (def->type == ZD_SETTING_STRING)
                (void)zd_settings_set(settings, def->key, 0,
                                      staging.strings[index],
                                      actor_permissions, &changed);
            else
                (void)zd_settings_set_number(settings, def->key,
                                             staging.values[index],
                                             actor_permissions, &changed);
        }
    }
    settings->stats.unknown_keys_dropped += staging.unknown_dropped;
#undef IMPORT_FAIL
    return 0;
}
