#include "fpu.h"

#define ZEROOS_MXCSR_DEFAULT 0x1f80U

static struct fpu_state initial_fpu_state;
static int initial_fpu_state_ready;

int fpu_system_init(void) {
    uint32_t mxcsr=ZEROOS_MXCSR_DEFAULT;

    /* cpu_init() has checked SSE2 and enabled CR4.OSFXSR/OSXMMEXCPT before
     * reaching this point. This runs before tasks or interrupt scheduling. */
    __asm__ volatile ("fninit\n\t"
                      "fldz\n\t"
                      "fldz\n\t"
                      "fldz\n\t"
                      "fldz\n\t"
                      "fldz\n\t"
                      "fldz\n\t"
                      "fldz\n\t"
                      "fldz\n\t"
                      "fninit"
                      ::: "memory");
    __asm__ volatile ("pxor %%xmm0, %%xmm0\n\t"
                      "pxor %%xmm1, %%xmm1\n\t"
                      "pxor %%xmm2, %%xmm2\n\t"
                      "pxor %%xmm3, %%xmm3\n\t"
                      "pxor %%xmm4, %%xmm4\n\t"
                      "pxor %%xmm5, %%xmm5\n\t"
                      "pxor %%xmm6, %%xmm6\n\t"
                      "pxor %%xmm7, %%xmm7\n\t"
                      "pxor %%xmm8, %%xmm8\n\t"
                      "pxor %%xmm9, %%xmm9\n\t"
                      "pxor %%xmm10, %%xmm10\n\t"
                      "pxor %%xmm11, %%xmm11\n\t"
                      "pxor %%xmm12, %%xmm12\n\t"
                      "pxor %%xmm13, %%xmm13\n\t"
                      "pxor %%xmm14, %%xmm14\n\t"
                      "pxor %%xmm15, %%xmm15"
                      ::: "memory");
    __asm__ volatile ("ldmxcsr %0" : : "m"(mxcsr) : "memory");
    __asm__ volatile ("fxsave64 (%0)"
                      :
                      : "r"(&initial_fpu_state)
                      : "memory");
    initial_fpu_state_ready=1;
    return 0;
}

int fpu_state_init(struct fpu_state *state) {
    if (!state || !initial_fpu_state_ready ||
        ((uint64_t)state & 0xfU)!=0)
        return -1;

    for (uint32_t i=0;i<ZEROOS_FPU_STATE_SIZE;++i)
        state->bytes[i]=initial_fpu_state.bytes[i];
    return 0;
}

void fpu_context_switch(struct fpu_state *outgoing,
                        const struct fpu_state *incoming) {
    if (!outgoing || !incoming ||
        ((uint64_t)outgoing & 0xfU)!=0 ||
        ((uint64_t)incoming & 0xfU)!=0)
        for (;;) __asm__ volatile ("cli; hlt" ::: "memory");

    /* This is called only at the serialized scheduler handoff boundary, with
     * interrupts disabled and before the outgoing CPU ownership is released. */
    __asm__ volatile ("fxsave64 (%0)"
                      :
                      : "r"(outgoing)
                      : "memory");
    __asm__ volatile ("fxrstor64 (%0)"
                      :
                      : "r"(incoming)
                      : "memory");
}
