/* PE header validator.  See pe.h. */
#include <zeroos/compat/pe.h>

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int zpe_validate(const uint8_t *image, uint32_t file_size,
                 struct zpe_info *out_info) {
    uint32_t lfanew, pe_off, opt_off, sect_off, opt_size;
    uint16_t machine, nsec, opt_magic;
    uint32_t entry, img_size, hdr_size;

    if (!image)
        return ZPE_BADARG;
    if (file_size < 64) /* DOS header minimum */
        return ZPE_TRUNCATED;
    if (image[0] != 'M' || image[1] != 'Z')
        return ZPE_BAD_MAGIC;
    lfanew = rd32(image + 0x3c);
    if (lfanew < 64 || lfanew > file_size)
        return ZPE_BAD_LAYOUT;
    pe_off = lfanew;
    if ((uint64_t)pe_off + 4 + 20 + 2 > file_size)
        return ZPE_TRUNCATED;
    if (image[pe_off] != 'P' || image[pe_off + 1] != 'E' ||
        image[pe_off + 2] != 0 || image[pe_off + 3] != 0)
        return ZPE_BAD_MAGIC;

    machine = rd16(image + pe_off + 4);
    if (machine != 0x8664) /* IMAGE_FILE_MACHINE_AMD64 */
        return ZPE_UNSUPPORTED_MACHINE;
    nsec = rd16(image + pe_off + 6);
    opt_size = rd16(image + pe_off + 20);
    opt_off = pe_off + 24;
    if ((uint64_t)opt_off + opt_size > file_size)
        return ZPE_TRUNCATED;
    if (opt_size < 24)
        return ZPE_TRUNCATED;
    opt_magic = rd16(image + opt_off);
    if (opt_magic != 0x20b) /* PE32+ */
        return ZPE_UNSUPPORTED_FORMAT;

    entry = rd32(image + opt_off + 16);
    img_size = rd32(image + opt_off + 56);
    hdr_size = rd32(image + opt_off + 60);
    if (nsec == 0 || nsec > 96) /* PE limit */
        return ZPE_BAD_LAYOUT;
    sect_off = opt_off + opt_size;
    if ((uint64_t)sect_off + (uint64_t)nsec * 40 > file_size)
        return ZPE_TRUNCATED;
    if (hdr_size == 0 || hdr_size > file_size || hdr_size > img_size)
        return ZPE_BAD_LAYOUT;
    if (img_size == 0)
        return ZPE_BAD_LAYOUT;
    if (entry >= img_size)
        return ZPE_BAD_LAYOUT;

    if (out_info) {
        out_info->image_size = img_size;
        out_info->header_size = hdr_size;
        out_info->entry_rva = entry;
        out_info->section_count = nsec;
        out_info->file_size = file_size;
    }
    return ZPE_OK;
}

const char *zpe_result_str(int result) {
    switch (result) {
    case ZPE_OK: return "ok";
    case ZPE_BADARG: return "bad argument";
    case ZPE_TRUNCATED: return "truncated image";
    case ZPE_BAD_MAGIC: return "bad MZ/PE magic";
    case ZPE_UNSUPPORTED_MACHINE: return "machine not x86-64";
    case ZPE_UNSUPPORTED_FORMAT: return "not PE32+";
    case ZPE_BAD_LAYOUT: return "header layout out of range";
    default: return "unknown";
    }
}
