#ifndef ZEROOS_ANDROID_H
#define ZEROOS_ANDROID_H

#include "types.h"
#include "sync.h"

#define ZEROOS_ANDROID_MAX_APPS 16U
#define ZEROOS_ANDROID_MAX_PACKAGES 32U

enum zeroos_android_state {
    ZEROOS_ANDROID_STOPPED = 0,
    ZEROOS_ANDROID_DORMANT,
    ZEROOS_ANDROID_WARM,
    ZEROOS_ANDROID_ACTIVE,
    ZEROOS_ANDROID_THROTTLED,
    ZEROOS_ANDROID_SUSPENDED
};

enum zeroos_android_app_state {
    ZEROOS_ANDROID_APP_INSTALLED = 0,
    ZEROOS_ANDROID_APP_RUNNING,
    ZEROOS_ANDROID_APP_PAUSED,
    ZEROOS_ANDROID_APP_STOPPED
};

struct zeroos_android_package {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    char package_name[128];
    char version[32];
    uint64_t size_bytes;
    uint64_t permissions;
    struct spinlock lock;
};

struct zeroos_android_app {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_android_state runtime_state;
    enum zeroos_android_app_state app_state;
    uint64_t package_id;
    uint64_t owner_task_id;
    uint8_t isolated;
    struct spinlock lock;
};

int android_system_init(void);
int android_package_install(const char *package_name, const char *version, uint64_t size, uint64_t perms, uint64_t *pkg_id_out);
int android_package_uninstall(uint64_t pkg_id);
int android_app_launch(uint64_t pkg_id, uint64_t owner_task_id, uint64_t *app_id_out);
int android_app_pause(uint64_t app_id);
int android_app_resume(uint64_t app_id);
int android_app_stop(uint64_t app_id);
int android_debug_validate(void);

#endif
