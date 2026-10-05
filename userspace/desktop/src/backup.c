#include <zeroos/desktop/backup.h>

static int zb_eq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a && b && a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return (!a && !b) || (a && b && a[i] == 0 && b[i] == 0);
}

static void zb_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!cap)
        return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static struct zd_backup_job *find_job_mut(struct zd_backup_engine *be, const char *name) {
    if (!be || !name || !name[0])
        return 0;
    for (uint32_t i = 0; i < ZD_BACKUP_MAX_JOBS; ++i) {
        if (be->jobs[i].in_use && zb_eq(be->jobs[i].name, name))
            return &be->jobs[i];
    }
    return 0;
}

void zd_backup_init(struct zd_backup_engine *be) {
    if (!be)
        return;
    for (uint32_t i = 0; i < sizeof(*be); ++i)
        ((uint8_t *)be)[i] = 0;
}

int zd_backup_create_job(struct zd_backup_engine *be, const char *name, const char *target,
                         enum zd_backup_type type, enum zd_backup_dest dest, uint32_t total_blocks, int encrypt) {
    if (!be || !name || !target || total_blocks == 0 || total_blocks > ZD_BACKUP_MAX_BLOCKS)
        return -22;
    if (find_job_mut(be, name))
        return -16; /* EBUSY */

    for (uint32_t i = 0; i < ZD_BACKUP_MAX_JOBS; ++i) {
        if (!be->jobs[i].in_use) {
            struct zd_backup_job *j = &be->jobs[i];
            zb_copy(j->name, name, sizeof(j->name));
            zb_copy(j->target_path, target, sizeof(j->target_path));
            j->type = type;
            j->dest = dest;
            j->total_blocks = total_blocks;
            j->completed_blocks = 0;
            j->encryption_enabled = encrypt ? 1 : 0;
            j->state = ZD_BACKUP_RUNNING;
            j->in_use = 1;
            if (type == ZD_BACKUP_FULL)
                be->stats.full_backups++;
            else
                be->stats.incremental_backups++;
            return 0;
        }
    }
    return -28; /* ENOSPC */
}

int zd_backup_write_block(struct zd_backup_engine *be, const char *name, uint32_t block_id,
                          uint32_t len, uint32_t crc32) {
    if (!be || !name)
        return -22;
    struct zd_backup_job *j = find_job_mut(be, name);
    if (!j || j->state != ZD_BACKUP_RUNNING)
        return -1;
    if (block_id >= j->total_blocks)
        return -22;

    j->blocks[block_id].block_id = block_id;
    j->blocks[block_id].length = len;
    j->blocks[block_id].crc32 = crc32;
    j->blocks[block_id].encrypted = j->encryption_enabled;
    j->blocks[block_id].verified = 1;

    j->completed_blocks++;
    if (j->completed_blocks == j->total_blocks)
        j->state = ZD_BACKUP_COMPLETED;

    return 0;
}

int zd_backup_interrupt(struct zd_backup_engine *be, const char *name) {
    if (!be || !name)
        return -22;
    struct zd_backup_job *j = find_job_mut(be, name);
    if (!j || j->state != ZD_BACKUP_RUNNING)
        return -1;
    j->state = ZD_BACKUP_INTERRUPTED;
    return 0;
}

int zd_backup_resume(struct zd_backup_engine *be, const char *name) {
    if (!be || !name)
        return -22;
    struct zd_backup_job *j = find_job_mut(be, name);
    if (!j || j->state != ZD_BACKUP_INTERRUPTED)
        return -1;
    j->state = ZD_BACKUP_RUNNING;
    be->stats.resumes++;
    return 0;
}

int zd_backup_verify_restore(struct zd_backup_engine *be, const char *name) {
    if (!be || !name)
        return -22;
    struct zd_backup_job *j = find_job_mut(be, name);
    if (!j || j->state != ZD_BACKUP_COMPLETED)
        return -1;

    /* Verify all blocks have valid checksums and verified flags */
    for (uint32_t b = 0; b < j->total_blocks; ++b) {
        if (!j->blocks[b].verified || j->blocks[b].crc32 == 0) {
            be->stats.integrity_failures++;
            return -3; /* EPERM: corruption */
        }
    }
    be->stats.restores_verified++;
    return 0;
}

const struct zd_backup_job *zd_backup_get_job(const struct zd_backup_engine *be, const char *name) {
    if (!be || !name)
        return 0;
    for (uint32_t i = 0; i < ZD_BACKUP_MAX_JOBS; ++i) {
        if (be->jobs[i].in_use && zb_eq(be->jobs[i].name, name))
            return &be->jobs[i];
    }
    return 0;
}
