#ifndef ZEROOS_GPT_H
#define ZEROOS_GPT_H
#include "block.h"

/*
 * GUID Partition Table (UEFI 2.x ch. 5) reader.
 *
 * Validation (each failure is reported with a stable reason code):
 *  header: signature, revision 1.0, 92 <= size <= sector, header CRC32,
 *          MyLBA (1 or last LBA), AlternateLBA consistency, usable range
 *          inside the disk and after/before the header+entry arrays,
 *          entry size (>=128, power of two, <= 1024), entry count
 *          (<= GPT_MAX_ENTRIES), entry-array CRC32, no 64-bit overflow.
 *  entries: first <= last, within [first_usable, last_usable], pairwise
 *          non-overlapping.
 * Recovery policy: primary valid -> use it (backup damage is reported);
 * primary invalid, backup valid -> use backup and report "degraded" (the
 * host tool `tools/storage/gpt.py repair` rewrites the primary); both
 * invalid -> no partitions are exposed. The kernel never rewrites a GPT.
 *
 * Mount policy: only partitions with the ZEROOS ZJFS type GUID are
 * candidates for the root filesystem; the ZEROOS scratch type marks space
 * the destructive driver self-tests may overwrite. Attribute bit 60 makes
 * the partition device read-only.
 */
#define GPT_MAX_ENTRIES 256U
#define GPT_MAX_PARTITIONS 8U

enum gpt_error {
    GPT_OK=0,
    GPT_ERR_IO=1,
    GPT_ERR_SIGNATURE=2,
    GPT_ERR_REVISION=3,
    GPT_ERR_HEADER_SIZE=4,
    GPT_ERR_HEADER_CRC=5,
    GPT_ERR_MY_LBA=6,
    GPT_ERR_USABLE_RANGE=7,
    GPT_ERR_ENTRY_GEOMETRY=8,
    GPT_ERR_ENTRY_CRC=9,
    GPT_ERR_ENTRY_RANGE=10,
    GPT_ERR_OVERLAP=11,
    GPT_ERR_TOO_MANY=12,
    GPT_ERR_NO_MEMORY=13,
    GPT_ERR_ALTERNATE=14
};

struct gpt_partition {
    uint32_t number;            /* 1-based entry index */
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    char name[37];              /* ASCII-folded UTF-16LE name */
};

struct gpt_result {
    int primary_status;         /* enum gpt_error */
    int backup_status;
    int used_backup;
    int pmbr_present;
    uint64_t first_usable;
    uint64_t last_usable;
    uint8_t disk_guid[16];
    uint32_t partition_count;
    struct gpt_partition partitions[GPT_MAX_PARTITIONS];
};

extern const uint8_t gpt_type_zeroos_zjfs[16];
extern const uint8_t gpt_type_zeroos_scratch[16];

const char *gpt_error_name(int error);
/* Returns 0 when a valid table (primary or backup) was found. */
int gpt_scan(struct block_device *disk, struct gpt_result *result);
/* Register validated partitions as block devices; returns count. */
int gpt_register(struct block_device *disk, const struct gpt_result *result);
int gpt_guid_equal(const uint8_t *a, const uint8_t *b);

#endif
