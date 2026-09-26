#ifndef ZEROOS_SECURITY_H
#define ZEROOS_SECURITY_H

#include "types.h"
#include "sync.h"

#define ZEROOS_SECURITY_MAX_CAPS 64U
#define ZEROOS_SECURITY_MAX_SANDBOXES 16U
#define ZEROOS_SECURITY_MAX_AUDIT 256U

enum zeroos_security_state {
    ZEROOS_SECURITY_STOPPED = 0,
    ZEROOS_SECURITY_DORMANT,
    ZEROOS_SECURITY_WARM,
    ZEROOS_SECURITY_ACTIVE,
    ZEROOS_SECURITY_THROTTLED,
    ZEROOS_SECURITY_SUSPENDED
};

enum zeroos_capability {
    ZEROOS_CAP_FS_READ = 1,
    ZEROOS_CAP_FS_WRITE = 2,
    ZEROOS_CAP_NET_ACCESS = 4,
    ZEROOS_CAP_DEVICE_ACCESS = 8,
    ZEROOS_CAP_PROCESS_SPAWN = 16,
    ZEROOS_CAP_DISPLAY_ACCESS = 32,
    ZEROOS_CAP_AUDIO_ACCESS = 64,
    ZEROOS_CAP_INPUT_ACCESS = 128
};

struct zeroos_sandbox {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_security_state state;
    uint64_t owner_task_id;
    uint64_t capabilities;
    uint8_t isolated_fs;
    uint8_t isolated_net;
    uint8_t isolated_devices;
    struct spinlock lock;
};

struct zeroos_audit_entry {
    uint64_t timestamp;
    uint64_t task_id;
    uint32_t event_type;
    uint32_t result;
    char details[64];
    uint8_t valid;
};

int security_system_init(void);
int security_sandbox_create(uint64_t owner_task_id, uint64_t capabilities, uint64_t *sandbox_id_out);
int security_sandbox_check_capability(uint64_t sandbox_id, enum zeroos_capability cap);
int security_sandbox_destroy(uint64_t sandbox_id);
int security_audit_log(uint64_t task_id, uint32_t event_type, uint32_t result, const char *details);
int security_debug_validate(void);

#endif
