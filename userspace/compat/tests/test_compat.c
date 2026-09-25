/* Windows compatibility core tests — standalone binary, exit-code gated
 * from `make compat-check`.  Mirrors the desktop harness counters. */
#include <stdio.h>
#include <string.h>
#include <zeroos/compat/compat.h>

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

#define RUN(fn)                                                              \
    do {                                                                     \
        current = #fn;                                                       \
        fn();                                                                \
    } while (0)

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

int main(void) {
    RUN(test_lifecycle);
    RUN(test_capacity);
    RUN(test_paths);
    RUN(test_registry);
    RUN(test_dlls);
    printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
