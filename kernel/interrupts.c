#include "interrupts.h"
#include "pic.h"
#include "timer.h"

struct idt_entry {
    uint16_t offset_low; uint16_t selector; uint8_t ist; uint8_t type_attr;
    uint16_t offset_mid; uint32_t offset_high; uint32_t zero;
} __attribute__((packed));

struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));

struct irq_binding { irq_handler_t handler; void *context; };

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
    serial_write_public("\n  vector="); serial_write_hex(frame->vector);
    serial_write_public(" error="); serial_write_hex(frame->error_code);
    serial_write_public(" rip="); serial_write_hex(frame->rip);
    if (frame->vector == 14) {
        serial_write_public(" cr2="); serial_write_hex(read_cr2());
    }
    serial_write_public("\nZEROOS: kernel halted after fatal exception.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

static void idt_set_gate(uint8_t vector, void *handler) {
    uint64_t address=(uint64_t)handler;
    idt[vector].offset_low=(uint16_t)(address&0xffff);
    idt[vector].selector=0x08;
    idt[vector].ist=0;
    idt[vector].type_attr=0x8e;
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

void interrupt_dispatch(struct interrupt_frame *frame) {
    if (frame->vector < 32)
        halt_exception(frame);

    if (frame->vector >= 32 && frame->vector < 48) {
        uint8_t irq=(uint8_t)(frame->vector-32);
        struct irq_binding *binding=&irq_bindings[irq];
        if (binding->handler)
            binding->handler(irq, frame, binding->context);
        pic_send_eoi(irq);
        return;
    }
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

    for (uint16_t i=0;i<256;++i) idt_set_gate((uint8_t)i,isr_stub_table[i]);

    struct idtr descriptor={.limit=(uint16_t)(sizeof(idt)-1),.base=(uint64_t)idt};
    lidt(&descriptor);

    if (irq_register(0,timer_irq_handler,0)!=0) {
        serial_write_public("ZEROOS PANIC: timer IRQ registration failed.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    __asm__ volatile ("sti");
}
