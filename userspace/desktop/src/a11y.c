#include <zeroos/desktop/a11y.h>

void zd_a11y_init(struct zd_a11y *a11y) {
    if (!a11y)
        return;
    zd_memset(a11y, 0, sizeof(*a11y));
    a11y->next_id = 2; /* 1 is the root */
    a11y->nodes[0].id = ZD_A11Y_ROOT;
    a11y->nodes[0].parent = ZD_A11Y_INVALID;
    a11y->nodes[0].first_child = ZD_A11Y_INVALID;
    a11y->nodes[0].next_sibling = ZD_A11Y_INVALID;
    a11y->nodes[0].role = ZD_ROLE_CONTAINER;
    zd_str_copy(a11y->nodes[0].name, sizeof(a11y->nodes[0].name), "desktop");
    a11y->nodes[0].in_use = 1;
    a11y->focused = ZD_A11Y_INVALID;
}

static struct zd_a11y_node *node_lookup(struct zd_a11y *a11y, uint32_t id) {
    uint32_t index;
    if (!a11y || id == ZD_A11Y_INVALID)
        return (struct zd_a11y_node *)0;
    for (index = 0; index < ZD_A11Y_MAX_NODES; ++index)
        if (a11y->nodes[index].in_use && a11y->nodes[index].id == id)
            return &a11y->nodes[index];
    return (struct zd_a11y_node *)0;
}

const struct zd_a11y_node *zd_a11y_node_const(const struct zd_a11y *a11y,
                                              uint32_t id) {
    return node_lookup((struct zd_a11y *)a11y, id);
}

struct zd_a11y_node *zd_a11y_node(struct zd_a11y *a11y, uint32_t id) {
    return node_lookup(a11y, id);
}

int zd_a11y_create(struct zd_a11y *a11y, uint32_t parent,
                   enum zd_a11y_role role, const char *name, uint32_t states,
                   uint32_t widget, uint32_t *out_id) {
    struct zd_a11y_node *parent_node;
    struct zd_a11y_node *slot = (struct zd_a11y_node *)0;
    uint32_t index;

    if (!a11y || !out_id)
        return -ZD_EINVAL;
    parent_node = node_lookup(a11y, parent);
    if (!parent_node)
        return -ZD_ENOENT;
    for (index = 0; index < ZD_A11Y_MAX_NODES; ++index)
        if (!a11y->nodes[index].in_use) {
            slot = &a11y->nodes[index];
            break;
        }
    if (!slot)
        return -ZD_ENOSPC;
    if (a11y->next_id == 0)
        a11y->next_id = 2;
    zd_memset(slot, 0, sizeof(*slot));
    slot->id = a11y->next_id++;
    slot->parent = parent;
    slot->first_child = ZD_A11Y_INVALID;
    slot->next_sibling = ZD_A11Y_INVALID;
    slot->role = role;
    slot->states = states;
    slot->widget = widget;
    slot->in_use = 1;
    zd_str_copy(slot->name, sizeof(slot->name), name ? name : "");
    if (parent_node->first_child == ZD_A11Y_INVALID) {
        parent_node->first_child = slot->id;
    } else {
        struct zd_a11y_node *cursor =
            node_lookup(a11y, parent_node->first_child);
        while (cursor && cursor->next_sibling != ZD_A11Y_INVALID)
            cursor = node_lookup(a11y, cursor->next_sibling);
        if (!cursor) {
            slot->in_use = 0;
            return -ZD_ESTATE;
        }
        cursor->next_sibling = slot->id;
    }
    ++a11y->stats.created;
    *out_id = slot->id;
    return 0;
}

static void unlink_child(struct zd_a11y *a11y, uint32_t parent,
                         uint32_t child) {
    struct zd_a11y_node *parent_node = node_lookup(a11y, parent);
    struct zd_a11y_node *child_node;
    if (!parent_node)
        return;
    if (parent_node->first_child == child) {
        child_node = node_lookup(a11y, child);
        parent_node->first_child = child_node ?
            child_node->next_sibling : ZD_A11Y_INVALID;
        return;
    }
    {
        struct zd_a11y_node *cursor =
            node_lookup(a11y, parent_node->first_child);
        while (cursor && cursor->next_sibling != ZD_A11Y_INVALID) {
            if (cursor->next_sibling == child) {
                child_node = node_lookup(a11y, child);
                cursor->next_sibling = child_node ?
                    child_node->next_sibling : ZD_A11Y_INVALID;
                return;
            }
            cursor = node_lookup(a11y, cursor->next_sibling);
        }
    }
}

static void destroy_recursive(struct zd_a11y *a11y, uint32_t id) {
    struct zd_a11y_node *node = node_lookup(a11y, id);
    if (!node)
        return;
    while (node && node->first_child != ZD_A11Y_INVALID)
        destroy_recursive(a11y, node->first_child);
    node = node_lookup(a11y, id);
    if (!node)
        return;
    if (a11y->focused == id)
        a11y->focused = ZD_A11Y_INVALID;
    unlink_child(a11y, node->parent, id);
    node->in_use = 0;
    ++a11y->stats.destroyed;
}

int zd_a11y_destroy(struct zd_a11y *a11y, uint32_t id) {
    struct zd_a11y_node *node;
    if (!a11y)
        return -ZD_EINVAL;
    if (id == ZD_A11Y_ROOT)
        return -ZD_EPERM;
    node = node_lookup(a11y, id);
    if (!node)
        return -ZD_ENOENT;
    destroy_recursive(a11y, id);
    return 0;
}

int zd_a11y_set_states(struct zd_a11y *a11y, uint32_t id, uint32_t states) {
    struct zd_a11y_node *node = node_lookup(a11y, id);
    uint32_t previous;
    if (!node)
        return -ZD_ENOENT;
    previous = node->states;
    node->states = states;
    if ((previous & ZD_A11Y_FOCUSED) && !(states & ZD_A11Y_FOCUSED) &&
        a11y->focused == id)
        a11y->focused = ZD_A11Y_INVALID;
    return 0;
}

int zd_a11y_set_name(struct zd_a11y *a11y, uint32_t id, const char *name) {
    struct zd_a11y_node *node = node_lookup(a11y, id);
    if (!node || !name)
        return -ZD_EINVAL;
    zd_str_copy(node->name, sizeof(node->name), name);
    return 0;
}

static int focusable(const struct zd_a11y_node *node) {
    return node && node->in_use &&
           (node->states & ZD_A11Y_FOCUSABLE) &&
           !(node->states & ZD_A11Y_DISABLED) &&
           !(node->states & ZD_A11Y_HIDDEN);
}

/* Iterative preorder (document order) walk. Children are pushed in
 * reverse so the first child pops first; siblings arrive naturally as
 * children of their parent, so nothing is pushed twice. */
static uint32_t collect_order(struct zd_a11y *a11y, uint32_t *order,
                              uint32_t capacity) {
    uint32_t stack[ZD_A11Y_MAX_NODES];
    uint32_t stack_count = 0;
    uint32_t count = 0;

    stack[stack_count++] = ZD_A11Y_ROOT;
    while (stack_count && count < capacity) {
        struct zd_a11y_node *node;
        uint32_t children[ZD_A11Y_MAX_NODES];
        uint32_t child_count = 0;
        uint32_t child;
        uint32_t id = stack[--stack_count];
        node = node_lookup(a11y, id);
        if (!node)
            continue;
        order[count++] = id;
        child = node->first_child;
        while (child != ZD_A11Y_INVALID && child_count < ZD_A11Y_MAX_NODES) {
            struct zd_a11y_node *child_node = node_lookup(a11y, child);
            children[child_count++] = child;
            child = child_node ? child_node->next_sibling : ZD_A11Y_INVALID;
        }
        while (child_count && stack_count < ZD_A11Y_MAX_NODES)
            stack[stack_count++] = children[--child_count];
    }
    return count;
}

static uint32_t focus_step(struct zd_a11y *a11y, int forward) {
    uint32_t order[ZD_A11Y_MAX_NODES];
    uint32_t count = collect_order(a11y, order, ZD_ARRAY_COUNT(order));
    uint32_t position = count;
    uint32_t index;
    uint32_t current = a11y->focused;

    if (count == 0)
        return ZD_A11Y_INVALID;
    for (index = 0; index < count; ++index)
        if (order[index] == current) {
            position = index;
            break;
        }
    if (position == count)
        return ZD_A11Y_INVALID; /* nothing focused yet: use focus_node */

    for (index = 1; index <= count; ++index) {
        uint32_t at;
        if (forward)
            at = (position + index) % count;
        else
            at = (position + count - (index % count)) % count;
        if (focusable(node_lookup(a11y, order[at])))
            return order[at];
    }
    return ZD_A11Y_INVALID;
}

static void focus_apply(struct zd_a11y *a11y, uint32_t target) {
    struct zd_a11y_node *previous = a11y->focused != ZD_A11Y_INVALID ?
        node_lookup(a11y, a11y->focused) : (struct zd_a11y_node *)0;
    struct zd_a11y_node *next_node = node_lookup(a11y, target);
    if (!next_node)
        return;
    if (previous && previous->id != target)
        previous->states &= ~ZD_A11Y_FOCUSED;
    next_node->states |= ZD_A11Y_FOCUSED;
    a11y->focused = target;
    ++a11y->stats.focus_moves;
}

uint32_t zd_a11y_focus_next(struct zd_a11y *a11y) {
    uint32_t target;
    if (!a11y)
        return ZD_A11Y_INVALID;
    if (a11y->focused == ZD_A11Y_INVALID) {
        /* First Tab focuses the first focusable in document order. */
        uint32_t order[ZD_A11Y_MAX_NODES];
        uint32_t count = collect_order(a11y, order, ZD_ARRAY_COUNT(order));
        uint32_t index;
        for (index = 0; index < count; ++index)
            if (focusable(node_lookup(a11y, order[index]))) {
                focus_apply(a11y, order[index]);
                return order[index];
            }
        return ZD_A11Y_INVALID;
    }
    target = focus_step(a11y, 1);
    if (target == ZD_A11Y_INVALID)
        return ZD_A11Y_INVALID;
    focus_apply(a11y, target);
    return target;
}

uint32_t zd_a11y_focus_prev(struct zd_a11y *a11y) {
    uint32_t target;
    if (!a11y || a11y->focused == ZD_A11Y_INVALID)
        return ZD_A11Y_INVALID;
    target = focus_step(a11y, 0);
    if (target == ZD_A11Y_INVALID)
        return ZD_A11Y_INVALID;
    focus_apply(a11y, target);
    return target;
}

uint32_t zd_a11y_focused(const struct zd_a11y *a11y) {
    return a11y ? a11y->focused : ZD_A11Y_INVALID;
}

int zd_a11y_focus_node(struct zd_a11y *a11y, uint32_t id) {
    struct zd_a11y_node *node;
    if (!a11y)
        return -ZD_EINVAL;
    node = node_lookup(a11y, id);
    if (!node)
        return -ZD_ENOENT;
    if (!focusable(node))
        return -ZD_ESTATE;
    if (a11y->focused == id)
        return 0;
    focus_apply(a11y, id);
    return 0;
}

uint32_t zd_a11y_snapshot(const struct zd_a11y *a11y,
                          struct zd_a11y_snapshot_row *out,
                          uint32_t capacity) {
    uint32_t order[ZD_A11Y_MAX_NODES];
    uint32_t count;
    uint32_t written = 0;
    uint32_t index;

    if (!a11y || !out || capacity == 0)
        return 0;
    count = collect_order((struct zd_a11y *)a11y, order,
                          ZD_ARRAY_COUNT(order));
    for (index = 0; index < count && written < capacity; ++index) {
        const struct zd_a11y_node *node =
            zd_a11y_node_const(a11y, order[index]);
        if (!node)
            continue;
        if ((node->states & ZD_A11Y_HIDDEN) && node->id != ZD_A11Y_ROOT)
            continue;
        out[written].id = node->id;
        out[written].role = node->role;
        out[written].states = node->states;
        zd_str_copy(out[written].name, sizeof(out[written].name), node->name);
        ++written;
    }
    return written;
}

int zd_a11y_announce(struct zd_a11y *a11y, uint32_t node, const char *text,
                     enum zd_a11y_urgency urgency) {
    struct zd_a11y_announcement *slot = (struct zd_a11y_announcement *)0;
    uint32_t index;
    if (!a11y || !text || !*text)
        return -ZD_EINVAL;
    if (node != ZD_A11Y_INVALID && !node_lookup(a11y, node))
        return -ZD_ENOENT;
    if (!a11y->profile.screen_reader_enabled)
        return 0; /* no consumer: do not queue dead announcements */

    if ((int)urgency >= ZD_A11Y_URGENCY_HIGH) {
        /* Preempt: drop every pending non-high announcement. */
        for (index = 0; index < ZD_A11Y_MAX_ANNOUNCEMENTS; ++index) {
            struct zd_a11y_announcement *item = &a11y->announcements[index];
            if (item->in_use && (int)item->urgency < ZD_A11Y_URGENCY_HIGH) {
                item->in_use = 0;
                if (a11y->announcement_count)
                    --a11y->announcement_count;
                ++a11y->stats.announcements_dropped;
            }
        }
    }

    if (a11y->announcement_count >= ZD_A11Y_MAX_ANNOUNCEMENTS) {
        /* Queue full: drop the oldest sequence. */
        uint32_t oldest_index = 0;
        uint64_t oldest_sequence = ~0ULL;
        for (index = 0; index < ZD_A11Y_MAX_ANNOUNCEMENTS; ++index)
            if (a11y->announcements[index].in_use &&
                a11y->announcements[index].sequence < oldest_sequence) {
                oldest_sequence = a11y->announcements[index].sequence;
                oldest_index = index;
            }
        a11y->announcements[oldest_index].in_use = 0;
        --a11y->announcement_count;
        ++a11y->stats.announcements_dropped;
    }
    for (index = 0; index < ZD_A11Y_MAX_ANNOUNCEMENTS; ++index)
        if (!a11y->announcements[index].in_use) {
            slot = &a11y->announcements[index];
            break;
        }
    if (!slot)
        return -ZD_ENOSPC;
    slot->in_use = 1;
    slot->node = node;
    slot->urgency = urgency;
    slot->sequence = ++a11y->announcement_sequence;
    zd_str_copy(slot->text, sizeof(slot->text), text);
    ++a11y->announcement_count;
    ++a11y->stats.announcements;
    return 0;
}

int zd_a11y_next_announcement(struct zd_a11y *a11y,
                              struct zd_a11y_announcement *out) {
    uint32_t index;
    struct zd_a11y_announcement *best = (struct zd_a11y_announcement *)0;
    if (!a11y || !out)
        return -ZD_EINVAL;
    for (index = 0; index < ZD_A11Y_MAX_ANNOUNCEMENTS; ++index) {
        struct zd_a11y_announcement *item = &a11y->announcements[index];
        if (!item->in_use)
            continue;
        if (!best || (int)item->urgency > (int)best->urgency ||
            ((int)item->urgency == (int)best->urgency &&
             item->sequence < best->sequence))
            best = item;
    }
    if (!best)
        return -ZD_ENOENT;
    *out = *best;
    best->in_use = 0;
    if (a11y->announcement_count)
        --a11y->announcement_count;
    return 0;
}

void zd_a11y_set_profile(struct zd_a11y *a11y,
                         const struct zd_a11y_profile *profile) {
    if (!a11y || !profile)
        return;
    a11y->profile = *profile;
}

const struct zd_a11y_profile *zd_a11y_profile(const struct zd_a11y *a11y) {
    return a11y ? &a11y->profile : (const struct zd_a11y_profile *)0;
}

int zd_a11y_reduced_motion(const struct zd_a11y *a11y) {
    return a11y && a11y->profile.reduced_motion;
}

int zd_a11y_high_contrast(const struct zd_a11y *a11y) {
    return a11y && a11y->profile.high_contrast;
}
