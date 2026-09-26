/* Canonical UI condition contract.  See ui.h. */
#include <zeroos/desktop/ui.h>
#include <zeroos/desktop/i18n.h>

void zd_ui_init(struct zd_ui_surface *u) {
    if (!u)
        return;
    u->condition = ZD_UI_NORMAL;
    u->modes = 0;
    u->transitions = 0;
}

int zd_ui_set_condition(struct zd_ui_surface *u,
                        enum zd_ui_condition c) {
    if (!u)
        return -22;
    if ((int)c < 0 || (int)c >= ZD_UI_CONDITION_COUNT)
        return -22;
    if (u->condition != c) {
        u->condition = c;
        u->transitions++;
    }
    return 0;
}

int zd_ui_set_modes(struct zd_ui_surface *u, uint32_t flags,
                    int enabled) {
    if (!u || (flags & ~(uint32_t)ZD_UI_F_ALL))
        return -22;
    if (enabled)
        u->modes |= flags;
    else
        u->modes &= ~flags;
    return 0;
}

int zd_ui_interactive(enum zd_ui_condition c) {
    switch (c) {
    case ZD_UI_NORMAL:
    case ZD_UI_EMPTY:
    case ZD_UI_OFFLINE:
        return 1;
    case ZD_UI_LOADING:
    case ZD_UI_ERROR:
    case ZD_UI_PERMISSION_DENIED:
    case ZD_UI_LOW_RESOURCE:
    default:
        return 0;
    }
}

const char *zd_ui_condition_key(enum zd_ui_condition c) {
    switch (c) {
    case ZD_UI_NORMAL: return "state.normal";
    case ZD_UI_LOADING: return "state.loading";
    case ZD_UI_EMPTY: return "state.empty";
    case ZD_UI_ERROR: return "state.error";
    case ZD_UI_OFFLINE: return "state.offline";
    case ZD_UI_PERMISSION_DENIED: return "state.permission_denied";
    case ZD_UI_LOW_RESOURCE: return "state.low_resource";
    default: return 0;
    }
}

const char *zd_ui_condition_name(struct zd_i18n *i18n,
                                 enum zd_ui_condition c) {
    const char *key = zd_ui_condition_key(c);
    if (!key || !i18n)
        return 0;
    return zd_i18n_text(i18n, key);
}

enum zd_ui_condition zd_ui_condition_from_rc(int rc) {
    if (rc >= 0)
        return ZD_UI_NORMAL;
    switch (-rc) {
    case 4: /* ZD_EPERM */
        return ZD_UI_PERMISSION_DENIED;
    case 3: /* ZD_ENOENT */
        return ZD_UI_EMPTY;
    case 2: /* ZD_ENOSPC */
    case 10: /* ZD_EOVERFLOW */
        return ZD_UI_LOW_RESOURCE;
    case 5: /* ZD_EBUSY */
    case 9: /* ZD_ESTATE */
        return ZD_UI_LOADING;
    case 6: /* ZD_EAGAIN */
        return ZD_UI_OFFLINE;
    case 7: /* ZD_ECANCELED */
        return ZD_UI_NORMAL;
    default:
        return ZD_UI_ERROR;
    }
}
