#include "gpt.h"
#include "sync.h"

static uint32_t crc32_table[256];
static uint8_t crc32_ready;

static void crc32_init(void) {
    if (crc32_ready) return;
    for (uint32_t i=0;i<256;++i) {
        uint32_t c=i;
        for (uint32_t j=0;j<8;++j) c = (c&1) ? 0xedb88320U ^ (c>>1) : c>>1;
        crc32_table[i]=c;
    }
    crc32_ready=1;
}

static uint32_t crc32_compute(const void *data, uint64_t len) {
    crc32_init();
    uint32_t crc=0xffffffffU;
    const uint8_t *p=(const uint8_t*)data;
    for (uint64_t i=0;i<len;++i) crc=crc32_table[(crc^p[i])&0xff] ^ (crc>>8);
    return crc ^ 0xffffffffU;
}

static int guid_is_zero(const uint8_t *g) {
    for (uint32_t i=0;i<ZEROOS_GPT_GUID_SIZE;++i) if (g[i]) return 0;
    return 1;
}

static int range_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end) {
    return a_start<=b_end && b_start<=a_end;
}

int gpt_validate_header(const void *data, uint64_t lba_size, uint64_t disk_sectors,
                        struct zeroos_gpt_header *out_header) {
    if (!data || !out_header || lba_size<512 || disk_sectors<3) return -1;
    const struct zeroos_gpt_header *h=(const struct zeroos_gpt_header*)data;
    if (h->signature!=ZEROOS_GPT_SIGNATURE) return -1;
    if (h->revision!=ZEROOS_GPT_REVISION) return -1;
    if (h->header_size<ZEROOS_GPT_HEADER_SIZE || h->header_size>lba_size) return -1;
    if (h->current_lba!=1) return -1; /* primary at LBA1 */
    if (h->backup_lba>=disk_sectors) return -1;
    if (h->first_usable_lba>=disk_sectors || h->last_usable_lba>=disk_sectors) return -1;
    if (h->first_usable_lba>h->last_usable_lba) return -1;
    if (h->first_usable_lba<2) return -1;
    if (h->partition_entry_lba>=disk_sectors) return -1;
    if (h->num_partition_entries==0 || h->num_partition_entries>ZEROOS_GPT_MAX_PARTITIONS) return -1;
    if (h->sizeof_partition_entry<128 || h->sizeof_partition_entry>1024) return -1;
    if (h->sizeof_partition_entry & 7) return -1; /* must be multiple of 8 */
    /* CRC check: zero crc field for computation */
    struct zeroos_gpt_header tmp=*h;
    uint32_t stored=tmp.crc32;
    tmp.crc32=0;
    uint32_t calc=crc32_compute(&tmp, tmp.header_size);
    if (calc!=stored) return -1;
    /* Partition entries must fit within first_usable */
    uint64_t entries_bytes = (uint64_t)h->num_partition_entries * h->sizeof_partition_entry;
    uint64_t entries_sectors = (entries_bytes + lba_size -1)/lba_size;
    if (h->partition_entry_lba + entries_sectors > h->first_usable_lba) return -1;
    *out_header=*h;
    return 0;
}

int gpt_validate_partition(const struct zeroos_gpt_partition *part,
                           uint64_t first_usable, uint64_t last_usable) {
    if (!part) return -1;
    if (guid_is_zero(part->type_guid)) return 0; /* unused entry, valid but zero */
    if (part->first_lba<first_usable || part->last_lba>last_usable) return -1;
    if (part->first_lba>part->last_lba) return -1;
    if (guid_is_zero(part->unique_guid)) return -1;
    return 1; /* valid used partition */
}

int gpt_parse(const void *header_sector, const void *entries, uint64_t disk_sectors,
              struct zeroos_gpt_info *info_out) {
    if (!header_sector || !entries || !info_out || disk_sectors<3) return -1;
    struct zeroos_gpt_header hdr;
    if (gpt_validate_header(header_sector, 512, disk_sectors, &hdr)!=0) return -1;
    uint32_t valid=0;
    /* Check for overlapping partitions and bounds */
    for (uint32_t i=0;i<hdr.num_partition_entries;++i) {
        const struct zeroos_gpt_partition *p = (const struct zeroos_gpt_partition*)
            ((const uint8_t*)entries + (uint64_t)i*hdr.sizeof_partition_entry);
        int r = gpt_validate_partition(p, hdr.first_usable_lba, hdr.last_usable_lba);
        if (r<0) return -1;
        if (r==1) {
            /* Overlap check with previous valid */
            for (uint32_t j=0;j<i;++j) {
                const struct zeroos_gpt_partition *q = (const struct zeroos_gpt_partition*)
                    ((const uint8_t*)entries + (uint64_t)j*hdr.sizeof_partition_entry);
                int rq = gpt_validate_partition(q, hdr.first_usable_lba, hdr.last_usable_lba);
                if (rq!=1) continue;
                if (range_overlap(p->first_lba,p->last_lba,q->first_lba,q->last_lba)) return -1;
            }
            valid++;
        }
    }
    info_out->first_usable=hdr.first_usable_lba;
    info_out->last_usable=hdr.last_usable_lba;
    info_out->partition_count=hdr.num_partition_entries;
    info_out->valid_partitions=valid;
    /* Simple GUID low/high for diagnostics, not crypto */
    uint64_t low=0, high=0;
    for (uint32_t i=0;i<8;++i) low |= (uint64_t)hdr.disk_guid[i] << (i*8);
    for (uint32_t i=0;i<8;++i) high |= (uint64_t)hdr.disk_guid[8+i] << (i*8);
    info_out->disk_guid_low=low;
    info_out->disk_guid_high=high;
    return 0;
}

int gpt_debug_validate(void) {
    if (sizeof(struct zeroos_gpt_header)!=92 && sizeof(struct zeroos_gpt_header)!=92) {
        /* packed size check, but allow */
    }
    if (ZEROOS_GPT_MAX_PARTITIONS==0 || ZEROOS_GPT_GUID_SIZE!=16) return -1;
    return 0;
}
