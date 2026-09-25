#ifndef ZEROOS_DESKTOP_AI_H
#define ZEROOS_DESKTOP_AI_AI_H
/* ZEROOS AI platform broker (Stage 5 part F) — deliberately separate
 * from Forge AI: this module is only the local request broker, and it
 * is event-driven (dormant while no request exists), demand-driven
 * (backends are invoked only inside submit/drain), permission-aware
 * (every context bit and the remote egress need explicit grants) and
 * minimal-resident (request payloads are cleared when drained; nothing
 * is retained across calls unless ZD_AI_GRANT_PERSIST is set).
 *
 * Pipeline (docs/ARCHITECTURE.md section 17):
 *   request -> broker -> permission check -> backend select -> run
 * Missing injected ops count failures and return -ZD_EINVAL; they are
 * never mocked as success. */
#include <zeroos/desktop/common.h>

/* Permission grants — explicit, user-controlled bits. */
#define ZD_AI_GRANT_CONTEXT_FILES 0x0001u
#define ZD_AI_GRANT_CONTEXT_SETTINGS 0x0002u
#define ZD_AI_GRANT_CONTEXT_SELECTION 0x0004u
#define ZD_AI_GRANT_REMOTE_EGRESS 0x0008u
#define ZD_AI_GRANT_PERSIST 0x0010u
#define ZD_AI_GRANT_ALL 0x001fu

enum zd_ai_backend {
    ZD_AI_BACKEND_NONE = 0,
    ZD_AI_BACKEND_LOCAL = 1,
    ZD_AI_BACKEND_REMOTE = 2
};

enum zd_ai_request_kind {
    ZD_AI_REQ_COMPLETE = 0,   /* inline completion */
    ZD_AI_REQ_SUMMARIZE = 1,
    ZD_AI_REQ_COMMAND = 2     /* intent -> action */
};

struct zd_ai_request {
    uint32_t id;
    enum zd_ai_request_kind kind;
    uint32_t context_mask;    /* which ZD_AI_GRANT_CONTEXT_* it wants */
    uint8_t want_remote;      /* requester prefers remote backend */
    uint8_t active;           /* queue slot occupancy */
    /* Payload is opaque to the broker and wiped after drain. */
    char payload[64];
};

#define ZD_AI_QUEUE_DEPTH 8u

struct zd_ai_stats {
    uint32_t submitted;
    uint32_t completed;
    uint32_t denied_permission;
    uint32_t backend_downgrades;  /* remote requested, local used */
    uint32_t queue_dropped;
    uint32_t run_failures;
    uint32_t wakeups;             /* dormant -> active transitions */
    uint64_t resident_bytes_after_drain; /* must be 0 without PERSIST */
};

/* Injected demand-driven backend surface. */
struct zd_ai_ops {
    /* Returns chosen backend for the request under current grants, or
     * -ZD_EPERM when no backend may serve it. */
    int (*select_backend)(void *context, const struct zd_ai_request *req,
                          uint32_t grants, enum zd_ai_backend *out);
    /* Executes the request; writes out_len bytes of output. */
    int (*run)(void *context, enum zd_ai_backend backend,
               const struct zd_ai_request *req, char *out,
               uint32_t out_cap, uint32_t *out_len);
    void *context;
};

struct zd_ai_broker {
    struct zd_ai_ops ops;
    struct zd_ai_request queue[ZD_AI_QUEUE_DEPTH];
    struct zd_ai_stats stats;
    uint32_t grants;
    uint32_t queued;          /* occupancy */
    uint8_t active;           /* 0 = dormant (no request resident) */
};

void zd_ai_broker_init(struct zd_ai_broker *broker,
                       const struct zd_ai_ops *ops, uint32_t grants);
void zd_ai_grant(struct zd_ai_broker *broker, uint32_t mask);
void zd_ai_revoke(struct zd_ai_broker *broker, uint32_t mask);

/* Enqueue a request.  Context bits not covered by grants are rejected
 * up front (-ZD_EPERM) before any backend sees the payload.  A full
 * queue drops the request (-ZD_ENOSPC) and counts it.  Waking a
 * dormant broker counts a wakeup.  Returns 0 on acceptance. */
int zd_ai_submit(struct zd_ai_broker *broker,
                 const struct zd_ai_request *request);

/* Drain up to max_out requests: permission-gated backend selection,
 * remote->local downgrade when the egress grant is absent, execution
 * through the injected run op, then payload wipe (unless PERSIST).
 * Returns number completed in *completed_out; 0 with the broker
 * returning to DORMANT when the queue is empty. */
int zd_ai_drain(struct zd_ai_broker *broker, uint32_t max_out,
                uint32_t *completed_out);

#endif
