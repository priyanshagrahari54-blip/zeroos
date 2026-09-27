#ifndef ZEROOS_ZJFS_H
#define ZEROOS_ZJFS_H
#include "vfs.h"

/*
 * ZJFS — ZEROOS Journaling File System, on-disk format version 1.
 * Normative description: docs/ZJFS.md. The host tools in tools/storage/
 * (zjfs.py: mkfs, fsck, ls, cat, put) implement the same format; any change
 * here must bump ZJ_VERSION or a feature bit and update both.
 *
 * All integers little-endian. Block size 4096. Block numbers are u32 in
 * block maps (max 16 TiB); u64 in the superblock/journal.
 *
 * Layout: [0] superblock | [1..J] journal (block 1 = journal superblock)
 *         | inode bitmap | block bitmap | inode table | data ... |
 *         [last] backup superblock
 *
 * Checksums: CRC-32C. Metadata blocks carry csum = crc32c(bytes 0..4091)
 * XOR block_number at offset 4092 so misdirected writes are detected;
 * inodes carry crc32c(bytes 0..251) XOR ino at offset 252; directory
 * blocks carry the checksum in their header (offset 8). File data is not
 * checksummed (documented limitation).
 */

#define ZJ_MAGIC            0x53464a5aU     /* "ZJFS" */
#define ZJ_VERSION          1U
#define ZJ_BLOCK_SIZE       4096U
#define ZJ_INODE_SIZE       256U
#define ZJ_INODES_PER_BLOCK (ZJ_BLOCK_SIZE / ZJ_INODE_SIZE)
#define ZJ_BITS_PER_BITMAP  (4088U * 8U)
#define ZJ_BITMAP_MAGIC     0x4d424a5aU     /* "ZJBM" */
#define ZJ_INDIRECT_MAGIC   0x4e494a5aU     /* "ZJIN" */
#define ZJ_DIR_MAGIC        0x52444a5aU     /* "ZJDR" */
#define ZJ_JSB_MAGIC        0x534a4a5aU     /* "ZJJS" */
#define ZJ_DESC_MAGIC       0x44544a5aU     /* "ZJTD" */
#define ZJ_COMMIT_MAGIC     0x43544a5aU     /* "ZJTC" */
#define ZJ_PTRS_PER_BLOCK   1022U
#define ZJ_DIRECT           12U
#define ZJ_ROOT_INO         1U
#define ZJ_LOSTFOUND_INO    2U
#define ZJ_DESC_MAX         500U
#define ZJ_DIR_HEADER       16U
#define ZJ_DIRENT_HEADER    8U
#define ZJ_MAX_FILE_BLOCKS  (ZJ_DIRECT + ZJ_PTRS_PER_BLOCK + \
                             (uint64_t)ZJ_PTRS_PER_BLOCK * ZJ_PTRS_PER_BLOCK)

/* Superblock state bits. */
#define ZJ_STATE_CLEAN 0x1U
#define ZJ_STATE_DIRTY 0x2U
#define ZJ_STATE_ERROR 0x4U     /* sticky until fsck clears it */

/* Inode flags. */
#define ZJ_IF_ORPHAN 0x1U        /* on orphan list: destroy/truncate at mount */
#define ZJ_IF_TRUNC  0x2U        /* chunked truncate in progress to i_size */

/* Directory entry types. */
#define ZJ_FT_REG 1U
#define ZJ_FT_DIR 2U

struct zj_super {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t inode_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t free_blocks;
    uint64_t free_inodes;
    uint64_t journal_start;
    uint64_t journal_blocks;
    uint64_t inode_bitmap_start;
    uint64_t inode_bitmap_blocks;
    uint64_t block_bitmap_start;
    uint64_t block_bitmap_blocks;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t data_start;
    uint32_t root_ino;
    uint32_t state;
    uint64_t features_compat;
    uint64_t features_incompat;
    uint64_t features_ro_compat;
    uint8_t uuid[16];
    char label[32];
    uint64_t mount_count;
    uint64_t last_mount_time;
    uint64_t last_write_time;
    uint64_t last_check_time;
    uint32_t error_count;
    uint32_t last_error_code;
    uint64_t last_error_block;
    uint64_t last_error_time;
    uint64_t orphan_head;
    uint64_t generation_counter;
    uint64_t lost_found_ino;
    uint8_t reserved[4092 - 280];
    uint32_t checksum;
} __attribute__((packed));

struct zj_dinode {
    uint16_t mode;
    uint16_t links;
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    uint64_t size;
    uint64_t blocks;
    uint64_t atime_ns;
    uint64_t mtime_ns;
    uint64_t ctime_ns;
    uint64_t crtime_ns;
    uint32_t generation;
    uint32_t orphan_next;
    uint32_t direct[ZJ_DIRECT];
    uint32_t indirect;
    uint32_t dindirect;
    uint32_t tindirect;        /* reserved, must be 0 in version 1 */
    uint32_t parent;           /* directories: parent directory inode */
    uint8_t reserved[116];
    uint32_t checksum;
} __attribute__((packed));

struct zj_jsb {
    uint32_t magic;
    uint32_t version;
    uint64_t sequence;         /* oldest transaction that may need replay */
    uint64_t blocks;
    uint32_t state;
    uint8_t reserved[4092 - 28];
    uint32_t checksum;
} __attribute__((packed));

struct zj_desc {
    uint32_t magic;
    uint32_t count;
    uint64_t sequence;
    uint64_t targets[ZJ_DESC_MAX];
    uint8_t reserved[4092 - 16 - ZJ_DESC_MAX * 8];
    uint32_t checksum;
} __attribute__((packed));

struct zj_commit {
    uint32_t magic;
    uint32_t count;
    uint64_t sequence;
    uint32_t data_crc;         /* crc32c chained over the journaled blocks */
    uint32_t reserved0;
    uint64_t commit_time_ns;
    uint8_t reserved[4092 - 32];
    uint32_t checksum;
} __attribute__((packed));

_Static_assert(sizeof(struct zj_super) == 4096, "zj_super size");
_Static_assert(sizeof(struct zj_dinode) == 256, "zj_dinode size");
_Static_assert(sizeof(struct zj_jsb) == 4096, "zj_jsb size");
_Static_assert(sizeof(struct zj_desc) == 4096, "zj_desc size");
_Static_assert(sizeof(struct zj_commit) == 4096, "zj_commit size");

/* Format parameters (kernel-side mkfs is used for ramdisk self-tests only;
 * the mount path never formats). */
struct zj_format_options {
    uint64_t inode_count;      /* 0 = default (blocks / 4, >= 64) */
    uint64_t journal_blocks;   /* 0 = default min(1024, max(64, blocks/32)) */
    const char *label;
};

struct zj_metrics {
    uint64_t commits;
    uint64_t commit_blocks;
    uint64_t max_txn_blocks;
    uint64_t journal_flushes;
    uint64_t replayed_txns;
    uint64_t replayed_blocks;
    uint64_t orphans_recovered;
    uint64_t meta_hits;
    uint64_t meta_misses;
    uint64_t meta_evictions;
    uint64_t checksum_errors;
    uint64_t io_errors;
    uint64_t enospc;
    uint64_t aborts;
    uint64_t allocations;
    uint64_t frees;
};

int zjfs_format(struct block_device *device, const struct zj_format_options *options);
int zjfs_probe(struct block_device *device);    /* 0 if a ZJFS superblock validates */
int zjfs_mount(struct block_device *device, uint32_t flags, struct vfs_superblock *sb);
/* Full consistency check of an unmounted device (bitmaps vs reachable
 * blocks, link counts, directory structure, checksums). repair=1 fixes
 * leaked blocks/inodes, link counts and free counters and clears ERROR. */
struct zj_check_report {
    uint64_t inodes_used;
    uint64_t blocks_used;
    uint32_t checksum_errors;
    uint32_t structure_errors;
    uint32_t leaked_blocks;
    uint32_t leaked_inodes;
    uint32_t link_errors;
    uint32_t counter_errors;
    uint32_t repaired;
    uint32_t fatal;
};
int zjfs_check(struct block_device *device, int repair, struct zj_check_report *report);
void zjfs_metrics_snapshot(struct vfs_superblock *sb, struct zj_metrics *out);
/* Commit the running transaction (tests, periodic commit). */
int zjfs_commit(struct vfs_superblock *sb);
/* Periodic work: commit if the running transaction is older than
 * ZJ_COMMIT_INTERVAL ticks. */
int zjfs_periodic(struct vfs_superblock *sb);     /* 1 = txn still open */
/* Called (IRQ-safe, non-blocking) when a transaction opens. */
void zjfs_set_txn_hook(void (*hook)(void));
#define ZJ_COMMIT_INTERVAL 500U

uint32_t zj_meta_csum(const void *block, uint64_t block_number);

#endif
