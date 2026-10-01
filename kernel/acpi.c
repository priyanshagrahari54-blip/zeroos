#include "acpi.h"

#define MULTIBOOT_TAG_TYPE_END 0U
#define MULTIBOOT_TAG_TYPE_ACPI_OLD 14U
#define MULTIBOOT_TAG_TYPE_ACPI_NEW 15U

#define BOOT_IDENTITY_LIMIT (1ULL << 30)
#define BOOT_INFO_MAX (16ULL * 1024ULL * 1024ULL)
#define ACPI_HEADER_SIZE 36U
#define ACPI_RSDP_V1_SIZE 20U
#define ACPI_RSDP_V2_MIN_SIZE 36U
#define ACPI_TABLE_MAX (1ULL * 1024ULL * 1024ULL)

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_rsdp_v1 {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct acpi_rsdp_v2 {
    struct acpi_rsdp_v1 v1;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

static struct acpi_info state;

static int physical_range_valid(uint64_t address, uint64_t length) {
    return length!=0 && address<BOOT_IDENTITY_LIMIT &&
           length<=BOOT_IDENTITY_LIMIT-address;
}

static int signature_is(const char *actual, const char *expected) {
    for (uint32_t i=0; i<4; ++i)
        if (actual[i]!=expected[i])
            return 0;
    return 1;
}

static int checksum_valid(const void *address, uint64_t length) {
    const uint8_t *bytes=(const uint8_t *)address;
    uint8_t sum=0;
    for (uint64_t i=0; i<length; ++i)
        sum=(uint8_t)(sum+bytes[i]);
    return sum==0;
}

static int rsdp_valid(const uint8_t *candidate, uint32_t available,
                      uint32_t *length_out) {
    const struct acpi_rsdp_v1 *v1=(const struct acpi_rsdp_v1 *)candidate;
    uint32_t length=ACPI_RSDP_V1_SIZE;

    if (available<ACPI_RSDP_V1_SIZE ||
        v1->signature[0]!='R' || v1->signature[1]!='S' ||
        v1->signature[2]!='D' || v1->signature[3]!=' ' ||
        v1->signature[4]!='P' || v1->signature[5]!='T' ||
        v1->signature[6]!='R' || v1->signature[7]!=' ' ||
        !checksum_valid(candidate,ACPI_RSDP_V1_SIZE))
        return 0;

    if (v1->revision>=2) {
        if (available<ACPI_RSDP_V2_MIN_SIZE)
            return 0;
        const struct acpi_rsdp_v2 *v2=(const struct acpi_rsdp_v2 *)candidate;
        length=v2->length;
        if (length<ACPI_RSDP_V2_MIN_SIZE || length>ACPI_TABLE_MAX ||
            length>available || !checksum_valid(candidate,length))
            return 0;
    }

    *length_out=length;
    return 1;
}

static int table_valid(uint64_t physical, const char *signature,
                       const struct acpi_sdt_header **header_out) {
    if (!physical_range_valid(physical,ACPI_HEADER_SIZE))
        return 0;

    const struct acpi_sdt_header *header=
        (const struct acpi_sdt_header *)(uint64_t)physical;
    if (header->length<ACPI_HEADER_SIZE ||
        header->length>ACPI_TABLE_MAX ||
        !physical_range_valid(physical,header->length) ||
        !checksum_valid(header,header->length) ||
        (signature && !signature_is(header->signature,signature)))
        return 0;

    if (header_out)
        *header_out=header;
    return 1;
}

/* Locate a table by signature. Every root entry must be a valid table:
 * a malformed root fails closed rather than skipping entries. */
static int find_table(uint64_t root_physical, uint8_t xsdt,
                      const char *signature, uint64_t *table_out) {
    const struct acpi_sdt_header *root;
    if (!table_valid(root_physical,xsdt ? "XSDT" : "RSDT",&root))
        return 0;

    uint64_t entry_bytes=xsdt ? 8ULL : 4ULL;
    uint64_t payload=root->length-ACPI_HEADER_SIZE;
    if (payload%entry_bytes!=0)
        return 0;

    const uint8_t *entries=(const uint8_t *)root+ACPI_HEADER_SIZE;
    uint64_t count=payload/entry_bytes;
    for (uint64_t i=0; i<count; ++i) {
        uint64_t table_physical;
        if (xsdt)
            table_physical=*(const uint64_t *)(entries+i*8ULL);
        else
            table_physical=*(const uint32_t *)(entries+i*4ULL);

        const struct acpi_sdt_header *header;
        if (!table_valid(table_physical,0,&header))
            return 0;
        if (signature_is(header->signature,signature)) {
            *table_out=table_physical;
            return 1;
        }
    }
    return 0;
}

static int find_madt(uint64_t root_physical, uint8_t xsdt,
                     uint64_t *madt_out) {
    return find_table(root_physical,xsdt,"APIC",madt_out);
}

/* ------------------------------------------------------------- S5 / FADT */

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|
           ((uint32_t)p[3]<<24);
}

static uint64_t read_u64(const uint8_t *p) {
    return (uint64_t)read_u32(p)|((uint64_t)read_u32(p+4)<<32);
}

/* Integer data objects allowed in the S5 package: ZeroOp, OneOp,
 * BytePrefix, WordPrefix and DWordPrefix (ACPI 6.5 §20.2.3). */
static int aml_integer(const uint8_t *aml, uint32_t length, uint32_t *cursor,
                       uint32_t *value) {
    uint32_t p=*cursor;
    if (p>=length)
        return 0;
    switch (aml[p]) {
    case 0x00: *value=0; *cursor=p+1U; return 1;
    case 0x01: *value=1; *cursor=p+1U; return 1;
    case 0x0A:
        if (length-p<2U) return 0;
        *value=aml[p+1U]; *cursor=p+2U; return 1;
    case 0x0B:
        if (length-p<3U) return 0;
        *value=(uint32_t)aml[p+1U]|((uint32_t)aml[p+2U]<<8);
        *cursor=p+3U; return 1;
    case 0x0C:
        if (length-p<5U) return 0;
        *value=read_u32(aml+p+1U); *cursor=p+5U; return 1;
    default:
        return 0;
    }
}

uint8_t acpi_parse_s5(const uint8_t *aml, uint32_t length,
                      uint16_t *slp_typ_a, uint16_t *slp_typ_b) {
    if (!aml || length<5U)
        return ZEROOS_ACPI_S5_NO_OBJECT;
    for (uint32_t i=1; i+4U<length; ++i) {
        if (aml[i]!='_' || aml[i+1U]!='S' || aml[i+2U]!='5' || aml[i+3U]!='_')
            continue;
        /* NameOp directly before the NameSeg, optionally via a root prefix. */
        if (!(aml[i-1U]==0x08 ||
              (i>=2U && aml[i-1U]=='\\' && aml[i-2U]==0x08)))
            continue;
        uint32_t p=i+4U;
        if (aml[p]!=0x12)                        /* PackageOp */
            return ZEROOS_ACPI_S5_BAD_OBJECT;
        ++p;
        if (p>=length)
            return ZEROOS_ACPI_S5_BAD_OBJECT;
        uint32_t follow=(uint32_t)(aml[p]>>6);   /* PkgLength extra bytes */
        uint32_t pkg_length=follow ? (uint32_t)(aml[p]&0x0FU) : (uint32_t)(aml[p]&0x3FU);
        if (length-p<follow+2U)
            return ZEROOS_ACPI_S5_BAD_OBJECT;
        for (uint32_t k=0; k<follow; ++k)
            pkg_length|=(uint32_t)aml[p+1U+k]<<(4U+8U*k);
        uint32_t package_end=p+pkg_length;       /* PkgLength counts itself */
        if (pkg_length<2U || package_end>length)
            return ZEROOS_ACPI_S5_BAD_OBJECT;
        p+=1U+follow;
        uint32_t elements=aml[p++];
        uint32_t a=0, b=0;
        if (elements<1U || !aml_integer(aml,package_end,&p,&a) || a>7U)
            return ZEROOS_ACPI_S5_BAD_OBJECT;
        b=a;
        if (elements>=2U && (!aml_integer(aml,package_end,&p,&b) || b>7U))
            return ZEROOS_ACPI_S5_BAD_OBJECT;
        *slp_typ_a=(uint16_t)a;
        *slp_typ_b=(uint16_t)b;
        return ZEROOS_ACPI_S5_OK;
    }
    return ZEROOS_ACPI_S5_NO_OBJECT;
}

/* Generic Address Structure in system I/O space with a 16-bit port. */
static int gas_io_port(const uint8_t *gas, uint16_t *port) {
    uint64_t address=read_u64(gas+4);
    if (gas[0]!=1U || address==0 || address>0xFFFFULL)
        return 0;
    *port=(uint16_t)address;
    return 1;
}

static void parse_power(uint64_t root_physical, uint8_t xsdt) {
    uint64_t fadt_physical=0;
    const struct acpi_sdt_header *fadt;
    state.s5_error=ZEROOS_ACPI_S5_NO_FADT;
    if (!find_table(root_physical,xsdt,"FACP",&fadt_physical) ||
        !table_valid(fadt_physical,"FACP",&fadt) || fadt->length<116U)
        return;
    state.fadt_physical=fadt_physical;
    const uint8_t *f=(const uint8_t *)(uint64_t)fadt_physical;
    uint32_t length=fadt->length;
    uint32_t flags=read_u32(f+112);

    if (length>=129U && (flags&(1U<<10)) && gas_io_port(f+116,&state.reset_port)) {
        state.reset_value=f[128];
        state.reset_supported=1;
    }
    if (flags&(1U<<20)) {                        /* HW_REDUCED_ACPI */
        state.s5_error=ZEROOS_ACPI_S5_HW_REDUCED;
        return;
    }

    uint32_t smi=read_u32(f+48);
    state.smi_command_port=smi<=0xFFFFU ? (uint16_t)smi : 0;
    state.acpi_enable_value=f[52];
    uint32_t pm1a=read_u32(f+64), pm1b=read_u32(f+68);
    uint16_t port=0;
    if (length>=184U && gas_io_port(f+172,&port))
        state.pm1a_control_port=port;
    else if (pm1a && pm1a<=0xFFFFU)
        state.pm1a_control_port=(uint16_t)pm1a;
    if (length>=196U && gas_io_port(f+184,&port))
        state.pm1b_control_port=port;
    else if (pm1b && pm1b<=0xFFFFU)
        state.pm1b_control_port=(uint16_t)pm1b;
    if (!state.pm1a_control_port || f[89]<2U) {  /* PM1_CNT_LEN */
        state.s5_error=ZEROOS_ACPI_S5_NO_PM1A;
        return;
    }

    uint64_t dsdt=read_u32(f+40);
    if (length>=148U && read_u64(f+140))
        dsdt=read_u64(f+140);
    const struct acpi_sdt_header *dsdt_header;
    if (!dsdt || !table_valid(dsdt,"DSDT",&dsdt_header)) {
        state.s5_error=ZEROOS_ACPI_S5_NO_DSDT;
        return;
    }
    state.dsdt_physical=dsdt;
    state.s5_error=acpi_parse_s5((const uint8_t *)(uint64_t)dsdt+ACPI_HEADER_SIZE,
                                 dsdt_header->length-ACPI_HEADER_SIZE,
                                 &state.slp_typ_a,&state.slp_typ_b);
    state.s5_supported=state.s5_error==ZEROOS_ACPI_S5_OK;
}

const char *acpi_s5_error_name(uint8_t error) {
    switch (error) {
    case ZEROOS_ACPI_S5_OK: return "ok";
    case ZEROOS_ACPI_S5_NO_FADT: return "no FADT";
    case ZEROOS_ACPI_S5_HW_REDUCED: return "hardware-reduced ACPI";
    case ZEROOS_ACPI_S5_NO_PM1A: return "no PM1a control block";
    case ZEROOS_ACPI_S5_NO_DSDT: return "no valid DSDT";
    case ZEROOS_ACPI_S5_NO_OBJECT: return "no _S5_ object";
    case ZEROOS_ACPI_S5_BAD_OBJECT: return "malformed _S5_ object";
    default: return "ACPI not discovered";
    }
}

int acpi_s5_self_test(void) {
    /* Name(_S5_, Package(4){0x05, 0x05, Zero, Zero}) */
    static const uint8_t good[]={0x10,0x08,'_','S','5','_',0x12,0x08,0x04,
                                 0x0A,0x05,0x0A,0x05,0x00,0x00};
    /* Name(\_S5_, Package(2){One, Zero}) */
    static const uint8_t rooted[]={0x08,'\\','_','S','5','_',0x12,0x04,0x02,
                                   0x01,0x00};
    /* NameSeg without NameOp (e.g. a method reference) must be ignored. */
    static const uint8_t no_nameop[]={0x70,'_','S','5','_',0x12,0x04,0x02,
                                      0x01,0x00};
    /* PkgLength runs past the table end. */
    static const uint8_t truncated[]={0x08,'_','S','5','_',0x12,0x3F,0x02,
                                      0x0A,0x05};
    /* SLP_TYP wider than 3 bits. */
    static const uint8_t wide[]={0x08,'_','S','5','_',0x12,0x05,0x02,
                                 0x0A,0x09,0x00};
    uint16_t a=0xFFFF, b=0xFFFF;
    if (acpi_parse_s5(good+1,sizeof(good)-1U,&a,&b)!=ZEROOS_ACPI_S5_OK ||
        a!=5U || b!=5U)
        return -1;
    if (acpi_parse_s5(rooted,sizeof(rooted),&a,&b)!=ZEROOS_ACPI_S5_OK ||
        a!=1U || b!=0U)
        return -2;
    if (acpi_parse_s5(no_nameop,sizeof(no_nameop),&a,&b)!=ZEROOS_ACPI_S5_NO_OBJECT)
        return -3;
    if (acpi_parse_s5(truncated,sizeof(truncated),&a,&b)!=ZEROOS_ACPI_S5_BAD_OBJECT)
        return -4;
    if (acpi_parse_s5(wide,sizeof(wide),&a,&b)!=ZEROOS_ACPI_S5_BAD_OBJECT)
        return -5;
    if (acpi_parse_s5(0,0,&a,&b)!=ZEROOS_ACPI_S5_NO_OBJECT)
        return -6;
    return 0;
}

static int parse_madt(uint64_t physical) {
    const struct acpi_sdt_header *header;
    if (!table_valid(physical,"APIC",&header) ||
        header->length<ACPI_HEADER_SIZE+8U)
        return 0;

    const uint8_t *bytes=(const uint8_t *)(uint64_t)physical;
    uint32_t lapic_address=*(const uint32_t *)(bytes+ACPI_HEADER_SIZE);
    state.local_apic_address=lapic_address;
    state.local_apic_flags=*(const uint32_t *)(bytes+ACPI_HEADER_SIZE+4U);

    uint64_t cursor=ACPI_HEADER_SIZE+8U;
    while (cursor<header->length) {
        if (header->length-cursor<2U)
            return 0;
        uint8_t type=bytes[cursor];
        uint8_t length=bytes[cursor+1U];
        if (length<2U || length>header->length-cursor)
            return 0;

        switch (type) {
        case 0: /* Processor Local APIC. */
            if (length<8U || state.processor_count>=ZEROOS_ACPI_MAX_PROCESSORS)
                return 0;
            if (*(const uint32_t *)(bytes+cursor+4U)&1U) {
                state.processors[state.processor_count].processor_uid=bytes[cursor+2U];
                state.processors[state.processor_count].apic_id=bytes[cursor+3U];
                state.processors[state.processor_count].flags=
                    *(const uint32_t *)(bytes+cursor+4U);
                ++state.processor_count;
            }
            break;
        case 1: /* IOAPIC. */
            if (length<12U || state.ioapic_count>=ZEROOS_ACPI_MAX_IOAPICS)
                return 0;
            state.ioapics[state.ioapic_count].id=bytes[cursor+2U];
            state.ioapics[state.ioapic_count].address=
                *(const uint32_t *)(bytes+cursor+4U);
            state.ioapics[state.ioapic_count].gsi_base=
                *(const uint32_t *)(bytes+cursor+8U);
            if (state.ioapic_count==0)
                state.first_ioapic_address=state.ioapics[0].address;
            ++state.ioapic_count;
            break;
        case 2: /* Interrupt source override. */
            if (length<10U ||
                state.interrupt_override_count>=ZEROOS_ACPI_MAX_OVERRIDES)
                return 0;
            state.overrides[state.interrupt_override_count].bus=bytes[cursor+2U];
            state.overrides[state.interrupt_override_count].source=bytes[cursor+3U];
            state.overrides[state.interrupt_override_count].gsi=
                *(const uint32_t *)(bytes+cursor+4U);
            state.overrides[state.interrupt_override_count].flags=
                *(const uint16_t *)(bytes+cursor+8U);
            ++state.interrupt_override_count;
            break;
        case 5: /* Local APIC address override. */
            if (length<12U)
                return 0;
            state.local_apic_address=*(const uint64_t *)(bytes+cursor+4U);
            break;
        default:
            /* NMI and newer MADT records are skipped by length. */
            break;
        }
        cursor+=length;
    }

    return state.processor_count!=0 && state.local_apic_address!=0;
}

static const uint8_t *find_rsdp(uint64_t multiboot_info,
                                uint32_t total_size,
                                uint32_t *available_out) {
    const uint8_t *cursor=(const uint8_t *)(uint64_t)multiboot_info+8U;
    const uint8_t *end=(const uint8_t *)(uint64_t)multiboot_info+total_size;
    const uint8_t *old_candidate=0;
    uint32_t old_available=0;

    while (cursor+sizeof(struct multiboot_tag)<=end) {
        const struct multiboot_tag *tag=(const struct multiboot_tag *)cursor;
        if (tag->size<sizeof(*tag) || cursor+tag->size>end)
            return 0;
        if (tag->type==MULTIBOOT_TAG_TYPE_ACPI_NEW &&
            tag->size>sizeof(*tag)) {
            uint32_t available=tag->size-sizeof(*tag);
            uint32_t rsdp_length;
            if (rsdp_valid(cursor+sizeof(*tag),available,&rsdp_length)) {
                *available_out=available;
                return cursor+sizeof(*tag);
            }
        }
        if (tag->type==MULTIBOOT_TAG_TYPE_ACPI_OLD &&
            tag->size>sizeof(*tag) && !old_candidate) {
            uint32_t available=tag->size-sizeof(*tag);
            uint32_t rsdp_length;
            if (rsdp_valid(cursor+sizeof(*tag),available,&rsdp_length)) {
                old_candidate=cursor+sizeof(*tag);
                old_available=available;
            }
        }
        if (tag->type==MULTIBOOT_TAG_TYPE_END)
            break;
        uint64_t advance=((uint64_t)tag->size+7ULL)&~7ULL;
        if (advance==0 || cursor+advance<cursor || cursor+advance>end)
            return 0;
        cursor+=advance;
    }

    if (old_candidate) {
        *available_out=old_available;
        return old_candidate;
    }
    return 0;
}

int acpi_discover(uint64_t multiboot_info) {
    state=(struct acpi_info){0};
    state.initialized=1;
    state.s5_error=ZEROOS_ACPI_S5_NOT_DISCOVERED;

    if (multiboot_info==0 || !physical_range_valid(multiboot_info,8U)) {
        state.error=ZEROOS_ACPI_ERROR_NO_HANDOFF;
        return 0;
    }

    uint32_t total_size=*(const uint32_t *)(uint64_t)multiboot_info;
    if (total_size<16U || total_size>BOOT_INFO_MAX ||
        !physical_range_valid(multiboot_info,total_size)) {
        state.error=ZEROOS_ACPI_ERROR_BAD_MULTIBOOT;
        return 0;
    }

    uint32_t rsdp_available=0;
    const uint8_t *rsdp=find_rsdp(multiboot_info,total_size,&rsdp_available);
    if (!rsdp) {
        state.error=ZEROOS_ACPI_ERROR_NO_RSDP;
        return 0;
    }

    uint32_t rsdp_length=0;
    if (!rsdp_valid(rsdp,rsdp_available,&rsdp_length)) {
        state.error=ZEROOS_ACPI_ERROR_BAD_RSDP;
        return 0;
    }

    const struct acpi_rsdp_v1 *v1=(const struct acpi_rsdp_v1 *)rsdp;
    state.rsdp_physical=(uint64_t)rsdp;
    state.rsdp_revision=v1->revision;

    uint8_t xsdt=0;
    uint64_t root_physical=v1->rsdt_address;
    if (v1->revision>=2) {
        const struct acpi_rsdp_v2 *v2=(const struct acpi_rsdp_v2 *)rsdp;
        if (v2->xsdt_address!=0) {
            xsdt=1;
            root_physical=v2->xsdt_address;
        }
    }

    uint64_t madt_physical=0;
    if (!find_madt(root_physical,xsdt,&madt_physical)) {
        state.error=ZEROOS_ACPI_ERROR_BAD_ROOT;
        return 0;
    }
    state.madt_physical=madt_physical;
    parse_power(root_physical,xsdt);
    if (!parse_madt(madt_physical)) {
        state.error=ZEROOS_ACPI_ERROR_BAD_MADT;
        return 0;
    }

    state.valid=1;
    state.error=ZEROOS_ACPI_ERROR_NONE;
    return 0;
}

const struct acpi_info *acpi_info(void) {
    return &state;
}
