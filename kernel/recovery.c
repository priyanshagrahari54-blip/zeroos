#include "recovery.h"
#include "timer.h"

static struct spinlock recovery_lock;
static struct zeroos_snapshot snapshots[ZEROOS_RECOVERY_MAX_SNAPSHOTS];
static struct zeroos_update updates[ZEROOS_RECOVERY_MAX_UPDATES];

int recovery_system_init(void) {
    spinlock_init(&recovery_lock);
    for (uint32_t i=0;i<ZEROOS_RECOVERY_MAX_SNAPSHOTS;++i) {
        snapshots[i].used=0;
        snapshots[i].generation=0;
        spinlock_init(&snapshots[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_RECOVERY_MAX_UPDATES;++i) {
        updates[i].used=0;
        updates[i].generation=0;
        spinlock_init(&updates[i].lock);
    }
    return 0;
}

int recovery_snapshot_create(enum zeroos_snapshot_type type, const char *desc, uint64_t size, uint64_t *snapshot_id_out) {
    if (!desc || !snapshot_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&recovery_lock);
    for (uint32_t i=0;i<ZEROOS_RECOVERY_MAX_SNAPSHOTS;++i) {
        if (snapshots[i].used) continue;
        if (snapshots[i].generation==0xffffffffU) continue;
        snapshots[i].generation++;
        if (snapshots[i].generation==0) continue;
        snapshots[i].used=1;
        snapshots[i].type=type;
        snapshots[i].timestamp=timer_ticks();
        snapshots[i].size_bytes=size;
        snapshots[i].valid=1;
        uint32_t n=0;
        while (n<63 && desc[n]) { snapshots[i].description[n]=desc[n]; n++; }
        snapshots[i].description[n]=0;
        snapshots[i].id = ((uint64_t)snapshots[i].generation<<16) | (uint64_t)(i+1);
        *snapshot_id_out=snapshots[i].id;
        spin_unlock_irqrestore(&recovery_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&recovery_lock,flags);
    return -1;
}

int recovery_snapshot_restore(uint64_t snapshot_id) {
    uint64_t flags=spin_lock_irqsave(&recovery_lock);
    uint32_t slot=(uint32_t)(snapshot_id & 0xffffULL);
    uint32_t gen=(uint32_t)(snapshot_id>>16);
    if (slot==0 || slot>ZEROOS_RECOVERY_MAX_SNAPSHOTS || gen==0) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    struct zeroos_snapshot *s=&snapshots[slot-1];
    if (!s->used || s->generation!=gen || !s->valid) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    spin_unlock_irqrestore(&recovery_lock,flags);
    return 0;
}

int recovery_snapshot_delete(uint64_t snapshot_id) {
    uint64_t flags=spin_lock_irqsave(&recovery_lock);
    uint32_t slot=(uint32_t)(snapshot_id & 0xffffULL);
    uint32_t gen=(uint32_t)(snapshot_id>>16);
    if (slot==0 || slot>ZEROOS_RECOVERY_MAX_SNAPSHOTS || gen==0) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    struct zeroos_snapshot *s=&snapshots[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->used=0;
    s->valid=0;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&recovery_lock,flags);
    return 0;
}

int recovery_update_stage(uint64_t from_ver, uint64_t to_ver, uint64_t snapshot_id, uint64_t *update_id_out) {
    if (!update_id_out || from_ver>=to_ver) return -1;
    uint64_t flags=spin_lock_irqsave(&recovery_lock);
    for (uint32_t i=0;i<ZEROOS_RECOVERY_MAX_UPDATES;++i) {
        if (updates[i].used) continue;
        if (updates[i].generation==0xffffffffU) continue;
        updates[i].generation++;
        if (updates[i].generation==0) continue;
        updates[i].used=1;
        updates[i].state=ZEROOS_RECOVERY_DORMANT;
        updates[i].from_version=from_ver;
        updates[i].to_version=to_ver;
        updates[i].staged=1;
        updates[i].verified=0;
        updates[i].applied=0;
        updates[i].snapshot_id=snapshot_id;
        updates[i].id = ((uint64_t)updates[i].generation<<16) | (uint64_t)(i+1);
        *update_id_out=updates[i].id;
        spin_unlock_irqrestore(&recovery_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&recovery_lock,flags);
    return -1;
}

int recovery_update_verify(uint64_t update_id) {
    uint64_t flags=spin_lock_irqsave(&recovery_lock);
    uint32_t slot=(uint32_t)(update_id & 0xffffULL);
    uint32_t gen=(uint32_t)(update_id>>16);
    if (slot==0 || slot>ZEROOS_RECOVERY_MAX_UPDATES || gen==0) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    struct zeroos_update *u=&updates[slot-1];
    if (!u->used || u->generation!=gen || !u->staged) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    uint64_t uflags=spin_lock_irqsave(&u->lock);
    u->verified=1;
    u->state=ZEROOS_RECOVERY_WARM;
    spin_unlock_irqrestore(&u->lock,uflags);
    spin_unlock_irqrestore(&recovery_lock,flags);
    return 0;
}

int recovery_update_apply(uint64_t update_id) {
    uint64_t flags=spin_lock_irqsave(&recovery_lock);
    uint32_t slot=(uint32_t)(update_id & 0xffffULL);
    uint32_t gen=(uint32_t)(update_id>>16);
    if (slot==0 || slot>ZEROOS_RECOVERY_MAX_UPDATES || gen==0) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    struct zeroos_update *u=&updates[slot-1];
    if (!u->used || u->generation!=gen || !u->verified) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    uint64_t uflags=spin_lock_irqsave(&u->lock);
    u->applied=1;
    u->state=ZEROOS_RECOVERY_ACTIVE;
    spin_unlock_irqrestore(&u->lock,uflags);
    spin_unlock_irqrestore(&recovery_lock,flags);
    return 0;
}

int recovery_update_rollback(uint64_t update_id) {
    uint64_t flags=spin_lock_irqsave(&recovery_lock);
    uint32_t slot=(uint32_t)(update_id & 0xffffULL);
    uint32_t gen=(uint32_t)(update_id>>16);
    if (slot==0 || slot>ZEROOS_RECOVERY_MAX_UPDATES || gen==0) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    struct zeroos_update *u=&updates[slot-1];
    if (!u->used || u->generation!=gen) { spin_unlock_irqrestore(&recovery_lock,flags); return -1; }
    uint64_t uflags=spin_lock_irqsave(&u->lock);
    if (u->applied) {
        /* Rollback via snapshot */
        u->applied=0;
    }
    u->state=ZEROOS_RECOVERY_DORMANT;
    spin_unlock_irqrestore(&u->lock,uflags);
    spin_unlock_irqrestore(&recovery_lock,flags);
    return 0;
}

int recovery_debug_validate(void) { return 0; }
