#include "types.h"
#include "memory.h"
#include "timer.h"
#include "vmm.h"

#define COM1 0x3F8
#define VMM_SELF_TEST_VA 0x4000000000ULL

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
        if (*text == '\n')
            serial_putc('\r');
        serial_putc(*text++);
    }
}

extern void interrupts_init(void);

static void serial_write_u64(uint64_t value) {
    char buffer[21];
    int pos = 20;
    buffer[pos] = '\0';

    if (value == 0) {
        serial_write_public("0");
        return;
    }

    while (value > 0 && pos > 0) {
        buffer[--pos] = (char)('0' + (value % 10));
        value /= 10;
    }

    serial_write_public(&buffer[pos]);
}

static void kernel_panic(const char *message) {
    serial_write_public("ZEROOS PANIC: ");
    serial_write_public(message);
    serial_write_public("\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void memory_self_test(void) {
    uint64_t before = memory_free_pages();
    void *a = page_alloc();
    void *b = page_alloc();

    if (!a || !b || a == b)
        kernel_panic("physical page allocator self-test failed");

    page_free(b);
    page_free(a);

    if (memory_free_pages() != before)
        kernel_panic("physical page allocator accounting failed");

    serial_write_public("ZEROOS: physical allocator self-test passed.\n");
}

static void vmm_self_test(void) {
    void *physical = page_alloc();
    if (!physical)
        kernel_panic("VMM self-test could not allocate a page");

    if (vmm_map_page(VMM_SELF_TEST_VA,
                     (uint64_t)physical,
                     VMM_WRITABLE | VMM_NO_EXECUTE) != 0)
        kernel_panic("VMM map failed");

    if (vmm_translate(VMM_SELF_TEST_VA) != (uint64_t)physical)
        kernel_panic("VMM translation mismatch");

    if (vmm_unmap_page(VMM_SELF_TEST_VA) != 0)
        kernel_panic("VMM unmap failed");

    page_free(physical);
    serial_write_public("ZEROOS: virtual memory self-test passed.\n");
}

void kernel_main(uint64_t multiboot_info, uint64_t multiboot_magic) {
    serial_init();
    serial_write_public("\nZEROOS kernel starting...\n");
    serial_write_public("ZEROOS: entered x86-64 long mode.\n");
    serial_write_public("ZEROOS: serial console initialized.\n");

    if ((uint32_t)multiboot_magic == 0x36d76289)
        serial_write_public("ZEROOS: Multiboot2 handoff verified.\n");
    else
        kernel_panic("unexpected Multiboot2 boot magic");

    memory_init(multiboot_info);
    serial_write_public("ZEROOS: physical page allocator initialized.\n");
    serial_write_public("ZEROOS: managed pages: ");
    serial_write_u64(memory_total_pages());
    serial_write_public("\nZEROOS: free pages: ");
    serial_write_u64(memory_free_pages());
    serial_write_public("\n");

    memory_self_test();

    if (vmm_init() != 0)
        kernel_panic("virtual memory initialization failed");

    serial_write_public("ZEROOS: virtual memory manager initialized.\n");
    vmm_self_test();

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
