#include "interrupts.h"
#include "pic.h"
#include "timer.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
extern void *isr_stub_table[256];

extern void serial_write_public(const char *text);

static void idt_set_gate(uint8_t vector, void *handler) {
    uint64_t address = (uint64_t)handler;
    idt[vector].offset_low = (uint16_t)(address & 0xffff);
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].type_attr = 0x8e;
    idt[vector].offset_mid = (uint16_t)((address >> 16) & 0xffff);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].zero = 0;
}

static inline void lidt(const struct idtr *descriptor) {
    __asm__ volatile ("lidt %0" : : "m"(*descriptor));
}

void interrupt_dispatch(struct interrupt_frame *frame) {
    if (frame->vector < 32) {
        serial_write_public("ZEROOS: CPU exception vector ");
        if (frame->vector == 3) {
            serial_write_public("3 (#BP breakpoint).\n");
        } else {
            serial_write_public("unexpected exception.\n");
        }
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    if (frame->vector == 32) {
        timer_tick();
    }
}

void interrupts_init(void) {
    pic_init();
    timer_init();

    for (uint16_t i = 0; i < 256; ++i) {
        idt_set_gate((uint8_t)i, isr_stub_table[i]);
    }

    struct idtr descriptor = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)idt
    };

    lidt(&descriptor);
    __asm__ volatile ("sti");
}
