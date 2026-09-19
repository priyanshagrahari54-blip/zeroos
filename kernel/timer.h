#ifndef ZEROOS_TIMER_H
#define ZEROOS_TIMER_H
#include "types.h"

typedef void (*timer_tick_hook_t)(void);

void timer_init(void);
void timer_tick(void);
uint64_t timer_ticks(void);
uint32_t timer_frequency_hz(void);
int timer_register_tick_hook(timer_tick_hook_t hook);

#endif
