#ifndef ZEROOS_CRC_H
#define ZEROOS_CRC_H
#include "types.h"

/* CRC-32 (IEEE 802.3, reflected 0xEDB88320): GPT header/entry checksums.
 * CRC-32C (Castagnoli, reflected 0x82F63B78): ZJFS metadata checksums.
 * Both use the conventional ~0 init and final inversion; *_update() chains
 * over discontiguous buffers when fed the previous result. */
uint32_t crc32_ieee(const void *data, uint64_t length);
uint32_t crc32c(const void *data, uint64_t length);
uint32_t crc32c_update(uint32_t crc, const void *data, uint64_t length);
int crc_self_test(void);

#endif
