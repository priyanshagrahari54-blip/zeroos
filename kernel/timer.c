#include "timer.h"
#include "sync.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_BASE_HZ 1193182U
#define PIT_HZ 100U

static struct atomic_u64 ticks;
static timer_tick_hook_t tick_hook;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void timer_init(void) {
    uint16_t divisor = (uint16_t)(PIT_BASE_HZ / PIT_HZ);
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xff));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
    atomic_u64_init(&ticks, 0);
    tick_hook = 0;
}

void timer_tick(void) {
    atomic_u64_fetch_add(&ticks, 1);
    if (tick_hook)
        tick_hook();
}

uint64_t timer_ticks(void) {
    return atomic_u64_load(&ticks);
}

uint32_t timer_frequency_hz(void) {
    return PIT_HZ;
}

int timer_register_tick_hook(timer_tick_hook_t hook) {
    if (tick_hook != 0 || hook == 0)
        return -1;
    tick_hook = hook;
    return 0;
}
