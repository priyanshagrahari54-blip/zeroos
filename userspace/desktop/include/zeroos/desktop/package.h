/* ZEROOS Package Management Core (Stage 6 / ARCHITECTURE section 19).
 *
 * Implements package lifecycle, dependency resolution, signature/digest
 * verification, transactional staging, rollback on install failure,
 * quarantine of unverified packages, offline cache, and resource accounting.
 * Zero dynamic heap allocation, fully host-testable.
 */
#ifndef ZEROOS_DESKTOP_PACKAGE_H
#define ZEROOS_DESKTOP_PACKAGE_H

#include <stdint.h>

#define ZD_PKG_MAX 32
#define ZD_PKG_NAME_MAX 32
#define ZD_PKG_VER_MAX 16
#define ZD_PKG_DEPS_MAX 4
#define ZD_PKG_HASH_LEN 32

enum zd_pkg_state {
    ZD_PKG_EMPTY = 0,
    ZD_PKG_AVAILABLE,
    ZD_PKG_STAGED,
    ZD_PKG_INSTALLED,
    ZD_PKG_QUARANTINED
};

struct zd_pkg_dep {
    char name[ZD_PKG_NAME_MAX];
    uint32_t min_version_code;
};

struct zd_pkg_record {
    char name[ZD_PKG_NAME_MAX];
    char version[ZD_PKG_VER_MAX];
    uint32_t version_code;
    uint32_t installed_bytes;
    uint32_t file_count;
    struct zd_pkg_dep deps[ZD_PKG_DEPS_MAX];
    uint32_t dep_count;
    uint8_t hash[ZD_PKG_HASH_LEN];
    int state;               /* enum zd_pkg_state */
    uint8_t verified;
    uint8_t quarantined;
};

struct zd_pkg_manager {
    struct zd_pkg_record repo[ZD_PKG_MAX];
    struct zd_pkg_record installed[ZD_PKG_MAX];
    uint32_t total_installed_bytes;
    uint32_t offline_cache_bytes;
    struct {
        uint32_t installed;
        uint32_t uninstalled;
        uint32_t upgraded;
        uint32_t rollbacks;
        uint32_t verification_failures;
        uint32_t dep_failures;
        uint32_t quarantined_count;
    } stats;
};

void zd_pkg_init(struct zd_pkg_manager *pm);

/* Repository registration */
int zd_pkg_repo_add(struct zd_pkg_manager *pm, const struct zd_pkg_record *pkg);

/* Dependency verification & resolution (checks installed + candidate) */
int zd_pkg_resolve_deps(const struct zd_pkg_manager *pm, const struct zd_pkg_record *pkg);

/* Transactional install: checks signature -> resolves deps -> stages -> commits.
 * On hash mismatch, package is quarantined and install fails closed (-3). */
int zd_pkg_install(struct zd_pkg_manager *pm, const char *name, const uint8_t *expected_hash);

/* Upgrade: validates new version > old version and replaces atomically */
int zd_pkg_upgrade(struct zd_pkg_manager *pm, const char *name, const uint8_t *expected_hash);

/* Removal: refuses if another installed package depends on it */
int zd_pkg_remove(struct zd_pkg_manager *pm, const char *name);

/* Rollback: restores previous version from offline cache */
int zd_pkg_rollback(struct zd_pkg_manager *pm, const char *name);

/* Lookup */
const struct zd_pkg_record *zd_pkg_find_installed(const struct zd_pkg_manager *pm, const char *name);
const struct zd_pkg_record *zd_pkg_find_repo(const struct zd_pkg_manager *pm, const char *name);

#endif /* ZEROOS_DESKTOP_PACKAGE_H */
