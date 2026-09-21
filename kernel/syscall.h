#ifndef ZEROOS_SYSCALL_H
#define ZEROOS_SYSCALL_H

#include "types.h"
#include "vmm.h"

/* ZEROOS Stage-1 ABI: RAX number/result; args RDI, RSI, RDX, R10, R8, R9.
 * Failure is -1. RAX/RCX/R11 and argument registers may be clobbered;
 * RBX/RBP/R12-R15 are preserved. User RSP need not be aligned for SYSCALL.
 * Entry switches to a trusted task stack without dereferencing user RSP.
 * Return RCX/RSP must be canonical mapped user addresses; RSP writable.
 * All copy ranges require whole-range validation before any user access.
 * See docs/SYSCALL_ABI.md for the stable public contract and limitations.
 */
#define ZEROOS_SYSCALL_EXIT    0
#define ZEROOS_SYSCALL_YIELD   1
#define ZEROOS_SYSCALL_WRITE   2
#define ZEROOS_SYSCALL_GETPID  3
#define ZEROOS_SYSCALL_GETTID  4
/* Reserved Stage-2 slots: exec/wait/fd/process-control will be added here. */
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
    uint64_t user_rsp;
};

_Static_assert(sizeof(struct syscall_frame) == 80, "syscall entry frame size");
_Static_assert(__builtin_offsetof(struct syscall_frame, rax) == 16, "syscall number offset");
_Static_assert(__builtin_offsetof(struct syscall_frame, user_rsp) == 72, "syscall RSP offset");

void syscall_init(void);
uint64_t syscall_dispatch(struct syscall_frame *frame);
int copy_from_user(void *dest, const struct vmm_space *space,
                   uint64_t virtual_address, uint64_t length);

#endif
