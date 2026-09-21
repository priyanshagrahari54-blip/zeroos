#ifndef ZEROOS_GDT_H
#define ZEROOS_GDT_H
#include "types.h"

/*
 * ZEROOS GDT layout (selector = index * 8):
 *
 *   0  null
 *   1  kernel code   (0x08) - DPL0, 64-bit
 *   2  kernel data   (0x10) - DPL0
 *   3  user data     (0x18) - DPL3
 *   4  user code     (0x20) - DPL3, 64-bit
 *   5  TSS           (0x28) - task state segment
 *
 * The user-data/user-code ordering is load-bearing: SYSRET derives
 *   CS <- (STAR[63:48] + 16) | 3   and   SS <- (STAR[63:48] + 8) | 3
 * so with STAR[63:48] = 0x10 the return selectors are 0x23 (user code,
 * RPL3) and 0x1B (user data, RPL3).
 */
#define ZEROOS_SEL_NULL         0x00
#define ZEROOS_SEL_KERNEL_CODE  0x08
#define ZEROOS_SEL_KERNEL_DATA  0x10
#define ZEROOS_SEL_USER_DATA    0x18
#define ZEROOS_SEL_USER_CODE    0x20
#define ZEROOS_SEL_TSS          0x28

#define ZEROOS_USER_CS_RING3    (ZEROOS_SEL_USER_CODE | 3)
#define ZEROOS_USER_SS_RING3    (ZEROOS_SEL_USER_DATA | 3)

/* SYSRET/STAR base field: (user_data_index - 1) << 3. */
#define ZEROOS_STAR_USER_BASE   0x10ULL

void gdt_init(void);
void gdt_set_rsp0(uint64_t kernel_stack_top);
uint64_t gdt_get_rsp0(void);

#endif
