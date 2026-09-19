#ifndef ZEROOS_INTERRUPTS_H
#define ZEROOS_INTERRUPTS_H

typedef unsigned long long uint64_t;

struct interrupt_frame {
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

void interrupts_init(void);
void interrupt_dispatch(struct interrupt_frame *frame);

#endif
