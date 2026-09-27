#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

static void test_tree_build_and_destroy(void) {
    struct zd_a11y a11y;
    uint32_t window = 0;
    uint32_t button = 0;
    uint32_t entry = 0;
    uint32_t nested = 0;
    zd_a11y_init(&a11y);
    ZD_CHECK_EQ(zd_a11y_focused(&a11y), ZD_A11Y_INVALID);

    ZD_CHECK_OK(zd_a11y_create(&a11y, ZD_A11Y_ROOT, ZD_ROLE_WINDOW,
                               "Files", 0, 1, &window));
    ZD_CHECK_OK(zd_a11y_create(&a11y, window, ZD_ROLE_BUTTON, "Open",
                               ZD_A11Y_FOCUSABLE, 2, &button));
    ZD_CHECK_OK(zd_a11y_create(&a11y, window, ZD_ROLE_ENTRY, "Name",
                               ZD_A11Y_FOCUSABLE, 3, &entry));
    ZD_CHECK_OK(zd_a11y_create(&a11y, button, ZD_ROLE_TEXT, "label", 0, 0,
                               &nested));
    /* Creating under a missing parent fails. */
    ZD_CHECK_ERR(zd_a11y_create(&a11y, 9999, ZD_ROLE_TEXT, "x", 0, 0, &nested),
                 ZD_ENOENT);
    /* Destroying the window removes the whole subtree. */
    ZD_CHECK_OK(zd_a11y_destroy(&a11y, window));
    ZD_CHECK(zd_a11y_node(&a11y, window) == 0);
    ZD_CHECK(zd_a11y_node(&a11y, button) == 0);
    ZD_CHECK(zd_a11y_node(&a11y, entry) == 0);
    ZD_CHECK(zd_a11y_node(&a11y, nested) == 0);
    ZD_CHECK(zd_a11y_node(&a11y, ZD_A11Y_ROOT) != 0);
    /* Root is permanent. */
    ZD_CHECK_ERR(zd_a11y_destroy(&a11y, ZD_A11Y_ROOT), ZD_EPERM);
    ZD_CHECK_ERR(zd_a11y_destroy(&a11y, 4242), ZD_ENOENT);
}

static void test_focus_traversal(void) {
    struct zd_a11y a11y;
    uint32_t ids[5];
    uint32_t index;
    uint32_t seen[ZD_A11Y_MAX_NODES];
    uint32_t seen_count = 0;
    zd_a11y_init(&a11y);
    ZD_CHECK_OK(zd_a11y_create(&a11y, ZD_A11Y_ROOT, ZD_ROLE_BAR, "bar", 0, 0,
                               &ids[0]));
    ZD_CHECK_OK(zd_a11y_create(&a11y, ZD_A11Y_ROOT, ZD_ROLE_BUTTON, "b1",
                               ZD_A11Y_FOCUSABLE, 1, &ids[1]));
    ZD_CHECK_OK(zd_a11y_create(&a11y, ZD_A11Y_ROOT, ZD_ROLE_BUTTON, "b2",
                               ZD_A11Y_FOCUSABLE, 2, &ids[2]));
    ZD_CHECK_OK(zd_a11y_create(&a11y, ids[0], ZD_ROLE_BUTTON, "b3",
                               ZD_A11Y_FOCUSABLE, 3, &ids[3]));
    ZD_CHECK_OK(zd_a11y_create(&a11y, ZD_A11Y_ROOT, ZD_ROLE_BUTTON, "b4",
                               ZD_A11Y_FOCUSABLE | ZD_A11Y_DISABLED, 4,
                               &ids[4]));

    /* Disabled node is skipped; traversal covers exactly 3 focusables. */
    for (index = 0; index < 6U; ++index) {
        uint32_t target = zd_a11y_focus_next(&a11y);
        ZD_CHECK(target != ZD_A11Y_INVALID);
        if (target != ZD_A11Y_INVALID) {
            uint32_t inner;
            int dup = 0;
            for (inner = 0; inner < seen_count; ++inner)
                if (seen[inner] == target)
                    dup = 1;
            if (!dup)
                seen[seen_count++] = target;
            ZD_CHECK(target != ids[4]); /* disabled never focused */
        }
    }
    ZD_CHECK_EQ(seen_count, 3U);
    /* Wrap works and focuses are exclusive (only one FOCUSED flag). */
    {
        uint32_t focused_flags = 0;
        for (index = 0; index < ZD_A11Y_MAX_NODES; ++index)
            if (a11y.nodes[index].in_use &&
                (a11y.nodes[index].states & ZD_A11Y_FOCUSED))
                ++focused_flags;
        ZD_CHECK_EQ(focused_flags, 1U);
    }
    /* Reverse traversal lands on a different node then continues. */
    {
        uint32_t current = zd_a11y_focused(&a11y);
        uint32_t previous = zd_a11y_focus_prev(&a11y);
        ZD_CHECK(previous != ZD_A11Y_INVALID);
        ZD_CHECK_NE(previous, current);
    }
    /* Hiding the focused node drops focus on the next walk. */
    ZD_CHECK_OK(zd_a11y_set_states(&a11y, zd_a11y_focused(&a11y),
                                   ZD_A11Y_FOCUSABLE | ZD_A11Y_HIDDEN));
    (void)zd_a11y_focus_next(&a11y);
    {
        const struct zd_a11y_node *focused =
            zd_a11y_node_const(&a11y, zd_a11y_focused(&a11y));
        ZD_CHECK(focused != 0);
        if (focused)
            ZD_CHECK(!(focused->states & ZD_A11Y_HIDDEN));
    }
    /* focus_node rejects non-focusable targets. */
    ZD_CHECK_ERR(zd_a11y_focus_node(&a11y, ids[0]), ZD_ESTATE);
    ZD_CHECK_ERR(zd_a11y_focus_node(&a11y, 4242), ZD_ENOENT);
}

static void test_snapshot_and_announcements(void) {
    struct zd_a11y a11y;
    struct zd_a11y_profile profile;
    struct zd_a11y_snapshot_row rows[16];
    struct zd_a11y_announcement announcement;
    uint32_t window = 0;
    uint32_t button = 0;
    uint32_t count;
    zd_a11y_init(&a11y);

    ZD_CHECK_OK(zd_a11y_create(&a11y, ZD_A11Y_ROOT, ZD_ROLE_WINDOW, "win", 0,
                               0, &window));
    ZD_CHECK_OK(zd_a11y_create(&a11y, window, ZD_ROLE_BUTTON, "btn",
                               ZD_A11Y_FOCUSABLE, 0, &button));
    count = zd_a11y_snapshot(&a11y, rows, 16);
    ZD_CHECK_EQ(count, 3U); /* root, window, button */
    ZD_CHECK(strcmp(rows[0].name, "desktop") == 0);
    ZD_CHECK(strcmp(rows[2].name, "btn") == 0);
    ZD_CHECK_EQ(rows[2].role, ZD_ROLE_BUTTON);

    /* Announcements require the screen reader to be enabled. */
    ZD_CHECK_OK(zd_a11y_announce(&a11y, window, "quiet", ZD_A11Y_URGENCY_LOW));
    ZD_CHECK_EQ(a11y.announcement_count, 0U);
    memset(&profile, 0, sizeof(profile));
    profile.screen_reader_enabled = 1;
    profile.reduced_motion = 1;
    profile.high_contrast = 1;
    profile.captions_enabled = 1;
    zd_a11y_set_profile(&a11y, &profile);
    ZD_CHECK(zd_a11y_reduced_motion(&a11y));
    ZD_CHECK(zd_a11y_high_contrast(&a11y));

    ZD_CHECK_OK(zd_a11y_announce(&a11y, window, "one", ZD_A11Y_URGENCY_LOW));
    ZD_CHECK_OK(zd_a11y_announce(&a11y, window, "two", ZD_A11Y_URGENCY_NORMAL));
    ZD_CHECK_OK(zd_a11y_announce(&a11y, window, "urgent",
                                 ZD_A11Y_URGENCY_HIGH));
    /* High urgency preempted the pending low/normal announcements. */
    ZD_CHECK_EQ(a11y.announcement_count, 1U);
    ZD_CHECK_OK(zd_a11y_next_announcement(&a11y, &announcement));
    ZD_CHECK(strcmp(announcement.text, "urgent") == 0);
    ZD_CHECK_ERR(zd_a11y_next_announcement(&a11y, &announcement), ZD_ENOENT);
    /* FIFO within the same urgency class. */
    ZD_CHECK_OK(zd_a11y_announce(&a11y, window, "first", ZD_A11Y_URGENCY_NORMAL));
    ZD_CHECK_OK(zd_a11y_announce(&a11y, window, "second", ZD_A11Y_URGENCY_NORMAL));
    ZD_CHECK_OK(zd_a11y_next_announcement(&a11y, &announcement));
    ZD_CHECK(strcmp(announcement.text, "first") == 0);
    /* Negative paths. */
    ZD_CHECK_ERR(zd_a11y_announce(&a11y, 4242, "x", ZD_A11Y_URGENCY_LOW),
                 ZD_ENOENT);
    ZD_CHECK_ERR(zd_a11y_announce(&a11y, window, "", ZD_A11Y_URGENCY_LOW),
                 ZD_EINVAL);
}

void zd_test_a11y_suite(void) {
    printf(" suite: accessibility\n");
    ZD_RUN(test_tree_build_and_destroy);
    ZD_RUN(test_focus_traversal);
    ZD_RUN(test_snapshot_and_announcements);
}
