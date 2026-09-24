#include "desktop.h"
#include "timer.h"

static struct spinlock desktop_lock;
static struct zeroos_desktop_service services[ZEROOS_DESKTOP_MAX_SERVICES];
static struct zeroos_notification notifications[ZEROOS_DESKTOP_MAX_NOTIFICATIONS];

int desktop_system_init(void) {
    spinlock_init(&desktop_lock);
    for (uint32_t i=0;i<ZEROOS_DESKTOP_MAX_SERVICES;++i) {
        services[i].used=0;
        services[i].generation=0;
        services[i].state=ZEROOS_DESKTOP_SERVICE_STOPPED;
        spinlock_init(&services[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_DESKTOP_MAX_NOTIFICATIONS;++i) {
        notifications[i].used=0;
        notifications[i].generation=0;
        spinlock_init(&notifications[i].lock);
    }
    return 0;
}

int desktop_service_register(enum zeroos_desktop_service_type type, const char *name,
                             uint64_t task_id, uint32_t max_restarts, uint64_t *service_id_out) {
    if (!name || !service_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&desktop_lock);
    for (uint32_t i=0;i<ZEROOS_DESKTOP_MAX_SERVICES;++i) {
        if (services[i].used) continue;
        if (services[i].generation==0xffffffffU) continue;
        services[i].generation++;
        if (services[i].generation==0) continue;
        services[i].used=1;
        services[i].type=type;
        services[i].state=ZEROOS_DESKTOP_SERVICE_DORMANT;
        services[i].task_id=task_id;
        services[i].restart_count=0;
        services[i].max_restarts=max_restarts ? max_restarts : 3;
        services[i].last_restart_ticks=0;
        uint32_t n=0;
        while (n<ZEROOS_DESKTOP_MAX_NAME-1 && name[n]) { services[i].name[n]=name[n]; n++; }
        services[i].name[n]=0;
        services[i].id = ((uint64_t)services[i].generation<<16) | (uint64_t)(i+1);
        *service_id_out=services[i].id;
        spin_unlock_irqrestore(&desktop_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&desktop_lock,flags);
    return -1;
}

int desktop_service_set_state(uint64_t service_id, enum zeroos_desktop_service_state state) {
    uint64_t flags=spin_lock_irqsave(&desktop_lock);
    uint32_t slot=(uint32_t)(service_id & 0xffffULL);
    uint32_t gen=(uint32_t)(service_id>>16);
    if (slot==0 || slot>ZEROOS_DESKTOP_MAX_SERVICES || gen==0) { spin_unlock_irqrestore(&desktop_lock,flags); return -1; }
    struct zeroos_desktop_service *s=&services[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&desktop_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->state=state;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&desktop_lock,flags);
    return 0;
}

int desktop_service_restart(uint64_t service_id) {
    uint64_t flags=spin_lock_irqsave(&desktop_lock);
    uint32_t slot=(uint32_t)(service_id & 0xffffULL);
    uint32_t gen=(uint32_t)(service_id>>16);
    if (slot==0 || slot>ZEROOS_DESKTOP_MAX_SERVICES || gen==0) { spin_unlock_irqrestore(&desktop_lock,flags); return -1; }
    struct zeroos_desktop_service *s=&services[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&desktop_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    if (s->restart_count>=s->max_restarts) { spin_unlock_irqrestore(&s->lock,sflags); spin_unlock_irqrestore(&desktop_lock,flags); return -1; }
    s->restart_count++;
    s->last_restart_ticks=timer_ticks();
    s->state=ZEROOS_DESKTOP_SERVICE_WARM;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&desktop_lock,flags);
    return 0;
}

int desktop_service_lookup_by_type(enum zeroos_desktop_service_type type, uint64_t *service_id_out) {
    if (!service_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&desktop_lock);
    for (uint32_t i=0;i<ZEROOS_DESKTOP_MAX_SERVICES;++i) {
        if (!services[i].used) continue;
        if (services[i].type!=type) continue;
        *service_id_out=services[i].id;
        spin_unlock_irqrestore(&desktop_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&desktop_lock,flags);
    return -1;
}

int desktop_notification_post(const char *title, const char *body, uint32_t priority, uint64_t *notif_id_out) {
    if (!title || !body || !notif_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&desktop_lock);
    for (uint32_t i=0;i<ZEROOS_DESKTOP_MAX_NOTIFICATIONS;++i) {
        if (notifications[i].used) continue;
        if (notifications[i].generation==0xffffffffU) continue;
        notifications[i].generation++;
        if (notifications[i].generation==0) continue;
        notifications[i].used=1;
        notifications[i].priority=priority;
        notifications[i].read=0;
        notifications[i].timestamp=timer_ticks();
        uint32_t n=0;
        while (n<63 && title[n]) { notifications[i].title[n]=title[n]; n++; }
        notifications[i].title[n]=0;
        n=0;
        while (n<255 && body[n]) { notifications[i].body[n]=body[n]; n++; }
        notifications[i].body[n]=0;
        notifications[i].id = ((uint64_t)notifications[i].generation<<16) | (uint64_t)(i+1);
        *notif_id_out=notifications[i].id;
        spin_unlock_irqrestore(&desktop_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&desktop_lock,flags);
    return -1;
}

int desktop_notification_dismiss(uint64_t notif_id) {
    uint64_t flags=spin_lock_irqsave(&desktop_lock);
    uint32_t slot=(uint32_t)(notif_id & 0xffffULL);
    uint32_t gen=(uint32_t)(notif_id>>16);
    if (slot==0 || slot>ZEROOS_DESKTOP_MAX_NOTIFICATIONS || gen==0) { spin_unlock_irqrestore(&desktop_lock,flags); return -1; }
    struct zeroos_notification *n=&notifications[slot-1];
    if (!n->used || n->generation!=gen) { spin_unlock_irqrestore(&desktop_lock,flags); return -1; }
    uint64_t nflags=spin_lock_irqsave(&n->lock);
    n->used=0;
    spin_unlock_irqrestore(&n->lock,nflags);
    spin_unlock_irqrestore(&desktop_lock,flags);
    return 0;
}

int desktop_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&desktop_lock);
    for (uint32_t i=0;i<ZEROOS_DESKTOP_MAX_SERVICES;++i) if (services[i].used && services[i].restart_count>services[i].max_restarts) { spin_unlock_irqrestore(&desktop_lock,flags); return -1; }
    spin_unlock_irqrestore(&desktop_lock,flags);
    return 0;
}
