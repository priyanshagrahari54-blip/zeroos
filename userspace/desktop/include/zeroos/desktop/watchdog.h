#ifndef ZEROOS_DESKTOP_WATCHDOG_H
#define ZEROOS_DESKTOP_WATCHDOG_H

/* Desktop service watchdog: restart policy, exponential backoff, health
 * windows, escalation. Pure decision engine — actual restarts go through
 * the service manager; failures escalate to the session lifecycle without
 * ever involving the kernel scheduler or memory manager. */

#include <zeroos/desktop/common.h>

#define ZD_WATCHDOG_MAX_SERVICES 16
#define ZD_WATCHDOG_NAME_CAP 24
#define ZD_WATCHDOG_MAX_LOG 32
#define ZD_WATCHDOG_INVALID ((uint32_t)0)

enum zd_watchdog_policy {
    ZD_WD_NEVER = 0,      /* do not restart */
    ZD_WD_ON_FAILURE = 1, /* restart only on nonzero exit */
    ZD_WD_ALWAYS = 2      /* restart on any exit */
};

enum zd_watchdog_decision {
    ZD_WD_CONTINUE = 0,          /* service healthy */
    ZD_WD_RESTART = 1,           /* restart after delay_ns */
    ZD_WD_GIVE_UP = 2,           /* budget exhausted; leave dead */
    ZD_WD_ESCALATE = 3,          /* session-level degradation required */
    ZD_WD_WAIT = 4               /* backoff still pending */
};

enum zd_watchdog_event_kind {
    ZD_WD_EVENT_REGISTER = 0,
    ZD_WD_EVENT_START = 1,
    ZD_WD_EVENT_EXIT = 2,
    ZD_WD_EVENT_DECISION = 3,
    ZD_WD_EVENT_UNREGISTER = 4
};

struct zd_watchdog_service {
    uint32_t id;
    char name[ZD_WATCHDOG_NAME_CAP];
    enum zd_watchdog_policy policy;
    uint32_t max_restarts;
    uint64_t health_window_ns;  /* restarts outside window reset budget */
    uint64_t backoff_base_ns;
    uint64_t backoff_cap_ns;
    uint64_t heartbeat_interval_ns; /* 0 = heartbeat monitoring disabled */
    uint64_t last_heartbeat_ns;
    uint32_t restart_count;
    uint32_t running;
    uint32_t gave_up;
    uint64_t started_ns;
    uint64_t last_restart_ns;
    uint64_t current_backoff_ns;
    uint32_t in_use;
};

struct zd_watchdog_log_entry {
    uint32_t service;
    enum zd_watchdog_event_kind kind;
    enum zd_watchdog_decision decision;
    uint64_t timestamp_ns;
    int32_t exit_status;
    uint64_t delay_ns;
};

struct zd_watchdog {
    struct zd_watchdog_service services[ZD_WATCHDOG_MAX_SERVICES];
    uint32_t next_id;
    uint32_t escalated;
    uint32_t degraded;
    uint64_t total_restarts;
    uint64_t total_giveups;
    struct zd_watchdog_log_entry log[ZD_WATCHDOG_MAX_LOG];
    uint32_t log_head;
    uint32_t log_count;
    struct {
        void (*on_decision)(void *context, uint32_t service_id,
                            enum zd_watchdog_decision decision,
                            uint64_t delay_ns);
        void (*on_degraded)(void *context);
        void *context;
    } listener;
    uint32_t has_listener;
};

struct zd_watchdog_result {
    enum zd_watchdog_decision decision;
    uint64_t delay_ns;
};

void zd_watchdog_init(struct zd_watchdog *watchdog);
int zd_watchdog_set_listener(struct zd_watchdog *watchdog,
                             void (*on_decision)(void *context,
                                                 uint32_t service_id,
                                                 enum zd_watchdog_decision
                                                 decision,
                                                 uint64_t delay_ns),
                             void (*on_degraded)(void *context),
                             void *context);
int zd_watchdog_register(struct zd_watchdog *watchdog, const char *name,
                         enum zd_watchdog_policy policy, uint32_t max_restarts,
                         uint64_t health_window_ns, uint64_t backoff_base_ns,
                         uint64_t backoff_cap_ns, uint32_t *out_id);
int zd_watchdog_unregister(struct zd_watchdog *watchdog, uint32_t id);
/* Heartbeat monitoring: interval 0 disables. While the service is running,
 * zd_watchdog_evaluate escalates when now - last_heartbeat exceeds the
 * interval (the caller supplies the timer event that drives evaluate). */
int zd_watchdog_set_heartbeat(struct zd_watchdog *watchdog, uint32_t id,
                              uint64_t interval_ns);
struct zd_watchdog_service *zd_watchdog_service(struct zd_watchdog *watchdog,
                                                uint32_t id);
/* Event-driven evaluations; no periodic polling inside the watchdog. */
int zd_watchdog_note_start(struct zd_watchdog *watchdog, uint32_t id,
                           uint64_t now_ns);
int zd_watchdog_note_heartbeat(struct zd_watchdog *watchdog, uint32_t id,
                               uint64_t now_ns);
int zd_watchdog_note_exit(struct zd_watchdog *watchdog, uint32_t id,
                          uint64_t now_ns, int32_t exit_status,
                          struct zd_watchdog_result *out_result);
/* Evaluates pending backoff deadlines at now_ns (caller timer event). */
int zd_watchdog_evaluate(struct zd_watchdog *watchdog, uint32_t id,
                         uint64_t now_ns, struct zd_watchdog_result *out_result);
uint32_t zd_watchdog_restart_pending(const struct zd_watchdog *watchdog,
                                     uint32_t id, uint64_t now_ns,
                                     uint64_t *out_delay_ns);
int zd_watchdog_session_degraded(const struct zd_watchdog *watchdog);
const char *zd_watchdog_decision_name(enum zd_watchdog_decision decision);
/* Bounded audit log: newest entries only. Returns count copied. */
uint32_t zd_watchdog_log(const struct zd_watchdog *watchdog,
                         struct zd_watchdog_log_entry *out, uint32_t capacity);

#endif
