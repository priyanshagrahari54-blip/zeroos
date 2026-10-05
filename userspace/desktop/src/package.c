#include <zeroos/desktop/package.h>

static int zp_eq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a && b && a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return (!a && !b) || (a && b && a[i] == 0 && b[i] == 0);
}

static int zp_memeq(const uint8_t *a, const uint8_t *b, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

void zd_pkg_init(struct zd_pkg_manager *pm) {
    if (!pm)
        return;
    for (uint32_t i = 0; i < sizeof(*pm); ++i)
        ((uint8_t *)pm)[i] = 0;
}

int zd_pkg_repo_add(struct zd_pkg_manager *pm, const struct zd_pkg_record *pkg) {
    if (!pm || !pkg || !pkg->name[0] || !pkg->version[0])
        return -22;

    for (uint32_t i = 0; i < ZD_PKG_MAX; ++i) {
        if (pm->repo[i].state != ZD_PKG_EMPTY && zp_eq(pm->repo[i].name, pkg->name)) {
            pm->repo[i] = *pkg;
            pm->repo[i].state = ZD_PKG_AVAILABLE;
            return 0;
        }
    }

    for (uint32_t i = 0; i < ZD_PKG_MAX; ++i) {
        if (pm->repo[i].state == ZD_PKG_EMPTY) {
            pm->repo[i] = *pkg;
            pm->repo[i].state = ZD_PKG_AVAILABLE;
            return 0;
        }
    }
    return -28; /* ENOSPC */
}

const struct zd_pkg_record *zd_pkg_find_installed(const struct zd_pkg_manager *pm, const char *name) {
    if (!pm || !name)
        return 0;
    for (uint32_t i = 0; i < ZD_PKG_MAX; ++i) {
        if (pm->installed[i].state == ZD_PKG_INSTALLED && zp_eq(pm->installed[i].name, name))
            return &pm->installed[i];
    }
    return 0;
}

const struct zd_pkg_record *zd_pkg_find_repo(const struct zd_pkg_manager *pm, const char *name) {
    if (!pm || !name)
        return 0;
    for (uint32_t i = 0; i < ZD_PKG_MAX; ++i) {
        if (pm->repo[i].state != ZD_PKG_EMPTY && zp_eq(pm->repo[i].name, name))
            return &pm->repo[i];
    }
    return 0;
}

int zd_pkg_resolve_deps(const struct zd_pkg_manager *pm, const struct zd_pkg_record *pkg) {
    if (!pm || !pkg)
        return -22;
    for (uint32_t d = 0; d < pkg->dep_count; ++d) {
        const struct zd_pkg_dep *dep = &pkg->deps[d];
        const struct zd_pkg_record *inst = zd_pkg_find_installed(pm, dep->name);
        if (!inst || inst->version_code < dep->min_version_code) {
            return -2; /* ENOENT / unmet dep */
        }
    }
    return 0;
}

int zd_pkg_install(struct zd_pkg_manager *pm, const char *name, const uint8_t *expected_hash) {
    if (!pm || !name)
        return -22;
    const struct zd_pkg_record *repo_pkg = zd_pkg_find_repo(pm, name);
    if (!repo_pkg)
        return -2; /* not found in repo */
    if (zd_pkg_find_installed(pm, name))
        return -16; /* EBUSY / already installed */

    /* Verify signature / digest */
    if (expected_hash && !zp_memeq(repo_pkg->hash, expected_hash, ZD_PKG_HASH_LEN)) {
        pm->stats.verification_failures++;
        pm->stats.quarantined_count++;
        /* Mark in repo as quarantined */
        for (uint32_t i = 0; i < ZD_PKG_MAX; ++i) {
            if (pm->repo[i].state == ZD_PKG_AVAILABLE && zp_eq(pm->repo[i].name, name)) {
                pm->repo[i].quarantined = 1;
                pm->repo[i].state = ZD_PKG_QUARANTINED;
                break;
            }
        }
        return -3; /* EPERM / verification failed */
    }

    /* Check dependencies */
    if (zd_pkg_resolve_deps(pm, repo_pkg) != 0) {
        pm->stats.dep_failures++;
        return -2;
    }

    /* Find empty install slot */
    for (uint32_t i = 0; i < ZD_PKG_MAX; ++i) {
        if (pm->installed[i].state == ZD_PKG_EMPTY) {
            pm->installed[i] = *repo_pkg;
            pm->installed[i].state = ZD_PKG_INSTALLED;
            pm->installed[i].verified = 1;
            pm->total_installed_bytes += repo_pkg->installed_bytes;
            pm->stats.installed++;
            return 0;
        }
    }
    return -28; /* ENOSPC */
}

int zd_pkg_upgrade(struct zd_pkg_manager *pm, const char *name, const uint8_t *expected_hash) {
    if (!pm || !name)
        return -22;
    struct zd_pkg_record *inst = 0;
    for (uint32_t i = 0; i < ZD_PKG_MAX; ++i) {
        if (pm->installed[i].state == ZD_PKG_INSTALLED && zp_eq(pm->installed[i].name, name)) {
            inst = &pm->installed[i];
            break;
        }
    }
    if (!inst)
        return -2; /* not installed */

    const struct zd_pkg_record *repo_pkg = zd_pkg_find_repo(pm, name);
    if (!repo_pkg)
        return -2;
    if (repo_pkg->version_code <= inst->version_code)
        return -1; /* not an upgrade */

    /* Verify digest */
    if (expected_hash && !zp_memeq(repo_pkg->hash, expected_hash, ZD_PKG_HASH_LEN)) {
        pm->stats.verification_failures++;
        pm->stats.quarantined_count++;
        return -3;
    }

    if (zd_pkg_resolve_deps(pm, repo_pkg) != 0) {
        pm->stats.dep_failures++;
        return -2;
    }

    /* Atomic upgrade */
    if (pm->total_installed_bytes >= inst->installed_bytes)
        pm->total_installed_bytes -= inst->installed_bytes;
    pm->total_installed_bytes += repo_pkg->installed_bytes;
    *inst = *repo_pkg;
    inst->state = ZD_PKG_INSTALLED;
    inst->verified = 1;
    pm->stats.upgraded++;
    return 0;
}

int zd_pkg_remove(struct zd_pkg_manager *pm, const char *name) {
    if (!pm || !name)
        return -22;

    /* Check if other installed packages depend on this package */
    for (uint32_t i = 0; i < ZD_PKG_MAX; ++i) {
        if (pm->installed[i].state == ZD_PKG_INSTALLED && !zp_eq(pm->installed[i].name, name)) {
            for (uint32_t d = 0; d < pm->installed[i].dep_count; ++d) {
                if (zp_eq(pm->installed[i].deps[d].name, name)) {
                    return -16; /* EBUSY: dependent package exists */
                }
            }
        }
    }

    for (uint32_t i = 0; i < ZD_PKG_MAX; ++i) {
        if (pm->installed[i].state == ZD_PKG_INSTALLED && zp_eq(pm->installed[i].name, name)) {
            if (pm->total_installed_bytes >= pm->installed[i].installed_bytes)
                pm->total_installed_bytes -= pm->installed[i].installed_bytes;
            pm->installed[i].state = ZD_PKG_EMPTY;
            pm->stats.uninstalled++;
            return 0;
        }
    }
    return -2; /* not found */
}

int zd_pkg_rollback(struct zd_pkg_manager *pm, const char *name) {
    if (!pm || !name)
        return -22;
    pm->stats.rollbacks++;
    return 0;
}
