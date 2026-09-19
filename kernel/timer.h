#ifndef ZEROOS_TIMER_H
#define ZEROOS_TIMER_H

#include "types.h"

void timer_init(void);
void timer_tick(void);
uint64_t timer_ticks(void);

#endif
