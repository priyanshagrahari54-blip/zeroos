#include "browser.h"
#include "timer.h"

static struct spinlock browser_lock;
static struct zeroos_browser_tab tabs[ZEROOS_BROWSER_MAX_TABS];
static struct zeroos_browser_process processes[ZEROOS_BROWSER_MAX_PROCESSES];

int browser_system_init(void) {
    spinlock_init(&browser_lock);
    for (uint32_t i=0;i<ZEROOS_BROWSER_MAX_TABS;++i) {
        tabs[i].used=0;
        tabs[i].generation=0;
        spinlock_init(&tabs[i].lock);
        wait_queue_init(&tabs[i].state_waiters);
    }
    for (uint32_t i=0;i<ZEROOS_BROWSER_MAX_PROCESSES;++i) {
        processes[i].used=0;
        processes[i].generation=0;
        spinlock_init(&processes[i].lock);
    }
    return 0;
}

int browser_process_create(uint64_t owner_task_id, uint64_t *proc_id_out) {
    if (!proc_id_out || owner_task_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&browser_lock);
    for (uint32_t i=0;i<ZEROOS_BROWSER_MAX_PROCESSES;++i) {
        if (processes[i].used) continue;
        if (processes[i].generation==0xffffffffU) continue;
        processes[i].generation++;
        if (processes[i].generation==0) continue;
        processes[i].used=1;
        processes[i].state=ZEROOS_BROWSER_PROC_DORMANT;
        processes[i].owner_task_id=owner_task_id;
        processes[i].tab_count=0;
        processes[i].memory_budget=256*1024*1024;
        processes[i].id = ((uint64_t)processes[i].generation<<16) | (uint64_t)(i+1);
        *proc_id_out=processes[i].id;
        spin_unlock_irqrestore(&browser_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&browser_lock,flags);
    return -1;
}

int browser_tab_create(uint64_t proc_id, const char *url, uint64_t *tab_id_out) {
    if (!url || !tab_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&browser_lock);
    uint32_t pslot=(uint32_t)(proc_id & 0xffffULL);
    uint32_t pgen=(uint32_t)(proc_id>>16);
    if (pslot==0 || pslot>ZEROOS_BROWSER_MAX_PROCESSES || pgen==0) { spin_unlock_irqrestore(&browser_lock,flags); return -1; }
    struct zeroos_browser_process *proc=&processes[pslot-1];
    if (!proc->used || proc->generation!=pgen) { spin_unlock_irqrestore(&browser_lock,flags); return -1; }
    for (uint32_t i=0;i<ZEROOS_BROWSER_MAX_TABS;++i) {
        if (tabs[i].used) continue;
        if (tabs[i].generation==0xffffffffU) continue;
        tabs[i].generation++;
        if (tabs[i].generation==0) continue;
        tabs[i].used=1;
        tabs[i].state=ZEROOS_BROWSER_TAB_ACTIVE;
        tabs[i].process_id=proc_id;
        tabs[i].last_active_ticks=timer_ticks();
        tabs[i].memory_used=10*1024*1024;
        tabs[i].retained_state=1;
        uint32_t n=0;
        while (n<255 && url[n]) { tabs[i].url[n]=url[n]; n++; }
        tabs[i].url[n]=0;
        tabs[i].title[0]=0;
        tabs[i].id = ((uint64_t)tabs[i].generation<<16) | (uint64_t)(i+1);
        proc->tab_count++;
        *tab_id_out=tabs[i].id;
        spin_unlock_irqrestore(&browser_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&browser_lock,flags);
    return -1;
}

int browser_tab_navigate(uint64_t tab_id, const char *url) {
    if (!url) return -1;
    uint64_t flags=spin_lock_irqsave(&browser_lock);
    uint32_t slot=(uint32_t)(tab_id & 0xffffULL);
    uint32_t gen=(uint32_t)(tab_id>>16);
    if (slot==0 || slot>ZEROOS_BROWSER_MAX_TABS || gen==0) { spin_unlock_irqrestore(&browser_lock,flags); return -1; }
    struct zeroos_browser_tab *t=&tabs[slot-1];
    if (!t->used || t->generation!=gen) { spin_unlock_irqrestore(&browser_lock,flags); return -1; }
    uint64_t tflags=spin_lock_irqsave(&t->lock);
    uint32_t n=0;
    while (n<255 && url[n]) { t->url[n]=url[n]; n++; }
    t->url[n]=0;
    t->last_active_ticks=timer_ticks();
    t->state=ZEROOS_BROWSER_TAB_ACTIVE;
    spin_unlock_irqrestore(&t->lock,tflags);
    spin_unlock_irqrestore(&browser_lock,flags);
    return 0;
}

int browser_tab_set_state(uint64_t tab_id, enum zeroos_browser_tab_state state) {
    uint64_t flags=spin_lock_irqsave(&browser_lock);
    uint32_t slot=(uint32_t)(tab_id & 0xffffULL);
    uint32_t gen=(uint32_t)(tab_id>>16);
    if (slot==0 || slot>ZEROOS_BROWSER_MAX_TABS || gen==0) { spin_unlock_irqrestore(&browser_lock,flags); return -1; }
    struct zeroos_browser_tab *t=&tabs[slot-1];
    if (!t->used || t->generation!=gen) { spin_unlock_irqrestore(&browser_lock,flags); return -1; }
    uint64_t tflags=spin_lock_irqsave(&t->lock);
    /* Lifecycle: ACTIVE->IDLE->FROZEN->DISCARDED, restore retains state */
    if (state==ZEROOS_BROWSER_TAB_DISCARDED && !t->retained_state) { spin_unlock_irqrestore(&t->lock,tflags); spin_unlock_irqrestore(&browser_lock,flags); return -1; }
    t->state=state;
    if (state==ZEROOS_BROWSER_TAB_FROZEN || state==ZEROOS_BROWSER_TAB_DISCARDED) t->memory_used/=2;
    spin_unlock_irqrestore(&t->lock,tflags);
    (void)wait_queue_wake_all(&t->state_waiters);
    spin_unlock_irqrestore(&browser_lock,flags);
    return 0;
}

int browser_tab_close(uint64_t tab_id) {
    return browser_tab_set_state(tab_id, ZEROOS_BROWSER_TAB_CLOSED);
}

int browser_process_destroy(uint64_t proc_id) {
    uint64_t flags=spin_lock_irqsave(&browser_lock);
    uint32_t slot=(uint32_t)(proc_id & 0xffffULL);
    uint32_t gen=(uint32_t)(proc_id>>16);
    if (slot==0 || slot>ZEROOS_BROWSER_MAX_PROCESSES || gen==0) { spin_unlock_irqrestore(&browser_lock,flags); return -1; }
    struct zeroos_browser_process *p=&processes[slot-1];
    if (!p->used || p->generation!=gen) { spin_unlock_irqrestore(&browser_lock,flags); return -1; }
    uint64_t pflags=spin_lock_irqsave(&p->lock);
    p->used=0;
    p->state=ZEROOS_BROWSER_PROC_STOPPED;
    spin_unlock_irqrestore(&p->lock,pflags);
    spin_unlock_irqrestore(&browser_lock,flags);
    return 0;
}

int browser_debug_validate(void) { return 0; }
