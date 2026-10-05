/* ZEROOS Recovery & Diagnostics Tests (Stage 6) */
#include <zeroos/desktop/recovery.h>
#include "test_harness.h"
#include <string.h>

void zd_test_recovery_suite(void) {
    zd_test_current = "recovery";
    printf(" suite: recovery\n");
    struct zd_recovery_env rec;
    char buf[128];

    zd_recovery_init(&rec, 0);
    ZD_CHECK(rec.active_slot == 0);
    ZD_CHECK(rec.rollback_slot == 1);
    ZD_CHECK(rec.safe_boot_active == 0);
    ZD_CHECK(rec.network_enabled == 1);

    /* Safe boot mode switches off network and arms safe boot */
    ZD_CHECK(zd_recovery_set_mode(&rec, ZD_REC_MODE_SAFE_BOOT) == 0);
    ZD_CHECK(rec.safe_boot_active == 1);
    ZD_CHECK(rec.network_enabled == 0);
    ZD_CHECK(rec.stats.safe_boots == 1);

    /* Normal mode restore */
    ZD_CHECK(zd_recovery_set_mode(&rec, ZD_REC_MODE_NORMAL) == 0);
    ZD_CHECK(rec.safe_boot_active == 0);
    ZD_CHECK(rec.network_enabled == 1);

    /* Diagnostics execution */
    struct zd_recovery_diag diag;
    ZD_CHECK(zd_recovery_run_diagnostics(&rec, &diag) == 0);
    ZD_CHECK(diag.ram_total_mb == 2048);
    ZD_CHECK(diag.storage_health_pct >= 90);
    ZD_CHECK(diag.gpt_valid == 1);

    /* FSCK clean check */
    ZD_CHECK(zd_recovery_run_fsck(&rec, 0) == ZD_FSCK_CLEAN);
    ZD_CHECK(rec.stats.fsck_runs == 1);

    /* Simulated primary superblock corruption -> auto repair via backup */
    rec.diag.primary_sb_valid = 0;
    ZD_CHECK(zd_recovery_run_fsck(&rec, 1) == ZD_FSCK_REPAIRED);
    ZD_CHECK(rec.diag.primary_sb_valid == 1);

    /* Rollback execution */
    ZD_CHECK(zd_recovery_trigger_rollback(&rec) == 0);
    ZD_CHECK(rec.active_slot == 1);
    ZD_CHECK(rec.rollback_slot == 0);
    ZD_CHECK(rec.stats.rollbacks_performed == 1);

    /* Factory reset */
    ZD_CHECK(zd_recovery_factory_reset(&rec, 1) == 0);
    ZD_CHECK(rec.stats.factory_resets == 1);
    ZD_CHECK(rec.active_slot == 0);

    /* Recovery shell command dispatcher */
    ZD_CHECK(zd_recovery_exec_cmd(&rec, "diag", buf, sizeof(buf)) == 0);
    ZD_CHECK(strstr(buf, "RAM 2048MB") != NULL);

    ZD_CHECK(zd_recovery_exec_cmd(&rec, "fsck", buf, sizeof(buf)) == 0);
    ZD_CHECK(strstr(buf, "FSCK: clean") != NULL);

    ZD_CHECK(zd_recovery_exec_cmd(&rec, "rollback", buf, sizeof(buf)) == 0);
    ZD_CHECK(rec.active_slot == 1);

    ZD_CHECK(zd_recovery_exec_cmd(&rec, "invalid_cmd", buf, sizeof(buf)) == -1);
}
