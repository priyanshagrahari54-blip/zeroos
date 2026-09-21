#include "interrupts.h"
#include "pic.h"
#include "timer.h"
#include "scheduler.h"
#include "task.h"
#include "process.h"

struct idt_entry {
    uint16_t offset_low; uint16_t selector; uint8_t ist; uint8_t type_attr;
    uint16_t offset_mid; uint32_t offset_high; uint32_t zero;
} __attribute__((packed));

struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));

struct irq_binding { irq_handler_t handler; void *context; };

_Static_assert(sizeof(struct idt_entry) == 16, "x86-64 IDT gate size");
_Static_assert(__builtin_offsetof(struct idt_entry, ist) == 4, "IDT IST offset");
_Static_assert(__builtin_offsetof(struct idt_entry, type_attr) == 5, "IDT type offset");
_Static_assert(__builtin_offsetof(struct interrupt_frame, rip) == 136, "ISR RIP offset");
_Static_assert(sizeof(struct interrupt_frame) == 176, "ISR frame size");

static struct idt_entry idt[256];
static struct irq_binding irq_bindings[16];

extern void *isr_stub_table[256];
extern void serial_write_public(const char *text);

static inline uint64_t read_cr2(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char buffer[19];
    buffer[0]='0'; buffer[1]='x';
    for (int i=0;i<16;++i) buffer[2+i]=digits[(value>>(60-i*4))&0xf];
    buffer[18]='\0';
    serial_write_public(buffer);
}

static void exception_name(uint64_t vector) {
    static const char *names[32] = {
        "#DE divide error","#DB debug","NMI","#BP breakpoint","#OF overflow",
        "#BR bound range","#UD invalid opcode","#NM device not available",
        "#DF double fault","coprocessor segment","#TS invalid TSS",
        "#NP segment not present","#SS stack fault","#GP general protection",
        "#PF page fault","reserved","#MF x87 floating point","#AC alignment check",
        "#MC machine check","#XM SIMD floating point","#VE virtualization exception",
        "#CP control protection","reserved","reserved","reserved","reserved",
        "reserved","reserved","#VC VMM communication","#SX security exception",
        "reserved"
    };
    serial_write_public(names[vector < 32 ? vector : 31]);
}

static void halt_exception(struct interrupt_frame *frame) {
    serial_write_public("ZEROOS: exception ");
    exception_name(frame->vector);
    serial_write_public("\n  frame="); serial_write_hex((uint64_t)frame);
    serial_write_public(" vector="); serial_write_hex(frame->vector);
    serial_write_public(" error="); serial_write_hex(frame->error_code);
    serial_write_public(" rip="); serial_write_hex(frame->rip);
    serial_write_public(" cs="); serial_write_hex(frame->cs);
    serial_write_public(" rflags="); serial_write_hex(frame->rflags);
    serial_write_public(" rsp="); serial_write_hex(frame->rsp);
    serial_write_public(" ss="); serial_write_hex(frame->ss);
    {
        struct task *task=task_current();
        if (task) {
            serial_write_public(" task="); serial_write_hex(task->id);
            serial_write_public(" stack="); serial_write_hex(task->stack_base);
            serial_write_public(" saved="); serial_write_hex(task->saved_stack);
        }
    }
    if (frame->vector == 14) {
        serial_write_public(" cr2="); serial_write_hex(read_cr2());
    }
    serial_write_public("\nZEROOS: kernel halted after fatal exception.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

static void idt_set_gate(uint8_t vector, void *handler, uint8_t ist) {
    uint64_t address=(uint64_t)handler;
    idt[vector].offset_low=(uint16_t)(address&0xffff);
    idt[vector].selector=0x08;
    idt[vector].ist=(uint8_t)(ist & 7U);
    idt[vector].type_attr=0x8e; /* present, DPL0 interrupt gate */
    idt[vector].offset_mid=(uint16_t)((address>>16)&0xffff);
    idt[vector].offset_high=(uint32_t)(address>>32);
    idt[vector].zero=0;
}

static inline void lidt(const struct idtr *descriptor) {
    __asm__ volatile ("lidt %0" : : "m"(*descriptor));
}

static void timer_irq_handler(uint8_t irq, struct interrupt_frame *frame, void *context) {
    (void)irq; (void)frame; (void)context;
    timer_tick();
}

/*
 * User-mode fault containment. Exceptions taken at CPL3 (page fault,
 * general protection, segment-not-present, stack fault) must not take the
 * kernel down: the current process is killed, reported, and the scheduler
 * switches to the next task through the normal IRQ-exit path.
 * Kernel-mode exceptions remain fatal.
 */
static uint64_t user_fault_dispatch(struct interrupt_frame *frame) {
    /*
     * User-mode faults arrive with the user RFLAGS, which may have IF set.
     * The handler marks the current task as a zombie before the IRQ-exit
     * reschedule runs; a timer tick landing in that window would see a
     * non-running current task and panic. Disable interrupts for the whole
     * fault path; the iretq epilogue restores the (new) task's RFLAGS.
     */
    __asm__ volatile ("cli" ::: "memory");
    serial_write_public("ZEROOS: user fault contained: vector=");
    serial_write_hex(frame->vector);
    serial_write_public(" rip=");
    serial_write_hex(frame->rip);
    if (frame->vector == 14) {
        serial_write_public(" cr2=");
        serial_write_hex(read_cr2());
    }
    {
        struct task *task = task_current();
        if (task) {
            serial_write_public(" task=");
            serial_write_hex(task->id);
        }
    }
    serial_write_public("\n");

    struct task *task = task_current();
    if (!task || !task->process) {
        /* An RPL3 exception without a user process is an internal
           invariant break; treat it as fatal. */
        halt_exception(frame);
    }
    process_user_fault(frame->rip, frame->vector);
    return task_reschedule_from_interrupt(frame);
}

uint64_t interrupt_dispatch(struct interrupt_frame *frame) {
    if (frame->vector < 32) {
        /* Emergency-stack faults are fatal regardless of interrupted CPL;
         * they must never be handed to the task-stack rescheduler. */
        if (frame->vector==2 || frame->vector==8) halt_exception(frame);
        /*
         * In 64-bit mode the CPU always pushes the full five-word frame
         * (SS, RSP, RFLAGS, CS, RIP), so both the CS and SS words are
         * valid here. CS.RPL is the privilege of the interrupted code:
         * 0 for kernel execution, 3 for user execution.
         */
        if ((frame->cs & 3) != 0)
            return user_fault_dispatch(frame);
        halt_exception(frame);
    }

    if (frame->vector >= 32 && frame->vector < 48) {
        uint8_t irq=(uint8_t)(frame->vector-32);
        struct irq_binding *binding=&irq_bindings[irq];
        if (binding->handler)
            binding->handler(irq, frame, binding->context);
        pic_send_eoi(irq);

        /*
         * Scheduling is deliberately deferred until after the device EOI
         * and handler return. The assembly epilogue then restores either
         * this frame or another task's complete frame.
         */
        return task_reschedule_from_interrupt(frame);
    }

    return (uint64_t)frame;
}

int irq_register(uint8_t irq, irq_handler_t handler, void *context) {
    if (irq>=16 || handler==0 || irq_bindings[irq].handler!=0) return -1;
    irq_bindings[irq].context=context;
    irq_bindings[irq].handler=handler;
    return 0;
}

int irq_unregister(uint8_t irq, irq_handler_t handler, void *context) {
    if (irq>=16 || handler==0) return -1;
    if (irq_bindings[irq].handler!=handler || irq_bindings[irq].context!=context) return -1;
    irq_bindings[irq].handler=0;
    irq_bindings[irq].context=0;
    return 0;
}

void interrupts_init(void) {
    pic_init();
    timer_init();

    for (uint16_t i=0;i<256;++i) idt_set_gate((uint8_t)i,isr_stub_table[i],0);

    /* Independent, fixed emergency stacks; IST is unrelated to RSP0. */
    idt_set_gate(8, isr_stub_table[8], 1);
    idt_set_gate(2, isr_stub_table[2], 2);

    struct idtr descriptor={.limit=(uint16_t)(sizeof(idt)-1),.base=(uint64_t)idt};
    lidt(&descriptor);
    struct idtr readback;
    __asm__ volatile ("sidt %0" : "=m"(readback));
    if (readback.limit != descriptor.limit || readback.base != descriptor.base ||
        idt[8].ist != 1 || idt[2].ist != 2 ||
        idt[8].type_attr != 0x8e || idt[2].type_attr != 0x8e) {
        serial_write_public("ZEROOS PANIC: IDT load verification failed.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

#if ZEROOS_LATE_FAULT_TEST == 2
    /* Software invocation exercises the NMI gate and IST2 stack. */
    __asm__ volatile ("int $2");
#elif ZEROOS_LATE_FAULT_TEST == 6
    __asm__ volatile ("ud2");
#elif ZEROOS_LATE_FAULT_TEST == 8
    /* A #GP whose handler is not present escalates to #DF. This is a
     * real double fault, not INT 8 (which would not push an error code). */
    idt[13].type_attr = 0;
    __asm__ volatile ("mov $0xffff, %%ax; mov %%ax, %%ss" : : : "rax", "memory");
#endif

    if (irq_register(0,timer_irq_handler,0)!=0) {
        serial_write_public("ZEROOS PANIC: timer IRQ registration failed.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    __asm__ volatile ("sti");
}
