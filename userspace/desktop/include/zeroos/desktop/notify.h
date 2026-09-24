#ifndef ZEROOS_DESKTOP_NOTIFY_H
#define ZEROOS_DESKTOP_NOTIFY_H

/* Notification center: grouping, priority, deduplication, rate limiting,
 * dismissal, deferral, expiry and accessibility semantics.
 *
 * Expiry/visibility evaluation is lazy and driven by caller-supplied
 * timestamps; the core contains no polling timer. */

#include <zeroos/desktop/common.h>

#define ZD_NOTIFY_MAX 64
#define ZD_NOTIFY_MAX_APPS 16
#define ZD_NOTIFY_TITLE_CAP 48
#define ZD_NOTIFY_BODY_CAP 96
#define ZD_NOTIFY_APP_CAP 24
#define ZD_NOTIFY_KEY_CAP 32
#define ZD_NOTIFY_MAX_LISTENERS 4

typedef uint32_t zd_notification_id;
#define ZD_NOTIFICATION_INVALID ((zd_notification_id)0)

enum zd_notify_priority {
    ZD_NOTIFY_LOW = 0,
    ZD_NOTIFY_NORMAL = 1,
    ZD_NOTIFY_HIGH = 2,
    ZD_NOTIFY_CRITICAL = 3
};

enum zd_notify_state {
    ZD_NOTIFY_STATE_ACTIVE = 0,
    ZD_NOTIFY_STATE_DEFERRED = 1,
    ZD_NOTIFY_STATE_DISMISSED = 2,
    ZD_NOTIFY_STATE_EXPIRED = 3
};

/* Accessibility announcement policy derived from priority. */
enum zd_notify_a11y {
    ZD_NOTIFY_A11Y_QUIET = 0,       /* no automatic announcement */
    ZD_NOTIFY_A11Y_POLITE = 1,      /* announce when idle */
    ZD_NOTIFY_A11Y_ASSERTIVE = 2    /* interrupt the screen reader */
};

struct zd_notification {
    zd_notification_id id;
    char app_id[ZD_NOTIFY_APP_CAP];
    char category[ZD_NOTIFY_APP_CAP];
    char dedupe_key[ZD_NOTIFY_KEY_CAP];
    char title[ZD_NOTIFY_TITLE_CAP];
    char body[ZD_NOTIFY_BODY_CAP];
    enum zd_notify_priority priority;
    enum zd_notify_state state;
    uint64_t posted_ns;
    uint64_t deferred_until_ns;
    uint64_t expires_at_ns;
    uint32_t dedupe_count;
    uint32_t in_use;
};

struct zd_notify_group {
    char app_id[ZD_NOTIFY_APP_CAP];
    char category[ZD_NOTIFY_APP_CAP];
    uint32_t count;
    enum zd_notify_priority highest_priority;
    uint64_t latest_ns;
    zd_notification_id newest_id;
};

struct zd_notify_stats {
    uint64_t posted;
    uint64_t deduped;
    uint64_t rate_limited;
    uint64_t dismissed;
    uint64_t deferred;
    uint64_t expired;
    uint64_t active;
};

struct zd_notify_post {
    const char *app_id;
    const char *category;      /* optional, defaults to "general" */
    const char *dedupe_key;    /* optional; enables dedup when set */
    const char *title;
    const char *body;
    enum zd_notify_priority priority;
    uint64_t ttl_ns;           /* 0 = priority default */
};

struct zd_notify {
    struct zd_notification items[ZD_NOTIFY_MAX];
    uint32_t count;
    zd_notification_id next_id;
    struct zd_notify_stats stats;
    /* Per-app token buckets. */
    struct {
        char app_id[ZD_NOTIFY_APP_CAP];
        uint32_t tokens;
        uint32_t capacity;
        uint64_t last_refill_ns;
        uint32_t in_use;
    } apps[ZD_NOTIFY_MAX_APPS];
    uint32_t global_tokens;
    uint32_t global_capacity;
    uint64_t global_refill_ns;
    uint32_t max_per_window;   /* posts allowed per app per window */
    uint64_t rate_window_ns;
    uint64_t dedupe_window_ns;
    uint32_t listener_count;
    struct {
        void (*on_post)(void *context, zd_notification_id id,
                        enum zd_notify_priority priority);
        void (*on_rate_limited)(void *context, const char *app_id);
        void *context;
    } listeners[ZD_NOTIFY_MAX_LISTENERS];
};

void zd_notify_init(struct zd_notify *notify);
int zd_notify_add_listener(struct zd_notify *notify, void (*on_post)(void *context,
                            zd_notification_id id,
                            enum zd_notify_priority priority),
                           void (*on_rate_limited)(void *context,
                                                   const char *app_id),
                           void *context);
/* Returns the notification id, or -ZD_EAGAIN when rate limited. */
int zd_notify_post(struct zd_notify *notify, const struct zd_notify_post *post,
                   uint64_t now_ns, zd_notification_id *out_id);
int zd_notify_dismiss(struct zd_notify *notify, zd_notification_id id);
uint32_t zd_notify_dismiss_group(struct zd_notify *notify, const char *app_id,
                                 const char *category);
int zd_notify_defer(struct zd_notify *notify, zd_notification_id id,
                    uint64_t now_ns, uint64_t defer_for_ns);
struct zd_notification *zd_notify_get(struct zd_notify *notify,
                                      zd_notification_id id);
/* Visible set at now: active + due deferred, expiry applied. Fills ids
 * ordered by priority desc then recency desc. Returns count. */
uint32_t zd_notify_visible(struct zd_notify *notify, uint64_t now_ns,
                           zd_notification_id *out_ids, uint32_t capacity);
uint32_t zd_notify_groups(struct zd_notify *notify, uint64_t now_ns,
                          struct zd_notify_group *out_groups,
                          uint32_t capacity);
enum zd_notify_a11y zd_notify_a11y_policy(enum zd_notify_priority priority);
const char *zd_notify_priority_name(enum zd_notify_priority priority);

#endif
