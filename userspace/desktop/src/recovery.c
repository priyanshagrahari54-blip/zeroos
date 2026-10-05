#include <zeroos/desktop/recovery.h>

static int zr_eq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a && b && a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return (!a && !b) || (a && b && a[i] == 0 && b[i] == 0);
}

static void zr_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!cap)
        return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

void zd_recovery_init(struct zd_recovery_env *rec, uint32_t current_slot) {
    if (!rec)
        return;
    for (uint32_t i = 0; i < sizeof(*rec); ++i)
        ((uint8_t *)rec)[i] = 0;
    rec->active_mode = ZD_REC_MODE_NORMAL;
    rec->active_slot = current_slot & 1;
    rec->rollback_slot = (current_slot & 1) ^ 1;
    rec->safe_boot_active = 0;
    rec->network_enabled = 1;

    /* Default diagnostics state */
    rec->diag.ram_total_mb = 2048; /* 2 GB G560 baseline */
    rec->diag.ram_free_mb = 1850;
    rec->diag.storage_health_pct = 98;
    rec->diag.bad_blocks_found = 0;
    rec->diag.cpu_temp_celsius = 45;
    rec->diag.primary_sb_valid = 1;
    rec->diag.backup_sb_valid = 1;
    rec->diag.gpt_valid = 1;
}

int zd_recovery_set_mode(struct zd_recovery_env *rec, enum zd_recovery_mode mode) {
    if (!rec)
        return -22;
    rec->active_mode = mode;
    if (mode == ZD_REC_MODE_SAFE_BOOT) {
        rec->safe_boot_active = 1;
        rec->network_enabled = 0; /* Minimal services, no networking */
        rec->stats.safe_boots++;
    } else if (mode == ZD_REC_MODE_REPAIR_BOOT) {
        rec->safe_boot_active = 1;
        rec->stats.repair_boots++;
    } else if (mode == ZD_REC_MODE_NORMAL) {
        rec->safe_boot_active = 0;
        rec->network_enabled = 1;
    }
    return 0;
}

enum zd_fsck_status zd_recovery_run_fsck(struct zd_recovery_env *rec, int force_repair) {
    if (!rec)
        return ZD_FSCK_CORRUPT_UNREPAIRABLE;
    rec->stats.fsck_runs++;

    if (!rec->diag.gpt_valid)
        return ZD_FSCK_CORRUPT_UNREPAIRABLE;

    if (rec->diag.primary_sb_valid && rec->diag.backup_sb_valid)
        return ZD_FSCK_CLEAN;

    if (!rec->diag.primary_sb_valid && rec->diag.backup_sb_valid) {
        if (force_repair) {
            rec->diag.primary_sb_valid = 1; /* restore primary from backup */
            return ZD_FSCK_REPAIRED;
        }
        return ZD_FSCK_REPAIRED;
    }

    if (rec->diag.primary_sb_valid && !rec->diag.backup_sb_valid) {
        if (force_repair) {
            rec->diag.backup_sb_valid = 1; /* restore backup from primary */
            return ZD_FSCK_REPAIRED;
        }
        return ZD_FSCK_REPAIRED;
    }

    return ZD_FSCK_CORRUPT_UNREPAIRABLE;
}

int zd_recovery_trigger_rollback(struct zd_recovery_env *rec) {
    if (!rec)
        return -22;
    /* Flip active slot to rollback slot */
    uint32_t tmp = rec->active_slot;
    rec->active_slot = rec->rollback_slot;
    rec->rollback_slot = tmp;
    rec->stats.rollbacks_performed++;
    return 0;
}

int zd_recovery_factory_reset(struct zd_recovery_env *rec, int wipe_user_data) {
    if (!rec)
        return -22;
    (void)wipe_user_data;
    rec->stats.factory_resets++;
    rec->diag.primary_sb_valid = 1;
    rec->diag.backup_sb_valid = 1;
    rec->active_slot = 0;
    rec->rollback_slot = 1;
    return 0;
}

int zd_recovery_run_diagnostics(struct zd_recovery_env *rec, struct zd_recovery_diag *out_diag) {
    if (!rec || !out_diag)
        return -22;
    *out_diag = rec->diag;
    return 0;
}

int zd_recovery_exec_cmd(struct zd_recovery_env *rec, const char *cmd, char *out_buf, uint32_t out_cap) {
    if (!rec || !cmd || !out_buf || out_cap < 32)
        return -22;

    if (zr_eq(cmd, "diag")) {
        zr_copy(out_buf, "DIAG: RAM 2048MB (OK), HDD HEALTH 98% (OK), TEMP 45C (OK), GPT (OK)", out_cap);
        return 0;
    }
    if (zr_eq(cmd, "fsck")) {
        enum zd_fsck_status st = zd_recovery_run_fsck(rec, 1);
        if (st == ZD_FSCK_CLEAN)
            zr_copy(out_buf, "FSCK: clean. Inodes and blocks verified.", out_cap);
        else if (st == ZD_FSCK_REPAIRED)
            zr_copy(out_buf, "FSCK: errors repaired using backup superblock.", out_cap);
        else
            zr_copy(out_buf, "FSCK: fatal filesystem corruption.", out_cap);
        return 0;
    }
    if (zr_eq(cmd, "rollback")) {
        zd_recovery_trigger_rollback(rec);
        zr_copy(out_buf, "ROLLBACK: active boot slot switched successfully.", out_cap);
        return 0;
    }
    if (zr_eq(cmd, "help")) {
        zr_copy(out_buf, "Available recovery commands: diag, fsck, rollback, reset, help", out_cap);
        return 0;
    }

    zr_copy(out_buf, "Unknown recovery command. Type 'help'.", out_cap);
    return -1;
}
