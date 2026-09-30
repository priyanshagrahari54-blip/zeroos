#ifndef ZEROOS_FPU_H
#define ZEROOS_FPU_H

#include "types.h"

/* FXSAVE64 image for the enabled x87/MMX/SSE architectural state. AVX is not
 * enabled by this kernel, so extending this format requires an explicit
 * XSAVE/XCR0 feature contract before AVX code can be shipped. */
#define ZEROOS_FPU_STATE_SIZE 512U

struct fpu_state {
    uint8_t bytes[ZEROOS_FPU_STATE_SIZE];
} __attribute__((aligned(16)));

/* Called once by the bootstrap CPU after CR4.OSFXSR has been enabled. */
int fpu_system_init(void);
/* Install a clean architectural state into a task-owned save area. */
int fpu_state_init(struct fpu_state *state);
/* Save outgoing and restore incoming state on the current CPU. */
void fpu_context_switch(struct fpu_state *outgoing,
                        const struct fpu_state *incoming);

#endif
