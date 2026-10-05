/* ZEROOS Resumable, Encrypted Backup Engine (Section 22).
 *
 * Implements full, incremental, and changed-block backups with snapshot
 * binding, ChaCha20-Poly1305 encryption, block integrity verification,
 * resumable recovery after interruption, and target destination support.
 * Zero heap allocation, fully host-testable.
 */
#ifndef ZEROOS_DESKTOP_BACKUP_H
#define ZEROOS_DESKTOP_BACKUP_H

#include <stdint.h>

#define ZD_BACKUP_NAME_LEN 32
#define ZD_BACKUP_PATH_LEN 96
#define ZD_BACKUP_MAX_JOBS 8
#define ZD_BACKUP_MAX_BLOCKS 64

enum zd_backup_type {
    ZD_BACKUP_FULL = 0,
    ZD_BACKUP_INCREMENTAL,
    ZD_BACKUP_CHANGED_BLOCK
};

enum zd_backup_dest {
    ZD_BACKUP_DEST_LOCAL_DISK = 0,
    ZD_BACKUP_DEST_EXTERNAL_USB_HDD,
    ZD_BACKUP_DEST_NETWORK_NAS
};

enum zd_backup_state {
    ZD_BACKUP_IDLE = 0,
    ZD_BACKUP_RUNNING,
    ZD_BACKUP_INTERRUPTED,
    ZD_BACKUP_COMPLETED,
    ZD_BACKUP_FAILED
};

struct zd_backup_block {
    uint32_t block_id;
    uint32_t length;
    uint32_t crc32;
    uint8_t encrypted;
    uint8_t verified;
};

struct zd_backup_job {
    char name[ZD_BACKUP_NAME_LEN];
    char target_path[ZD_BACKUP_PATH_LEN];
    enum zd_backup_type type;
    enum zd_backup_dest dest;
    enum zd_backup_state state;
    uint32_t total_blocks;
    uint32_t completed_blocks;
    uint32_t base_snapshot_id;
    struct zd_backup_block blocks[ZD_BACKUP_MAX_BLOCKS];
    uint8_t encryption_enabled;
    uint8_t in_use;
};

struct zd_backup_engine {
    struct zd_backup_job jobs[ZD_BACKUP_MAX_JOBS];
    struct {
        uint32_t full_backups;
        uint32_t incremental_backups;
        uint32_t resumes;
        uint32_t restores_verified;
        uint32_t integrity_failures;
    } stats;
};

void zd_backup_init(struct zd_backup_engine *be);

/* Job lifecycle */
int zd_backup_create_job(struct zd_backup_engine *be, const char *name, const char *target,
                         enum zd_backup_type type, enum zd_backup_dest dest, uint32_t total_blocks, int encrypt);

/* Step-driven execution (writes blocks sequentially; interruptable at any point) */
int zd_backup_write_block(struct zd_backup_engine *be, const char *name, uint32_t block_id,
                          uint32_t len, uint32_t crc32);

/* Interruption simulation (e.g. power loss or unplugged drive) */
int zd_backup_interrupt(struct zd_backup_engine *be, const char *name);

/* Resume interrupted backup */
int zd_backup_resume(struct zd_backup_engine *be, const char *name);

/* Verify restore integrity */
int zd_backup_verify_restore(struct zd_backup_engine *be, const char *name);

const struct zd_backup_job *zd_backup_get_job(const struct zd_backup_engine *be, const char *name);

#endif /* ZEROOS_DESKTOP_BACKUP_H */
