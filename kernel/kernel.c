#include "types.h"
#include "timer.h"

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, (uint8_t)c);
}

void serial_write_public(const char *text) {
    while (*text) {
        if (*text == '\n') serial_putc('\r');
        serial_putc(*text++);
    }
}

extern void interrupts_init(void);

void kernel_main(uint64_t multiboot_info, uint64_t multiboot_magic) {
    serial_init();
    serial_write_public("\nZEROOS kernel starting...\n");
    serial_write_public("ZEROOS: entered x86-64 long mode.\n");
    serial_write_public("ZEROOS: serial console initialized.\n");

    if ((uint32_t)multiboot_magic == 0x36d76289)
        serial_write_public("ZEROOS: Multiboot2 handoff verified.\n");
    else
        serial_write_public("ZEROOS: warning: unexpected boot magic.\n");

    (void)multiboot_info;
    interrupts_init();
    serial_write_public("ZEROOS: IDT installed and interrupts enabled.\n");
    serial_write_public("ZEROOS: PIT timer configured at 100 Hz.\n");
    serial_write_public("ZEROOS: foundation milestone reached.\n");

    uint64_t last_report = 0;
    for (;;) {
        __asm__ volatile ("hlt");

        uint64_t now = timer_ticks();
        if (now >= last_report + 100) {
            last_report = now;
            serial_write_public("ZEROOS: timer tick 100.\n");
        }
    }
}
