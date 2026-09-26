#ifndef ZEROOS_GAMING_H
#define ZEROOS_GAMING_H
#include "types.h"
#include "sync.h"
#define ZEROOS_GAMING_MAX_PROFILES 16U
enum zeroos_gaming_state { ZEROOS_GAMING_STOPPED=0,ZEROOS_GAMING_DORMANT,ZEROOS_GAMING_WARM,ZEROOS_GAMING_ACTIVE,ZEROOS_GAMING_THROTTLED,ZEROOS_GAMING_SUSPENDED };
struct zeroos_game_profile { uint64_t id; uint32_t gen; uint8_t used; char name[64]; uint32_t target_fps; uint8_t low_latency; uint8_t recording; uint64_t owner_task_id; struct spinlock lock; };
int gaming_system_init(void);
int gaming_profile_create(const char *name,uint32_t target_fps,uint64_t owner_task_id,uint64_t *id_out);
int gaming_profile_set_state(uint64_t id,enum zeroos_gaming_state state);
int gaming_debug_validate(void);
#endif
