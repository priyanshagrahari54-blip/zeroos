#include <stdint.h>
#include <stdio.h>

#include "tick_deadline.h"

static uint32_t checks;

#define CHECK(expression) do { \
    ++checks; \
    if (!(expression)) { \
        fprintf(stderr,"tick_deadline_test: failed at line %d: %s\n", \
                __LINE__,#expression); \
        return -1; \
    } \
} while (0)

static int test_bounded_timeout_conversion(void) {
    uint64_t deadline=0x55ULL;

    CHECK(zeroos_tick_deadline_from_timeout(100,20,&deadline)==0);
    CHECK(deadline==120);
    CHECK(zeroos_tick_deadline_distance_valid(100,deadline));
    CHECK(!zeroos_tick_deadline_expired(100,deadline));

    CHECK(zeroos_tick_deadline_from_timeout(
              0,ZEROOS_TICK_DEADLINE_MAX_DELTA,&deadline)==0);
    CHECK(deadline==ZEROOS_TICK_DEADLINE_MAX_DELTA);
    CHECK(zeroos_tick_deadline_distance_valid(0,deadline));
    CHECK(!zeroos_tick_deadline_expired(0,deadline));

    deadline=0x55ULL;
    CHECK(zeroos_tick_deadline_from_timeout(
              0,ZEROOS_TICK_DEADLINE_MAX_DELTA+1ULL,&deadline)!=0);
    CHECK(deadline==0x55ULL);
    CHECK(zeroos_tick_deadline_from_timeout(0,1,0)!=0);
    return 0;
}

static int test_modular_wraparound(void) {
    const uint64_t now=UINT64_MAX-2ULL;
    uint64_t deadline=0;

    CHECK(zeroos_tick_deadline_from_timeout(now,5,&deadline)==0);
    CHECK(deadline==2);
    CHECK(zeroos_tick_deadline_distance_valid(now,deadline));
    CHECK(!zeroos_tick_deadline_expired(now,deadline));
    CHECK(!zeroos_tick_deadline_expired(now+4ULL,deadline));
    CHECK(zeroos_tick_deadline_expired(now+5ULL,deadline));
    CHECK(zeroos_tick_deadline_expired(now+6ULL,deadline));
    return 0;
}

static int test_due_past_and_ambiguous_deadlines(void) {
    uint64_t deadline=0;

    CHECK(zeroos_tick_deadline_from_timeout(77,0,&deadline)==0);
    CHECK(deadline==77);
    CHECK(zeroos_tick_deadline_distance_valid(77,deadline));
    CHECK(zeroos_tick_deadline_expired(77,deadline));

    CHECK(!zeroos_tick_deadline_distance_valid(78,deadline));
    CHECK(zeroos_tick_deadline_expired(78,deadline));

    deadline=ZEROOS_TICK_DEADLINE_MAX_DELTA+1ULL;
    CHECK(!zeroos_tick_deadline_distance_valid(0,deadline));
    CHECK(zeroos_tick_deadline_expired(0,deadline));
    return 0;
}

int main(void) {
    CHECK(test_bounded_timeout_conversion()==0);
    CHECK(test_modular_wraparound()==0);
    CHECK(test_due_past_and_ambiguous_deadlines()==0);
    printf("tick_deadline_test: checks=%u failures=0\n",checks);
    return 0;
}
