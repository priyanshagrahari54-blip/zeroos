#include "fpu.h"

static struct fpu_state host_state;
static struct fpu_state task_a_state;
static struct fpu_state task_b_state;

struct fpu_probe_pattern {
    uint64_t xmm[16][2];
    uint64_t x87[8];
    uint32_t mxcsr;
};

#define FPU_PATTERN_A(n) { 0x1000000000000000ULL+(n), 0xa000000000000000ULL+(n) }
#define FPU_PATTERN_B(n) { 0x5000000000000000ULL+(n), 0xe000000000000000ULL+(n) }
static const struct fpu_probe_pattern pattern_a={
    {FPU_PATTERN_A(0),FPU_PATTERN_A(1),FPU_PATTERN_A(2),FPU_PATTERN_A(3),
     FPU_PATTERN_A(4),FPU_PATTERN_A(5),FPU_PATTERN_A(6),FPU_PATTERN_A(7),
     FPU_PATTERN_A(8),FPU_PATTERN_A(9),FPU_PATTERN_A(10),FPU_PATTERN_A(11),
     FPU_PATTERN_A(12),FPU_PATTERN_A(13),FPU_PATTERN_A(14),FPU_PATTERN_A(15)},
    {0x3ff0000000000000ULL,0x3ff4000000000000ULL,
     0x3ff8000000000000ULL,0x3ffc000000000000ULL,
     0x4000000000000000ULL,0x4004000000000000ULL,
     0x4008000000000000ULL,0x4010000000000000ULL},
    0x9f80U
};
static const struct fpu_probe_pattern pattern_b={
    {FPU_PATTERN_B(0),FPU_PATTERN_B(1),FPU_PATTERN_B(2),FPU_PATTERN_B(3),
     FPU_PATTERN_B(4),FPU_PATTERN_B(5),FPU_PATTERN_B(6),FPU_PATTERN_B(7),
     FPU_PATTERN_B(8),FPU_PATTERN_B(9),FPU_PATTERN_B(10),FPU_PATTERN_B(11),
     FPU_PATTERN_B(12),FPU_PATTERN_B(13),FPU_PATTERN_B(14),FPU_PATTERN_B(15)},
    {0xbff0000000000000ULL,0xbff4000000000000ULL,
     0xbff8000000000000ULL,0xbffc000000000000ULL,
     0xc000000000000000ULL,0xc004000000000000ULL,
     0xc008000000000000ULL,0xc010000000000000ULL},
    0x1f80U
};
#undef FPU_PATTERN_A
#undef FPU_PATTERN_B

#define FPU_LOAD_XMM(reg,offset) "movdqu " #offset "(%0), %%xmm" #reg "\n\t"
#define FPU_STORE_XMM(reg,offset) "movdqu %%xmm" #reg ", " #offset "(%0)\n\t"
#define FPU_LOAD_ALL_XMM \
    FPU_LOAD_XMM(0,0) FPU_LOAD_XMM(1,16) FPU_LOAD_XMM(2,32) FPU_LOAD_XMM(3,48) \
    FPU_LOAD_XMM(4,64) FPU_LOAD_XMM(5,80) FPU_LOAD_XMM(6,96) FPU_LOAD_XMM(7,112) \
    FPU_LOAD_XMM(8,128) FPU_LOAD_XMM(9,144) FPU_LOAD_XMM(10,160) FPU_LOAD_XMM(11,176) \
    FPU_LOAD_XMM(12,192) FPU_LOAD_XMM(13,208) FPU_LOAD_XMM(14,224) FPU_LOAD_XMM(15,240)
#define FPU_STORE_ALL_XMM \
    FPU_STORE_XMM(0,0) FPU_STORE_XMM(1,16) FPU_STORE_XMM(2,32) FPU_STORE_XMM(3,48) \
    FPU_STORE_XMM(4,64) FPU_STORE_XMM(5,80) FPU_STORE_XMM(6,96) FPU_STORE_XMM(7,112) \
    FPU_STORE_XMM(8,128) FPU_STORE_XMM(9,144) FPU_STORE_XMM(10,160) FPU_STORE_XMM(11,176) \
    FPU_STORE_XMM(12,192) FPU_STORE_XMM(13,208) FPU_STORE_XMM(14,224) FPU_STORE_XMM(15,240)
#define FPU_LOAD_X87(offset) "fldl " #offset "(%0)\n\t"
#define FPU_STORE_X87(offset) "fstpl " #offset "(%0)\n\t"
#define FPU_LOAD_ALL_X87 \
    FPU_LOAD_X87(56) FPU_LOAD_X87(48) FPU_LOAD_X87(40) FPU_LOAD_X87(32) \
    FPU_LOAD_X87(24) FPU_LOAD_X87(16) FPU_LOAD_X87(8) FPU_LOAD_X87(0)
#define FPU_STORE_ALL_X87 \
    FPU_STORE_X87(0) FPU_STORE_X87(8) FPU_STORE_X87(16) FPU_STORE_X87(24) \
    FPU_STORE_X87(32) FPU_STORE_X87(40) FPU_STORE_X87(48) FPU_STORE_X87(56)

static void load_pattern(const struct fpu_probe_pattern *pattern) {
    __asm__ volatile (FPU_LOAD_ALL_XMM
                      : : "r"(&pattern->xmm[0][0]) : "memory");
    __asm__ volatile (FPU_LOAD_ALL_X87
                      : : "r"(&pattern->x87[0]) : "memory");
    __asm__ volatile ("ldmxcsr %0"
                      : : "m"(pattern->mxcsr) : "memory");
}

static int live_matches(const struct fpu_probe_pattern *expected) {
    uint64_t observed[16][2];
    uint64_t observed_x87[8];
    uint32_t observed_mxcsr;
    __asm__ volatile (FPU_STORE_ALL_XMM
                      : : "r"(&observed[0][0]) : "memory");
    __asm__ volatile (FPU_STORE_ALL_X87
                      : : "r"(&observed_x87[0]) : "memory");
    __asm__ volatile ("stmxcsr %0"
                      : "=m"(observed_mxcsr) : : "memory");
    for (uint32_t reg=0;reg<16;++reg)
        if (observed[reg][0]!=expected->xmm[reg][0] ||
            observed[reg][1]!=expected->xmm[reg][1])
            return 0;
    for (uint32_t reg=0;reg<8;++reg)
        if (observed_x87[reg]!=expected->x87[reg])
            return 0;
    return observed_mxcsr==expected->mxcsr;
}

static int live_is_initial(void) {
    uint64_t observed[16][2];
    uint16_t control,status;
    uint32_t mxcsr;
    __asm__ volatile (FPU_STORE_ALL_XMM
                      : : "r"(&observed[0][0]) : "memory");
    __asm__ volatile ("fnstcw %0\n\t"
                      "fnstsw %1\n\t"
                      "stmxcsr %2"
                      : "=m"(control),"=m"(status),"=m"(mxcsr)
                      : : "memory");
    for (uint32_t reg=0;reg<16;++reg)
        if (observed[reg][0]!=0 || observed[reg][1]!=0)
            return 0;
    return control==0x037fU && status==0 && mxcsr==0x1f80U;
}

int main(void) {
    int failed=0;
    __asm__ volatile ("fxsave64 (%0)" : : "r"(&host_state) : "memory");

    if (fpu_system_init()!=0 ||
        fpu_state_init(&task_a_state)!=0 ||
        fpu_state_init(&task_b_state)!=0) {
        failed=1;
        goto restore_host;
    }

    load_pattern(&pattern_a);
    fpu_context_switch(&task_a_state,&task_b_state);
    if (!live_is_initial())
        failed=1;

    load_pattern(&pattern_b);
    fpu_context_switch(&task_b_state,&task_a_state);
    if (!live_matches(&pattern_a))
        failed=1;

    load_pattern(&pattern_a);
    fpu_context_switch(&task_a_state,&task_b_state);
    if (!live_matches(&pattern_b))
        failed=1;

restore_host:
    __asm__ volatile ("fxrstor64 (%0)" : : "r"(&host_state) : "memory");
    return failed;
}

#undef FPU_LOAD_XMM
#undef FPU_STORE_XMM
#undef FPU_LOAD_ALL_XMM
#undef FPU_STORE_ALL_XMM

#undef FPU_LOAD_X87
#undef FPU_STORE_X87
#undef FPU_LOAD_ALL_X87
#undef FPU_STORE_ALL_X87
