#include "interrupts.h"
#include "cpu.h"
#include "apic.h"
#include "pic.h"
#include "timer.h"
#include "gdt.h"
#include "scheduler.h"
#include "task.h"
#include "thread.h"
#include "tlb.h"
#include "sync.h"
#include "syscall.h"

struct idt_entry {
    uint16_t offset_low; uint16_t selector; uint8_t ist; uint8_t type_attr;
    uint16_t offset_mid; uint32_t offset_high; uint32_t zero;
} __attribute__((packed));

struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));

struct irq_binding { irq_handler_t handler; void *context; };

static struct idt_entry idt[256];
static struct irq_binding irq_bindings[16];
/* Message-signalled device vectors. Allocation is serialized by a spinlock;
 * the dispatcher reads the handler with acquire ordering, and the context is
 * published before the handler. */
static struct irq_binding device_vectors[ZEROOS_DEVICE_VECTOR_COUNT];
static struct spinlock device_vector_lock;
static uint64_t device_vector_spurious;
static struct idtr runtime_idtr;

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

static int canonical_address(uint64_t address) {
    uint64_t sign=(address>>47)&1ULL;
    uint64_t upper=address>>48;
    return sign ? upper==0xffffULL : upper==0;
}

static int interrupt_frame_sane(const struct interrupt_frame *frame) {
    if (!frame || frame->vector>=256 ||
        !(frame->rflags & (1ULL<<1)))
        return 0;
    if ((frame->cs & 3ULL)==3ULL) {
        if ((frame->ss & 3ULL)!=3ULL || !canonical_address(frame->rip) ||
            !canonical_address(frame->rsp))
            return 0;
    } else if ((frame->cs & 3ULL)!=0 || !canonical_address(frame->rip)) {
        return 0;
    }
    return 1;
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
    serial_write_public("\n  vector="); serial_write_hex(frame->vector);
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
        serial_write_public(" pf[protection="); serial_write_hex(frame->error_code & 1ULL);
        serial_write_public(" write="); serial_write_hex((frame->error_code>>1)&1ULL);
        serial_write_public(" user="); serial_write_hex((frame->error_code>>2)&1ULL);
        serial_write_public(" reserved="); serial_write_hex((frame->error_code>>3)&1ULL);
        serial_write_public(" instruction="); serial_write_hex((frame->error_code>>4)&1ULL);
        serial_write_public("]");
    }
    serial_write_public("\nZEROOS: kernel halted after fatal exception.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

static int user_exception_containable(uint64_t vector) {
    /* Platform-fatal delivery is never converted into process termination. */
    return vector<32 && vector!=2 && vector!=8 && vector!=15 && vector!=18;
}

static void contain_user_exception(struct interrupt_frame *frame) {
    struct thread *thread=thread_current();

    serial_write_public("ZEROOS: terminating thread after user exception ");
    exception_name(frame->vector);
    serial_write_public(" tid=");
    if (thread)
        serial_write_hex(thread->tid);
    else
        serial_write_public("0");
    if (frame->vector==14) {
        serial_write_public(" cr2=");
        serial_write_hex(read_cr2());
    }
    serial_write_public(".\n");

    /*
     * User faults arrive on the task's privilege-entry/IST path, not as a
     * resumable kernel-task frame. Termination therefore retires the faulting
     * task without publishing the IST frame as scheduler-owned context.
     */
    cpu_irq_exit();
    if (!thread || thread_exit(0x100ULL+frame->vector)!=0)
        halt_exception(frame);
    for (;;) __asm__ volatile ("cli; hlt");
}

static void idt_set_gate(uint8_t vector, void *handler) {
    uint64_t address=(uint64_t)handler;
    idt[vector].offset_low=(uint16_t)(address&0xffff);
    idt[vector].selector=0x08;
    idt[vector].ist=gdt_exception_ist(vector);
    idt[vector].type_attr=0x8e;
    idt[vector].offset_mid=(uint16_t)((address>>16)&0xffff);
    idt[vector].offset_high=(uint32_t)(address>>32);
    idt[vector].zero=0;
}

static inline void lidt(const struct idtr *descriptor) {
    __asm__ volatile ("lidt %0" : : "m"(*descriptor));
}

void interrupts_load_current_cpu(void) {
    if (runtime_idtr.base)
        lidt(&runtime_idtr);
}

static void timer_irq_handler(uint8_t irq, struct interrupt_frame *frame, void *context) {
    (void)irq; (void)frame; (void)context;
    timer_tick();
}

uint64_t interrupt_dispatch(struct interrupt_frame *frame) {
    if (!interrupt_frame_sane(frame)) {
        serial_write_public("ZEROOS PANIC: malformed interrupt frame.\n");
        for (;;) __asm__ volatile("cli; hlt");
    }

    uint64_t result=(uint64_t)frame;
    cpu_irq_enter();

    if (frame->vector < 32) {
        if ((frame->cs & 3ULL)==3ULL &&
            user_exception_containable(frame->vector))
            contain_user_exception(frame);
        halt_exception(frame);
    }

    if (frame->vector==ZEROOS_TLB_SHOOTDOWN_VECTOR) {
        if (tlb_handle_ipi(cpu_current_id())!=0) {
            serial_write_public("ZEROOS PANIC: unclaimed TLB shootdown IPI.\n");
            for (;;) __asm__ volatile ("cli; hlt");
        }
        if (apic_controller()==ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC)
            apic_eoi();
        cpu_irq_exit();
        return (uint64_t)frame;
    }

    if (frame->vector==ZEROOS_SCHEDULER_WAKE_VECTOR) {
        /* The AP scheduler gate is polled from the private bootstrap loop;
         * this vector only releases its HLT and never performs a handoff from
         * interrupt context. */
        if (apic_controller()==ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC)
            apic_eoi();
        cpu_irq_exit();
        return (uint64_t)frame;
    }

    if (frame->vector==ZEROOS_SCHEDULER_OFFLINE_VECTOR) {
        if (cpu_current_id()==0) {
            serial_write_public("ZEROOS PANIC: BSP received CPU-offline IPI.\\n");
            for (;;) __asm__ volatile ("cli; hlt");
        }
        result=task_cpu_offline_from_interrupt(frame);
        if (apic_controller()==ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC)
            apic_eoi();
        cpu_irq_exit();
        return result;
    }

    if (frame->vector==ZEROOS_SYSCALL_VECTOR) {
        syscall_dispatch(frame);
        if (task_current() && task_current()->state==TASK_RUNNING &&
            task_need_resched())
            result=task_reschedule_from_interrupt(frame);
        cpu_irq_exit();
        return result;
    }

    if (frame->vector==ZEROOS_SCHEDULER_TICK_VECTOR) {
        if (cpu_current_id()!=0 && task_scheduler_ready()) {
            scheduler_tick_remote();
            result=task_reschedule_from_interrupt(frame);
        }
        if (apic_controller()==ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC)
            apic_eoi();
        cpu_irq_exit();
        return result;
    }

    if (frame->vector >= ZEROOS_DEVICE_VECTOR_BASE &&
        frame->vector < ZEROOS_DEVICE_VECTOR_BASE+ZEROOS_DEVICE_VECTOR_COUNT) {
        struct irq_binding *binding=
            &device_vectors[frame->vector-ZEROOS_DEVICE_VECTOR_BASE];
        irq_handler_t handler=__atomic_load_n(&binding->handler,__ATOMIC_ACQUIRE);
        if (handler)
            handler((uint8_t)frame->vector,frame,binding->context);
        else
            ++device_vector_spurious;
        /* MSI/MSI-X are edge messages to the local APIC. */
        apic_eoi();
        if (cpu_current_id()!=0) {
            cpu_irq_exit();
            return (uint64_t)frame;
        }
        result=task_reschedule_from_interrupt(frame);
        cpu_irq_exit();
        return result;
    }

    if (frame->vector >= 32 && frame->vector < 48) {
        uint8_t irq=(uint8_t)(frame->vector-32);
        struct irq_binding *binding=&irq_bindings[irq];
        if (binding->handler)
            binding->handler(irq, frame, binding->context);
        if (apic_controller()==ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC)
            apic_eoi();
        else
            pic_send_eoi(irq);

        /* APs do not borrow the BSP scheduler context. Their future
         * per-CPU device/timer queues get a separate dispatch boundary. */
        if (cpu_current_id()!=0) {
            cpu_irq_exit();
            return (uint64_t)frame;
        }

        /*
         * Scheduling is deliberately deferred until after the device EOI
         * and handler return. The assembly epilogue then restores either
         * this frame or another task's complete frame.
         */
        result=task_reschedule_from_interrupt(frame);
    }

    cpu_irq_exit();
    return result;
}

int irq_register(uint8_t irq, irq_handler_t handler, void *context) {
    if (irq>=16 || handler==0 || irq_bindings[irq].handler!=0) return -1;
    irq_bindings[irq].context=context;
    irq_bindings[irq].handler=handler;
    return 0;
}

int irq_vector_alloc(irq_handler_t handler, void *context) {
    int vector=-1;
    if (!handler)
        return -1;
    uint64_t flags=spin_lock_irqsave(&device_vector_lock);
    for (uint32_t i=0; i<ZEROOS_DEVICE_VECTOR_COUNT; ++i) {
        if (device_vectors[i].handler)
            continue;
        device_vectors[i].context=context;
        __atomic_store_n(&device_vectors[i].handler,handler,__ATOMIC_RELEASE);
        vector=(int)(ZEROOS_DEVICE_VECTOR_BASE+i);
        break;
    }
    spin_unlock_irqrestore(&device_vector_lock,flags);
    return vector;
}

int irq_vector_free(int vector) {
    if (vector<(int)ZEROOS_DEVICE_VECTOR_BASE ||
        vector>=(int)(ZEROOS_DEVICE_VECTOR_BASE+ZEROOS_DEVICE_VECTOR_COUNT))
        return -1;
    uint64_t flags=spin_lock_irqsave(&device_vector_lock);
    struct irq_binding *binding=&device_vectors[vector-ZEROOS_DEVICE_VECTOR_BASE];
    __atomic_store_n(&binding->handler,(irq_handler_t)0,__ATOMIC_RELEASE);
    binding->context=0;
    spin_unlock_irqrestore(&device_vector_lock,flags);
    return 0;
}

uint64_t irq_vector_spurious_count(void) {
    return device_vector_spurious;
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

    spinlock_init(&device_vector_lock);
    for (uint16_t i=0;i<256;++i) idt_set_gate((uint8_t)i,isr_stub_table[i]);
    /* User software may enter only through the versioned syscall vector;
     * every other gate remains supervisor-only (DPL0). */
    idt[ZEROOS_SYSCALL_VECTOR].type_attr=0xee;

    runtime_idtr=(struct idtr){
        .limit=(uint16_t)(sizeof(idt)-1),
        .base=(uint64_t)idt
    };
    interrupts_load_current_cpu();

    if (irq_register(0,timer_irq_handler,0)!=0) {
        serial_write_public("ZEROOS PANIC: timer IRQ registration failed.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    if (apic_activate_timer()==0)
        serial_write_public("ZEROOS: LAPIC/IOAPIC timer routing activated.\n");
    else
        serial_write_public("ZEROOS: legacy PIC timer routing retained.\n");
    serial_write_public("ZEROOS: user fault containment policy armed.\n");
    __asm__ volatile ("sti");
}
