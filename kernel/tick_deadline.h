#ifndef ZEROOS_TICK_DEADLINE_H
#define ZEROOS_TICK_DEADLINE_H

#include "types.h"

/* Tick deadlines use signed half-range ordering. Future intervals beyond
 * INT64_MAX cannot be ordered consistently after the uint64_t counter wraps. */
#define ZEROOS_TICK_DEADLINE_MAX_DELTA 0x7fffffffffffffffULL

/* Construct an absolute deadline from a bounded relative timeout. Unsigned
 * addition intentionally wraps; the modular distance remains the timeout. */
static inline int zeroos_tick_deadline_from_timeout(uint64_t now,
                                                    uint64_t timeout_ticks,
                                                    uint64_t *deadline_out) {
    if (!deadline_out || timeout_ticks>ZEROOS_TICK_DEADLINE_MAX_DELTA)
        return -1;
    *deadline_out=now+timeout_ticks;
    return 0;
}

/* True for due or future deadlines no more than INT64_MAX ticks away. */
static inline int zeroos_tick_deadline_distance_valid(uint64_t now,
                                                       uint64_t deadline) {
    return (deadline-now)<=ZEROOS_TICK_DEADLINE_MAX_DELTA;
}

/* Treat due, past, and ambiguous (>INT64_MAX) deadlines as expired. */
static inline int zeroos_tick_deadline_expired(uint64_t now,
                                                uint64_t deadline) {
    uint64_t remaining=deadline-now;
    return remaining==0 ||
           remaining>ZEROOS_TICK_DEADLINE_MAX_DELTA;
}

#endif
