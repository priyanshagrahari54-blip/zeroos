#ifndef ZEROOS_DESKTOP_SEARCH_H
#define ZEROOS_DESKTOP_SEARCH_H

/* Universal Search pipeline:
 *   input -> parser -> intent -> providers -> ranking/merge -> UI
 *
 * Indexing is incremental, asynchronous (slice-based), cancellable,
 * low-priority, event-driven and resource-aware. The core never scans a
 * disk: documents enter the index only through explicit seed or change
 * events, and the full-scan statistic must remain zero under churn. */

#include <zeroos/desktop/common.h>
#include <zeroos/desktop/window.h>

#define ZD_SEARCH_MAX_TOKENS 8
#define ZD_SEARCH_TOKEN_CAP 32
#define ZD_SEARCH_QUERY_CAP 96
#define ZD_SEARCH_MAX_PROVIDERS 16
#define ZD_SEARCH_MAX_RESULTS 32
#define ZD_SEARCH_INDEX_CAPACITY 1024
#define ZD_SEARCH_LABEL_CAP 48
#define ZD_SEARCH_PATH_CAP 96
#define ZD_SEARCH_MAX_LISTENERS 4

enum zd_search_kind {
    ZD_SEARCH_ANY = 0,
    ZD_SEARCH_APP = 1,
    ZD_SEARCH_FILE = 2,
    ZD_SEARCH_FOLDER = 3,
    ZD_SEARCH_SETTING = 4,
    ZD_SEARCH_DOCUMENT = 5,
    ZD_SEARCH_RECENT = 6,
    ZD_SEARCH_COMMAND = 7,
    ZD_SEARCH_DIAGNOSTIC = 8,
    ZD_SEARCH_AI_ACTION = 9,
    ZD_SEARCH_KIND_COUNT = 10
};

struct zd_intent {
    enum zd_search_kind kind;
    char raw[ZD_SEARCH_QUERY_CAP];
    char tokens[ZD_SEARCH_MAX_TOKENS][ZD_SEARCH_TOKEN_CAP];
    uint32_t token_count;
    uint32_t quoted[ZD_SEARCH_MAX_TOKENS]; /* 1 when the token was quoted */
};

struct zd_search_result {
    uint64_t document_id;
    enum zd_search_kind kind;
    char label[ZD_SEARCH_LABEL_CAP];
    char path[ZD_SEARCH_PATH_CAP];
    uint32_t score;
    uint32_t provider_priority;
    uint64_t recency_score;   /* higher = fresher */
    uint32_t use_count;
    uint32_t available;       /* 0 = provider reported unavailable */
};

struct zd_search_provider;
typedef int (*zd_search_provider_query_fn)(void *context,
                                           const struct zd_intent *intent,
                                           struct zd_search_result *results,
                                           uint32_t capacity,
                                           volatile uint32_t *cancel_token,
                                           uint32_t query_generation);
typedef uint32_t (*zd_search_provider_available_fn)(void *context);

struct zd_search_provider {
    const char *name;
    uint32_t kind_mask;       /* bit (1u << kind); ZD_SEARCH_ANY kind = all */
    uint32_t priority;        /* higher wins ties */
    zd_search_provider_available_fn available;
    zd_search_provider_query_fn query;
    void *context;
    uint32_t registered;
};

enum zd_index_doc_kind {
    ZD_INDEX_APP = 0,
    ZD_INDEX_FILE = 1,
    ZD_INDEX_FOLDER = 2,
    ZD_INDEX_SETTING = 3,
    ZD_INDEX_DOCUMENT = 4,
    ZD_INDEX_COMMAND = 5,
    ZD_INDEX_DIAGNOSTIC = 6,
    ZD_INDEX_AI_ACTION = 7
};

struct zd_index_document {
    uint64_t id;              /* stable hash of key */
    uint64_t recency_score;
    uint32_t use_count;
    uint32_t in_use;
    uint32_t deleted;         /* tombstone until slices reap it */
    uint32_t tokenized;
    enum zd_index_doc_kind kind;
    char key[ZD_SEARCH_PATH_CAP];
    char label[ZD_SEARCH_LABEL_CAP];
};

struct zd_index_stats {
    uint64_t seeded;
    uint64_t events_applied;
    uint64_t slices_run;
    uint64_t docs_tokenized;
    uint64_t full_scans;      /* must stay 0: no whole-disk rescans */
    uint64_t queries;
    uint64_t queries_canceled;
    uint32_t paused;
    uint32_t cursor;
    uint32_t cancel_generation;
    uint32_t query_generation;
};

struct zd_search_stats {
    uint64_t queries;
    uint64_t results_returned;
    uint64_t provider_unavailable;
    uint64_t stale_drops;
};

struct zd_search;

typedef void (*zd_search_index_event_fn)(void *context,
                                         enum zd_index_doc_kind kind,
                                         const char *key);

struct zd_search {
    struct zd_search_provider providers[ZD_SEARCH_MAX_PROVIDERS];
    uint32_t provider_count;
    struct zd_index_document documents[ZD_SEARCH_INDEX_CAPACITY];
    uint32_t document_count;
    struct zd_index_stats index_stats;
    struct zd_search_stats stats;
    uint32_t listener_count;
    struct {
        zd_search_index_event_fn callback;
        void *context;
    } listeners[ZD_SEARCH_MAX_LISTENERS];
    const struct zd_window *const *app_windows; /* optional live app list */
    uint32_t app_window_count;
};

void zd_search_init(struct zd_search *search);
int zd_search_add_provider(struct zd_search *search,
                           const struct zd_search_provider *provider);
int zd_search_add_index_listener(struct zd_search *search,
                                 zd_search_index_event_fn callback,
                                 void *context);
/* Parser: handles quoted segments and kind prefixes (app:, file:, folder:,
 * set:, doc:, recent:, cmd:, diag:, ai:). Returns 0 or -ZD_EINVAL /
 * -ZD_EOVERFLOW for empty or oversized input. */
int zd_search_parse(const char *input, struct zd_intent *out_intent);
const char *zd_search_kind_name(enum zd_search_kind kind);

/* --- Index (event-driven; no disk access inside the core) --- */
int zd_search_index_upsert(struct zd_search *search,
                           enum zd_index_doc_kind kind, const char *key,
                           const char *label);
int zd_search_index_remove(struct zd_search *search, const char *key);
int zd_search_index_touch(struct zd_search *search, const char *key,
                          uint64_t recency_score);
/* Incremental tokenization slice: at most budget_documents are processed
 * per call; returns 1 when more work remains, 0 when caught up, negative
 * on error. Cancellation generation invalidates in-flight slices. */
int zd_search_index_step(struct zd_search *search, uint32_t budget_documents);
void zd_search_index_set_paused(struct zd_search *search, uint32_t paused);
uint32_t zd_search_index_cancel(struct zd_search *search);

/* Query: fan out providers for the intent, rank and merge. results_out
 * receives up to capacity entries ordered by descending score. meta
 * fields on the first result row document availability handling. */
int zd_search_query(struct zd_search *search, const char *input,
                    struct zd_search_result *results_out, uint32_t capacity,
                    uint32_t *out_count);

/* Built-in index provider: searches zd_search documents. Registered
 * automatically by zd_search_init as provider name "index". */
void zd_search_register_index_provider(struct zd_search *search);

/* Ranking (exposed for tests and deterministic replay). */
uint32_t zd_search_score(const struct zd_intent *intent,
                         const struct zd_search_result *result);
uint32_t zd_search_rank_merge(const struct zd_intent *intent,
                              struct zd_search_result *results, uint32_t count);

/* Live application provider helper: builds results from the window manager
 * application list registered via zd_search_set_live_apps. */
void zd_search_set_live_apps(struct zd_search *search,
                             const struct zd_window *const *windows,
                             uint32_t count);

#endif
