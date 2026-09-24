#ifndef ZEROOS_DESKTOP_A11Y_H
#define ZEROOS_DESKTOP_A11Y_H

/* Semantic accessibility tree, keyboard focus traversal, screen-reader
 * announcements and the accessibility profile (high contrast, reduced
 * motion, captions, alternative input) consumed by the shell. */

#include <zeroos/desktop/common.h>
#include <zeroos/desktop/window.h> /* enum zd_a11y_role */

#define ZD_A11Y_MAX_NODES 256
#define ZD_A11Y_NAME_CAP 40
#define ZD_A11Y_TEXT_CAP 64
#define ZD_A11Y_MAX_ANNOUNCEMENTS 16
#define ZD_A11Y_ROOT ((uint32_t)1)
#define ZD_A11Y_INVALID ((uint32_t)0)

enum zd_a11y_state_bit {
    ZD_A11Y_FOCUSABLE = (1U << 0),
    ZD_A11Y_FOCUSED = (1U << 1),
    ZD_A11Y_DISABLED = (1U << 2),
    ZD_A11Y_HIDDEN = (1U << 3),
    ZD_A11Y_CHECKED = (1U << 4),
    ZD_A11Y_EXPANDED = (1U << 5),
    ZD_A11Y_SELECTED = (1U << 6)
};

enum zd_a11y_urgency {
    ZD_A11Y_URGENCY_LOW = 0,
    ZD_A11Y_URGENCY_NORMAL = 1,
    ZD_A11Y_URGENCY_HIGH = 2
};

enum zd_a11y_input_mode {
    ZD_A11Y_INPUT_POINTER = 0,
    ZD_A11Y_INPUT_KEYBOARD = 1,
    ZD_A11Y_INPUT_SWITCH = 2,
    ZD_A11Y_INPUT_VOICE = 3
};

struct zd_a11y_node {
    uint32_t id;
    uint32_t parent;
    uint32_t first_child;
    uint32_t next_sibling;
    enum zd_a11y_role role; /* reuse window.h roles via common include */
    char name[ZD_A11Y_NAME_CAP];
    uint32_t states;
    uint32_t in_use;
    uint32_t widget;        /* optional bound window id */
};

struct zd_a11y_profile {
    uint32_t screen_reader_enabled;
    uint32_t high_contrast;
    uint32_t reduced_motion;
    uint32_t captions_enabled;
    enum zd_a11y_input_mode input_mode;
};

struct zd_a11y_announcement {
    uint32_t node;
    char text[ZD_A11Y_TEXT_CAP];
    enum zd_a11y_urgency urgency;
    uint64_t sequence;
    uint32_t in_use;
};

struct zd_a11y {
    struct zd_a11y_node nodes[ZD_A11Y_MAX_NODES];
    uint32_t next_id;
    uint32_t focused;
    struct zd_a11y_profile profile;
    uint32_t announcement_count;
    uint32_t announcement_head;
    struct zd_a11y_announcement announcements[ZD_A11Y_MAX_ANNOUNCEMENTS];
    uint64_t announcement_sequence;
    struct {
        uint64_t created;
        uint64_t destroyed;
        uint64_t focus_moves;
        uint64_t announcements;
        uint64_t announcements_dropped;
    } stats;
};

void zd_a11y_init(struct zd_a11y *a11y);
/* The root node exists after init. Creating returns the new node id. */
int zd_a11y_create(struct zd_a11y *a11y, uint32_t parent, enum zd_a11y_role role,
                   const char *name, uint32_t states, uint32_t widget,
                   uint32_t *out_id);
/* Destroys the node and its subtree; focused node is cleared if inside. */
int zd_a11y_destroy(struct zd_a11y *a11y, uint32_t id);
struct zd_a11y_node *zd_a11y_node(struct zd_a11y *a11y, uint32_t id);
const struct zd_a11y_node *zd_a11y_node_const(const struct zd_a11y *a11y,
                                              uint32_t id);
int zd_a11y_set_states(struct zd_a11y *a11y, uint32_t id, uint32_t states);
int zd_a11y_set_name(struct zd_a11y *a11y, uint32_t id, const char *name);
/* Focus traversal across focusable, visible, enabled nodes in document
 * order. Wraps. Returns the newly focused id or ZD_A11Y_INVALID when no
 * candidate exists. */
uint32_t zd_a11y_focus_next(struct zd_a11y *a11y);
uint32_t zd_a11y_focus_prev(struct zd_a11y *a11y);
uint32_t zd_a11y_focused(const struct zd_a11y *a11y);
int zd_a11y_focus_node(struct zd_a11y *a11y, uint32_t id);
/* Screen-reader snapshot: depth-first traversal of visible nodes filling
 * caller storage with (role,name,states) triples. Returns count. */
struct zd_a11y_snapshot_row {
    uint32_t id;
    enum zd_a11y_role role;
    char name[ZD_A11Y_NAME_CAP];
    uint32_t states;
};
uint32_t zd_a11y_snapshot(const struct zd_a11y *a11y,
                          struct zd_a11y_snapshot_row *out, uint32_t capacity);
/* Announcements: HIGH urgency preempts (drops oldest pending low/normal). */
int zd_a11y_announce(struct zd_a11y *a11y, uint32_t node, const char *text,
                     enum zd_a11y_urgency urgency);
int zd_a11y_next_announcement(struct zd_a11y *a11y,
                              struct zd_a11y_announcement *out);
void zd_a11y_set_profile(struct zd_a11y *a11y,
                         const struct zd_a11y_profile *profile);
const struct zd_a11y_profile *zd_a11y_profile(const struct zd_a11y *a11y);
/* Convenience predicates for shell UI states. */
int zd_a11y_reduced_motion(const struct zd_a11y *a11y);
int zd_a11y_high_contrast(const struct zd_a11y *a11y);

#endif
