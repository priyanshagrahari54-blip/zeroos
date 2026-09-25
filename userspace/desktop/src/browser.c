#include <zeroos/desktop/browser.h>

static struct zd_tab *tab_find(struct zd_browser *b, uint32_t id) {
    uint32_t i;
    if (!b || !id)
        return 0;
    for (i = 0; i < ZD_BROWSER_MAX_TABS; ++i)
        if (b->tabs[i].used && b->tabs[i].id == id)
            return &b->tabs[i];
    return 0;
}

void zd_browser_init(struct zd_browser *browser, uint32_t idle_ticks,
                     uint32_t freeze_ticks, uint32_t discard_ticks) {
    uint32_t i;

    if (!browser)
        return;
    for (i = 0; i < sizeof(*browser); ++i)
        ((uint8_t *)browser)[i] = 0;
    /* Callers may pass stage deltas (0) to take the defaults. */
    browser->idle_ticks = idle_ticks ? idle_ticks
                                     : ZD_BROWSER_IDLE_TICKS_DEFAULT;
    browser->freeze_ticks = freeze_ticks ? freeze_ticks
                                         : ZD_BROWSER_FREEZE_TICKS_DEFAULT;
    browser->discard_ticks = discard_ticks ? discard_ticks
                                           : ZD_BROWSER_DISCARD_TICKS_DEFAULT;
    if (browser->freeze_ticks < browser->idle_ticks)
        browser->freeze_ticks = browser->idle_ticks;
    if (browser->discard_ticks < browser->freeze_ticks)
        browser->discard_ticks = browser->freeze_ticks;
}

int zd_browser_open(struct zd_browser *browser, uint32_t content_bytes,
                    uint32_t *id_out) {
    uint32_t i;
    uint32_t next_id = 1;

    if (!browser || !id_out)
        return -ZD_EINVAL;
    if (browser->tab_count >= ZD_BROWSER_MAX_TABS)
        return -ZD_ENOSPC;
    for (i = 0; i < ZD_BROWSER_MAX_TABS; ++i) {
        if (!browser->tabs[i].used) {
            struct zd_tab *t = &browser->tabs[i];
            uint32_t j;
            for (j = 0; j < ZD_BROWSER_MAX_TABS; ++j)
                if (browser->tabs[j].used &&
                    browser->tabs[j].id >= next_id)
                    next_id = browser->tabs[j].id + 1;
            t->id = next_id;
            t->state = ZD_TAB_ACTIVE;
            t->last_focus_tick = browser->now;
            t->content_bytes = content_bytes;
            t->has_document = 1;
            t->used = 1;
            browser->tab_count++;
            browser->stats.opened++;
            *id_out = t->id;
            /* Opening focuses the new tab (single focused tab rule). */
            browser->active_id = t->id;
            return 0;
        }
    }
    return -ZD_ENOSPC; /* unreachable when tab_count is honest */
}

int zd_browser_close(struct zd_browser *browser, uint32_t id) {
    struct zd_tab *t = tab_find(browser, id);

    if (!browser)
        return -ZD_EINVAL;
    if (!t)
        return -ZD_ENOENT;
    if (browser->active_id == id)
        browser->active_id = 0;
    t->used = 0;
    t->id = 0;
    browser->tab_count--;
    browser->stats.closed++;
    return 0;
}

int zd_browser_state(const struct zd_browser *browser, uint32_t id,
                     enum zd_tab_state *state_out) {
    struct zd_tab *t;

    if (!browser || !state_out)
        return -ZD_EINVAL;
    t = tab_find((struct zd_browser *)browser, id);
    if (!t)
        return -ZD_ENOENT;
    *state_out = t->state;
    return 0;
}

/* Tick ladder: from last focus, promote down the ladder while the
 * absolute idle threshold is met.  Focus and reload reset the clock. */
static void tab_advance(struct zd_browser *b, struct zd_tab *t) {
    uint64_t idle_for = b->now - t->last_focus_tick;

    if (t->state == ZD_TAB_CRASHED || t->state == ZD_TAB_DISCARDED)
        return;
    if (idle_for >= b->discard_ticks) {
        if (t->state != ZD_TAB_DISCARDED) {
            if (t->has_document) {
                t->has_document = 0;
                t->content_bytes = 0;
                b->stats.discards++;
            }
            t->state = ZD_TAB_DISCARDED;
        }
        return;
    }
    if (idle_for >= b->freeze_ticks) {
        if (t->state != ZD_TAB_FROZEN) {
            t->state = ZD_TAB_FROZEN;
            b->stats.freezes++;
        }
        return;
    }
    if (idle_for >= b->idle_ticks && t->state == ZD_TAB_ACTIVE) {
        t->state = ZD_TAB_IDLE;
        return;
    }
}

int zd_browser_event(struct zd_browser *browser, uint32_t id,
                     enum zd_tab_event event, uint64_t now) {
    struct zd_tab *t;

    if (!browser)
        return -ZD_EINVAL;
    if (now < browser->now)
        return -ZD_EINVAL; /* clock must be monotonic */
    browser->now = now;
    t = tab_find(browser, id);
    if (!t)
        return -ZD_ENOENT;

    switch (event) {
    case ZD_TAB_EV_TICK:
        tab_advance(browser, t);
        return 0;
    case ZD_TAB_EV_FOCUS:
        if (t->state == ZD_TAB_CRASHED)
            return -ZD_ESTATE; /* must reload first */
        /* Reopening a discarded document triggers a reload cycle. */
        if (t->state == ZD_TAB_DISCARDED) {
            t->has_document = 1;
            t->content_bytes = t->content_bytes ? t->content_bytes : 4096u;
            t->reloads++;
            browser->stats.reloads++;
        }
        t->state = ZD_TAB_ACTIVE;
        t->last_focus_tick = now;
        browser->active_id = id;
        return 0;
    case ZD_TAB_EV_BLUR:
        if (browser->active_id == id)
            browser->active_id = 0;
        /* Blur does not change state by itself; the ladder reacts. */
        return 0;
    case ZD_TAB_EV_RENDERER_CRASH:
        if (t->state == ZD_TAB_CRASHED)
            return -ZD_ESTATE; /* double-crash: needs reload */
        t->state = ZD_TAB_CRASHED;
        t->crashes++;
        browser->stats.crashes++;
        if (browser->active_id == id)
            browser->active_id = 0;
        return 0;
    case ZD_TAB_EV_RELOAD:
        if (t->state != ZD_TAB_CRASHED && t->state != ZD_TAB_DISCARDED)
            return -ZD_EINVAL; /* healthy tabs must not "recover" */
        t->state = ZD_TAB_ACTIVE;
        t->has_document = 1;
        if (!t->content_bytes)
            t->content_bytes = 4096u;
        t->reloads++;
        t->last_focus_tick = now;
        browser->stats.reloads++;
        if (t->crashes)
            browser->stats.crash_recoveries++;
        browser->active_id = id;
        return 0;
    case ZD_TAB_EV_DISCARD_NOW:
        if (t->state == ZD_TAB_CRASHED)
            return -ZD_ESTATE;
        if (t->has_document) {
            t->has_document = 0;
            t->content_bytes = 0;
            browser->stats.discards++;
        }
        t->state = ZD_TAB_DISCARDED;
        return 0;
    default:
        return -ZD_EINVAL;
    }
}
