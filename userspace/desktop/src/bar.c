/* ZERO Bar core.  See bar.h for the contract. */
#include <zeroos/desktop/bar.h>

static int b_bad_int(void) { return -22; }

static void b_copy(char *d, uint32_t cap, const char *s) {
    uint32_t i = 0;
    if (!d || !cap)
        return;
    if (!s) {
        d[0] = 0;
        return;
    }
    while (s[i] && i + 1 < cap) {
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
}

int zd_bar_init(struct zd_bar *b, uint32_t logical_width, uint32_t dpi_scale) {
    int i;
    if (!b || !logical_width || !dpi_scale)
        return b_bad_int();
    for (i = 0; i < (int)ZD_BAR_APPLET_COUNT; ++i) {
        b->applets[i].x = 0;
        b->applets[i].w = 0;
    }
    b->width_logical = logical_width;
    b->dpi_scale = dpi_scale;
    b->ops.toggle = 0;
    b->ops.ctx = 0;
    b->focus_idx = 0;
    for (i = 0; i < (int)ZD_BAR_TOGGLE_COUNT; ++i) {
        b->toggle_on[i] = 0;
        b->toggle_denied[i] = 0;
    }
    b->dnd_active = 0;
    b->notif_count = 0;
    b->workspace = 0;
    b->workspace_count = 1;
    b->title[0] = 0;
    b->stats.opens = b->stats.toggles = 0;
    b->stats.denied_toggles = b->stats.focus_moves = 0;
    return 0;
}

void zd_bar_set_ops(struct zd_bar *b, const struct zd_bar_ops *ops) {
    if (!b)
        return;
    if (ops) {
        b->ops = *ops;
    } else {
        b->ops.toggle = 0;
        b->ops.ctx = 0;
    }
}

int zd_bar_layout(struct zd_bar *b) {
    /* Fixed logical widths (100% reference); title is flexible. */
    static const uint32_t base_w[ZD_BAR_APPLET_COUNT] = {
        40, 96, 240, 32, 96, 160
    };
    const uint32_t others = (40 + 96 + 32 + 96 + 160);
    int i;
    uint32_t x = 0;
    if (!b)
        return b_bad_int();
    for (i = 0; i < (int)ZD_BAR_APPLET_COUNT; ++i) {
        uint32_t w = base_w[i];
        if (i == ZD_BAR_APPLET_TITLE) {
            if (b->width_logical > others)
                w = b->width_logical - others;
            else
                w = 0; /* width too small: title collapses, no overflow */
        }
        b->applets[i].x = x;
        b->applets[i].w = w;
        x += w;
    }
    return 0;
}

uint32_t zd_bar_physical_width(const struct zd_bar *b) {
    if (!b)
        return 0;
    return b->width_logical * b->dpi_scale;
}

static int b_hit(const struct zd_bar *b, uint32_t x, int *idx_out) {
    int i;
    for (i = 0; i < (int)ZD_BAR_APPLET_COUNT; ++i) {
        uint32_t s = b->applets[i].x, e = s + b->applets[i].w;
        if (b->applets[i].w && x >= s && x < e) {
            *idx_out = i;
            return 0;
        }
    }
    return -2; /* ENOENT: gap (zero-width applets never hit) */
}

static int b_action_for(const struct zd_bar *b, int idx, int *action,
                        int *arg) {
    switch (idx) {
    case ZD_BAR_APPLET_MENU:
        *action = ZD_BAR_ACT_OPEN_LAUNCHER;
        *arg = 0;
        return 0;
    case ZD_BAR_APPLET_WORKSPACES:
        *action = ZD_BAR_ACT_SWITCH_WORKSPACE;
        *arg = (int)((b->workspace + 1) % (b->workspace_count ? b->workspace_count : 1));
        return 0;
    case ZD_BAR_APPLET_NOTIFICATIONS:
        *action = ZD_BAR_ACT_OPEN_NOTIFICATIONS;
        *arg = 0;
        return 0;
    case ZD_BAR_APPLET_CLOCK:
        *action = ZD_BAR_ACT_OPEN_CLOCK;
        *arg = 0;
        return 0;
    case ZD_BAR_APPLET_QUICK:
        *action = ZD_BAR_ACT_TOGGLE;
        *arg = ZD_BAR_TOGGLE_WIFI; /* default: first toggle; the shell
                                    * refines by hit-testing inside */
        return 0;
    case ZD_BAR_APPLET_TITLE:
    default:
        /* clicking the title = workspace overview fallback */
        *action = ZD_BAR_ACT_SWITCH_WORKSPACE;
        *arg = (int)b->workspace;
        return 0;
    }
}

int zd_bar_click(struct zd_bar *b, uint32_t x, int *action_out, int *arg_out) {
    int idx, r;
    if (!b || !action_out || !arg_out)
        return b_bad_int();
    if (b_hit(b, x, &idx) < 0) {
        *action_out = ZD_BAR_ACT_NONE;
        *arg_out = 0;
        return -2;
    }
    r = b_action_for(b, idx, action_out, arg_out);
    if (r == 0 && idx == ZD_BAR_APPLET_MENU)
        b->stats.opens++;
    return r;
}

int zd_bar_focus_next(struct zd_bar *b) {
    if (!b)
        return b_bad_int();
    b->focus_idx = (b->focus_idx + 1) % (uint32_t)ZD_BAR_APPLET_COUNT;
    b->stats.focus_moves++;
    return 0;
}

int zd_bar_activate_focused(struct zd_bar *b, int *action_out, int *arg_out) {
    if (!b || !action_out || !arg_out)
        return b_bad_int();
    return b_action_for(b, (int)b->focus_idx, action_out, arg_out);
}

int zd_bar_toggle_set(struct zd_bar *b, int toggle_id, int on) {
    int r;
    if (!b || toggle_id < 0 || toggle_id >= (int)ZD_BAR_TOGGLE_COUNT)
        return b_bad_int();
    on = on ? 1 : 0;
    if (b->ops.toggle) {
        r = b->ops.toggle(b->ops.ctx, toggle_id, on);
        if (r < 0) {
            b->toggle_denied[toggle_id] = 1;
            b->stats.denied_toggles++;
            return r;
        }
    }
    b->toggle_denied[toggle_id] = 0;
    b->toggle_on[toggle_id] = (uint8_t)on;
    b->stats.toggles++;
    if (toggle_id == (int)ZD_BAR_TOGGLE_DND)
        b->dnd_active = (uint8_t)on;
    return 0;
}

int zd_bar_set_workspace(struct zd_bar *b, uint32_t idx) {
    if (!b)
        return b_bad_int();
    if (idx >= b->workspace_count || !b->workspace_count)
        return -22;
    b->workspace = idx;
    return 0;
}

void zd_bar_set_title(struct zd_bar *b, const char *title) {
    if (b)
        b_copy(b->title, sizeof(b->title), title);
}

void zd_bar_set_notif_count(struct zd_bar *b, uint32_t n) {
    if (b)
        b->notif_count = n;
}
