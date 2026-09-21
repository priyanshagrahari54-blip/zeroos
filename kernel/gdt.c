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
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct zeroos_tss) == 104, "x86-64 TSS size");
_Static_assert(__builtin_offsetof(struct zeroos_tss, rsp0) == 4, "TSS RSP0");
_Static_assert(__builtin_offsetof(struct zeroos_tss, ist) == 36, "TSS IST1");
_Static_assert(__builtin_offsetof(struct zeroos_tss, iomap_base) == 102, "TSS IOPB");
_Static_assert(sizeof(struct gdt_entry) == 8, "GDT slot size");
static uint8_t double_fault_stack[16384] __attribute__((aligned(16)));
static uint8_t nmi_stack[16384] __attribute__((aligned(16)));

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

static void lgdt(const struct gdt_pointer *descriptor) {
    __asm__ volatile ("lgdt %0" : : "m"(*descriptor));
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
    /* Standard x86-64 descriptors; the ZEROOS selector order is fixed
     * by our documented syscall ABI. Code has L=1/D=0; data has D=1.
     * User data is ordinary expand-up data, not executable code.
     */
    gdt_fill(&gdt[0], 0, 0, 0, 0);
    gdt_fill(&gdt[1], 0xffffff, 0, 0x9A, 0xAF);
    gdt_fill(&gdt[2], 0xffffff, 0, 0x92, 0xC0);
    gdt_fill(&gdt[3], 0xffffff, 0, 0xF2, 0xC0);
    gdt_fill(&gdt[4], 0xffffff, 0, 0xFA, 0xA0);
    gdt_fill(&gdt[5], 103, (uint64_t)&tss, 0x89, 0x00);

    /* The upper half of the 16-byte TSS descriptor stores base[63:32]. */
    uint64_t upper = (uint64_t)&tss >> 32;
    uint8_t *bytes = (uint8_t *)&gdt[6];
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (uint8_t)(upper >> (i * 8));
    tss.rsp0 = (uint64_t)&stack_top;
    tss.ist[0] = (uint64_t)(double_fault_stack + sizeof(double_fault_stack));
    tss.ist[1] = (uint64_t)(nmi_stack + sizeof(nmi_stack));
    tss.iomap_base = sizeof(tss);

    gdt_pointer.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_pointer.base = (uint64_t)gdt;

    lgdt(&gdt_pointer);
    struct gdt_pointer readback;
    __asm__ volatile ("sgdt %0" : "=m"(readback));
    if (readback.limit != gdt_pointer.limit || readback.base != gdt_pointer.base) {
        serial_write_public("ZEROOS PANIC: GDT load verification failed.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
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
