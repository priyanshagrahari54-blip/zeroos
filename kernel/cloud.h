#ifndef ZEROOS_CLOUD_H
#define ZEROOS_CLOUD_H
#include "types.h"
#include "sync.h"
#define ZEROOS_CLOUD_MAX_SERVICES 8U
enum zeroos_cloud_state { ZEROOS_CLOUD_STOPPED=0,ZEROOS_CLOUD_DORMANT,ZEROOS_CLOUD_WARM,ZEROOS_CLOUD_ACTIVE,ZEROOS_CLOUD_THROTTLED,ZEROOS_CLOUD_SUSPENDED };
struct zeroos_cloud_service { uint64_t id; uint32_t gen; uint8_t used; enum zeroos_cloud_state state; char name[32]; uint8_t offline_capable; uint64_t owner_task_id; struct spinlock lock; };
int cloud_system_init(void);
int cloud_service_register(const char *name,uint8_t offline,uint64_t owner,uint64_t *id_out);
int cloud_service_set_state(uint64_t id,enum zeroos_cloud_state state);
int cloud_debug_validate(void);
#endif
