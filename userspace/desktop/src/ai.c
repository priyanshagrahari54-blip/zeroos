#include <zeroos/desktop/ai.h>

static void wipe_request(struct zd_ai_request *req) {
    uint32_t i;

    for (i = 0; i < sizeof(req->payload); ++i)
        req->payload[i] = 0;
    req->id = 0;
    req->context_mask = 0;
    req->want_remote = 0;
    req->kind = ZD_AI_REQ_COMPLETE;
    req->active = 0;
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
    broker->grants = grants;
    broker->active = 0; /* demand-driven: dormant until submit */
    broker->queued = 0;
}

void zd_ai_grant(struct zd_ai_broker *broker, uint32_t mask) {
    if (broker)
        broker->grants |= (mask & ZD_AI_GRANT_ALL);
}

void zd_ai_revoke(struct zd_ai_broker *broker, uint32_t mask) {
    if (broker)
        broker->grants &= ~(mask & ZD_AI_GRANT_ALL);
}

int zd_ai_submit(struct zd_ai_broker *broker,
                 const struct zd_ai_request *request) {
    uint32_t i;

    if (!broker || !request)
        return -ZD_EINVAL;
    /* Permission gate: requested context must be fully granted. */
    if (request->context_mask & ~broker->grants) {
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
    if (!broker->active && broker->queued == 0)
        broker->stats.wakeups++;
    for (i = 0; i < ZD_AI_QUEUE_DEPTH; ++i) {
        if (!broker->queue[i].active) {
            broker->queue[i] = *request;
            broker->queue[i].active = 1;
            broker->queued++;
            broker->stats.submitted++;
            broker->active = 1;
            return 0;
        }
    }
    return -ZD_ENOSPC; /* unreachable when queued is honest */
}

int zd_ai_drain(struct zd_ai_broker *broker, uint32_t max_out,
                uint32_t *completed_out) {
    uint32_t done = 0;
    uint32_t guard = 0;

    if (!broker || !completed_out)
        return -ZD_EINVAL;
    *completed_out = 0;
    while (broker->queued && done < max_out && guard < ZD_AI_QUEUE_DEPTH) {
        struct zd_ai_request *req = 0;
        enum zd_ai_backend backend = ZD_AI_BACKEND_NONE;
        char out[128];
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
        /* Remote egress requires the grant: downgrade to local. */
        if (backend == ZD_AI_BACKEND_REMOTE &&
            !(broker->grants & ZD_AI_GRANT_REMOTE_EGRESS)) {
            backend = ZD_AI_BACKEND_LOCAL;
            broker->stats.backend_downgrades++;
        }

        rc = broker->ops.run(broker->ops.context, backend, req, out,
                             (uint32_t)sizeof(out), &out_len);
        wipe_request(req); /* minimal resident state: payload gone */
        broker->queued--;
        if (rc != 0) {
            broker->stats.run_failures++;
            continue;
        }
        broker->stats.completed++;
        done++;

        if (!(broker->grants & ZD_AI_GRANT_PERSIST)) {
            uint32_t k;
            for (k = 0; k < sizeof(out); ++k)
                out[k] = 0;
        }
    }

    if (broker->queued == 0)
        broker->active = 0; /* demand-driven: dormant again */
    broker->stats.resident_bytes_after_drain = 0;
    *completed_out = done;
    return 0;
}
