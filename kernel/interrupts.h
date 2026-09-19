#ifndef ZEROOS_INTERRUPTS_H
#define ZEROOS_INTERRUPTS_H
#include "types.h"

struct interrupt_frame {
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip,cs,rflags,rsp,ss;
};

typedef void (*irq_handler_t)(uint8_t irq, struct interrupt_frame *frame, void *context);

void interrupts_init(void);
uint64_t interrupt_dispatch(struct interrupt_frame *frame);
int irq_register(uint8_t irq, irq_handler_t handler, void *context);
int irq_unregister(uint8_t irq, irq_handler_t handler, void *context);

#endif
