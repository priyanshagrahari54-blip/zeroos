#ifndef ZEROOS_AUTOMATION_H
#define ZEROOS_AUTOMATION_H
#include "types.h"
#include "sync.h"
#define ZEROOS_AUTOMATION_MAX_RULES 32U
enum zeroos_automation_state { ZEROOS_AUTOMATION_STOPPED=0,ZEROOS_AUTOMATION_DORMANT,ZEROOS_AUTOMATION_WARM,ZEROOS_AUTOMATION_ACTIVE,ZEROOS_AUTOMATION_THROTTLED,ZEROOS_AUTOMATION_SUSPENDED };
enum zeroos_automation_trigger { ZEROOS_TRIGGER_FILE=0,ZEROOS_TRIGGER_APP,ZEROOS_TRIGGER_DEVICE,ZEROOS_TRIGGER_TIMER,ZEROOS_TRIGGER_NETWORK,ZEROOS_TRIGGER_SYSTEM };
struct zeroos_automation_rule { uint64_t id; uint32_t gen; uint8_t used; enum zeroos_automation_state state; enum zeroos_automation_trigger trigger; char action[64]; uint64_t owner_task_id; uint64_t permissions; struct spinlock lock; };
int automation_system_init(void);
int automation_rule_create(enum zeroos_automation_trigger trig,const char *action,uint64_t owner,uint64_t perms,uint64_t *id_out);
int automation_rule_set_state(uint64_t id,enum zeroos_automation_state state);
int automation_debug_validate(void);
#endif
