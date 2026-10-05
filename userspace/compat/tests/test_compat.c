/* Windows compatibility core tests — standalone binary, exit-code gated
 * from `make compat-check`.  Mirrors the desktop harness counters. */
#include <stdio.h>
#include <string.h>
#include <zeroos/compat/compat.h>
#include <zeroos/compat/pe.h>

static int checks, failures;
static const char *current = "";

#define CHECK(expr)                                                          \
    do {                                                                     \
        ++checks;                                                            \
        if (!(expr)) {                                                       \
            ++failures;                                                      \
            fprintf(stderr, "FAIL %s:%d [%s] %s\n", __FILE__, __LINE__,      \
                    current, #expr);                                         \
        }                                                                    \
    } while (0)
#define MK_TWO_SECTIONS() do {                                              \
        MK_BASE();                                                          \
        img[0x86]=2;                                                        \
        img[0xD1]=0x30;                                                     \
        img[0x1b8]=0x00; img[0x1b9]=0x01;                                   \
        img[0x1bc]=0x00; img[0x1bd]=0x20;                                   \
    } while (0)

#define RUN(fn)                                                              \
    do {                                                                     \
        current = #fn;                                                       \
        fn();                                                                \
    } while (0)

static int pe_result_known(int result) {
    return result == ZPE_OK || result == ZPE_BADARG ||
           result == ZPE_TRUNCATED || result == ZPE_BAD_MAGIC ||
           result == ZPE_UNSUPPORTED_MACHINE ||
           result == ZPE_UNSUPPORTED_FORMAT || result == ZPE_BAD_LAYOUT;
}

static void test_lifecycle(void) {
    struct zcompat c;
    enum zcompat_app_state st;

    zcompat_init(&c);
    CHECK(zcompat_install(&c, "NoteApp", 12) == ZCOMPAT_OK);
    CHECK(zcompat_install(&c, "NoteApp", 12) == ZCOMPAT_BADSTATE);
    CHECK(zcompat_state(&c, "NoteApp", &st) == ZCOMPAT_OK);
    CHECK(st == ZCOMPAT_APP_INSTALLED);

    /* INSTALLED != RUNNING: starting is a distinct, explicit move. */
    CHECK(zcompat_start(&c, "NoteApp", 4096) == ZCOMPAT_OK);
    CHECK(zcompat_state(&c, "NoteApp", &st) == ZCOMPAT_OK);
    CHECK(st == ZCOMPAT_APP_RUNNING);
    CHECK(zcompat_start(&c, "NoteApp", 1) == ZCOMPAT_BADSTATE);
    CHECK(zcompat_stop(&c, "Mystery") == ZCOMPAT_NOTFOUND);

    /* Stop releases ALL residency: dormant when unused. */
    CHECK(zcompat_stop(&c, "NoteApp") == ZCOMPAT_OK);
    CHECK(zcompat_stop(&c, "NoteApp") == ZCOMPAT_BADSTATE);
    for (uint32_t i = 0; i < ZCOMPAT_MAX_APPS; ++i)
        if (c.apps[i].used)
            CHECK(c.apps[i].resident_bytes == 0);

    /* Crash only from RUNNING; recovery is an explicit start. */
    CHECK(zcompat_start(&c, "NoteApp", 2048) == ZCOMPAT_OK);
    CHECK(zcompat_crash(&c, "NoteApp") == ZCOMPAT_OK);
    CHECK(zcompat_state(&c, "NoteApp", &st) == ZCOMPAT_OK);
    CHECK(st == ZCOMPAT_APP_STOPPED);
    for (uint32_t i = 0; i < ZCOMPAT_MAX_APPS; ++i)
        if (c.apps[i].used)
            CHECK(c.apps[i].resident_bytes == 0);
    CHECK(zcompat_crash(&c, "NoteApp") == ZCOMPAT_BADSTATE);
    CHECK(zcompat_start(&c, "NoteApp", 1024) == ZCOMPAT_OK);

    /* Uninstall refuses while running. */
    CHECK(zcompat_uninstall(&c, "NoteApp") == ZCOMPAT_BADSTATE);
    CHECK(zcompat_stop(&c, "NoteApp") == ZCOMPAT_OK);
    CHECK(zcompat_uninstall(&c, "NoteApp") == ZCOMPAT_OK);
    CHECK(zcompat_state(&c, "NoteApp", &st) == ZCOMPAT_NOTFOUND);
    CHECK(zcompat_install(&c, "", 1) == ZCOMPAT_BADARG);
}

static void test_capacity(void) {
    struct zcompat c;
    char name[16];

    zcompat_init(&c);
    for (uint32_t i = 0; i < ZCOMPAT_MAX_APPS; ++i) {
        name[0] = 'A';
        name[1] = (char)('0' + i);
        name[2] = 0;
        CHECK(zcompat_install(&c, name, 1) == ZCOMPAT_OK);
    }
    CHECK(zcompat_install(&c, "overflow", 1) == ZCOMPAT_NOSPACE);
}

static void test_paths(void) {
    struct zcompat c;
    char out[ZCOMPAT_PATH];

    zcompat_init(&c);
    CHECK(zcompat_add_drive(&c, 'C', "/data/win-c") == ZCOMPAT_OK);
    CHECK(zcompat_add_drive(&c, 'c', "/data/other") == ZCOMPAT_BADSTATE);
    CHECK(zcompat_add_drive(&c, 'D', "relative/path") == ZCOMPAT_BADARG);

    CHECK(zcompat_translate_path(&c, "C:\\Users\\a\\x.txt", out,
                                 sizeof(out)) == ZCOMPAT_OK);
    CHECK(strcmp(out, "/data/win-c/Users/a/x.txt") == 0);
    CHECK(zcompat_translate_path(&c, "c:/docs/y.txt", out,
                                 sizeof(out)) == ZCOMPAT_OK);
    CHECK(strcmp(out, "/data/win-c/docs/y.txt") == 0);

    /* Explicit diagnostics: UNC, unknown drive, NTFS stream. */
    CHECK(zcompat_translate_path(&c, "\\\\srv\\share\\f", out,
                                 sizeof(out)) == ZCOMPAT_UNSUPPORTED);
    CHECK(zcompat_translate_path(&c, "Z:\\f", out, sizeof(out)) ==
          ZCOMPAT_NOTFOUND);
    CHECK(zcompat_translate_path(&c, "C:\\a.txt:stream", out,
                                 sizeof(out)) == ZCOMPAT_UNSUPPORTED);
    CHECK(zcompat_translate_path(&c, "C:", out, sizeof(out)) ==
          ZCOMPAT_BADARG);
    CHECK(c.stats.path_rejected == 4);
    CHECK(c.stats.path_translated == 2);
}

static void test_registry(void) {
    struct zcompat c;
    char out[ZCOMPAT_PATH];
    int writable = 0;
    static const char sz[] = "value";
    static const uint8_t bin[3] = {1, 2, 3};

    zcompat_init(&c);
    CHECK(zcompat_add_hive(&c, "HKLM", "/system/windows/hklm", 0) ==
          ZCOMPAT_OK);
    CHECK(zcompat_add_hive(&c, "HKCU", "/system/windows/hkcu", 1) ==
          ZCOMPAT_OK);
    CHECK(zcompat_add_hive(&c, "HKLM", "/dup", 1) == ZCOMPAT_BADSTATE);

    CHECK(zcompat_reg_resolve(&c, "HKLM\\Software\\Vendor\\App", out,
                              sizeof(out), &writable) == ZCOMPAT_OK);
    CHECK(strcmp(out, "/system/windows/hklm/Software/Vendor/App") == 0);
    CHECK(writable == 0);
    CHECK(zcompat_reg_resolve(&c, "HKCU\\Env", out, sizeof(out),
                              &writable) == ZCOMPAT_OK);
    CHECK(writable == 1);
    /* Unknown hive: explicit unsupported, never silently emulated. */
    CHECK(zcompat_reg_resolve(&c, "HKXX\\Ghost", out, sizeof(out),
                              &writable) == ZCOMPAT_UNSUPPORTED);
    CHECK(c.stats.reg_rejected == 1);

    CHECK(zcompat_reg_validate_value("k", ZCOMPAT_REG_SZ, sz,
                                     sizeof(sz)) == ZCOMPAT_OK);
    CHECK(zcompat_reg_validate_value("k", ZCOMPAT_REG_SZ, "abc", 3) ==
          ZCOMPAT_ERR);
    CHECK(zcompat_reg_validate_value("k", ZCOMPAT_REG_DWORD, bin, 3) ==
          ZCOMPAT_ERR);
    CHECK(zcompat_reg_validate_value("k", ZCOMPAT_REG_DWORD, bin, 4) ==
          ZCOMPAT_OK);
    CHECK(zcompat_reg_validate_value("k", ZCOMPAT_REG_BINARY, bin, 3) ==
          ZCOMPAT_OK);
    CHECK(zcompat_reg_validate_value("k", (enum zcompat_reg_type)99, bin,
                                     3) == ZCOMPAT_UNSUPPORTED);
    CHECK(zcompat_reg_validate_value("", ZCOMPAT_REG_DWORD, bin, 4) ==
          ZCOMPAT_BADARG);
}

static void test_dlls(void) {
    struct zcompat c;
    uint32_t h1 = 0, h2 = 0, rc = 0;

    zcompat_init(&c);
    CHECK(zcompat_dll_load(&c, "USER32.dll", &h1) == ZCOMPAT_OK);
    CHECK(h1 != 0);
    CHECK(zcompat_dll_load(&c, "user32.DLL", &h2) == ZCOMPAT_OK);
    CHECK(h1 == h2); /* case-insensitive singleton */
    CHECK(zcompat_dll_refcount(&c, "User32.dll", &rc) == ZCOMPAT_OK);
    CHECK(rc == 2);
    CHECK(zcompat_dll_unload(&c, "user32.dll") == ZCOMPAT_OK);
    CHECK(zcompat_dll_refcount(&c, "USER32.dll", &rc) == ZCOMPAT_OK);
    CHECK(rc == 1);
    CHECK(zcompat_dll_unload(&c, "USER32.dll") == ZCOMPAT_OK);
    CHECK(zcompat_dll_refcount(&c, "USER32.dll", &rc) == ZCOMPAT_NOTFOUND);
    CHECK(zcompat_dll_unload(&c, "USER32.dll") == ZCOMPAT_NOTFOUND);
    CHECK(c.stats.dll_missing == 1);
    CHECK(zcompat_dll_load(&c, "", &h1) == ZCOMPAT_BADARG);
    CHECK(zcompat_dll_load(&c, "ok.dll", 0) == ZCOMPAT_BADARG);
}


static void test_pe(void) {
    static uint8_t img[1024];
    struct zpe_info info;
    int rc;

    /* minimal valid x86-64 PE32+ skeleton */
#define MK_BASE() do {                                                      \
        memset(img, 0, sizeof(img));                                        \
        img[0]='M'; img[1]='Z';                                             \
        img[0x3c]=0x80;                                                     \
        img[0x80]='P'; img[0x81]='E';                                       \
        img[0x84]=0x64; img[0x85]=0x86;                                     \
        img[0x86]=1;                                                        \
        img[0x94]=0xf0; img[0x95]=0;                                        \
        img[0x98]=0x0b; img[0x99]=0x02;                                     \
        img[0xA8]=0x10; img[0xA9]=0x10;                                    \
        img[0xB8]=0x00; img[0xB9]=0x10; /* SectionAlignment: 0x1000 */       \
        img[0xBC]=0x00; img[0xBD]=0x02; /* FileAlignment: 0x0200 */          \
        img[0xD0]=0x00; img[0xD1]=0x20;                                     \
        img[0xD4]=0x00; img[0xD5]=0x02;                                     \
        img[0x190]=0x00; img[0x191]=0x01;                                   \
        img[0x194]=0x00; img[0x195]=0x10;                                   \
        img[0x198]=0x00; img[0x199]=0x02;                                   \
        img[0x19c]=0x00; img[0x19d]=0x02;                                   \
        img[0x1af]=0x20; /* IMAGE_SCN_MEM_EXECUTE */                        \
    } while (0)

    MK_BASE();
    rc = zpe_validate(img, sizeof(img), &info);
    CHECK(rc == ZPE_OK);
    CHECK(info.image_size == 0x2000);
    CHECK(info.section_count == 1);
    CHECK(info.entry_rva == 0x1010);
    CHECK(info.file_size == sizeof(img));
    CHECK(zpe_result_str(ZPE_OK) != NULL);
    CHECK(zpe_result_str(ZPE_BAD_MAGIC) != NULL);
    CHECK(zpe_result_str(-99) != NULL);

    MK_BASE(); img[0x94]=0x70; img[0x95]=0; img[0x104]=1;
    CHECK(zpe_validate(img, sizeof(img), &info) == ZPE_TRUNCATED);
    MK_BASE(); img[0x104]=1; /* one RVA data-directory entry */
    img[0x108]=0x00; img[0x109]=0x11; /* RVA 0x1100 */
    img[0x10c]=0x10; /* in-range directory is accepted */
    CHECK(zpe_validate(img, sizeof(img), &info) == ZPE_OK);
    MK_BASE(); img[0x104]=1;
    img[0x108]=0xf0; img[0x109]=0x1f; /* RVA 0x1ff0 */
    img[0x10c]=0x20; /* range crosses SizeOfImage */
    CHECK(zpe_validate(img, sizeof(img), &info) == ZPE_BAD_LAYOUT);
    MK_BASE(); img[0x104]=5; /* SECURITY directory is a file offset */
    img[0x128]=0; img[0x129]=4; img[0x12c]=0; img[0x12d]=2;
    CHECK(zpe_validate(img, sizeof(img), &info) == ZPE_BAD_LAYOUT);
    MK_BASE(); img[0x104]=5;
    img[0x128]=1; img[0x129]=2; /* security file offset not 8-byte aligned */
    img[0x12c]=0x20;
    CHECK(zpe_validate(img, sizeof(img), &info) == ZPE_BAD_LAYOUT);

    MK_BASE(); img[0xB8]=0; img[0xB9]=0; /* zero SectionAlignment */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0xBC]=0; img[0xBD]=3; /* FileAlignment not a power of 2 */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0xB9]=1; img[0xBD]=1; /* permitted sub-page alignment */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_OK);
    MK_BASE(); img[0xB9]=1; /* sub-page SectionAlignment requires same file */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0xD0]=1; /* SizeOfImage not SectionAlignment-aligned */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0xD4]=1; /* SizeOfHeaders not FileAlignment-aligned */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0x194]=1; /* section RVA not SectionAlignment-aligned */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0x19c]=1; /* raw pointer not FileAlignment-aligned */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0x198]=1; /* raw size not FileAlignment-aligned */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);

    CHECK(zpe_validate(NULL, 512, &info) == ZPE_BADARG);
    CHECK(zpe_validate(img, 10, &info) == ZPE_TRUNCATED);

    MK_BASE(); img[1]='X';
    CHECK(zpe_validate(img, 512, &info) == ZPE_BAD_MAGIC);

    MK_BASE(); img[0x3c]=0x00; img[0x3d]=0x10; /* e_lfanew > file */
    CHECK(zpe_validate(img, 512, &info) == ZPE_BAD_LAYOUT);
    MK_BASE(); img[0x3c]=0x10;                 /* e_lfanew < 64 */
    CHECK(zpe_validate(img, 512, &info) == ZPE_BAD_LAYOUT);

    MK_BASE(); img[0x81]='X';                   /* PE signature */
    CHECK(zpe_validate(img, 512, &info) == ZPE_BAD_MAGIC);

    MK_BASE(); img[0x84]=0x4c; img[0x85]=0x01;  /* i386 machine */
    CHECK(zpe_validate(img, 512, &info) == ZPE_UNSUPPORTED_MACHINE);

    MK_BASE(); img[0x98]=0x0b; img[0x99]=0x01;  /* PE32 not PE32+ */
    CHECK(zpe_validate(img, 512, &info) == ZPE_UNSUPPORTED_FORMAT);

    /* Declared optional header does not contain SizeOfImage/Headers. */
    MK_BASE(); img[0x94]=24; img[0x95]=0;
    CHECK(zpe_validate(img, 512, &info) == ZPE_TRUNCATED);

    MK_BASE(); img[0x86]=40;                    /* section table overrun */
    CHECK(zpe_validate(img, 512, &info) == ZPE_TRUNCATED);

    MK_BASE(); img[0x86]=0;                     /* zero sections */
    CHECK(zpe_validate(img, 512, &info) == ZPE_BAD_LAYOUT);

    MK_BASE(); img[0xA8]=0x00; img[0xA9]=0x20;  /* entry > image size */
    CHECK(zpe_validate(img, 512, &info) == ZPE_BAD_LAYOUT);

    MK_BASE(); img[0xA8]=0x10; img[0xA9]=0; /* entry points into headers */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0xA8]=0; img[0xA9]=0x04; /* entry points into unmapped gap */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0xA8]=0xff; img[0xA9]=0x11; /* final byte in section */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_OK);
    MK_BASE(); img[0xA8]=0; img[0xA9]=0x12; /* first byte past section end */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0x1af]=0; /* entry section is not executable */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_BASE(); img[0xA8]=0; img[0xA9]=0; /* DLL without an entry point */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_OK);

    MK_BASE(); img[0xD4]=0x00; img[0xD5]=0x10;  /* headers > file */
    CHECK(zpe_validate(img, 512, &info) == ZPE_BAD_LAYOUT);

    MK_BASE(); img[0x94]=0xff; img[0x95]=0xff;  /* opt hdr beyond file */
    CHECK(zpe_validate(img, 512, &info) == ZPE_TRUNCATED);

    MK_BASE(); img[0x194]=0x00; img[0x195]=0x20; /* section VA outside image */
    CHECK(zpe_validate(img,512,&info)==ZPE_BAD_LAYOUT);

    MK_BASE(); img[0x19c]=0x00; img[0x19d]=0x10; /* raw range exceeds file */
    CHECK(zpe_validate(img,512,&info)==ZPE_BAD_LAYOUT);

    MK_BASE(); img[0x194]=0x00; img[0x195]=0x00; /* overlaps headers */
    CHECK(zpe_validate(img,512,&info)==ZPE_BAD_LAYOUT);

    MK_BASE();
    img[0xB8]=0; img[0xB9]=1; /* small, matching section/file alignment */
    img[0xBC]=0; img[0xBD]=1;
    img[0xD4]=0; img[0xD5]=1; /* headers do not include section table */
    CHECK(zpe_validate(img,512,&info)==ZPE_BAD_LAYOUT);

    MK_TWO_SECTIONS();
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_OK);
    MK_TWO_SECTIONS();
    img[0x1bc]=0x00; img[0x1bd]=0x10; /* virtual ranges overlap */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);
    MK_TWO_SECTIONS();
    img[0x1c0]=0; img[0x1c1]=0x02; /* second raw size is aligned */
    img[0x1c4]=0; img[0x1c5]=0x02; /* raw ranges overlap at 0x200 */
    CHECK(zpe_validate(img,sizeof(img),&info)==ZPE_BAD_LAYOUT);

    /* Exhaust every declared truncation boundary with an allocation exactly
     * as long as file_size; this turns logical over-reads into ASan errors. */
    MK_BASE();
    for (uint32_t length = 0; length <= sizeof(img); ++length) {
        uint8_t exact[length ? length : 1U];
        if (length)
            memcpy(exact, img, length);
        rc = zpe_validate(exact, length, &info);
        CHECK(pe_result_known(rc));
    }

    /* Deterministic one-bit mutation sweep through the DOS/PE headers,
     * section table, and trailing bytes. The validator must reject malformed
     * layouts using a documented code and never read outside the image. */
    MK_BASE();
    for (uint32_t byte = 0; byte < sizeof(img); ++byte) {
        uint8_t original = img[byte];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            img[byte] = (uint8_t)(original ^ (uint8_t)(1U << bit));
            rc = zpe_validate(img, sizeof(img), &info);
            CHECK(pe_result_known(rc));
        }
        img[byte] = original;
    }
#undef MK_BASE
#undef MK_TWO_SECTIONS
}

int main(void) {
    RUN(test_lifecycle);
    RUN(test_capacity);
    RUN(test_paths);
    RUN(test_registry);
    RUN(test_dlls);
    RUN(test_pe);
    printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
