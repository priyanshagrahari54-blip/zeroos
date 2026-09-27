#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

static int post_events;
static int rate_events;

static void on_post(void *context, zd_notification_id id,
                    enum zd_notify_priority priority) {
    (void)context;
    (void)id;
    (void)priority;
    ++post_events;
}

static void on_rate(void *context, const char *app_id) {
    (void)context;
    (void)app_id;
    ++rate_events;
}

static struct zd_notify_post make_post(const char *app, const char *title,
                                       enum zd_notify_priority priority) {
    struct zd_notify_post post;
    memset(&post, 0, sizeof(post));
    post.app_id = app;
    post.title = title;
    post.body = "body";
    post.priority = priority;
    return post;
}

static void test_post_and_priority_ordering(void) {
    struct zd_notify notify;
    struct zd_notify_post post;
    zd_notification_id ids[8];
    uint32_t count;
    zd_notify_init(&notify);
    ZD_CHECK_OK(zd_notify_add_listener(&notify, on_post, on_rate, 0));

    post = make_post("mail", "low", ZD_NOTIFY_LOW);
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 1000, &ids[0]));
    post = make_post("mail", "critical", ZD_NOTIFY_CRITICAL);
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 2000, &ids[1]));
    post = make_post("sync", "normal", ZD_NOTIFY_NORMAL);
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 3000, &ids[2]));
    ZD_CHECK_EQ(post_events, 3);
    ZD_CHECK_EQ(notify.stats.posted, 3U);

    count = zd_notify_visible(&notify, 4000, ids, 8);
    ZD_CHECK_EQ(count, 3U);
    /* Critical first, then normal, then low. */
    ZD_CHECK_EQ(ids[0], notify.items[1].id);
    ZD_CHECK_EQ(zd_notify_get(&notify, ids[0])->priority, ZD_NOTIFY_CRITICAL);
    /* a11y policy mapping. */
    ZD_CHECK_EQ(zd_notify_a11y_policy(ZD_NOTIFY_CRITICAL),
                ZD_NOTIFY_A11Y_ASSERTIVE);
    ZD_CHECK_EQ(zd_notify_a11y_policy(ZD_NOTIFY_HIGH), ZD_NOTIFY_A11Y_POLITE);
    ZD_CHECK_EQ(zd_notify_a11y_policy(ZD_NOTIFY_NORMAL), ZD_NOTIFY_A11Y_QUIET);
    ZD_CHECK(zd_notify_get(&notify, 0) == 0);
}

static void test_deduplication(void) {
    struct zd_notify notify;
    struct zd_notify_post post;
    zd_notification_id first = 0;
    zd_notification_id second = 0;
    zd_notify_init(&notify);
    post = make_post("build", "compiling", ZD_NOTIFY_NORMAL);
    post.dedupe_key = "compile-job";
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 1000000000ULL, &first));
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 2000000000ULL, &second));
    ZD_CHECK_EQ(first, second);
    ZD_CHECK_EQ(notify.stats.deduped, 1U);
    ZD_CHECK_EQ(notify.stats.posted, 1U);
    ZD_CHECK_EQ(zd_notify_get(&notify, first)->dedupe_count, 1U);
    /* Outside the dedupe window: a new notification. */
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 2000000000ULL +
                               notify.dedupe_window_ns + 1ULL, &second));
    ZD_CHECK_NE(first, second);
    /* Dismissed items do not resurrect through dedupe. */
    ZD_CHECK_OK(zd_notify_dismiss(&notify, second));
    ZD_CHECK_OK(zd_notify_post(&notify, &post,
                               2000000000ULL + notify.dedupe_window_ns +
                               2ULL, &second));
    ZD_CHECK_EQ(zd_notify_get(&notify, second)->state, ZD_NOTIFY_STATE_ACTIVE);
}

static void test_rate_limiting(void) {
    struct zd_notify notify;
    struct zd_notify_post post;
    uint32_t index;
    uint32_t accepted = 0;
    uint64_t now = 0;
    zd_notification_id id = 0;
    zd_notify_init(&notify);
    ZD_CHECK_OK(zd_notify_add_listener(&notify, on_post, on_rate, 0));
    post = make_post("spammy", "update", ZD_NOTIFY_NORMAL);

    /* max_per_window = 5 per app per window. */
    for (index = 0; index < 8U; ++index) {
        int result = zd_notify_post(&notify, &post, now, &id);
        if (result == 0)
            ++accepted;
        else
            ZD_CHECK_ERR(result, ZD_EAGAIN);
    }
    ZD_CHECK_EQ(accepted, 5U);
    ZD_CHECK_EQ(notify.stats.rate_limited, 3U);
    ZD_CHECK_EQ(rate_events, 3);
    /* Another app unaffected by the first app's bucket. */
    post = make_post("other", "hello", ZD_NOTIFY_NORMAL);
    ZD_CHECK_OK(zd_notify_post(&notify, &post, now, &id));
    /* After one rate window elapses, tokens refill. */
    post = make_post("spammy", "update", ZD_NOTIFY_NORMAL);
    ZD_CHECK_OK(zd_notify_post(&notify, &post, now +
                               notify.rate_window_ns + 1ULL, &id));
}

static void test_defer_dismiss_expiry(void) {
    struct zd_notify notify;
    struct zd_notify_post post;
    zd_notification_id id = 0;
    zd_notification_id ids[4];
    uint32_t count;
    zd_notify_init(&notify);
    post = make_post("calendar", "meeting", ZD_NOTIFY_NORMAL);
    post.ttl_ns = 1000000ULL;
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 1000ULL, &id));

    /* Deferred: invisible until due. */
    ZD_CHECK_OK(zd_notify_defer(&notify, id, 1000ULL, 5000ULL));
    count = zd_notify_visible(&notify, 2000ULL, ids, 4);
    ZD_CHECK_EQ(count, 0U);
    count = zd_notify_visible(&notify, 7000ULL, ids, 4);
    ZD_CHECK_EQ(count, 1U); /* deferred until 6000 elapsed */

    /* Expiry: ttl reached -> expired, invisible. */
    count = zd_notify_visible(&notify, 1000ULL + 1000000ULL, ids, 4);
    ZD_CHECK_EQ(count, 0U);
    ZD_CHECK_EQ(notify.stats.expired, 1U);

    /* Dismiss rules. */
    post.ttl_ns = 0;
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 2000000000ULL, &id));
    ZD_CHECK_OK(zd_notify_dismiss(&notify, id));
    ZD_CHECK_ERR(zd_notify_dismiss(&notify, id), ZD_ESTATE);
    ZD_CHECK_ERR(zd_notify_defer(&notify, id, 2000000000ULL, 1000ULL),
                 ZD_ESTATE);
    ZD_CHECK_ERR(zd_notify_dismiss(&notify, 9999), ZD_ENOENT);
    /* Deferring with zero delay invalid. */
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 3000000000ULL, &id));
    ZD_CHECK_ERR(zd_notify_defer(&notify, id, 3000000000ULL, 0), ZD_EINVAL);
}

static void test_grouping(void) {
    struct zd_notify notify;
    struct zd_notify_post post;
    struct zd_notify_group groups[8];
    uint32_t count;
    zd_notification_id id;
    zd_notify_init(&notify);

    post = make_post("mail", "one", ZD_NOTIFY_LOW);
    post.category = "inbox";
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 1000ULL, &id));
    post = make_post("mail", "two", ZD_NOTIFY_HIGH);
    post.category = "inbox";
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 2000ULL, &id));
    post = make_post("mail", "three", ZD_NOTIFY_NORMAL);
    post.category = "calendar";
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 3000ULL, &id));
    post = make_post("sync", "done", ZD_NOTIFY_NORMAL);
    ZD_CHECK_OK(zd_notify_post(&notify, &post, 4000ULL, &id));

    count = zd_notify_groups(&notify, 5000ULL, groups, 8);
    ZD_CHECK_EQ(count, 3U); /* mail/inbox, mail/calendar, sync/general */
    {
        uint32_t index;
        for (index = 0; index < count; ++index) {
            if (strcmp(groups[index].app_id, "mail") == 0 &&
                strcmp(groups[index].category, "inbox") == 0) {
                ZD_CHECK_EQ(groups[index].count, 2U);
                ZD_CHECK_EQ(groups[index].highest_priority, ZD_NOTIFY_HIGH);
            }
        }
    }
    /* Group dismissal removes the whole bucket. */
    ZD_CHECK_EQ(zd_notify_dismiss_group(&notify, "mail", "inbox"), 2U);
    count = zd_notify_groups(&notify, 5000ULL, groups, 8);
    ZD_CHECK_EQ(count, 2U);
    ZD_CHECK_EQ(zd_notify_dismiss_group(&notify, "missing", 0), 0U);
}

static void test_flood_stress(void) {
    struct zd_notify notify;
    struct zd_notify_post post;
    uint32_t index;
    uint64_t now = 0;
    zd_notification_id id;
    zd_notify_init(&notify);
    /* Drive 1000 posts across time with refills; the shell dismisses as
     * users read notifications, so slots recycle without unbounded growth. */
    for (index = 0; index < 1000U; ++index) {
        char title[16];
        snprintf(title, sizeof(title), "n%u", index);
        post = make_post("stress", title, ZD_NOTIFY_NORMAL);
        now += notify.rate_window_ns;
        if (zd_notify_post(&notify, &post, now, &id) == 0)
            (void)zd_notify_dismiss(&notify, id);
        ZD_CHECK(notify.count <= ZD_NOTIFY_MAX);
    }
    ZD_CHECK_EQ(notify.stats.posted, 1000U);
    ZD_CHECK_EQ(notify.stats.dismissed, 1000U);
    ZD_CHECK(notify.count <= ZD_NOTIFY_MAX);
}

void zd_test_notify_suite(void) {
    printf(" suite: notifications\n");
    ZD_RUN(test_post_and_priority_ordering);
    ZD_RUN(test_deduplication);
    ZD_RUN(test_rate_limiting);
    ZD_RUN(test_defer_dismiss_expiry);
    ZD_RUN(test_grouping);
    ZD_RUN(test_flood_stress);
}
