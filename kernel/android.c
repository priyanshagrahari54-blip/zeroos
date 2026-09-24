#include "android.h"

static struct spinlock android_lock;
static struct zeroos_android_package packages[ZEROOS_ANDROID_MAX_PACKAGES];
static struct zeroos_android_app apps[ZEROOS_ANDROID_MAX_APPS];

int android_system_init(void) {
    spinlock_init(&android_lock);
    for (uint32_t i=0;i<ZEROOS_ANDROID_MAX_PACKAGES;++i) {
        packages[i].used=0;
        packages[i].generation=0;
        spinlock_init(&packages[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_ANDROID_MAX_APPS;++i) {
        apps[i].used=0;
        apps[i].generation=0;
        spinlock_init(&apps[i].lock);
    }
    return 0;
}

int android_package_install(const char *package_name, const char *version, uint64_t size, uint64_t perms, uint64_t *pkg_id_out) {
    if (!package_name || !version || !pkg_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&android_lock);
    for (uint32_t i=0;i<ZEROOS_ANDROID_MAX_PACKAGES;++i) {
        if (packages[i].used) continue;
        if (packages[i].generation==0xffffffffU) continue;
        packages[i].generation++;
        if (packages[i].generation==0) continue;
        packages[i].used=1;
        packages[i].size_bytes=size;
        packages[i].permissions=perms;
        uint32_t n=0;
        while (n<127 && package_name[n]) { packages[i].package_name[n]=package_name[n]; n++; }
        packages[i].package_name[n]=0;
        n=0;
        while (n<31 && version[n]) { packages[i].version[n]=version[n]; n++; }
        packages[i].version[n]=0;
        packages[i].id = ((uint64_t)packages[i].generation<<16) | (uint64_t)(i+1);
        *pkg_id_out=packages[i].id;
        spin_unlock_irqrestore(&android_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&android_lock,flags);
    return -1;
}

int android_package_uninstall(uint64_t pkg_id) {
    uint64_t flags=spin_lock_irqsave(&android_lock);
    uint32_t slot=(uint32_t)(pkg_id & 0xffffULL);
    uint32_t gen=(uint32_t)(pkg_id>>16);
    if (slot==0 || slot>ZEROOS_ANDROID_MAX_PACKAGES || gen==0) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    struct zeroos_android_package *p=&packages[slot-1];
    if (!p->used || p->generation!=gen) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    uint64_t pflags=spin_lock_irqsave(&p->lock);
    p->used=0;
    spin_unlock_irqrestore(&p->lock,pflags);
    spin_unlock_irqrestore(&android_lock,flags);
    return 0;
}

int android_app_launch(uint64_t pkg_id, uint64_t owner_task_id, uint64_t *app_id_out) {
    if (!app_id_out || owner_task_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&android_lock);
    uint32_t pslot=(uint32_t)(pkg_id & 0xffffULL);
    uint32_t pgen=(uint32_t)(pkg_id>>16);
    if (pslot==0 || pslot>ZEROOS_ANDROID_MAX_PACKAGES || pgen==0) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    struct zeroos_android_package *pkg=&packages[pslot-1];
    if (!pkg->used || pkg->generation!=pgen) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    for (uint32_t i=0;i<ZEROOS_ANDROID_MAX_APPS;++i) {
        if (apps[i].used) continue;
        if (apps[i].generation==0xffffffffU) continue;
        apps[i].generation++;
        if (apps[i].generation==0) continue;
        apps[i].used=1;
        apps[i].runtime_state=ZEROOS_ANDROID_ACTIVE;
        apps[i].app_state=ZEROOS_ANDROID_APP_RUNNING;
        apps[i].package_id=pkg_id;
        apps[i].owner_task_id=owner_task_id;
        apps[i].isolated=1;
        apps[i].id = ((uint64_t)apps[i].generation<<16) | (uint64_t)(i+1);
        *app_id_out=apps[i].id;
        spin_unlock_irqrestore(&android_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&android_lock,flags);
    return -1;
}

int android_app_pause(uint64_t app_id) {
    uint64_t flags=spin_lock_irqsave(&android_lock);
    uint32_t slot=(uint32_t)(app_id & 0xffffULL);
    uint32_t gen=(uint32_t)(app_id>>16);
    if (slot==0 || slot>ZEROOS_ANDROID_MAX_APPS || gen==0) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    struct zeroos_android_app *a=&apps[slot-1];
    if (!a->used || a->generation!=gen) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    uint64_t aflags=spin_lock_irqsave(&a->lock);
    a->app_state=ZEROOS_ANDROID_APP_PAUSED;
    a->runtime_state=ZEROOS_ANDROID_SUSPENDED;
    spin_unlock_irqrestore(&a->lock,aflags);
    spin_unlock_irqrestore(&android_lock,flags);
    return 0;
}

int android_app_resume(uint64_t app_id) {
    uint64_t flags=spin_lock_irqsave(&android_lock);
    uint32_t slot=(uint32_t)(app_id & 0xffffULL);
    uint32_t gen=(uint32_t)(app_id>>16);
    if (slot==0 || slot>ZEROOS_ANDROID_MAX_APPS || gen==0) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    struct zeroos_android_app *a=&apps[slot-1];
    if (!a->used || a->generation!=gen) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    uint64_t aflags=spin_lock_irqsave(&a->lock);
    a->app_state=ZEROOS_ANDROID_APP_RUNNING;
    a->runtime_state=ZEROOS_ANDROID_ACTIVE;
    spin_unlock_irqrestore(&a->lock,aflags);
    spin_unlock_irqrestore(&android_lock,flags);
    return 0;
}

int android_app_stop(uint64_t app_id) {
    uint64_t flags=spin_lock_irqsave(&android_lock);
    uint32_t slot=(uint32_t)(app_id & 0xffffULL);
    uint32_t gen=(uint32_t)(app_id>>16);
    if (slot==0 || slot>ZEROOS_ANDROID_MAX_APPS || gen==0) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    struct zeroos_android_app *a=&apps[slot-1];
    if (!a->used || a->generation!=gen) { spin_unlock_irqrestore(&android_lock,flags); return -1; }
    uint64_t aflags=spin_lock_irqsave(&a->lock);
    a->used=0;
    a->app_state=ZEROOS_ANDROID_APP_STOPPED;
    a->runtime_state=ZEROOS_ANDROID_STOPPED;
    spin_unlock_irqrestore(&a->lock,aflags);
    spin_unlock_irqrestore(&android_lock,flags);
    return 0;
}

int android_debug_validate(void) { return 0; }
