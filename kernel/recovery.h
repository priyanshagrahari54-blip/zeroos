#ifndef ZEROOS_RECOVERY_H
#define ZEROOS_RECOVERY_H

#include "types.h"
#include "sync.h"

#define ZEROOS_RECOVERY_MAX_SNAPSHOTS 16U
#define ZEROOS_RECOVERY_MAX_UPDATES 8U

enum zeroos_recovery_state {
    ZEROOS_RECOVERY_STOPPED = 0,
    ZEROOS_RECOVERY_DORMANT,
    ZEROOS_RECOVERY_WARM,
    ZEROOS_RECOVERY_ACTIVE,
    ZEROOS_RECOVERY_THROTTLED,
    ZEROOS_RECOVERY_SUSPENDED
};

enum zeroos_snapshot_type {
    ZEROOS_SNAPSHOT_FULL = 0,
    ZEROOS_SNAPSHOT_INCREMENTAL,
    ZEROOS_SNAPSHOT_TRANSACTIONAL
};

struct zeroos_snapshot {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_snapshot_type type;
    uint64_t timestamp;
    uint64_t size_bytes;
    char description[64];
    uint8_t valid;
    struct spinlock lock;
};

struct zeroos_update {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_recovery_state state;
    uint64_t from_version;
    uint64_t to_version;
    uint8_t staged;
    uint8_t verified;
    uint8_t applied;
    uint64_t snapshot_id;
    struct spinlock lock;
};

int recovery_system_init(void);
int recovery_snapshot_create(enum zeroos_snapshot_type type, const char *desc, uint64_t size, uint64_t *snapshot_id_out);
int recovery_snapshot_restore(uint64_t snapshot_id);
int recovery_snapshot_delete(uint64_t snapshot_id);
int recovery_update_stage(uint64_t from_ver, uint64_t to_ver, uint64_t snapshot_id, uint64_t *update_id_out);
int recovery_update_verify(uint64_t update_id);
int recovery_update_apply(uint64_t update_id);
int recovery_update_rollback(uint64_t update_id);
int recovery_debug_validate(void);

#endif
