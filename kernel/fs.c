#include "fs.h"

static struct spinlock fs_lock;
static struct zeroos_fs_type_ops fs_types[ZEROOS_FS_MAX_TYPES];
static struct zeroos_journal global_journal;

int fs_system_init(void) {
    spinlock_init(&fs_lock);
    for (uint32_t i=0;i<ZEROOS_FS_MAX_TYPES;++i) {
        fs_types[i].registered=0;
        fs_types[i].type=ZEROOS_FS_TYPE_UNKNOWN;
    }
    journal_init(&global_journal);
    return 0;
}

int fs_register_type(enum zeroos_fs_type type, const char *name,
                     int (*mount)(uint64_t,uint64_t),
                     int (*unmount)(uint64_t),
                     int (*read_inode)(uint64_t,void*,uint64_t,uint64_t),
                     int (*write_inode)(uint64_t,const void*,uint64_t,uint64_t),
                     int (*sync)(uint64_t)) {
    if (!name || type==ZEROOS_FS_TYPE_UNKNOWN) return -1;
    uint64_t flags=spin_lock_irqsave(&fs_lock);
    for (uint32_t i=0;i<ZEROOS_FS_MAX_TYPES;++i) {
        if (fs_types[i].registered) continue;
        fs_types[i].type=type;
        fs_types[i].mount=mount;
        fs_types[i].unmount=unmount;
        fs_types[i].read_inode=read_inode;
        fs_types[i].write_inode=write_inode;
        fs_types[i].sync=sync;
        fs_types[i].registered=1;
        uint32_t n=0;
        while (n<ZEROOS_FS_MAX_NAME-1 && name[n]) { fs_types[i].name[n]=name[n]; n++; }
        fs_types[i].name[n]=0;
        spin_unlock_irqrestore(&fs_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&fs_lock,flags);
    return -1;
}

int fs_mount(enum zeroos_fs_type type, uint64_t device_id, const char *mount_point, uint64_t *mount_id_out) {
    if (!mount_point || !mount_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&fs_lock);
    struct zeroos_fs_type_ops *ops=0;
    for (uint32_t i=0;i<ZEROOS_FS_MAX_TYPES;++i) if (fs_types[i].registered && fs_types[i].type==type) { ops=&fs_types[i]; break; }
    if (!ops) { spin_unlock_irqrestore(&fs_lock,flags); return -1; }
    spin_unlock_irqrestore(&fs_lock,flags);
    uint64_t mount_id=0;
    int rc=vfs_mount(device_id, type==ZEROOS_FS_TYPE_TMPFS ? "tmpfs" : "ext4", mount_point, &mount_id);
    if (rc!=0) return -1;
    if (ops->mount) {
        rc=ops->mount(device_id, mount_id);
        if (rc!=0) { (void)vfs_unmount(mount_id); return -1; }
    }
    *mount_id_out=mount_id;
    return 0;
}

int fs_unmount(uint64_t mount_id) {
    /* Find fs type via mount lookup omitted for simplicity */
    uint64_t flags=spin_lock_irqsave(&fs_lock);
    spin_unlock_irqrestore(&fs_lock,flags);
    return vfs_unmount(mount_id);
}

int journal_init(struct zeroos_journal *j) {
    if (!j) return -1;
    spinlock_init(&j->lock);
    j->state=ZEROOS_JOURNAL_ACTIVE;
    j->next_transaction_id=1;
    j->count=0;
    j->commits=0;
    j->aborts=0;
    for (uint32_t i=0;i<ZEROOS_FS_JOURNAL_MAX_ENTRIES;++i) j->entries[i].valid=0;
    return 0;
}

int journal_begin(struct zeroos_journal *j, uint64_t *transaction_id_out) {
    if (!j || !transaction_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&j->lock);
    if (j->state!=ZEROOS_JOURNAL_ACTIVE) { spin_unlock_irqrestore(&j->lock,flags); return -1; }
    *transaction_id_out=j->next_transaction_id++;
    if (j->next_transaction_id==0) j->next_transaction_id=1;
    spin_unlock_irqrestore(&j->lock,flags);
    return 0;
}

int journal_log(struct zeroos_journal *j, uint64_t transaction_id, uint64_t inode_id, uint64_t block, uint64_t size) {
    if (!j || transaction_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&j->lock);
    if (j->count>=ZEROOS_FS_JOURNAL_MAX_ENTRIES) { spin_unlock_irqrestore(&j->lock,flags); return -1; }
    for (uint32_t i=0;i<ZEROOS_FS_JOURNAL_MAX_ENTRIES;++i) {
        if (j->entries[i].valid) continue;
        j->entries[i].valid=1;
        j->entries[i].transaction_id=transaction_id;
        j->entries[i].inode_id=inode_id;
        j->entries[i].block_number=block;
        j->entries[i].size=size;
        j->entries[i].committed=0;
        j->count++;
        spin_unlock_irqrestore(&j->lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&j->lock,flags);
    return -1;
}

int journal_commit(struct zeroos_journal *j, uint64_t transaction_id) {
    if (!j || transaction_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&j->lock);
    uint32_t committed=0;
    for (uint32_t i=0;i<ZEROOS_FS_JOURNAL_MAX_ENTRIES;++i) {
        if (!j->entries[i].valid || j->entries[i].transaction_id!=transaction_id) continue;
        j->entries[i].committed=1;
        committed++;
    }
    /* In real FS, would flush to disk then clear */
    for (uint32_t i=0;i<ZEROOS_FS_JOURNAL_MAX_ENTRIES;++i) {
        if (!j->entries[i].valid || j->entries[i].transaction_id!=transaction_id) continue;
        if (!j->entries[i].committed) continue;
        j->entries[i].valid=0;
        j->count--;
    }
    j->commits++;
    spin_unlock_irqrestore(&j->lock,flags);
    return committed ? 0 : -1;
}

int journal_abort(struct zeroos_journal *j, uint64_t transaction_id) {
    if (!j || transaction_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&j->lock);
    for (uint32_t i=0;i<ZEROOS_FS_JOURNAL_MAX_ENTRIES;++i) {
        if (!j->entries[i].valid || j->entries[i].transaction_id!=transaction_id) continue;
        j->entries[i].valid=0;
        j->count--;
    }
    j->aborts++;
    spin_unlock_irqrestore(&j->lock,flags);
    return 0;
}

int fs_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&fs_lock);
    uint32_t registered=0;
    for (uint32_t i=0;i<ZEROOS_FS_MAX_TYPES;++i) if (fs_types[i].registered) registered++;
    spin_unlock_irqrestore(&fs_lock,flags);
    return 0;
}
