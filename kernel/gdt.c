#include "gdt.h"
#include "memory.h"

#define ZEROOS_GDT_ENTRIES 7U

struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

struct __attribute__((packed)) gdtr64 {
    uint16_t limit;
    uint64_t base;
};

static uint64_t gdt[ZEROOS_GDT_ENTRIES] __attribute__((aligned(8)));
static struct tss64 runtime_tss __attribute__((aligned(16)));
static struct gdtr64 runtime_gdtr;
static void *runtime_entry_stack;
static int initialized;

static void gdt_set_code(uint32_t index, uint32_t access) {
    /* Long-mode code: G=1, L=1, D=0. */
    gdt[index]=0x00a000000000ffffULL |
               ((uint64_t)(access & 0xffU) << 40);
}

static void gdt_set_data(uint32_t index, uint32_t access) {
    /* Data segment: G=1, D/B=1, L=0. */
    gdt[index]=0x00cf00000000ffffULL |
               ((uint64_t)(access & 0xffU) << 40);
}

static void gdt_set_tss(uint32_t index, uint64_t base, uint32_t limit) {
    uint64_t descriptor =
        ((uint64_t)(limit & 0xffffU)) |
        ((uint64_t)(base & 0xffffffULL) << 16) |
        ((uint64_t)0x89U << 40) |
        ((uint64_t)((limit >> 16) & 0xfU) << 48) |
        ((uint64_t)((base >> 24) & 0xffULL) << 56);

    gdt[index]=descriptor;
    gdt[index+1]=(base >> 32) & 0xffffffffULL;
}

static uint64_t read_rsp(void) {
    uint64_t rsp;
    __asm__ volatile ("mov %%rsp,%0" : "=r"(rsp));
    return rsp;
}

static void load_runtime_gdt(void) {
    runtime_gdtr.limit=(uint16_t)(sizeof(gdt)-1U);
    runtime_gdtr.base=(uint64_t)&gdt[0];

    __asm__ volatile ("lgdt %0" : : "m"(runtime_gdtr) : "memory");

    /*
     * The runtime kernel code selector remains 0x08, matching the bootstrap
     * GDT, so no far control transfer is needed here. Reload the data
     * segments and then load TR with the runtime TSS selector.
     */
    __asm__ volatile (
        "movw %[data], %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "ltr %[tss]"
        :
        : [data] "i"((uint16_t)ZEROOS_GDT_KERNEL_DATA),
          [tss] "r"((uint16_t)ZEROOS_GDT_TSS)
        : "ax", "memory"
    );
}

int gdt_init(void) {
    uint64_t stack_top;

    if (initialized)
        return 0;

    runtime_entry_stack=page_alloc();
    if (!runtime_entry_stack)
        return -1;

    stack_top=(uint64_t)runtime_entry_stack+ZEROOS_PAGE_SIZE;
    stack_top &= ~0xFULL;

    gdt[0]=0;
    gdt_set_code(1,0x9aU);
    gdt_set_data(2,0x92U);
    gdt_set_code(3,0xfaU);
    gdt_set_data(4,0xf2U);

    runtime_tss.rsp0=stack_top;
    runtime_tss.iomap_base=(uint16_t)sizeof(runtime_tss);
    gdt_set_tss(5,(uint64_t)&runtime_tss,
                 (uint32_t)(sizeof(runtime_tss)-1U));

    load_runtime_gdt();
    initialized=1;
    return 0;
}

int gdt_is_initialized(void) {
    return initialized;
}

uint16_t gdt_kernel_code_selector(void) {
    return ZEROOS_GDT_KERNEL_CODE;
}

uint16_t gdt_kernel_data_selector(void) {
    return ZEROOS_GDT_KERNEL_DATA;
}

uint16_t gdt_user_code_selector(void) {
    return ZEROOS_GDT_USER_CODE;
}

uint16_t gdt_user_data_selector(void) {
    return ZEROOS_GDT_USER_DATA;
}

uint16_t gdt_tss_selector(void) {
    return ZEROOS_GDT_TSS;
}

uint64_t gdt_base(void) {
    return runtime_gdtr.base;
}

uint64_t gdt_kernel_stack(void) {
    return runtime_tss.rsp0;
}

int gdt_set_kernel_stack(uint64_t stack_top) {
    if (!initialized || stack_top==0 || (stack_top & 0xfULL)!=0)
        return -1;

    runtime_tss.rsp0=stack_top;
    return 0;
}
