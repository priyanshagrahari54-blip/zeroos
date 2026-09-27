#ifndef ZEROOS_USER_H
#define ZEROOS_USER_H

#include "types.h"

#define ZEROOS_USER_BASE 0x00007f0000000000ULL
#define ZEROOS_USER_CODE_BASE ZEROOS_USER_BASE
#define ZEROOS_USER_DATA_BASE (ZEROOS_USER_BASE + 0x1000ULL)
#define ZEROOS_USER_STACK_PAGE (ZEROOS_USER_BASE + 0x1ff000ULL)
/* Consecutive stack pages mapped below ZEROOS_USER_STACK_PAGE; the stack
 * grows DOWN from ZEROOS_USER_STACK_TOP.  One page is enough for tiny
 * probes, but the session shell reserves >17 KiB of frame space in a
 * single function, so its launcher maps ZEROOS_USER_STACK_PAGES (64 KiB).
 * The layout keeps the gap from image/bss end (~0x51000) to the stack
 * region (0x1ef000) free for exactly this growth. */
#define ZEROOS_USER_STACK_PAGES 16ULL
#define ZEROOS_USER_STACK_TOP (ZEROOS_USER_BASE + 0x200000ULL - 16ULL)

int userspace_system_init(void);
int userspace_start_init(void);
int userspace_service_step(void);
int userspace_debug_validate(void);

struct thread;
int user_thread_enter(struct thread *thread);

#endif
