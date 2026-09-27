/* GPT validation matrix on a ramdisk (test-only table builder). */
#include "gpt.h"
#include "ramdisk.h"
#include "storage.h"
#include "../crc.h"
#include "../kstring.h"
#include "../memory.h"

#define TG_CHECK(cond, what) do { if (!(cond)) { \
    klog("ZEROOS: storage gpt test FAILED: %s (line %d).", what, __LINE__); \
    return -1; } } while (0)

#define TG_ENTRIES 128U

static void wr32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void wr64(uint8_t *p, uint64_t v) {
    wr32(p,(uint32_t)v);
    wr32(p+4,(uint32_t)(v>>32));
}

struct tg_part {
    uint64_t first;
    uint64_t last;
    const uint8_t *type;
};

/* Writes PMBR, primary+backup headers and entry arrays straight into the
 * ramdisk image (bypassing the block layer, like a host tool would). */
static void tg_build(struct block_device *disk, const struct tg_part *parts,
                     uint32_t count, uint8_t *entries, uint8_t *sector) {
    uint64_t last=disk->sectors-1ULL;
    uint64_t entry_sectors=TG_ENTRIES*128U/512U;       /* 32 */
    memset(entries,0,TG_ENTRIES*128U);
    for (uint32_t i=0; i<count; ++i) {
        uint8_t *e=entries+i*128U;
        memcpy(e,parts[i].type,16);
        for (uint32_t b=0; b<16; ++b)
            e[16+b]=(uint8_t)(0xa0+i+b);
        wr64(e+32,parts[i].first);
        wr64(e+40,parts[i].last);
        const char *name="zeroos";
        for (uint32_t c=0; name[c]; ++c)
            e[56+c*2]=(uint8_t)name[c];
    }
    uint32_t entries_crc=crc32_ieee(entries,TG_ENTRIES*128U);
    memset(sector,0,512);
    sector[446+4]=0xee;
    wr32(sector+446+8,1);
    wr32(sector+446+12,(uint32_t)(last>0xffffffffULL ? 0xffffffffULL : last));
    sector[510]=0x55;
    sector[511]=0xaa;
    ramdisk_poke(disk,0,sector,1);
    for (int backup=0; backup<2; ++backup) {
        memset(sector,0,512);
        memcpy(sector,"EFI PART",8);
        wr32(sector+8,0x00010000U);
        wr32(sector+12,92);
        wr64(sector+24,backup ? last : 1ULL);
        wr64(sector+32,backup ? 1ULL : last);
        wr64(sector+40,2ULL+entry_sectors);
        wr64(sector+48,last-1ULL-entry_sectors);
        for (uint32_t b=0; b<16; ++b)
            sector[56+b]=(uint8_t)(0x40+b);
        wr64(sector+72,backup ? last-entry_sectors : 2ULL);
        wr32(sector+80,TG_ENTRIES);
        wr32(sector+84,128);
        wr32(sector+88,entries_crc);
        wr32(sector+16,crc32_ieee(sector,92));
        ramdisk_poke(disk,backup ? last : 1ULL,sector,1);
        ramdisk_poke(disk,backup ? last-entry_sectors : 2ULL,entries,
                     (uint32_t)entry_sectors);
    }
}

/* Re-seal a header after editing a field so only the intended check
 * fails. */
static void tg_edit_header(struct block_device *disk, uint64_t lba,
                           uint32_t offset, uint64_t value, int width,
                           int reseal, uint8_t *sector) {
    ramdisk_peek(disk,lba,sector,1);
    if (width==8)
        wr64(sector+offset,value);
    else
        wr32(sector+offset,(uint32_t)value);
    if (reseal) {
        wr32(sector+16,0);
        wr32(sector+16,crc32_ieee(sector,92));
    }
    ramdisk_poke(disk,lba,sector,1);
}

int storage_selftest_gpt(void) {
    struct block_device *disk=ramdisk_create("ramgpt0",8U*1024U*1024U,
                                             BLOCK_MEDIA_SSD,4);
    uint8_t *entries=(uint8_t *)page_alloc_contiguous(5);
    uint8_t *sector=entries+4U*ZEROOS_PAGE_SIZE;
    static struct gpt_result result;
    TG_CHECK(disk && entries,"setup");
    uint64_t last=disk->sectors-1ULL;
    struct tg_part good[2]={
        {2048,4095,gpt_type_zeroos_zjfs},
        {4096,8191,gpt_type_zeroos_scratch}};

    /* 1. valid table */
    tg_build(disk,good,2,entries,sector);
    TG_CHECK(gpt_scan(disk,&result)==0,"valid table accepted");
    TG_CHECK(result.partition_count==2 && !result.used_backup &&
             result.primary_status==GPT_OK && result.backup_status==GPT_OK &&
             result.pmbr_present,"valid table parsed");
    TG_CHECK(result.partitions[0].first_lba==2048 &&
             result.partitions[1].last_lba==8191 &&
             gpt_guid_equal(result.partitions[0].type_guid,gpt_type_zeroos_zjfs),
             "partition fields");

    /* 2. primary header CRC damage -> backup, degraded */
    ramdisk_peek(disk,1,sector,1);
    sector[40]^=0x01;
    ramdisk_poke(disk,1,sector,1);
    TG_CHECK(gpt_scan(disk,&result)==0 && result.used_backup &&
             result.primary_status==GPT_ERR_HEADER_CRC &&
             result.partition_count==2,"backup used on header CRC damage");

    /* 3. primary entry-array damage -> backup */
    tg_build(disk,good,2,entries,sector);
    ramdisk_peek(disk,2,sector,1);
    sector[33]^=0xff;
    ramdisk_poke(disk,2,sector,1);
    TG_CHECK(gpt_scan(disk,&result)==0 && result.used_backup &&
             result.primary_status==GPT_ERR_ENTRY_CRC,"backup used on entry CRC damage");

    /* 4. backup damage only -> primary used, damage reported */
    tg_build(disk,good,2,entries,sector);
    ramdisk_peek(disk,last,sector,1);
    sector[0]='X';
    ramdisk_poke(disk,last,sector,1);
    TG_CHECK(gpt_scan(disk,&result)==0 && !result.used_backup &&
             result.backup_status==GPT_ERR_SIGNATURE,"backup damage reported");

    /* 5. both damaged -> rejected, nothing exposed */
    ramdisk_peek(disk,1,sector,1);
    sector[0]='X';
    ramdisk_poke(disk,1,sector,1);
    TG_CHECK(gpt_scan(disk,&result)==-SE_UCLEAN && result.partition_count==0,
             "both tables damaged rejected");

    /* 6. overlapping partitions (valid CRCs) -> rejected */
    struct tg_part overlap[2]={
        {2048,5000,gpt_type_zeroos_zjfs},
        {4096,8191,gpt_type_zeroos_scratch}};
    tg_build(disk,overlap,2,entries,sector);
    TG_CHECK(gpt_scan(disk,&result)==-SE_UCLEAN &&
             result.primary_status==GPT_ERR_OVERLAP,"overlap rejected");

    /* 7. partition beyond last usable LBA -> rejected */
    struct tg_part beyond[1]={{2048,last,gpt_type_zeroos_zjfs}};
    tg_build(disk,beyond,1,entries,sector);
    TG_CHECK(gpt_scan(disk,&result)==-SE_UCLEAN &&
             result.primary_status==GPT_ERR_ENTRY_RANGE,"out-of-range entry rejected");

    /* 8. first > last within an entry */
    struct tg_part inverted[1]={{4000,3000,gpt_type_zeroos_zjfs}};
    tg_build(disk,inverted,1,entries,sector);
    TG_CHECK(gpt_scan(disk,&result)==-SE_UCLEAN,"inverted entry rejected");

    /* 9. resealed header with hostile geometry: huge entry count (would
     * overflow a naive size computation), bad entry size, wrong MyLBA,
     * usable range overlapping the entry array, oversized header. */
    tg_build(disk,good,2,entries,sector);
    tg_edit_header(disk,1,80,0xffffffffULL,4,1,sector);
    TG_CHECK(gpt_scan(disk,&result)==0 && result.used_backup &&
             result.primary_status==GPT_ERR_ENTRY_GEOMETRY,"entry count bound");
    tg_build(disk,good,2,entries,sector);
    tg_edit_header(disk,1,84,96,4,1,sector);
    TG_CHECK(gpt_scan(disk,&result)==0 &&
             result.primary_status==GPT_ERR_ENTRY_GEOMETRY,"entry size validated");
    tg_build(disk,good,2,entries,sector);
    tg_edit_header(disk,1,24,7,8,1,sector);
    TG_CHECK(gpt_scan(disk,&result)==0 &&
             result.primary_status==GPT_ERR_MY_LBA,"MyLBA validated");
    tg_build(disk,good,2,entries,sector);
    tg_edit_header(disk,1,40,10,8,1,sector);
    TG_CHECK(gpt_scan(disk,&result)==0 &&
             result.primary_status==GPT_ERR_ENTRY_GEOMETRY,"usable range vs entries");
    tg_build(disk,good,2,entries,sector);
    tg_edit_header(disk,1,48,last+5ULL,8,1,sector);
    TG_CHECK(gpt_scan(disk,&result)==0 &&
             result.primary_status==GPT_ERR_USABLE_RANGE,"usable range vs disk");
    tg_build(disk,good,2,entries,sector);
    tg_edit_header(disk,1,12,600,4,0,sector);
    TG_CHECK(gpt_scan(disk,&result)==0 &&
             result.primary_status==GPT_ERR_HEADER_SIZE,"header size validated");

    /* 10. registration exposes bounded, correctly offset partitions */
    tg_build(disk,good,2,entries,sector);
    TG_CHECK(gpt_scan(disk,&result)==0,"rescan");
    TG_CHECK(gpt_register(disk,&result)==2,"partitions registered");
    struct block_device *p1=block_find("ramgpt0p1");
    TG_CHECK(p1 && p1->sectors==2048 && p1->start_lba==2048,"partition view");
    ramdisk_peek(disk,2048,sector,1);
    sector[0]=0x5a;
    ramdisk_poke(disk,2048,sector,1);
    memset(sector,0,512);
    TG_CHECK(block_rw(p1,BLOCK_OP_READ,0,sector,512,BLOCK_PRIO_NORMAL)==0 &&
             sector[0]==0x5a,"partition LBA remap");
    TG_CHECK(block_rw(p1,BLOCK_OP_READ,2048,sector,512,BLOCK_PRIO_NORMAL)==-SE_INVAL,
             "partition bound enforced");
    page_free_contiguous(entries,5);
    return 0;
}
