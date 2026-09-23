#ifndef ZEROOS_GDT_H
#define ZEROOS_GDT_H

#include "types.h"

#define ZEROOS_GDT_KERNEL_CODE 0x08U
#define ZEROOS_GDT_KERNEL_DATA 0x10U
#define ZEROOS_GDT_USER_CODE   0x1bU
#define ZEROOS_GDT_USER_DATA   0x23U
#define ZEROOS_GDT_TSS         0x28U
#define ZEROOS_GDT_IST_COUNT   5U

int gdt_init(void);
int gdt_init_cpu(uint32_t cpu_id);
int gdt_is_initialized(void);
int gdt_cpu_is_initialized(uint32_t cpu_id);
uint16_t gdt_kernel_code_selector(void);
uint16_t gdt_kernel_data_selector(void);
uint16_t gdt_user_code_selector(void);
uint16_t gdt_user_data_selector(void);
uint16_t gdt_tss_selector(void);
uint64_t gdt_base(void);
uint64_t gdt_kernel_stack(void);

/*
 * Updates the current CPU's TSS.RSP0 entry stack. This is the bridge used
 * later when a user thread gets its own protected kernel stack.
 */
int gdt_set_kernel_stack(uint64_t stack_top);

/* Returns the 1-based IST slot for a vector, or zero for the current stack. */
uint8_t gdt_exception_ist(uint8_t vector);
uint64_t gdt_ist_stack_top(uint8_t ist_index);
uint64_t gdt_ist_stack_top_for_cpu(uint32_t cpu_id, uint8_t ist_index);

#endif
