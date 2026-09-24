#include "pic.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

void pic_mask_all(void) {
    outb(PIC1_DATA,0xff);
    outb(PIC2_DATA,0xff);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t bit;
    uint8_t mask;

    if (irq >= 16 || irq == 2)
        return;
    port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    bit = irq < 8 ? irq : (uint8_t)(irq - 8U);
    mask = inb(port);
    mask = (uint8_t)(mask & (uint8_t)~(1U << bit));
    outb(port, mask);
}

void pic_init(void) {
    /* ICW1: start initialization, expect ICW4. */
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    /* ICW2: move IRQs away from CPU exception vectors 0-31. */
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    /* ICW3: master IRQ2 is the slave cascade input. */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4: 8086/88 mode. */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /*
     * Start with only IRQ0 (PIT timer) unmasked.
     * IRQ2 must remain unmasked because it is the slave cascade.
     */
    outb(PIC1_DATA, 0xFA);
    outb(PIC2_DATA, 0xFF);
}
