#include "timer.h"
#include "cpu.h"
#include "sync.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_BASE_HZ 1193182U
#define PIT_HZ 100U
#define CMOS_INDEX 0x70
#define CMOS_DATA 0x71

static struct atomic_u64 ticks;
static timer_tick_hook_t tick_hook;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void timer_init(void) {
    uint16_t divisor=(uint16_t)(PIT_BASE_HZ/PIT_HZ);
    outb(PIT_COMMAND,0x36);
    outb(PIT_CHANNEL0,(uint8_t)(divisor & 0xffU));
    outb(PIT_CHANNEL0,(uint8_t)(divisor >> 8));
    atomic_u64_init(&ticks,0);
    tick_hook=0;
}

void timer_tick(void) {
    atomic_u64_fetch_add(&ticks,1);
    if (tick_hook)
        tick_hook();
}

uint64_t timer_ticks(void) {
    return atomic_u64_load(&ticks);
}

uint32_t timer_frequency_hz(void) {
    return PIT_HZ;
}

uint64_t timer_monotonic_ns(void) {
    uint64_t frequency=cpu_tsc_frequency_hz();
    if (frequency && cpu_has(ZEROOS_CPU_FEATURE_INVARIANT_TSC)) {
        uint64_t tsc=cpu_read_tsc();
        uint64_t whole=tsc/frequency;
        uint64_t remainder=tsc%frequency;
        return whole*1000000000ULL +
               (remainder*1000000000ULL)/frequency;
    }
    return (timer_ticks()*1000000000ULL)/PIT_HZ;
}

const char *timer_clocksource(void) {
    if (cpu_tsc_frequency_hz() && cpu_has(ZEROOS_CPU_FEATURE_INVARIANT_TSC))
        return "invariant-tsc";
    return "pit";
}

static uint8_t cmos_read(uint8_t index) {
    outb(CMOS_INDEX,(uint8_t)(index|0x80U));
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0fU)+((value>>4)*10U));
}

static int leap_year(uint32_t year) {
    return (year%4U==0 && (year%100U!=0 || year%400U==0));
}

static uint64_t days_before_year(uint32_t year) {
    uint64_t y=year;
    return 365ULL*(y-1ULL)+(y-1ULL)/4ULL-(y-1ULL)/100ULL+
           (y-1ULL)/400ULL;
}

uint64_t timer_wallclock_unix_seconds(void) {
    uint8_t second, minute, hour, day, month, year, century, status_b;
    uint8_t stable_second;
    uint32_t guard=100000U;

    /* Avoid returning a torn RTC value while the CMOS update is in progress. */
    while ((cmos_read(0x0aU)&0x80U) && guard--)
        cpu_relax();

    do {
        second=cmos_read(0x00U);
        minute=cmos_read(0x02U);
        hour=cmos_read(0x04U);
        day=cmos_read(0x07U);
        month=cmos_read(0x08U);
        year=cmos_read(0x09U);
        century=cmos_read(0x32U);
        status_b=cmos_read(0x0bU);
        stable_second=cmos_read(0x00U);
    } while (second!=stable_second && --guard);

    if (!(status_b & 0x04U)) {
        second=bcd_to_binary(second);
        minute=bcd_to_binary(minute);
        hour=(uint8_t)((hour & 0x80U)|bcd_to_binary(hour & 0x7fU));
        day=bcd_to_binary(day);
        month=bcd_to_binary(month);
        year=bcd_to_binary(year);
        century=bcd_to_binary(century);
    }

    if (!(status_b & 0x02U) && (hour & 0x80U)) {
        hour=(uint8_t)(((hour & 0x7fU)+12U)%24U);
    } else {
        hour&=0x7fU;
    }

    uint32_t full_year;
    if (century>=19U && century<=30U)
        full_year=(uint32_t)century*100U+year;
    else
        full_year=2000U+year;

    if (full_year<1970U || month<1U || month>12U || day<1U || day>31U ||
        hour>23U || minute>59U || second>59U)
        return 0;

    static const uint8_t month_days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    uint64_t days=days_before_year(full_year)-days_before_year(1970U);
    for (uint32_t i=1; i<month; ++i) {
        days+=month_days[i-1];
        if (i==2U && leap_year(full_year))
            ++days;
    }
    days+=(uint64_t)day-1ULL;
    return days*86400ULL+(uint64_t)hour*3600ULL+
           (uint64_t)minute*60ULL+second;
}

int timer_register_tick_hook(timer_tick_hook_t hook) {
    if (tick_hook!=0 || hook==0)
        return -1;
    tick_hook=hook;
    return 0;
}
