/* ZEROOS Package Management Tests (Stage 6) */
#include <zeroos/desktop/package.h>
#include "test_harness.h"

void zd_test_package_suite(void) {
    zd_test_current = "package";
    printf(" suite: package\n");
    struct zd_pkg_manager pm;
    zd_pkg_init(&pm);

    uint8_t hash_lib[32] = {1, 2, 3, 4};
    uint8_t hash_app[32] = {5, 6, 7, 8};
    uint8_t bad_hash[32] = {9, 9, 9, 9};

    struct zd_pkg_record lib = {
        .name = "libzero",
        .version = "1.0.0",
        .version_code = 10,
        .installed_bytes = 1024 * 1024,
        .file_count = 12,
        .dep_count = 0
    };
    for (int i = 0; i < 32; ++i) lib.hash[i] = hash_lib[i];

    struct zd_pkg_record app = {
        .name = "zero-editor",
        .version = "1.0.0",
        .version_code = 100,
        .installed_bytes = 2 * 1024 * 1024,
        .file_count = 24,
        .dep_count = 1,
        .deps = { { .name = "libzero", .min_version_code = 10 } }
    };
    for (int i = 0; i < 32; ++i) app.hash[i] = hash_app[i];

    ZD_CHECK(zd_pkg_repo_add(&pm, &lib) == 0);
    ZD_CHECK(zd_pkg_repo_add(&pm, &app) == 0);

    /* Try to install app before its dependency is installed -> fails */
    ZD_CHECK(zd_pkg_install(&pm, "zero-editor", hash_app) == -2);
    ZD_CHECK(pm.stats.dep_failures == 1);

    /* Tampered package hash fails and triggers quarantine */
    ZD_CHECK(zd_pkg_install(&pm, "libzero", bad_hash) == -3);
    ZD_CHECK(pm.stats.verification_failures == 1);
    ZD_CHECK(pm.stats.quarantined_count == 1);
    const struct zd_pkg_record *q = zd_pkg_find_repo(&pm, "libzero");
    ZD_CHECK(q != 0 && q->quarantined == 1);

    /* Reset lib state to available with valid hash */
    zd_pkg_repo_add(&pm, &lib);

    /* Install dependency with valid hash */
    ZD_CHECK(zd_pkg_install(&pm, "libzero", hash_lib) == 0);
    ZD_CHECK(pm.stats.installed == 1);
    ZD_CHECK(zd_pkg_find_installed(&pm, "libzero") != 0);

    /* Now install app -> succeeds */
    ZD_CHECK(zd_pkg_install(&pm, "zero-editor", hash_app) == 0);
    ZD_CHECK(pm.stats.installed == 2);
    ZD_CHECK(pm.total_installed_bytes == 3 * 1024 * 1024);

    /* Cannot remove libzero while zero-editor depends on it */
    ZD_CHECK(zd_pkg_remove(&pm, "libzero") == -16);

    /* Upgrade zero-editor */
    struct zd_pkg_record app_v2 = app;
    app_v2.version_code = 101;
    app_v2.version[4] = '1'; /* "1.0.1" */
    uint8_t hash_v2[32] = {5, 6, 7, 9};
    for (int i = 0; i < 32; ++i) app_v2.hash[i] = hash_v2[i];
    zd_pkg_repo_add(&pm, &app_v2);

    ZD_CHECK(zd_pkg_upgrade(&pm, "zero-editor", hash_v2) == 0);
    ZD_CHECK(pm.stats.upgraded == 1);
    const struct zd_pkg_record *cur = zd_pkg_find_installed(&pm, "zero-editor");
    ZD_CHECK(cur != 0 && cur->version_code == 101);

    /* Remove zero-editor then libzero cleanly */
    ZD_CHECK(zd_pkg_remove(&pm, "zero-editor") == 0);
    ZD_CHECK(pm.stats.uninstalled == 1);
    ZD_CHECK(zd_pkg_remove(&pm, "libzero") == 0);
    ZD_CHECK(pm.stats.uninstalled == 2);
    ZD_CHECK(pm.total_installed_bytes == 0);
}
