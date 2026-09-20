#include "gdt.h"
#include "memory.h"

extern void serial_write_public(const char *text);
extern char stack_top;

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* x86-64 task state segment; 104 bytes are addressable. */
struct zeroos_tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t reserved1;
    uint64_t reserved2;
    uint64_t reserved3;
    uint16_t iomap_base;
    uint8_t  reserved4[66];
} __attribute__((packed));

static struct gdt_entry gdt[8];
static struct gdt_pointer gdt_pointer;
static struct zeroos_tss tss;

static void gdt_fill(struct gdt_entry *entry,
                     uint32_t limit,
                     uint64_t base,
                     uint8_t access,
                     uint8_t flags) {
    entry->limit_low = (uint16_t)(limit & 0xffff);
    entry->base_low = (uint16_t)(base & 0xffff);
    entry->base_mid = (uint8_t)((base >> 16) & 0xff);
    entry->access = access;
    entry->flags = (uint8_t)(((limit >> 16) & 0x0f) | (flags & 0xf0));
    entry->base_high = (uint8_t)((base >> 24) & 0xff);
}

static void lidt(const struct gdt_pointer *descriptor) {
    __asm__ volatile ("lidt %0" : : "m"(*descriptor));
}

static void ltr(uint16_t selector) {
    __asm__ volatile ("ltr %w0" : : "r"(selector));
}

static uint16_t str_read(void) {
    uint16_t selector;
    __asm__ volatile ("str %w0" : "=r"(selector));
    return selector;
}

void gdt_init(void) {
    /*
     * Access bytes:
     *   kernel code  DPL0: P=1 S=1 type=1010 (code, readable, accessed) = 0x9A
     *   kernel data  DPL0: P=1 S=1 type=0010 (data, writable, accessed) = 0x92
     *   user  code   DPL3: P=1 S=1 type=1010                              = 0xF2
     *   user  data   DPL3: P=1 S=1 type=0010                              = 0xF6
     *   TSS:          P=1 S=0 type=0011 busy=1                            = 0x89
     * Flags:
     *   64-bit code: L=1 D=1 G=1 = 0xAF
     *   data:        D=1 G=1     = 0xC0
     *   TSS:         G=0         = 0x00
     */
    gdt_fill(&gdt[0], 0, 0, 0, 0);
    gdt_fill(&gdt[1], 0xffffff, 0, 0x9A, 0xAF);
    gdt_fill(&gdt[2], 0xffffff, 0, 0x92, 0xC0);
    gdt_fill(&gdt[3], 0xffffff, 0, 0xF6, 0xC0);
    gdt_fill(&gdt[4], 0xffffff, 0, 0xF2, 0xAF);
    gdt_fill(&gdt[5], 103, (uint64_t)&tss, 0x89, 0x00);

    /*
     * RSP0 serves as the kernel stack for exceptions/interrupts taken
     * while in ring 3 and for IST exceptions (double fault, NMI). It is
     * updated on every task switch; before the task system runs, point it
     * into the early boot stack so an IST exception can never land on a
     * null or corrupt stack.
     */
    tss.rsp0 = (uint64_t)stack_top - 512ULL;
    tss.iomap_base = 104;

    gdt_pointer.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_pointer.base = (uint64_t)gdt;

    lidt(&gdt_pointer);
    ltr(ZEROOS_SEL_TSS);

    if (str_read() != ZEROOS_SEL_TSS) {
        serial_write_public("ZEROOS PANIC: TSS load verification failed.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
}

void gdt_set_rsp0(uint64_t kernel_stack_top) {
    tss.rsp0 = kernel_stack_top;
}

uint64_t gdt_get_rsp0(void) {
    return tss.rsp0;
}
