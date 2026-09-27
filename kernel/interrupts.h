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
void interrupts_load_current_cpu(void);
uint64_t interrupt_dispatch(struct interrupt_frame *frame);
int irq_register(uint8_t irq, irq_handler_t handler, void *context);
int irq_unregister(uint8_t irq, irq_handler_t handler, void *context);

/* MSI/MSI-X vectors for PCI devices (0x60-0x6f, delivered to the BSP LAPIC).
 * The handler runs in interrupt context with the vector as `irq`; EOI and
 * rescheduling are performed by the dispatcher. */
#define ZEROOS_DEVICE_VECTOR_BASE 0x60U
#define ZEROOS_DEVICE_VECTOR_COUNT 16U
int irq_vector_alloc(irq_handler_t handler, void *context);
int irq_vector_free(int vector);
uint64_t irq_vector_spurious_count(void);

#endif
