#ifndef ZEROOS_SCHEDULER_H
#define ZEROOS_SCHEDULER_H
#include "types.h"

int scheduler_init(void);
void scheduler_tick(void);
void scheduler_yield(void);
int scheduler_sleep_ticks(uint64_t ticks);
void scheduler_start(void);
uint64_t scheduler_ticks(void);

#endif
