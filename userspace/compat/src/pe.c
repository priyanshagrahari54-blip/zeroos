/* PE header validator.  See pe.h. */
#include <zeroos/compat/pe.h>

#define ZPE_IMAGE_SCN_MEM_EXECUTE 0x20000000U

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int is_power_of_two(uint32_t value) {
    return value && !(value & (value - 1U));
}

int zpe_validate(const uint8_t *image, uint32_t file_size,
                 struct zpe_info *out_info) {
    uint32_t lfanew, pe_off, opt_off, sect_off, opt_size;
    uint16_t machine, nsec, opt_magic;
    uint32_t entry, img_size, hdr_size, directory_count;
    uint32_t section_alignment, file_alignment;
    int entry_in_executable_section;

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
    /* PE32+ fixed optional header ends with NumberOfRvaAndSizes at byte 111. */
    if (opt_size < 112)
        return ZPE_TRUNCATED;
    opt_magic = rd16(image + opt_off);
    if (opt_magic != 0x20b) /* PE32+ */
        return ZPE_UNSUPPORTED_FORMAT;
    directory_count = rd32(image + opt_off + 108);
    if (directory_count > (opt_size - 112U) / 8U)
        return ZPE_TRUNCATED;

    entry = rd32(image + opt_off + 16);
    section_alignment = rd32(image + opt_off + 32);
    file_alignment = rd32(image + opt_off + 36);
    img_size = rd32(image + opt_off + 56);
    hdr_size = rd32(image + opt_off + 60);
    if (!is_power_of_two(section_alignment) ||
        !is_power_of_two(file_alignment) ||
        section_alignment < file_alignment)
        return ZPE_BAD_LAYOUT;
    if ((section_alignment < 0x1000U &&
         file_alignment != section_alignment) ||
        (section_alignment >= 0x1000U &&
         (file_alignment < 0x200U || file_alignment > 0x10000U)))
        return ZPE_BAD_LAYOUT;
    if (nsec == 0 || nsec > 96) /* PE limit */
        return ZPE_BAD_LAYOUT;
    sect_off = opt_off + opt_size;
    if ((uint64_t)sect_off + (uint64_t)nsec * 40 > file_size)
        return ZPE_TRUNCATED;
    if (hdr_size == 0 || hdr_size > file_size || hdr_size > img_size ||
        hdr_size < sect_off + (uint64_t)nsec * 40)
        return ZPE_BAD_LAYOUT;
    if (img_size == 0 || img_size % section_alignment != 0 ||
        hdr_size % file_alignment != 0)
        return ZPE_BAD_LAYOUT;
    /* Validate every declared directory, with the certificate table's
     * specified file-offset semantics handled separately from RVA entries. */
    for (uint32_t i = 0; i < directory_count; ++i) {
        const uint8_t *directory = image + opt_off + 112U + i * 8U;
        uint32_t address = rd32(directory);
        uint32_t size = rd32(directory + 4);
        if (address == 0 && size == 0)
            continue;
        if (address == 0 || size == 0)
            return ZPE_BAD_LAYOUT;
        if (i == 4U) { /* IMAGE_DIRECTORY_ENTRY_SECURITY uses a file offset. */
            if ((address & 7U) || address > file_size ||
                size > file_size - address)
                return ZPE_BAD_LAYOUT;
        } else if (address > img_size || size > img_size - address) {
            return ZPE_BAD_LAYOUT;
        }
    }
    if (entry >= img_size)
        return ZPE_BAD_LAYOUT;
    /* PE DLLs may omit an entry point (RVA 0). A nonzero entry must
     * resolve to a section mapped executable by the image loader. */
    entry_in_executable_section = entry == 0;
    for (uint32_t i=0; i<nsec; ++i) {
        const uint8_t *section=image+sect_off+(uint64_t)i*40ULL;
        uint32_t virtual_size=rd32(section+8);
        uint32_t virtual_address=rd32(section+12);
        uint32_t raw_size=rd32(section+16);
        uint32_t raw_offset=rd32(section+20);
        uint32_t mapped_size=virtual_size>raw_size ? virtual_size : raw_size;
        uint64_t virtual_end=(uint64_t)virtual_address+mapped_size;
        uint64_t raw_end=(uint64_t)raw_offset+raw_size;
        if (!mapped_size || virtual_address % section_alignment != 0 ||
            virtual_address < hdr_size || virtual_address > img_size ||
            mapped_size > img_size - virtual_address)
            return ZPE_BAD_LAYOUT;
        if (raw_size &&
            (raw_offset % file_alignment != 0 ||
             raw_size % file_alignment != 0 || raw_offset < hdr_size ||
             raw_offset > file_size || raw_size > file_size - raw_offset))
            return ZPE_BAD_LAYOUT;
        if (entry != 0 && entry >= virtual_address && entry < virtual_end &&
            (rd32(section + 36) & ZPE_IMAGE_SCN_MEM_EXECUTE))
            entry_in_executable_section = 1;
        for (uint32_t j=0; j<i; ++j) {
            const uint8_t *prior=image+sect_off+(uint64_t)j*40ULL;
            uint32_t prior_virtual_size=rd32(prior+8);
            uint32_t prior_virtual_address=rd32(prior+12);
            uint32_t prior_raw_size=rd32(prior+16);
            uint32_t prior_raw_offset=rd32(prior+20);
            uint32_t prior_mapped_size=
                prior_virtual_size>prior_raw_size ?
                prior_virtual_size : prior_raw_size;
            uint64_t prior_virtual_end=
                (uint64_t)prior_virtual_address+prior_mapped_size;
            uint64_t prior_raw_end=
                (uint64_t)prior_raw_offset+prior_raw_size;
            if ((virtual_address<prior_virtual_end &&
                 prior_virtual_address<virtual_end) ||
                (raw_size && prior_raw_size &&
                 raw_offset<prior_raw_end && prior_raw_offset<raw_end))
                return ZPE_BAD_LAYOUT;
        }
    }
    if (!entry_in_executable_section)
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
