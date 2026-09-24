#ifndef ZEROOS_STUDY_H
#define ZEROOS_STUDY_H
#include "types.h"
#include "sync.h"
#define ZEROOS_STUDY_MAX_SESSIONS 16U
enum zeroos_study_state { ZEROOS_STUDY_STOPPED=0,ZEROOS_STUDY_DORMANT,ZEROOS_STUDY_WARM,ZEROOS_STUDY_ACTIVE,ZEROOS_STUDY_THROTTLED,ZEROOS_STUDY_SUSPENDED };
struct zeroos_study_session { uint64_t id; uint32_t gen; uint8_t used; enum zeroos_study_state state; char subject[64]; uint64_t duration_ms; uint64_t owner_task_id; uint8_t focus_mode; struct spinlock lock; };
int study_system_init(void);
int study_session_create(const char *subject,uint64_t duration,uint64_t owner,uint64_t *id_out);
int study_session_set_focus(uint64_t id,uint8_t focus);
int study_debug_validate(void);
#endif
