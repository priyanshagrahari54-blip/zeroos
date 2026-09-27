#include <zeroos/compat/compat.h>

/* Minimal freestanding string helpers (no libc dependency). */
static uint32_t zc_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n])
        ++n;
    return n;
}

static void zc_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!cap)
        return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int zc_eq_ci(const char *a, const char *b) {
    uint32_t i = 0;
    for (;;) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = (char)(y - 'A' + 'a');
        if (x != y)
            return 0;
        if (!x)
            return 1;
        ++i;
    }
}

static int zc_starts_ci(const char *s, const char *prefix) {
    uint32_t i = 0;
    for (;;) {
        char x = s[i], y = prefix[i];
        if (y >= 'A' && y <= 'Z')
            y = (char)(y - 'A' + 'a');
        if (!y)
            return 1; /* prefix exhausted: s may continue (key path) */
        if (x >= 'A' && x <= 'Z')
            x = (char)(x - 'A' + 'a');
        if (x != y || !x)
            return 0;
        ++i;
    }
}

static struct zcompat_app *app_find(struct zcompat *c, const char *name) {
    uint32_t i;
    if (!c || !name)
        return 0;
    for (i = 0; i < ZCOMPAT_MAX_APPS; ++i)
        if (c->apps[i].used && zc_eq_ci(c->apps[i].name, name))
            return &c->apps[i];
    return 0;
}

void zcompat_init(struct zcompat *c) {
    uint32_t i;
    if (!c)
        return;
    for (i = 0; i < sizeof(*c); ++i)
        ((uint8_t *)c)[i] = 0;
}

int zcompat_add_drive(struct zcompat *c, char letter, const char *mount) {
    uint32_t i;
    if (!c || !mount || !mount[0] || mount[0] != '/')
        return ZCOMPAT_BADARG;
    if (letter >= 'a' && letter <= 'z')
        letter = (char)(letter - 'a' + 'A');
    if (letter < 'A' || letter > 'Z')
        return ZCOMPAT_BADARG;
    for (i = 0; i < ZCOMPAT_MAX_DRIVES; ++i) {
        if (c->drives[i].used && c->drives[i].letter == letter)
            return ZCOMPAT_BADSTATE; /* deliberate: one mount per drive */
        if (!c->drives[i].used) {
            c->drives[i].letter = letter;
            zc_copy(c->drives[i].mount, mount, ZCOMPAT_PATH);
            c->drives[i].used = 1;
            return ZCOMPAT_OK;
        }
    }
    return ZCOMPAT_NOSPACE;
}

int zcompat_add_hive(struct zcompat *c, const char *hive,
                     const char *scope, int writable) {
    uint32_t i;
    if (!c || !hive || !hive[0] || !scope || !scope[0])
        return ZCOMPAT_BADARG;
    for (i = 0; i < ZCOMPAT_MAX_HIVES; ++i) {
        if (c->hives[i].used && zc_eq_ci(c->hives[i].hive, hive))
            return ZCOMPAT_BADSTATE;
        if (!c->hives[i].used) {
            zc_copy(c->hives[i].hive, hive, sizeof(c->hives[i].hive));
            zc_copy(c->hives[i].scope, scope, ZCOMPAT_PATH);
            c->hives[i].writable = writable ? 1 : 0;
            c->hives[i].used = 1;
            return ZCOMPAT_OK;
        }
    }
    return ZCOMPAT_NOSPACE;
}

/* --- lifecycle --------------------------------------------------------- */

int zcompat_install(struct zcompat *c, const char *name, uint32_t files) {
    uint32_t i;

    if (!c || !name || !name[0] || !files)
        return ZCOMPAT_BADARG;
    if (app_find(c, name))
        return ZCOMPAT_BADSTATE; /* already installed */
    if (zc_len(name) >= ZCOMPAT_NAME)
        return ZCOMPAT_BADARG;
    for (i = 0; i < ZCOMPAT_MAX_APPS; ++i) {
        if (!c->apps[i].used) {
            zc_copy(c->apps[i].name, name, ZCOMPAT_NAME);
            c->apps[i].state = ZCOMPAT_APP_INSTALLED;
            c->apps[i].install_files = files;
            c->apps[i].resident_bytes = 0;
            c->apps[i].used = 1;
            c->stats.installed++;
            return ZCOMPAT_OK;
        }
    }
    return ZCOMPAT_NOSPACE;
}

int zcompat_start(struct zcompat *c, const char *name,
                  uint32_t resident_bytes) {
    struct zcompat_app *a = app_find(c, name);

    if (!c || !a)
        return ZCOMPAT_NOTFOUND;
    if (a->state == ZCOMPAT_APP_RUNNING)
        return ZCOMPAT_BADSTATE; /* INSTALLED != RUNNING, twice: error */
    if (a->state == ZCOMPAT_APP_INSTALLING)
        return ZCOMPAT_BADSTATE;
    a->state = ZCOMPAT_APP_RUNNING;
    a->resident_bytes = resident_bytes;
    c->stats.started++;
    return ZCOMPAT_OK;
}

int zcompat_stop(struct zcompat *c, const char *name) {
    struct zcompat_app *a = app_find(c, name);

    if (!c || !a)
        return ZCOMPAT_NOTFOUND;
    if (a->state != ZCOMPAT_APP_RUNNING)
        return ZCOMPAT_BADSTATE;
    a->state = ZCOMPAT_APP_STOPPED;
    a->resident_bytes = 0; /* dormant when unused: no residue */
    c->stats.stopped++;
    return ZCOMPAT_OK;
}

int zcompat_crash(struct zcompat *c, const char *name) {
    struct zcompat_app *a = app_find(c, name);

    if (!c || !a)
        return ZCOMPAT_NOTFOUND;
    if (a->state != ZCOMPAT_APP_RUNNING)
        return ZCOMPAT_BADSTATE;
    a->state = ZCOMPAT_APP_STOPPED;
    a->resident_bytes = 0;
    a->crashes++;
    c->stats.crashed++;
    return ZCOMPAT_OK; /* recovery = explicit start, never auto */
}

int zcompat_uninstall(struct zcompat *c, const char *name) {
    struct zcompat_app *a = app_find(c, name);

    if (!c || !a)
        return ZCOMPAT_NOTFOUND;
    if (a->state == ZCOMPAT_APP_RUNNING)
        return ZCOMPAT_BADSTATE; /* must stop first */
    a->used = 0;
    a->name[0] = 0;
    a->state = ZCOMPAT_APP_INSTALLING;
    a->install_files = 0;
    a->resident_bytes = 0;
    return ZCOMPAT_OK;
}

int zcompat_state(const struct zcompat *c, const char *name,
                  enum zcompat_app_state *out) {
    struct zcompat_app *a;

    if (!c || !out)
        return ZCOMPAT_BADARG;
    a = app_find((struct zcompat *)c, name); /* state is a pure read */
    if (!a)
        return ZCOMPAT_NOTFOUND;
    *out = a->state;
    return ZCOMPAT_OK;
}

/* --- paths -------------------------------------------------------------- */

int zcompat_translate_path(struct zcompat *c, const char *win_path,
                           char *out, uint32_t out_cap) {
    char letter;
    uint32_t i, mount_len, src;
    const struct zcompat_drive *d = 0;

    if (!c || !win_path || !out || out_cap < 2)
        return ZCOMPAT_BADARG;
    if (win_path[0] == '\\' && win_path[1] == '\\') {
        c->stats.path_rejected++; /* cast away const for counters */
        return ZCOMPAT_UNSUPPORTED; /* UNC: explicit diagnostic */
    }
    if (!win_path[0] || !win_path[1] || win_path[1] != ':') {
        c->stats.path_rejected++;
        return ZCOMPAT_BADARG;
    }
    letter = win_path[0];
    if (letter >= 'a' && letter <= 'z')
        letter = (char)(letter - 'a' + 'A');
    for (i = 0; i < ZCOMPAT_MAX_DRIVES; ++i)
        if (c->drives[i].used && c->drives[i].letter == letter)
            d = &c->drives[i];
    if (!d) {
        c->stats.path_rejected++;
        return ZCOMPAT_NOTFOUND;
    }
    /* NTFS alternate stream: "name:stream" after the drive colon —
     * reject when a bare colon appears in the remainder. */
    {
        const char *rest = win_path + 2;
        if (rest[0] == ':' ) {
            c->stats.path_rejected++;
            return ZCOMPAT_UNSUPPORTED;
        }
        for (i = 0; rest[i]; ++i)
            if (rest[i] == ':') {
                c->stats.path_rejected++;
                return ZCOMPAT_UNSUPPORTED;
            }
    }
    if (!win_path[2]) {
        c->stats.path_rejected++;
        return ZCOMPAT_BADARG; /* bare drive ("C:") has no path */
    }
    mount_len = zc_len(d->mount);
    src = 2;
    if (win_path[src] == '\\' || win_path[src] == '/')
        ++src;
    if (!win_path[src]) {
        c->stats.path_rejected++;
        return ZCOMPAT_BADARG;
    }
    if (mount_len + 1 + zc_len(win_path + src) + 1 > out_cap) {
        c->stats.path_rejected++;
        return ZCOMPAT_NOSPACE;
    }
    for (i = 0; i < mount_len; ++i)
        out[i] = d->mount[i];
    out[i++] = '/'; /* separator between mount and the translated tail */
    for (; win_path[src]; ++src)
        out[i++] = (win_path[src] == '\\') ? '/' : win_path[src];
    out[i] = 0;
    c->stats.path_translated++;
    return ZCOMPAT_OK;
}

/* --- registry ----------------------------------------------------------- */

int zcompat_reg_resolve(struct zcompat *c, const char *key,
                        char *out, uint32_t out_cap, int *writable_out) {
    uint32_t i, hive_len, k;
    const struct zcompat_hive *h = 0;

    if (!c || !key || !out || out_cap < 2)
        return ZCOMPAT_BADARG;
    for (i = 0; i < ZCOMPAT_MAX_HIVES; ++i)
        if (c->hives[i].used && zc_starts_ci(key, c->hives[i].hive)) {
            hive_len = zc_len(c->hives[i].hive);
            /* Boundary: hive token must end at '\' or end. */
            if (key[hive_len] == 0 || key[hive_len] == '\\') {
                h = &c->hives[i];
                break;
            }
        }
    if (!h) {
        c->stats.reg_rejected++;
        return ZCOMPAT_UNSUPPORTED;
    }
    hive_len = zc_len(h->hive);
    {
        const char *rest = key + hive_len;
        uint32_t total = zc_len(h->scope) + zc_len(rest);
        if (total + 1 > out_cap)
            return ZCOMPAT_NOSPACE;
        for (k = 0; k < zc_len(h->scope); ++k)
            out[k] = h->scope[k];
        i = k;
        for (k = 0; rest[k]; ++k) {
            char ch = rest[k];
            out[i++] = (ch == '\\') ? '/' : ch;
        }
        out[i] = 0;
    }
    if (writable_out)
        *writable_out = h->writable;
    c->stats.reg_reads++;
    return ZCOMPAT_OK;
}

int zcompat_reg_validate_value(const char *name,
                               enum zcompat_reg_type type,
                               const void *data, uint32_t len) {
    if (!name || !name[0] || !data)
        return ZCOMPAT_BADARG;
    switch (type) {
    case ZCOMPAT_REG_SZ: {
        const char *s = (const char *)data;
        if (len < 2 || s[len - 1] != 0)
            return ZCOMPAT_ERR; /* REG_SZ must be NUL-terminated */
        return ZCOMPAT_OK;
    }
    case ZCOMPAT_REG_DWORD:
        return len == 4 ? ZCOMPAT_OK : ZCOMPAT_ERR;
    case ZCOMPAT_REG_BINARY:
        return len > 0 && len <= 4096 ? ZCOMPAT_OK : ZCOMPAT_ERR;
    default:
        return ZCOMPAT_UNSUPPORTED;
    }
}

/* --- DLLs ---------------------------------------------------------------- */

int zcompat_dll_load(struct zcompat *c, const char *name, uint32_t *handle) {
    uint32_t i;

    if (!c || !name || !name[0] || !handle)
        return ZCOMPAT_BADARG;
    if (zc_len(name) >= ZCOMPAT_NAME)
        return ZCOMPAT_BADARG;
    for (i = 0; i < ZCOMPAT_MAX_DLLS; ++i) {
        if (c->dlls[i].loaded && zc_eq_ci(c->dlls[i].name, name)) {
            c->dlls[i].refcount++;
            *handle = i + 1;
            c->stats.dll_loads++;
            return ZCOMPAT_OK;
        }
    }
    for (i = 0; i < ZCOMPAT_MAX_DLLS; ++i) {
        if (!c->dlls[i].loaded) {
            zc_copy(c->dlls[i].name, name, ZCOMPAT_NAME);
            c->dlls[i].refcount = 1;
            c->dlls[i].loaded = 1;
            *handle = i + 1;
            c->stats.dll_loads++;
            return ZCOMPAT_OK;
        }
    }
    c->stats.dll_missing++; /* table exhaustion reported, not silent */
    return ZCOMPAT_NOSPACE;
}

int zcompat_dll_unload(struct zcompat *c, const char *name) {
    uint32_t i;

    if (!c || !name)
        return ZCOMPAT_BADARG;
    for (i = 0; i < ZCOMPAT_MAX_DLLS; ++i) {
        if (c->dlls[i].loaded && zc_eq_ci(c->dlls[i].name, name)) {
            if (c->dlls[i].refcount == 0)
                return ZCOMPAT_ERR;
            if (--c->dlls[i].refcount == 0) {
                c->dlls[i].loaded = 0;
                c->dlls[i].name[0] = 0;
            }
            c->stats.dll_unloads++;
            return ZCOMPAT_OK;
        }
    }
    c->stats.dll_missing++;
    return ZCOMPAT_NOTFOUND;
}

int zcompat_dll_refcount(const struct zcompat *c, const char *name,
                         uint32_t *out) {
    uint32_t i;

    if (!c || !name || !out)
        return ZCOMPAT_BADARG;
    for (i = 0; i < ZCOMPAT_MAX_DLLS; ++i)
        if (c->dlls[i].loaded && zc_eq_ci(c->dlls[i].name, name)) {
            *out = c->dlls[i].refcount;
            return ZCOMPAT_OK;
        }
    return ZCOMPAT_NOTFOUND;
}
