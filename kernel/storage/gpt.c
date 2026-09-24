#include "gpt.h"
#include "../crc.h"
#include "../kstring.h"
#include "../memory.h"

/* 8F3D2A10-5A4A-4653-9A2E-5A45524F4F53 (on-disk mixed-endian order). */
const uint8_t gpt_type_zeroos_zjfs[16]={
    0x10,0x2a,0x3d,0x8f,0x4a,0x5a,0x53,0x46,
    0x9a,0x2e,0x5a,0x45,0x52,0x4f,0x4f,0x53};
/* 8F3D2A11-5A4A-4653-9A2E-5A45524F4F53 */
const uint8_t gpt_type_zeroos_scratch[16]={
    0x11,0x2a,0x3d,0x8f,0x4a,0x5a,0x53,0x46,
    0x9a,0x2e,0x5a,0x45,0x52,0x4f,0x4f,0x53};

static const char *gpt_error_names[]={
    "ok","io","signature","revision","header-size","header-crc","my-lba",
    "usable-range","entry-geometry","entry-crc","entry-range","overlap",
    "too-many-partitions","no-memory","alternate-lba"};

const char *gpt_error_name(int error) {
    if (error<0 || error>GPT_ERR_ALTERNATE)
        return "unknown";
    return gpt_error_names[error];
}

int gpt_guid_equal(const uint8_t *a, const uint8_t *b) {
    return memcmp(a,b,16)==0;
}

static int gpt_guid_zero(const uint8_t *guid) {
    for (uint32_t i=0; i<16; ++i)
        if (guid[i])
            return 0;
    return 1;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p)|((uint64_t)rd32(p+4)<<32);
}

/*
 * Validate one header at `lba` and its entry array. On success the entry
 * array is left in `entries` (caller-provided, >= GPT_MAX_ENTRIES*128
 * bytes rounded to sectors).
 */
static int gpt_check_header(struct block_device *disk, uint64_t lba,
                            uint8_t *sector, uint8_t *entries,
                            uint64_t *entries_bytes_out, uint8_t *header_copy) {
    uint32_t ss=disk->sector_size;
    uint64_t last=disk->sectors-1ULL;
    if (block_rw(disk,BLOCK_OP_READ,lba,sector,ss,BLOCK_PRIO_NORMAL)!=0)
        return GPT_ERR_IO;
    if (memcmp(sector,"EFI PART",8)!=0)
        return GPT_ERR_SIGNATURE;
    if (rd32(sector+8)!=0x00010000U)
        return GPT_ERR_REVISION;
    uint32_t header_size=rd32(sector+12);
    if (header_size<92U || header_size>ss)
        return GPT_ERR_HEADER_SIZE;
    uint32_t stored_crc=rd32(sector+16);
    memcpy(header_copy,sector,header_size);
    header_copy[16]=header_copy[17]=header_copy[18]=header_copy[19]=0;
    if (crc32_ieee(header_copy,header_size)!=stored_crc)
        return GPT_ERR_HEADER_CRC;
    memcpy(header_copy,sector,header_size);

    uint64_t my_lba=rd64(sector+24);
    uint64_t alternate=rd64(sector+32);
    uint64_t first_usable=rd64(sector+40);
    uint64_t last_usable=rd64(sector+48);
    uint64_t entries_lba=rd64(sector+72);
    uint32_t count=rd32(sector+80);
    uint32_t entry_size=rd32(sector+84);
    uint32_t entries_crc=rd32(sector+88);
    int primary=lba==1ULL;

    if (my_lba!=lba)
        return GPT_ERR_MY_LBA;
    if (primary ? (alternate==0 || alternate>last || alternate==1ULL)
                : (alternate!=1ULL))
        return GPT_ERR_ALTERNATE;
    if (entry_size<128U || entry_size>1024U || (entry_size&(entry_size-1U)) ||
        count==0 || count>GPT_MAX_ENTRIES)
        return GPT_ERR_ENTRY_GEOMETRY;
    uint64_t entries_bytes=(uint64_t)count*entry_size;
    uint64_t entries_sectors=(entries_bytes+ss-1ULL)/ss;
    if (entries_lba>last || entries_sectors>last-entries_lba+1ULL)
        return GPT_ERR_ENTRY_GEOMETRY;
    uint64_t entries_end=entries_lba+entries_sectors-1ULL;
    /* Usable range: inside the disk, clear of both headers and of this
     * header's entry array. */
    if (first_usable>last_usable || last_usable>=last || first_usable<2ULL)
        return GPT_ERR_USABLE_RANGE;
    if (primary) {
        if (entries_lba<2ULL || entries_end>=first_usable)
            return GPT_ERR_ENTRY_GEOMETRY;
    } else {
        if (entries_lba<=last_usable || entries_end>=lba)
            return GPT_ERR_ENTRY_GEOMETRY;
    }
    if (block_rw(disk,BLOCK_OP_READ,entries_lba,entries,entries_sectors*ss,
                 BLOCK_PRIO_NORMAL)!=0)
        return GPT_ERR_IO;
    if (crc32_ieee(entries,entries_bytes)!=entries_crc)
        return GPT_ERR_ENTRY_CRC;
    *entries_bytes_out=entries_bytes;
    return GPT_OK;
}

static int gpt_parse_entries(const uint8_t *header, const uint8_t *entries,
                             struct gpt_result *result) {
    uint64_t first_usable=rd64(header+40);
    uint64_t last_usable=rd64(header+48);
    uint32_t count=rd32(header+80);
    uint32_t entry_size=rd32(header+84);
    result->first_usable=first_usable;
    result->last_usable=last_usable;
    memcpy(result->disk_guid,header+56,16);
    result->partition_count=0;
    for (uint32_t i=0; i<count; ++i) {
        const uint8_t *e=entries+(uint64_t)i*entry_size;
        if (gpt_guid_zero(e))
            continue;
        uint64_t first=rd64(e+32);
        uint64_t last=rd64(e+40);
        if (first>last || first<first_usable || last>last_usable)
            return GPT_ERR_ENTRY_RANGE;
        for (uint32_t j=0; j<result->partition_count; ++j) {
            const struct gpt_partition *o=&result->partitions[j];
            if (!(last<o->first_lba || first>o->last_lba))
                return GPT_ERR_OVERLAP;
        }
        if (result->partition_count>=GPT_MAX_PARTITIONS)
            return GPT_ERR_TOO_MANY;
        struct gpt_partition *p=&result->partitions[result->partition_count++];
        p->number=i+1U;
        p->first_lba=first;
        p->last_lba=last;
        p->attributes=rd64(e+48);
        memcpy(p->type_guid,e,16);
        memcpy(p->unique_guid,e+16,16);
        for (uint32_t c=0; c<36; ++c) {
            uint16_t ch=(uint16_t)(e[56+c*2]|(e[57+c*2]<<8));
            p->name[c]=(ch>=0x20 && ch<0x7f) ? (char)ch : (ch ? '?' : '\0');
            if (!ch)
                break;
        }
        p->name[36]='\0';
    }
    return GPT_OK;
}

int gpt_scan(struct block_device *disk, struct gpt_result *result) {
    uint32_t ss=disk->sector_size;
    uint64_t entry_pages=(GPT_MAX_ENTRIES*1024ULL+ZEROOS_PAGE_SIZE-1ULL)/ZEROOS_PAGE_SIZE;
    /* page 0: sector scratch, page 1: primary header, page 2: backup. */
    uint8_t *sector=(uint8_t *)page_alloc_contiguous(3);
    uint8_t *entries=(uint8_t *)page_alloc_contiguous(entry_pages);
    uint8_t *primary_header=sector+ZEROOS_PAGE_SIZE;
    uint8_t *backup_header=sector+2U*ZEROOS_PAGE_SIZE;
    uint64_t bytes=0;
    memset(result,0,sizeof(*result));
    if (!sector || !entries || ss>4096U || disk->sectors<68ULL) {
        if (sector) page_free_contiguous(sector,3);
        if (entries) page_free_contiguous(entries,entry_pages);
        result->primary_status=result->backup_status=GPT_ERR_NO_MEMORY;
        return -SE_NOMEM;
    }
    if (block_rw(disk,BLOCK_OP_READ,0,sector,ss,BLOCK_PRIO_NORMAL)==0 &&
        sector[510]==0x55 && sector[511]==0xaa) {
        for (uint32_t i=0; i<4; ++i)
            if (sector[446+i*16+4]==0xee)
                result->pmbr_present=1;
    }
    result->primary_status=gpt_check_header(disk,1,sector,entries,&bytes,
                                            primary_header);
    uint64_t backup_lba=disk->sectors-1ULL;
    if (result->primary_status==GPT_OK) {
        uint64_t alternate=rd64(primary_header+32);
        if (alternate!=backup_lba)
            backup_lba=alternate;       /* validated <= last by the check */
        result->primary_status=gpt_parse_entries(primary_header,entries,result);
    }
    /* Always verify the backup so damage is reported, not discovered later. */
    uint8_t *backup_entries=entries;
    int have_primary=result->primary_status==GPT_OK;
    struct gpt_result backup_result;
    if (have_primary) {
        backup_result=*result;
        backup_entries=(uint8_t *)page_alloc_contiguous(entry_pages);
    }
    if (!backup_entries) {
        result->backup_status=GPT_ERR_NO_MEMORY;
    } else {
        uint8_t *bh=sector;             /* reuse scratch sector */
        result->backup_status=gpt_check_header(disk,backup_lba,bh,backup_entries,
                                               &bytes,backup_header);
        if (result->backup_status==GPT_OK) {
            struct gpt_result *target=have_primary ? &backup_result : result;
            int parsed=gpt_parse_entries(backup_header,backup_entries,target);
            result->backup_status=parsed;
            if (parsed==GPT_OK && !have_primary) {
                result->used_backup=1;
            }
        }
        if (have_primary)
            page_free_contiguous(backup_entries,entry_pages);
    }
    int valid=have_primary || result->used_backup;
    klog("ZEROOS: gpt %s: primary=%s backup=%s pmbr=%d source=%s partitions=%u.",
         disk->name,gpt_error_name(result->primary_status),
         gpt_error_name(result->backup_status),result->pmbr_present,
         valid ? (result->used_backup ? "backup(degraded)" : "primary") : "none",
         valid ? result->partition_count : 0U);
    if (!valid)
        result->partition_count=0;
    page_free_contiguous(sector,3);
    page_free_contiguous(entries,entry_pages);
    return valid ? 0 : -SE_UCLEAN;
}

int gpt_register(struct block_device *disk, const struct gpt_result *result) {
    int registered=0;
    for (uint32_t i=0; i<result->partition_count; ++i) {
        const struct gpt_partition *p=&result->partitions[i];
        struct block_device *part=0;
        uint64_t sectors=p->last_lba-p->first_lba+1ULL;
        if (block_add_partition(disk,p->number,p->first_lba,sectors,p->type_guid,
                                p->unique_guid,&part)!=0)
            continue;
        if (p->attributes&(1ULL<<60))
            part->flags|=BLOCK_DEV_READ_ONLY;
        klog("ZEROOS: gpt %s: partition %u \"%s\" lba=%llu..%llu type=%s%s.",
             part->name,p->number,p->name,p->first_lba,p->last_lba,
             gpt_guid_equal(p->type_guid,gpt_type_zeroos_zjfs) ? "zeroos-zjfs" :
             (gpt_guid_equal(p->type_guid,gpt_type_zeroos_scratch) ? "zeroos-scratch" :
              "foreign"),(part->flags&BLOCK_DEV_READ_ONLY) ? " ro" : "");
        ++registered;
    }
    return registered;
}
