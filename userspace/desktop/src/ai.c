#include <zeroos/desktop/ai.h>

static void wipe_request(struct zd_ai_request *req) {
    volatile uint8_t *bytes = (volatile uint8_t *)req;
    uint32_t i;

    /* Clear payload, metadata, and padding without an optimizable dead store. */
    for (i = 0; i < sizeof(*req); ++i)
        bytes[i] = 0;
}

static void wipe_output(char *output, uint32_t capacity) {
    volatile char *bytes = (volatile char *)output;
    uint32_t i;

    for (i = 0; i < capacity; ++i)
        bytes[i] = 0;
}

static void update_resident_payload_bytes(struct zd_ai_broker *broker) {
    uint64_t resident = 0;
    uint32_t i;

    for (i = 0; i < ZD_AI_QUEUE_DEPTH; ++i)
        if (broker->queue[i].active)
            resident += sizeof(broker->queue[i].payload);
    broker->stats.resident_bytes_after_drain = resident;
}

void zd_ai_broker_init(struct zd_ai_broker *broker,
                       const struct zd_ai_ops *ops, uint32_t grants) {
    uint32_t i;

    if (!broker)
        return;
    for (i = 0; i < sizeof(*broker); ++i)
        ((uint8_t *)broker)[i] = 0;
    if (ops)
        broker->ops = *ops;
    /* Unknown/reserved permission bits never enter the broker. */
    broker->grants = grants & ZD_AI_GRANT_ALL;
    broker->active = 0; /* demand-driven: dormant until submit */
    broker->queued = 0;
}

void zd_ai_grant(struct zd_ai_broker *broker, uint32_t mask) {
    if (broker)
        broker->grants = (broker->grants & ZD_AI_GRANT_ALL) |
                         (mask & ZD_AI_GRANT_ALL);
}

void zd_ai_revoke(struct zd_ai_broker *broker, uint32_t mask) {
    if (broker)
        broker->grants = (broker->grants & ZD_AI_GRANT_ALL) &
                         ~(mask & ZD_AI_GRANT_ALL);
}

int zd_ai_submit(struct zd_ai_broker *broker,
                 const struct zd_ai_request *request) {
    struct zd_ai_request safe_request;
    uint32_t i;

    if (!broker || !request)
        return -ZD_EINVAL;
    if (request->kind < ZD_AI_REQ_COMPLETE ||
        request->kind > ZD_AI_REQ_COMMAND || request->want_remote > 1 ||
        (request->context_mask & ~ZD_AI_GRANT_CONTEXT_MASK) ||
        (request->action_mask & ~ZD_AI_GRANT_ACTION_ALL))
        return -ZD_EINVAL;

    /* Defend even if an invalid bit was written directly into public state. */
    broker->grants &= ZD_AI_GRANT_ALL;

    /* Permission gate: requested context must be fully granted. */
    if (request->context_mask & ~broker->grants ||
        request->action_mask & ~(broker->grants & ZD_AI_GRANT_ACTION_ALL)) {
        broker->stats.denied_permission++;
        return -ZD_EPERM;
    }
    if (request->want_remote &&
        !(broker->grants & ZD_AI_GRANT_REMOTE_EGRESS)) {
        /* Not a denial: submit is accepted, drain downgrades locally. */
    }
    if (broker->queued >= ZD_AI_QUEUE_DEPTH) {
        broker->stats.queue_dropped++;
        return -ZD_ENOSPC;
    }

    /* Snapshot fields explicitly so padding or the caller's active byte
     * cannot enter the queue. Do this before selecting/clearing a slot so
     * submission remains safe if request aliases broker storage. */
    safe_request.id = request->id;
    safe_request.kind = request->kind;
    safe_request.context_mask = request->context_mask;
    safe_request.action_mask = request->action_mask;
    safe_request.want_remote = request->want_remote;
    safe_request.active = 0;
    for (i = 0; i < sizeof(safe_request.payload); ++i)
        safe_request.payload[i] = request->payload[i];

    for (i = 0; i < ZD_AI_QUEUE_DEPTH; ++i) {
        if (!broker->queue[i].active) {
            struct zd_ai_request *slot = &broker->queue[i];
            uint32_t k;

            if (!broker->active && broker->queued == 0)
                broker->stats.wakeups++;
            wipe_request(slot);
            slot->id = safe_request.id;
            slot->kind = safe_request.kind;
            slot->context_mask = safe_request.context_mask;
            slot->action_mask = safe_request.action_mask;
            slot->want_remote = safe_request.want_remote;
            for (k = 0; k < sizeof(slot->payload); ++k)
                slot->payload[k] = safe_request.payload[k];
            slot->active = 1;
            broker->queued++;
            broker->stats.submitted++;
            broker->active = 1;
            wipe_request(&safe_request);
            return 0;
        }
    }

    wipe_request(&safe_request);
    broker->stats.queue_dropped++;
    return -ZD_ENOSPC; /* defensive: queued/slot occupancy disagreed */
}

int zd_ai_drain(struct zd_ai_broker *broker, uint32_t max_out,
                uint32_t *completed_out) {
    uint32_t done = 0;
    uint32_t processed = 0;
    uint32_t guard = 0;

    if (!broker || !completed_out)
        return -ZD_EINVAL;
    *completed_out = 0;
    if (broker->draining) {
        broker->stats.reentrant_drains++;
        return -ZD_EBUSY;
    }
    broker->draining = 1;
    broker->grants &= ZD_AI_GRANT_ALL;
    while (broker->queued && processed < max_out &&
           guard < ZD_AI_QUEUE_DEPTH) {
        struct zd_ai_request *req = 0;
        enum zd_ai_backend backend = ZD_AI_BACKEND_NONE;
        char out[128] = {0};
        uint32_t out_len = 0;
        uint32_t i;
        int rc;

        ++guard;
        for (i = 0; i < ZD_AI_QUEUE_DEPTH; ++i)
            if (broker->queue[i].active) {
                req = &broker->queue[i];
                break;
            }
        if (!req)
            break;
        ++processed;

        /* Queue contents are validated again at the execution boundary. */
        if ((req->context_mask & ~ZD_AI_GRANT_CONTEXT_MASK) ||
            (req->action_mask & ~ZD_AI_GRANT_ACTION_ALL)) {
            broker->stats.denied_permission++;
            wipe_request(req);
            broker->queued--;
            continue;
        }
        if (req->kind < ZD_AI_REQ_COMPLETE ||
            req->kind > ZD_AI_REQ_COMMAND || req->want_remote > 1) {
            broker->stats.run_failures++;
            wipe_request(req);
            broker->queued--;
            continue;
        }
        if (req->context_mask & ~broker->grants ||
            req->action_mask & ~(broker->grants & ZD_AI_GRANT_ACTION_ALL)) {
            broker->stats.denied_permission++;
            wipe_request(req);
            broker->queued--;
            continue;
        }

        if (!broker->ops.select_backend || !broker->ops.run) {
            /* Missing hooks are failures, never silent successes. */
            broker->stats.run_failures++;
            wipe_request(req);
            broker->queued--;
            continue;
        }

        rc = broker->ops.select_backend(broker->ops.context, req,
                                        broker->grants, &backend);
        if (rc != 0 || backend == ZD_AI_BACKEND_NONE) {
            broker->stats.denied_permission++;
            wipe_request(req);
            broker->queued--;
            continue;
        }
        if (backend != ZD_AI_BACKEND_LOCAL &&
            backend != ZD_AI_BACKEND_REMOTE) {
            broker->stats.run_failures++;
            wipe_request(req);
            broker->queued--;
            continue;
        }

        /* Revalidate grants after the selector and immediately before run. */
        broker->grants &= ZD_AI_GRANT_ALL;
        if ((req->context_mask & ~ZD_AI_GRANT_CONTEXT_MASK) ||
            (req->action_mask & ~ZD_AI_GRANT_ACTION_ALL) ||
            (req->context_mask & ~broker->grants) ||
            (req->action_mask &
             ~(broker->grants & ZD_AI_GRANT_ACTION_ALL))) {
            broker->stats.denied_permission++;
            wipe_request(req);
            broker->queued--;
            continue;
        }

        /* Remote egress requires the current grant: otherwise use local. */
        if (backend == ZD_AI_BACKEND_REMOTE &&
            !(broker->grants & ZD_AI_GRANT_REMOTE_EGRESS)) {
            backend = ZD_AI_BACKEND_LOCAL;
            broker->stats.backend_downgrades++;
        }

        rc = broker->ops.run(broker->ops.context, backend, req, out,
                             (uint32_t)sizeof(out), &out_len);
        wipe_request(req); /* minimal resident state: payload gone */
        broker->queued--;
        wipe_output(out, (uint32_t)sizeof(out));
        if (rc != 0 || out_len > sizeof(out)) {
            broker->stats.run_failures++;
            continue;
        }
        broker->stats.completed++;
        done++;
    }

    broker->active = broker->queued != 0;
    update_resident_payload_bytes(broker);
    broker->draining = 0;
    *completed_out = done;
    return 0;
}
