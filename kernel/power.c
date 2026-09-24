#include "power.h"

static struct spinlock power_lock;
static struct zeroos_power_domain power_domains[ZEROOS_POWER_MAX_DOMAINS];
static struct zeroos_thermal_zone thermal_zones[ZEROOS_POWER_MAX_THERMAL_ZONES];

int power_system_init(void) {
    spinlock_init(&power_lock);
    for (uint32_t i=0;i<ZEROOS_POWER_MAX_DOMAINS;++i) {
        power_domains[i].used=0;
        power_domains[i].generation=0;
        power_domains[i].state=ZEROOS_POWER_STOPPED;
        spinlock_init(&power_domains[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_POWER_MAX_THERMAL_ZONES;++i) {
        thermal_zones[i].used=0;
        thermal_zones[i].generation=0;
        thermal_zones[i].state=ZEROOS_THERMAL_NORMAL;
        spinlock_init(&thermal_zones[i].lock);
    }
    return 0;
}

int power_domain_register(const char *name, uint32_t voltage_mv, uint32_t freq_mhz,
                          uint64_t budget_mw, uint64_t *domain_id_out) {
    if (!name || !domain_id_out || voltage_mv==0) return -1;
    uint64_t flags=spin_lock_irqsave(&power_lock);
    for (uint32_t i=0;i<ZEROOS_POWER_MAX_DOMAINS;++i) {
        if (power_domains[i].used) continue;
        if (power_domains[i].generation==0xffffffffU) continue;
        power_domains[i].generation++;
        if (power_domains[i].generation==0) continue;
        power_domains[i].used=1;
        power_domains[i].state=ZEROOS_POWER_DORMANT;
        power_domains[i].voltage_mv=voltage_mv;
        power_domains[i].frequency_mhz=freq_mhz;
        power_domains[i].power_budget_mw=budget_mw;
        power_domains[i].current_power_mw=0;
        uint32_t n=0;
        while (n<31 && name[n]) { power_domains[i].name[n]=name[n]; n++; }
        power_domains[i].name[n]=0;
        power_domains[i].id = ((uint64_t)power_domains[i].generation<<16) | (uint64_t)(i+1);
        *domain_id_out=power_domains[i].id;
        spin_unlock_irqrestore(&power_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&power_lock,flags);
    return -1;
}

int power_domain_set_state(uint64_t domain_id, enum zeroos_power_state state) {
    uint64_t flags=spin_lock_irqsave(&power_lock);
    uint32_t slot=(uint32_t)(domain_id & 0xffffULL);
    uint32_t gen=(uint32_t)(domain_id>>16);
    if (slot==0 || slot>ZEROOS_POWER_MAX_DOMAINS || gen==0) { spin_unlock_irqrestore(&power_lock,flags); return -1; }
    struct zeroos_power_domain *d=&power_domains[slot-1];
    if (!d->used || d->generation!=gen) { spin_unlock_irqrestore(&power_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&d->lock);
    d->state=state;
    spin_unlock_irqrestore(&d->lock,dflags);
    spin_unlock_irqrestore(&power_lock,flags);
    return 0;
}

int power_domain_set_frequency(uint64_t domain_id, uint32_t freq_mhz) {
    if (freq_mhz==0) return -1;
    uint64_t flags=spin_lock_irqsave(&power_lock);
    uint32_t slot=(uint32_t)(domain_id & 0xffffULL);
    uint32_t gen=(uint32_t)(domain_id>>16);
    if (slot==0 || slot>ZEROOS_POWER_MAX_DOMAINS || gen==0) { spin_unlock_irqrestore(&power_lock,flags); return -1; }
    struct zeroos_power_domain *d=&power_domains[slot-1];
    if (!d->used || d->generation!=gen) { spin_unlock_irqrestore(&power_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&d->lock);
    d->frequency_mhz=freq_mhz;
    spin_unlock_irqrestore(&d->lock,dflags);
    spin_unlock_irqrestore(&power_lock,flags);
    return 0;
}

int thermal_zone_register(const char *name, int32_t warning_c, int32_t critical_c,
                          int32_t emergency_c, uint64_t *zone_id_out) {
    if (!name || !zone_id_out) return -1;
    if (warning_c>=critical_c || critical_c>=emergency_c) return -1;
    uint64_t flags=spin_lock_irqsave(&power_lock);
    for (uint32_t i=0;i<ZEROOS_POWER_MAX_THERMAL_ZONES;++i) {
        if (thermal_zones[i].used) continue;
        if (thermal_zones[i].generation==0xffffffffU) continue;
        thermal_zones[i].generation++;
        if (thermal_zones[i].generation==0) continue;
        thermal_zones[i].used=1;
        thermal_zones[i].state=ZEROOS_THERMAL_NORMAL;
        thermal_zones[i].trip_warning_c=warning_c;
        thermal_zones[i].trip_critical_c=critical_c;
        thermal_zones[i].trip_emergency_c=emergency_c;
        thermal_zones[i].temperature_c=25;
        uint32_t n=0;
        while (n<31 && name[n]) { thermal_zones[i].name[n]=name[n]; n++; }
        thermal_zones[i].name[n]=0;
        thermal_zones[i].id = ((uint64_t)thermal_zones[i].generation<<16) | (uint64_t)(i+1);
        *zone_id_out=thermal_zones[i].id;
        spin_unlock_irqrestore(&power_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&power_lock,flags);
    return -1;
}

int thermal_zone_update_temperature(uint64_t zone_id, int32_t temp_c) {
    uint64_t flags=spin_lock_irqsave(&power_lock);
    uint32_t slot=(uint32_t)(zone_id & 0xffffULL);
    uint32_t gen=(uint32_t)(zone_id>>16);
    if (slot==0 || slot>ZEROOS_POWER_MAX_THERMAL_ZONES || gen==0) { spin_unlock_irqrestore(&power_lock,flags); return -1; }
    struct zeroos_thermal_zone *z=&thermal_zones[slot-1];
    if (!z->used || z->generation!=gen) { spin_unlock_irqrestore(&power_lock,flags); return -1; }
    uint64_t zflags=spin_lock_irqsave(&z->lock);
    z->temperature_c=temp_c;
    if (temp_c>=z->trip_emergency_c) z->state=ZEROOS_THERMAL_EMERGENCY;
    else if (temp_c>=z->trip_critical_c) z->state=ZEROOS_THERMAL_CRITICAL;
    else if (temp_c>=z->trip_warning_c) z->state=ZEROOS_THERMAL_WARNING;
    else z->state=ZEROOS_THERMAL_NORMAL;
    spin_unlock_irqrestore(&z->lock,zflags);
    spin_unlock_irqrestore(&power_lock,flags);
    return 0;
}

int power_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&power_lock);
    for (uint32_t i=0;i<ZEROOS_POWER_MAX_DOMAINS;++i) if (power_domains[i].used && power_domains[i].voltage_mv==0) { spin_unlock_irqrestore(&power_lock,flags); return -1; }
    spin_unlock_irqrestore(&power_lock,flags);
    return 0;
}
