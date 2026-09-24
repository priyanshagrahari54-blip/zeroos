#include "ai.h"

static struct spinlock ai_lock;
static struct zeroos_ai_model models[ZEROOS_AI_MAX_MODELS];
static struct zeroos_ai_session sessions[ZEROOS_AI_MAX_SESSIONS];

int ai_system_init(void) {
    spinlock_init(&ai_lock);
    for (uint32_t i=0;i<ZEROOS_AI_MAX_MODELS;++i) {
        models[i].used=0;
        models[i].generation=0;
        spinlock_init(&models[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_AI_MAX_SESSIONS;++i) {
        sessions[i].used=0;
        sessions[i].generation=0;
        spinlock_init(&sessions[i].lock);
        wait_queue_init(&sessions[i].completion_waiters);
    }
    return 0;
}

int ai_model_register(const char *name, uint64_t memory_budget, uint64_t *model_id_out) {
    if (!name || !model_id_out || memory_budget==0) return -1;
    uint64_t flags=spin_lock_irqsave(&ai_lock);
    for (uint32_t i=0;i<ZEROOS_AI_MAX_MODELS;++i) {
        if (models[i].used) continue;
        if (models[i].generation==0xffffffffU) continue;
        models[i].generation++;
        if (models[i].generation==0) continue;
        models[i].used=1;
        models[i].state=ZEROOS_AI_DORMANT;
        models[i].memory_budget=memory_budget;
        models[i].memory_used=0;
        models[i].invocation_count=0;
        uint32_t n=0;
        while (n<31 && name[n]) { models[i].name[n]=name[n]; n++; }
        models[i].name[n]=0;
        models[i].id = ((uint64_t)models[i].generation<<16) | (uint64_t)(i+1);
        *model_id_out=models[i].id;
        spin_unlock_irqrestore(&ai_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&ai_lock,flags);
    return -1;
}

int ai_session_create(uint64_t model_id, enum zeroos_ai_task_type task_type,
                      uint64_t owner_task_id, uint64_t permissions, uint64_t *session_id_out) {
    if (!session_id_out || owner_task_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&ai_lock);
    uint32_t mslot=(uint32_t)(model_id & 0xffffULL);
    uint32_t mgen=(uint32_t)(model_id>>16);
    if (mslot==0 || mslot>ZEROOS_AI_MAX_MODELS || mgen==0) { spin_unlock_irqrestore(&ai_lock,flags); return -1; }
    struct zeroos_ai_model *m=&models[mslot-1];
    if (!m->used || m->generation!=mgen) { spin_unlock_irqrestore(&ai_lock,flags); return -1; }
    for (uint32_t i=0;i<ZEROOS_AI_MAX_SESSIONS;++i) {
        if (sessions[i].used) continue;
        if (sessions[i].generation==0xffffffffU) continue;
        sessions[i].generation++;
        if (sessions[i].generation==0) continue;
        sessions[i].used=1;
        sessions[i].state=ZEROOS_AI_DORMANT;
        sessions[i].task_type=task_type;
        sessions[i].model_id=model_id;
        sessions[i].owner_task_id=owner_task_id;
        sessions[i].permissions=permissions;
        sessions[i].privacy_aware=1;
        sessions[i].id = ((uint64_t)sessions[i].generation<<16) | (uint64_t)(i+1);
        *session_id_out=sessions[i].id;
        spin_unlock_irqrestore(&ai_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&ai_lock,flags);
    return -1;
}

int ai_session_invoke(uint64_t session_id) {
    uint64_t flags=spin_lock_irqsave(&ai_lock);
    uint32_t slot=(uint32_t)(session_id & 0xffffULL);
    uint32_t gen=(uint32_t)(session_id>>16);
    if (slot==0 || slot>ZEROOS_AI_MAX_SESSIONS || gen==0) { spin_unlock_irqrestore(&ai_lock,flags); return -1; }
    struct zeroos_ai_session *s=&sessions[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&ai_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->state=ZEROOS_AI_ACTIVE;
    spin_unlock_irqrestore(&s->lock,sflags);
    uint32_t mslot=(uint32_t)(s->model_id & 0xffffULL);
    if (mslot && mslot<=ZEROOS_AI_MAX_MODELS) {
        struct zeroos_ai_model *m=&models[mslot-1];
        uint64_t mflags=spin_lock_irqsave(&m->lock);
        m->state=ZEROOS_AI_ACTIVE;
        m->invocation_count++;
        spin_unlock_irqrestore(&m->lock,mflags);
    }
    spin_unlock_irqrestore(&ai_lock,flags);
    return 0;
}

int ai_session_complete(uint64_t session_id) {
    uint64_t flags=spin_lock_irqsave(&ai_lock);
    uint32_t slot=(uint32_t)(session_id & 0xffffULL);
    uint32_t gen=(uint32_t)(session_id>>16);
    if (slot==0 || slot>ZEROOS_AI_MAX_SESSIONS || gen==0) { spin_unlock_irqrestore(&ai_lock,flags); return -1; }
    struct zeroos_ai_session *s=&sessions[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&ai_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->state=ZEROOS_AI_DORMANT;
    spin_unlock_irqrestore(&s->lock,sflags);
    (void)wait_queue_wake_all(&s->completion_waiters);
    spin_unlock_irqrestore(&ai_lock,flags);
    return 0;
}

int ai_session_destroy(uint64_t session_id) {
    uint64_t flags=spin_lock_irqsave(&ai_lock);
    uint32_t slot=(uint32_t)(session_id & 0xffffULL);
    uint32_t gen=(uint32_t)(session_id>>16);
    if (slot==0 || slot>ZEROOS_AI_MAX_SESSIONS || gen==0) { spin_unlock_irqrestore(&ai_lock,flags); return -1; }
    struct zeroos_ai_session *s=&sessions[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&ai_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->used=0;
    s->state=ZEROOS_AI_STOPPED;
    (void)wait_queue_wake_all(&s->completion_waiters);
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&ai_lock,flags);
    return 0;
}

int ai_debug_validate(void) { return 0; }
