#include "crc.h"

static uint32_t crc_ieee_table[256];
static uint32_t crc_c_table[256];
static int crc_ready;

static void crc_build(uint32_t *table, uint32_t polynomial) {
    for (uint32_t i=0; i<256; ++i) {
        uint32_t value=i;
        for (int bit=0; bit<8; ++bit)
            value=(value & 1U) ? (value>>1)^polynomial : value>>1;
        table[i]=value;
    }
}

static void crc_init(void) {
    if (crc_ready)
        return;
    crc_build(crc_ieee_table,0xEDB88320U);
    crc_build(crc_c_table,0x82F63B78U);
    __atomic_store_n(&crc_ready,1,__ATOMIC_RELEASE);
}

static uint32_t crc_run(const uint32_t *table, uint32_t crc,
                        const uint8_t *bytes, uint64_t length) {
    for (uint64_t i=0; i<length; ++i)
        crc=table[(crc^bytes[i])&0xffU]^(crc>>8);
    return crc;
}

uint32_t crc32_ieee(const void *data, uint64_t length) {
    crc_init();
    return ~crc_run(crc_ieee_table,~0U,(const uint8_t *)data,length);
}

uint32_t crc32c_update(uint32_t crc, const void *data, uint64_t length) {
    crc_init();
    return ~crc_run(crc_c_table,~crc,(const uint8_t *)data,length);
}

uint32_t crc32c(const void *data, uint64_t length) {
    return crc32c_update(0,data,length);
}

int crc_self_test(void) {
    static const char check[]="123456789";
    /* Standard check values for both polynomials. */
    if (crc32_ieee(check,9)!=0xCBF43926U || crc32c(check,9)!=0xE3069283U)
        return -1;
    /* Chaining must equal the one-shot result. */
    if (crc32c_update(crc32c(check,4),check+4,5)!=0xE3069283U)
        return -1;
    return 0;
}
