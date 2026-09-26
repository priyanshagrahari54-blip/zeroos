#ifndef ZEROOS_DESKTOP_H
#define ZEROOS_DESKTOP_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_DESKTOP_MAX_SERVICES 16U
#define ZEROOS_DESKTOP_MAX_NOTIFICATIONS 64U
#define ZEROOS_DESKTOP_MAX_NAME 32U

enum zeroos_desktop_service_state {
    ZEROOS_DESKTOP_SERVICE_STOPPED = 0,
    ZEROOS_DESKTOP_SERVICE_DORMANT,
    ZEROOS_DESKTOP_SERVICE_WARM,
    ZEROOS_DESKTOP_SERVICE_ACTIVE,
    ZEROOS_DESKTOP_SERVICE_THROTTLED,
    ZEROOS_DESKTOP_SERVICE_SUSPENDED,
    ZEROOS_DESKTOP_SERVICE_FAILED
};

enum zeroos_desktop_service_type {
    ZEROOS_DESKTOP_SERVICE_GRAPHICS = 0,
    ZEROOS_DESKTOP_SERVICE_COMPOSITOR,
    ZEROOS_DESKTOP_SERVICE_WINDOW_MANAGER,
    ZEROOS_DESKTOP_SERVICE_SHELL,
    ZEROOS_DESKTOP_SERVICE_SEARCH,
    ZEROOS_DESKTOP_SERVICE_SETTINGS,
    ZEROOS_DESKTOP_SERVICE_NOTIFICATIONS,
    ZEROOS_DESKTOP_SERVICE_DOCS
};

struct zeroos_desktop_service {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_desktop_service_state state;
    enum zeroos_desktop_service_type type;
    char name[ZEROOS_DESKTOP_MAX_NAME];
    uint64_t task_id;
    uint32_t restart_count;
    uint32_t max_restarts;
    uint64_t last_restart_ticks;
    struct spinlock lock;
};

struct zeroos_notification {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    char title[64];
    char body[256];
    uint64_t timestamp;
    uint8_t read;
    uint32_t priority;
    struct spinlock lock;
};

int desktop_system_init(void);
int desktop_service_register(enum zeroos_desktop_service_type type, const char *name,
                             uint64_t task_id, uint32_t max_restarts, uint64_t *service_id_out);
int desktop_service_set_state(uint64_t service_id, enum zeroos_desktop_service_state state);
int desktop_service_restart(uint64_t service_id);
int desktop_service_lookup_by_type(enum zeroos_desktop_service_type type, uint64_t *service_id_out);
int desktop_notification_post(const char *title, const char *body, uint32_t priority, uint64_t *notif_id_out);
int desktop_notification_dismiss(uint64_t notif_id);
int desktop_debug_validate(void);

#endif
