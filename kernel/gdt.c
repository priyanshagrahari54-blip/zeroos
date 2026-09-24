#include "gdt.h"
#include "memory.h"
#include "cpu.h"

#define ZEROOS_GDT_ENTRIES 7U
#define ZEROOS_STACK_GUARD 0x5a45524f4953544bULL

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

struct gdt_cpu_state {
    uint64_t gdt[ZEROOS_GDT_ENTRIES] __attribute__((aligned(8)));
    struct tss64 tss __attribute__((aligned(16)));
    struct gdtr64 gdtr;
    void *entry_stack;
    void *ist_stacks[ZEROOS_GDT_IST_COUNT];
    uint64_t ist_tops[ZEROOS_GDT_IST_COUNT];
    uint8_t initialized;
};

static struct gdt_cpu_state states[ZEROOS_MAX_CPUS] __attribute__((aligned(64)));

static struct gdt_cpu_state *current_state(void) {
    uint32_t cpu_id=cpu_current_id();
    if (cpu_id>=ZEROOS_MAX_CPUS)
        return (struct gdt_cpu_state *)0;
    return &states[cpu_id];
}

static void gdt_set_code(uint64_t *gdt, uint32_t index, uint32_t access) {
    /* Long-mode code: G=1, L=1, D=0. */
    gdt[index]=0x00a000000000ffffULL |
               ((uint64_t)(access & 0xffU) << 40);
}

static void gdt_set_data(uint64_t *gdt, uint32_t index, uint32_t access) {
    /* Data segment: G=1, D/B=1, L=0. */
    gdt[index]=0x00cf00000000ffffULL |
               ((uint64_t)(access & 0xffU) << 40);
}

static void gdt_set_tss(uint64_t *gdt, uint32_t index,
                        uint64_t base, uint32_t limit) {
    uint64_t descriptor=
        ((uint64_t)(limit & 0xffffU)) |
        ((uint64_t)(base & 0xffffffULL) << 16) |
        ((uint64_t)0x89U << 40) |
        ((uint64_t)((limit >> 16) & 0xfU) << 48) |
        ((uint64_t)((base >> 24) & 0xffULL) << 56);

    gdt[index]=descriptor;
    gdt[index+1]=(base >> 32) & 0xffffffffULL;
}

static void load_runtime_gdt(struct gdt_cpu_state *state) {
    state->gdtr.limit=(uint16_t)(sizeof(state->gdt)-1U);
    state->gdtr.base=(uint64_t)&state->gdt[0];

    __asm__ volatile ("lgdt %0" : : "m"(state->gdtr) : "memory");

    /*
     * APs enter through a private bootstrap GDT, so reload CS as well as the
     * data selectors. The BSP takes the same far-return path, making the
     * runtime selector contract identical on every online CPU.
     */
    __asm__ volatile (
        "pushq %[code]\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw %[data], %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "ltr %[tss]"
        :
        : [code] "i"((uint64_t)ZEROOS_GDT_KERNEL_CODE),
          [data] "i"((uint16_t)ZEROOS_GDT_KERNEL_DATA),
          [tss] "r"((uint16_t)ZEROOS_GDT_TSS)
        : "rax", "memory"
    );
}

int gdt_init_cpu(uint32_t cpu_id) {
    if (cpu_id>=ZEROOS_MAX_CPUS)
        return -1;
    struct gdt_cpu_state *state=&states[cpu_id];
    if (state->initialized) {
        if (cpu_current_id()==cpu_id)
            load_runtime_gdt(state);
        return 0;
    }

    state->entry_stack=page_alloc();
    if (!state->entry_stack)
        return -1;

    for (uint32_t i=0; i<ZEROOS_GDT_IST_COUNT; ++i) {
        state->ist_stacks[i]=page_alloc();
        if (!state->ist_stacks[i]) {
            for (uint32_t j=0; j<i; ++j)
                page_free(state->ist_stacks[j]);
            page_free(state->entry_stack);
            state->entry_stack=0;
            return -1;
        }
        *(uint64_t *)state->ist_stacks[i]=ZEROOS_STACK_GUARD;
        state->ist_tops[i]=((uint64_t)state->ist_stacks[i]+
                            ZEROOS_PAGE_SIZE)&~0xFULL;
    }

    state->gdt[0]=0;
    gdt_set_code(state->gdt,1,0x9aU);
    gdt_set_data(state->gdt,2,0x92U);
    gdt_set_code(state->gdt,3,0xfaU);
    gdt_set_data(state->gdt,4,0xf2U);

    uint64_t entry_top=((uint64_t)state->entry_stack+ZEROOS_PAGE_SIZE)&~0xFULL;
    state->tss=(struct tss64){0};
    state->tss.rsp0=entry_top;
    state->tss.ist1=state->ist_tops[0];
    state->tss.ist2=state->ist_tops[1];
    state->tss.ist3=state->ist_tops[2];
    state->tss.ist4=state->ist_tops[3];
    state->tss.ist5=state->ist_tops[4];
    state->tss.iomap_base=(uint16_t)sizeof(state->tss);
    gdt_set_tss(state->gdt,5,(uint64_t)&state->tss,
                (uint32_t)(sizeof(state->tss)-1U));

    state->initialized=1;
    if (cpu_current_id()==cpu_id)
        load_runtime_gdt(state);
    return 0;
}

int gdt_init(void) {
    return gdt_init_cpu(0);
}

int gdt_is_initialized(void) {
    struct gdt_cpu_state *state=current_state();
    return state && state->initialized;
}

int gdt_cpu_is_initialized(uint32_t cpu_id) {
    return cpu_id<ZEROOS_MAX_CPUS && states[cpu_id].initialized;
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
    struct gdt_cpu_state *state=current_state();
    return state && state->initialized ? state->gdtr.base : 0;
}

uint64_t gdt_kernel_stack(void) {
    struct gdt_cpu_state *state=current_state();
    return state && state->initialized ? state->tss.rsp0 : 0;
}

int gdt_set_kernel_stack(uint64_t stack_top) {
    struct gdt_cpu_state *state=current_state();
    if (!state || !state->initialized || stack_top==0 ||
        (stack_top & 0xfULL)!=0)
        return -1;

    state->tss.rsp0=stack_top;
    return 0;
}

uint8_t gdt_exception_ist(uint8_t vector) {
    switch (vector) {
    case 2:  return 2; /* NMI. */
    case 8:  return 1; /* Double fault. */
    case 10: /* Invalid TSS. */
    case 11: /* Segment not present. */
    case 12: /* Stack fault. */
    case 13: /* General protection. */
    case 17: /* Alignment check. */
        return 5;
    case 14: return 4; /* Page fault. */
    case 18: return 3; /* Machine check. */
    default: return 0;
    }
}

uint64_t gdt_ist_stack_top(uint8_t ist_index) {
    struct gdt_cpu_state *state=current_state();
    if (!state || !state->initialized || ist_index==0 ||
        ist_index>ZEROOS_GDT_IST_COUNT)
        return 0;
    return state->ist_tops[ist_index-1U];
}

uint64_t gdt_ist_stack_top_for_cpu(uint32_t cpu_id, uint8_t ist_index) {
    if (cpu_id>=ZEROOS_MAX_CPUS || !states[cpu_id].initialized ||
        ist_index==0 || ist_index>ZEROOS_GDT_IST_COUNT)
        return 0;
    return states[cpu_id].ist_tops[ist_index-1U];
}
