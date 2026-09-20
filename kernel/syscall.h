#ifndef ZEROOS_SYSCALL_H
#define ZEROOS_SYSCALL_H

#include "types.h"
#include "vmm.h"

/*
 * ZEROOS system call ABI (Stage 1).
 *
 * Mechanism: SYSCALL / SYSRET (IA32_EFER.SCE).
 *
 *   number : RAX
 *   args   : RDI, RSI, RDX, R10, R8, R9      (SysV calling order)
 *   result : RAX                              (0 or per-call value on
 *                                              success; -1 on failure)
 *
 * On SYSCALL the CPU loads RIP from IA32_LSTAR, saves the user RIP into
 * RCX and the user RFLAGS into R11, and clears RFLAGS according to
 * IA32_SFMASK (IF, DF, AC). RSP is left unchanged, so the entry
 * trampoline first validates that the user RSP lies in the process user
 * region (PML4 slot 254 - the authoritative ring-3 proof, since after
 * SYSCALL CS is always the kernel selector) and moves to the top of the
 * task's kernel stack; the syscall frame is built there, and the user
 * RSP is restored before SYSRET (SYSRET does not touch RSP).
 *
 * Clobbered by every syscall: RAX, RCX, R11. Argument registers (RDI,
 * RSI, RDX, R10, R8, R9) are not preserved. Callee-saved registers
 * (RBX, RBP, R12-R15) are preserved. On entry RSP must follow SysV
 * alignment (8 mod 16 inside a function frame).
 *
 * User-pointer rule: any user-supplied address must be canonical, inside
 * the calling process's user range (PML4 slot 254), mapped for the whole
 * length (address+length overflow included), and carry the access rights
 * the operation requires. The kernel never dereferences a user pointer
 * outside copy_from_user()/copy_to_user() after full-range validation.
 */
#define ZEROOS_SYSCALL_EXIT    0
#define ZEROOS_SYSCALL_YIELD   1
#define ZEROOS_SYSCALL_WRITE   2
#define ZEROOS_SYSCALL_GETPID  3
#define ZEROOS_SYSCALL_GETTID  4
#define ZEROOS_SYSCALL_MAX     5

#define ZEROOS_SYSCALL_MAX_IO  512

struct syscall_frame {
    uint64_t r11;
    uint64_t rcx;
    uint64_t rax;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t r10;
    uint64_t r8;
    uint64_t r9;
};

void syscall_init(void);
uint64_t syscall_dispatch(struct syscall_frame *frame);
int copy_from_user(void *dest, const struct vmm_space *space,
                   uint64_t virtual_address, uint64_t length);

#endif
