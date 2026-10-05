/* ZEROOS Bootable Recovery & Safe Mode Architecture (Stage 6 / Req 21).
 *
 * Implements an isolated recovery environment capable of executing outside
 * normal desktop services:
 *  - Safe Boot / Repair Boot modes
 *  - Filesystem integrity verification (fsck / journal replay)
 *  - System snapshot rollback
 *  - Factory reset / keep-files reinstall
 *  - Offline hardware diagnostics (RAM, storage, thermals)
 *  - Recovery shell command dispatch
 */
#ifndef ZEROOS_DESKTOP_RECOVERY_H
#define ZEROOS_DESKTOP_RECOVERY_H

#include <stdint.h>

enum zd_recovery_mode {
    ZD_REC_MODE_NORMAL = 0,
    ZD_REC_MODE_SAFE_BOOT,
    ZD_REC_MODE_REPAIR_BOOT,
    ZD_REC_MODE_RECOVERY_SHELL,
    ZD_REC_MODE_FACTORY_RESET,
    ZD_REC_MODE_ROLLBACK
};

enum zd_fsck_status {
    ZD_FSCK_CLEAN = 0,
    ZD_FSCK_REPAIRED,
    ZD_FSCK_CORRUPT_UNREPAIRABLE
};

struct zd_recovery_diag {
    uint32_t ram_total_mb;
    uint32_t ram_free_mb;
    uint32_t storage_health_pct; /* 0..100 */
    uint32_t bad_blocks_found;
    int32_t cpu_temp_celsius;
    uint8_t primary_sb_valid;
    uint8_t backup_sb_valid;
    uint8_t gpt_valid;
};

struct zd_recovery_env {
    enum zd_recovery_mode active_mode;
    uint32_t active_slot;         /* 0 = Slot A, 1 = Slot B */
    uint32_t rollback_slot;       /* 1 = Slot B, 0 = Slot A */
    uint8_t safe_boot_active;
    uint8_t network_enabled;      /* disabled in safe boot by default */
    struct zd_recovery_diag diag;
    struct {
        uint32_t repair_boots;
        uint32_t fsck_runs;
        uint32_t rollbacks_performed;
        uint32_t factory_resets;
        uint32_t safe_boots;
    } stats;
};

void zd_recovery_init(struct zd_recovery_env *rec, uint32_t current_slot);

/* Mode transitions */
int zd_recovery_set_mode(struct zd_recovery_env *rec, enum zd_recovery_mode mode);

/* Filesystem check & repair invocation */
enum zd_fsck_status zd_recovery_run_fsck(struct zd_recovery_env *rec, int force_repair);

/* System snapshot rollback */
int zd_recovery_trigger_rollback(struct zd_recovery_env *rec);

/* Reinstallation policies */
int zd_recovery_factory_reset(struct zd_recovery_env *rec, int wipe_user_data);

/* Diagnostics query */
int zd_recovery_run_diagnostics(struct zd_recovery_env *rec, struct zd_recovery_diag *out_diag);

/* Recovery shell dispatcher: returns 0 on success, <0 on invalid command */
int zd_recovery_exec_cmd(struct zd_recovery_env *rec, const char *cmd, char *out_buf, uint32_t out_cap);

#endif /* ZEROOS_DESKTOP_RECOVERY_H */
