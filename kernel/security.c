#include "security.h"
#include "timer.h"

static struct spinlock security_lock;
static struct zeroos_sandbox sandboxes[ZEROOS_SECURITY_MAX_SANDBOXES];
static struct zeroos_audit_entry audit_log[ZEROOS_SECURITY_MAX_AUDIT];
static uint32_t audit_head;

int security_system_init(void) {
    spinlock_init(&security_lock);
    audit_head=0;
    for (uint32_t i=0;i<ZEROOS_SECURITY_MAX_SANDBOXES;++i) {
        sandboxes[i].used=0;
        sandboxes[i].generation=0;
        spinlock_init(&sandboxes[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_SECURITY_MAX_AUDIT;++i) audit_log[i].valid=0;
    return 0;
}

int security_sandbox_create(uint64_t owner_task_id, uint64_t capabilities, uint64_t *sandbox_id_out) {
    if (!sandbox_id_out || owner_task_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&security_lock);
    for (uint32_t i=0;i<ZEROOS_SECURITY_MAX_SANDBOXES;++i) {
        if (sandboxes[i].used) continue;
        if (sandboxes[i].generation==0xffffffffU) continue;
        sandboxes[i].generation++;
        if (sandboxes[i].generation==0) continue;
        sandboxes[i].used=1;
        sandboxes[i].state=ZEROOS_SECURITY_ACTIVE;
        sandboxes[i].owner_task_id=owner_task_id;
        sandboxes[i].capabilities=capabilities;
        sandboxes[i].isolated_fs=1;
        sandboxes[i].isolated_net=1;
        sandboxes[i].isolated_devices=1;
        sandboxes[i].id = ((uint64_t)sandboxes[i].generation<<16) | (uint64_t)(i+1);
        *sandbox_id_out=sandboxes[i].id;
        spin_unlock_irqrestore(&security_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&security_lock,flags);
    return -1;
}

int security_sandbox_check_capability(uint64_t sandbox_id, enum zeroos_capability cap) {
    uint64_t flags=spin_lock_irqsave(&security_lock);
    uint32_t slot=(uint32_t)(sandbox_id & 0xffffULL);
    uint32_t gen=(uint32_t)(sandbox_id>>16);
    if (slot==0 || slot>ZEROOS_SECURITY_MAX_SANDBOXES || gen==0) { spin_unlock_irqrestore(&security_lock,flags); return -1; }
    struct zeroos_sandbox *s=&sandboxes[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&security_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    int has = (s->capabilities & cap) ? 0 : -1;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&security_lock,flags);
    return has;
}

int security_sandbox_destroy(uint64_t sandbox_id) {
    uint64_t flags=spin_lock_irqsave(&security_lock);
    uint32_t slot=(uint32_t)(sandbox_id & 0xffffULL);
    uint32_t gen=(uint32_t)(sandbox_id>>16);
    if (slot==0 || slot>ZEROOS_SECURITY_MAX_SANDBOXES || gen==0) { spin_unlock_irqrestore(&security_lock,flags); return -1; }
    struct zeroos_sandbox *s=&sandboxes[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&security_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->used=0;
    s->state=ZEROOS_SECURITY_STOPPED;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&security_lock,flags);
    return 0;
}

int security_audit_log(uint64_t task_id, uint32_t event_type, uint32_t result, const char *details) {
    if (!details) return -1;
    uint64_t flags=spin_lock_irqsave(&security_lock);
    struct zeroos_audit_entry *e=&audit_log[audit_head];
    e->timestamp=timer_ticks();
    e->task_id=task_id;
    e->event_type=event_type;
    e->result=result;
    e->valid=1;
    uint32_t i=0;
    while (i<63 && details[i]) { e->details[i]=details[i]; i++; }
    e->details[i]=0;
    audit_head=(audit_head+1)%ZEROOS_SECURITY_MAX_AUDIT;
    spin_unlock_irqrestore(&security_lock,flags);
    return 0;
}

int security_debug_validate(void) { return 0; }
