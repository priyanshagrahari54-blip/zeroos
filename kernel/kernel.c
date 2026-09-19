typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

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
    while ((inb(COM1 + 5) & 0x20) == 0) {
    }
    outb(COM1, (uint8_t)c);
}

static void serial_write(const char *text) {
    while (*text) {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text++);
    }
}

void kernel_main(uint64_t multiboot_info, uint64_t multiboot_magic) {
    serial_init();

    serial_write("\nZEROOS kernel starting...\n");
    serial_write("ZEROOS: entered x86-64 long mode.\n");
    serial_write("ZEROOS: serial console initialized.\n");

    if ((uint32_t)multiboot_magic == 0x36d76289) {
        serial_write("ZEROOS: Multiboot2 handoff verified.\n");
    } else {
        serial_write("ZEROOS: warning: unexpected boot magic.\n");
    }

    (void)multiboot_info;
    serial_write("ZEROOS: foundation milestone reached.\n");
    serial_write("ZEROOS: next: interrupts, memory manager, and hardware discovery.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
