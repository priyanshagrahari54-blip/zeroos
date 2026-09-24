#ifndef ZEROOS_FS_H
#define ZEROOS_FS_H

#include "types.h"
#include "vfs.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_FS_MAX_TYPES 8U
#define ZEROOS_FS_MAX_NAME 16U
#define ZEROOS_FS_JOURNAL_MAX_ENTRIES 256U

enum zeroos_fs_type {
    ZEROOS_FS_TYPE_UNKNOWN = 0,
    ZEROOS_FS_TYPE_EXT4,
    ZEROOS_FS_TYPE_FAT32,
    ZEROOS_FS_TYPE_TMPFS,
    ZEROOS_FS_TYPE_PROCFS
};

enum zeroos_journal_state {
    ZEROOS_JOURNAL_STOPPED = 0,
    ZEROOS_JOURNAL_ACTIVE,
    ZEROOS_JOURNAL_COMMITTING,
    ZEROOS_JOURNAL_ERROR
};

struct zeroos_journal_entry {
    uint64_t transaction_id;
    uint64_t inode_id;
    uint64_t block_number;
    uint64_t size;
    uint8_t committed;
    uint8_t valid;
};

struct zeroos_journal {
    struct spinlock lock;
    enum zeroos_journal_state state;
    uint64_t next_transaction_id;
    struct zeroos_journal_entry entries[ZEROOS_FS_JOURNAL_MAX_ENTRIES];
    uint32_t count;
    uint64_t commits;
    uint64_t aborts;
};

struct zeroos_fs_type_ops {
    enum zeroos_fs_type type;
    char name[ZEROOS_FS_MAX_NAME];
    int (*mount)(uint64_t device_id, uint64_t mount_id);
    int (*unmount)(uint64_t mount_id);
    int (*read_inode)(uint64_t inode_id, void *buf, uint64_t offset, uint64_t len);
    int (*write_inode)(uint64_t inode_id, const void *buf, uint64_t offset, uint64_t len);
    int (*sync)(uint64_t mount_id);
    uint8_t registered;
};

int fs_system_init(void);
int fs_register_type(enum zeroos_fs_type type, const char *name,
                     int (*mount)(uint64_t,uint64_t),
                     int (*unmount)(uint64_t),
                     int (*read_inode)(uint64_t,void*,uint64_t,uint64_t),
                     int (*write_inode)(uint64_t,const void*,uint64_t,uint64_t),
                     int (*sync)(uint64_t));
int fs_mount(enum zeroos_fs_type type, uint64_t device_id, const char *mount_point, uint64_t *mount_id_out);
int fs_unmount(uint64_t mount_id);
int journal_init(struct zeroos_journal *j);
int journal_begin(struct zeroos_journal *j, uint64_t *transaction_id_out);
int journal_log(struct zeroos_journal *j, uint64_t transaction_id, uint64_t inode_id, uint64_t block, uint64_t size);
int journal_commit(struct zeroos_journal *j, uint64_t transaction_id);
int journal_abort(struct zeroos_journal *j, uint64_t transaction_id);
int fs_debug_validate(void);

#endif
