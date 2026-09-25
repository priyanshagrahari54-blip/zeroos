/* PE/COFF image header validation (Stage 5 part C — first loader
 * stage).  Validates DOS/PE/COFF/optional headers and the section
 * table against truncation and out-of-bounds fields with explicit
 * diagnostics.  Emulation/API translation happens later on top of
 * this contract.  Host-testable, freestanding-safe. */
#ifndef ZEROOS_COMPAT_PE_H
#define ZEROOS_COMPAT_PE_H

#include <stdint.h>

enum zpe_result {
    ZPE_OK = 0,
    ZPE_BADARG = -1,          /* null/empty input */
    ZPE_TRUNCATED = -2,       /* declared structures run past the end */
    ZPE_BAD_MAGIC = -3,       /* MZ or PE signature wrong */
    ZPE_UNSUPPORTED_MACHINE = -4, /* not x86-64 */
    ZPE_UNSUPPORTED_FORMAT = -5,  /* not PE32+ */
    ZPE_BAD_LAYOUT = -6       /* e_lfanew/entry/sections out of range */
};

struct zpe_info {
    uint32_t image_size;      /* SizeOfImage */
    uint32_t header_size;     /* SizeOfHeaders */
    uint32_t entry_rva;       /* AddressOfEntryPoint */
    uint32_t section_count;
    uint32_t file_size;       /* input size passed in */
};

/* Validate a full in-memory image.  file_size must equal the buffer
 * length; every declared offset is bounds-checked against it. */
int zpe_validate(const uint8_t *image, uint32_t file_size,
                 struct zpe_info *out_info);

const char *zpe_result_str(int result);

#endif /* ZEROOS_COMPAT_PE_H */
