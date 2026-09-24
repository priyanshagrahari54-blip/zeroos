#ifndef ZEROOS_GPT_H
#define ZEROOS_GPT_H

#include "types.h"

#define ZEROOS_GPT_SIGNATURE 0x5452415020494645ULL /* "EFI PART" */
#define ZEROOS_GPT_REVISION 0x00010000U
#define ZEROOS_GPT_HEADER_SIZE 92U
#define ZEROOS_GPT_MAX_PARTITIONS 128U
#define ZEROOS_GPT_GUID_SIZE 16U
#define ZEROOS_GPT_NAME_SIZE 72U

struct zeroos_gpt_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[ZEROOS_GPT_GUID_SIZE];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t sizeof_partition_entry;
    uint32_t partition_entry_array_crc32;
} __attribute__((packed));

struct zeroos_gpt_partition {
    uint8_t type_guid[ZEROOS_GPT_GUID_SIZE];
    uint8_t unique_guid[ZEROOS_GPT_GUID_SIZE];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[ZEROOS_GPT_NAME_SIZE/2];
} __attribute__((packed));

struct zeroos_gpt_info {
    uint64_t disk_guid_low;
    uint64_t disk_guid_high;
    uint64_t first_usable;
    uint64_t last_usable;
    uint32_t partition_count;
    uint32_t valid_partitions;
};

int gpt_validate_header(const void *data, uint64_t lba_size, uint64_t disk_sectors,
                        struct zeroos_gpt_header *out_header);
int gpt_validate_partition(const struct zeroos_gpt_partition *part,
                           uint64_t first_usable, uint64_t last_usable);
int gpt_parse(const void *header_sector, const void *entries, uint64_t disk_sectors,
              struct zeroos_gpt_info *info_out);
int gpt_debug_validate(void);

#endif
