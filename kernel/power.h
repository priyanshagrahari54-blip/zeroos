#ifndef ZEROOS_POWER_H
#define ZEROOS_POWER_H

#include "types.h"
#include "sync.h"

#define ZEROOS_POWER_MAX_DOMAINS 8U
#define ZEROOS_POWER_MAX_THERMAL_ZONES 8U

enum zeroos_power_state {
    ZEROOS_POWER_STOPPED = 0,
    ZEROOS_POWER_DORMANT,
    ZEROOS_POWER_WARM,
    ZEROOS_POWER_ACTIVE,
    ZEROOS_POWER_THROTTLED,
    ZEROOS_POWER_SUSPENDED
};

enum zeroos_thermal_state {
    ZEROOS_THERMAL_NORMAL = 0,
    ZEROOS_THERMAL_WARNING,
    ZEROOS_THERMAL_CRITICAL,
    ZEROOS_THERMAL_EMERGENCY
};

struct zeroos_power_domain {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_power_state state;
    char name[32];
    uint32_t voltage_mv;
    uint32_t frequency_mhz;
    uint64_t power_budget_mw;
    uint64_t current_power_mw;
    struct spinlock lock;
};

struct zeroos_thermal_zone {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_thermal_state state;
    char name[32];
    int32_t temperature_c; /* deciC? keep C for simplicity */
    int32_t trip_warning_c;
    int32_t trip_critical_c;
    int32_t trip_emergency_c;
    struct spinlock lock;
};

int power_system_init(void);
int power_domain_register(const char *name, uint32_t voltage_mv, uint32_t freq_mhz,
                          uint64_t budget_mw, uint64_t *domain_id_out);
int power_domain_set_state(uint64_t domain_id, enum zeroos_power_state state);
int power_domain_set_frequency(uint64_t domain_id, uint32_t freq_mhz);
int thermal_zone_register(const char *name, int32_t warning_c, int32_t critical_c,
                          int32_t emergency_c, uint64_t *zone_id_out);
int thermal_zone_update_temperature(uint64_t zone_id, int32_t temp_c);
int power_debug_validate(void);

#endif
