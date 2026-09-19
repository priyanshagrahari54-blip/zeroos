#include "timer.h"
#include "pic.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_BASE_HZ 1193182U
#define PIT_HZ 100U

static volatile uint64_t ticks;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void timer_init(void) {
    uint16_t divisor = (uint16_t)(PIT_BASE_HZ / PIT_HZ);

    /* Channel 0, lobyte/hibyte access, mode 3 square wave, binary. */
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xff));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));

    ticks = 0;
}

void timer_tick(void) {
    ++ticks;
    pic_send_eoi(0);
}

uint64_t timer_ticks(void) {
    return ticks;
}
