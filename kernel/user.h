#ifndef ZEROOS_USER_H
#define ZEROOS_USER_H

#include "types.h"

#define ZEROOS_USER_BASE 0x00007f0000000000ULL
#define ZEROOS_USER_CODE_BASE ZEROOS_USER_BASE
#define ZEROOS_USER_DATA_BASE (ZEROOS_USER_BASE + 0x1000ULL)
#define ZEROOS_USER_STACK_PAGE (ZEROOS_USER_BASE + 0x1ff000ULL)
#define ZEROOS_USER_STACK_TOP (ZEROOS_USER_BASE + 0x200000ULL - 16ULL)

int userspace_system_init(void);
int userspace_start_init(void);
int userspace_service_step(void);
int userspace_debug_validate(void);

struct thread;
int user_thread_enter(struct thread *thread);

#endif
