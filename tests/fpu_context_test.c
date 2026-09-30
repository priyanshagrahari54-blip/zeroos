#include "fpu.h"

static struct fpu_state host_state;
static struct fpu_state task_a_state;
static struct fpu_state task_b_state;
static const struct {
    uint64_t lane[2];
} pattern_a={{0x0123456789abcdefULL,0xfedcba9876543210ULL}},
  pattern_b={{0x55aa33cc0f0ff0f0ULL,0xa55ac33cf0f00f0fULL}};

static void load_patterns(const void *a, const void *b) {
    __asm__ volatile ("movdqu (%0), %%xmm0\n\t"
                      "movdqu (%1), %%xmm7"
                      : : "r"(a),"r"(b) : "memory");
}

static int check_patterns(const void *a, const void *b) {
    uint64_t xmm0[2],xmm7[2];
    __asm__ volatile ("movdqu %%xmm0, %0\n\t"
                      "movdqu %%xmm7, %1"
                      : "=m"(xmm0),"=m"(xmm7) : : "memory");
    const uint64_t *pa=(const uint64_t *)a;
    const uint64_t *pb=(const uint64_t *)b;
    return xmm0[0]==pa[0] && xmm0[1]==pa[1] &&
           xmm7[0]==pb[0] && xmm7[1]==pb[1];
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

    load_patterns(pattern_a.lane,pattern_a.lane);
    fpu_context_switch(&task_a_state,&task_b_state);
    if (!check_patterns((uint64_t[2]){0,0},(uint64_t[2]){0,0}))
        failed=1;

    load_patterns(pattern_b.lane,pattern_b.lane);
    fpu_context_switch(&task_b_state,&task_a_state);
    if (!check_patterns(pattern_a.lane,pattern_a.lane))
        failed=1;

    load_patterns(pattern_a.lane,pattern_a.lane);
    fpu_context_switch(&task_a_state,&task_b_state);
    if (!check_patterns(pattern_b.lane,pattern_b.lane))
        failed=1;

restore_host:
    __asm__ volatile ("fxrstor64 (%0)" : : "r"(&host_state) : "memory");
    return failed;
}
