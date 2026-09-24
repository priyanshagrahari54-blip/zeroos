#ifndef ZEROOS_SETTINGS_H
#define ZEROOS_SETTINGS_H

#include "types.h"
#include "sync.h"

#define ZEROOS_SETTINGS_MAX_KEYS 128U
#define ZEROOS_SETTINGS_MAX_KEY_LEN 64U
#define ZEROOS_SETTINGS_MAX_VALUE_LEN 256U

enum zeroos_settings_state {
    ZEROOS_SETTINGS_STOPPED = 0,
    ZEROOS_SETTINGS_DORMANT,
    ZEROOS_SETTINGS_WARM,
    ZEROOS_SETTINGS_ACTIVE,
    ZEROOS_SETTINGS_THROTTLED,
    ZEROOS_SETTINGS_SUSPENDED
};

struct zeroos_settings_entry {
    uint8_t used;
    char key[ZEROOS_SETTINGS_MAX_KEY_LEN];
    char value[ZEROOS_SETTINGS_MAX_VALUE_LEN];
    uint64_t version;
    uint64_t last_modified;
};

struct zeroos_settings_store {
    struct spinlock lock;
    enum zeroos_settings_state state;
    struct zeroos_settings_entry entries[ZEROOS_SETTINGS_MAX_KEYS];
    uint32_t count;
    uint64_t global_version;
};

int settings_system_init(void);
int settings_set(const char *key, const char *value);
int settings_get(const char *key, char *value_out, uint64_t cap);
int settings_delete(const char *key);
int settings_set_state(enum zeroos_settings_state state);
int settings_debug_validate(void);

#endif
