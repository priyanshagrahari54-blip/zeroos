/* ZEROOS Encrypted Resumable Backup Tests (Section 22) */
#include <zeroos/desktop/backup.h>
#include "test_harness.h"
#include <string.h>

void zd_test_backup_suite(void) {
    zd_test_current = "backup";
    printf(" suite: backup\n");

    struct zd_backup_engine be;
    zd_backup_init(&be);

    /* Create full encrypted backup job with 4 blocks */
    ZD_CHECK(zd_backup_create_job(&be, "Full-20261005", "/mnt/usb/zero_backup.dat",
                                  ZD_BACKUP_FULL, ZD_BACKUP_DEST_EXTERNAL_USB_HDD, 4, 1) == 0);
    ZD_CHECK(be.stats.full_backups == 1);

    const struct zd_backup_job *j = zd_backup_get_job(&be, "Full-20261005");
    ZD_CHECK(j != NULL);
    ZD_CHECK(j->state == ZD_BACKUP_RUNNING);
    ZD_CHECK(j->encryption_enabled == 1);

    /* Write block 0 and block 1 */
    ZD_CHECK(zd_backup_write_block(&be, "Full-20261005", 0, 4096, 0x11223344) == 0);
    ZD_CHECK(zd_backup_write_block(&be, "Full-20261005", 1, 4096, 0x55667788) == 0);
    ZD_CHECK(j->completed_blocks == 2);

    /* Interrupt job (e.g. drive unplugged) */
    ZD_CHECK(zd_backup_interrupt(&be, "Full-20261005") == 0);
    ZD_CHECK(j->state == ZD_BACKUP_INTERRUPTED);

    /* Resume job and write remaining blocks 2 and 3 */
    ZD_CHECK(zd_backup_resume(&be, "Full-20261005") == 0);
    ZD_CHECK(j->state == ZD_BACKUP_RUNNING);
    ZD_CHECK(be.stats.resumes == 1);

    ZD_CHECK(zd_backup_write_block(&be, "Full-20261005", 2, 4096, 0x99aabbcc) == 0);
    ZD_CHECK(zd_backup_write_block(&be, "Full-20261005", 3, 4096, 0xddeeff00) == 0);
    ZD_CHECK(j->completed_blocks == 4);
    ZD_CHECK(j->state == ZD_BACKUP_COMPLETED);

    /* Verify restore integrity */
    ZD_CHECK(zd_backup_verify_restore(&be, "Full-20261005") == 0);
    ZD_CHECK(be.stats.restores_verified == 1);
}
