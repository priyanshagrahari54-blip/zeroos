.section .text
.code64

/*
 * SYSCALL entry (target of IA32_LSTAR).
 *
 * SYSCALL does not change RSP: the CPU lands on the calling user stack
 * with RIP from IA32_LSTAR, user RIP in RCX, user RFLAGS in R11, and the
 * kernel CS/SS from STAR. This trampoline therefore:
 *
 *   1. moves to the top of the current task's kernel stack BEFORE any
 *      memory operation (a plain RIP-relative load of the current-task
 *      pointer; a call here would push onto the unvalidated user stack),
 *   2. saves the four callee-saved/scratch-sensitive registers and the
 *      user RSP onto the kernel stack,
 *   3. validates that the saved user RSP lies in the process user region
 *      (PML4 slot 254); a SYSCALL from kernel mode has a kernel RSP and
 *      is an invariant violation,
 *   4. builds the syscall frame on the kernel stack, so kernel state
 *      (register saves, nested interrupt frames, C frames) never lands
 *      in user-writable memory,
 *   5. restores the user RSP before SYSRET, because SYSRET does not
 *      modify RSP.
 *
 * Frame layout on the kernel stack (address ascending, deepest first):
 *   user R14, user R13, user RSP, user R12,
 *   then struct syscall_frame (r11, rcx, rax, rdi, rsi, rdx, r10, r8, r9)
 * The frame base (&frame[0] = r11) sits 64 bytes above the 13 pushed
 * slots, so the trampoline passes RSP+64 to C. Only this trampoline
 * touches the four slots below the frame.
 *
 * Clobbering per the ABI: RAX (result), RCX and R11 (hardware return
 * state). RDI/RSI/RDX/R10/R8/R9 are not preserved by the syscall.
 * Callee-saved registers (RBX, RBP, R12-R15) are all preserved:
 *   - R12, R13, R14 and the user RSP are saved into the frame and
 *     restored before SYSRET. R13 is saved BEFORE the prologue
 *     repurposes the R13 register to carry user R12.
 *   - R15 is never touched by this trampoline and is preserved by the
 *     SysV callee-saved contract across the syscall_dispatch call tree.
 *   - RBX/RBP are never used by this code and are preserved by the same
 *     SysV contract.
 * The push count (13) is load-bearing: it leaves RSP 0 mod 16 so the
 * call below enters C at 8 mod 16 per SysV.
 *
 * Alignment: the ABI requires RSP 8 mod 16 at SYSCALL. The kernel RSP
 * starts 8 bytes below the 4 KiB-aligned stack top; the 13 pushes
 * (104 bytes) bring it to 0 mod 16 so the `call` below leaves C code
 * at 8 mod 16.
 *
 * The PML4 index of an address is bits [51:39]. In 64-bit mode a 39-bit
 * shift would be truncated by the 5-bit shift-count mask, so the index
 * is computed as shrq $32 followed by shrl $7 (32+7 = 39) and masked to
 * 12 bits.
 *
 * struct task layout offsets (kernel/task.h):
 *   id 0, state 8, saved_stack 16, stack_base 24.
 * Kernel stack top = stack_base + 4096; entry RSP = top - 8 = base+4088.
 */
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    movq %r12, %r13                 /* r13 <- user r12 (callee-saved)      */
    movq %rsp, %r12                 /* r12 <- user rsp (restored later)    */
    movq current_task(%rip), %rax   /* no call: no push onto user stack    */
    testq %rax, %rax
    je .entry_invalid
    movq 24(%rax), %rax             /* rax <- task->stack_base (task.h:
                                       id 0, state 8, saved_stack 16,
                                       stack_base 24)                       */
    leaq 4088(%rax), %rsp           /* top - 8: 4096 - 8                   */
    pushq %r14                      /* user r14 (deepest)                  */
    pushq %r13                      /* user r13: saved BEFORE R13 is       */
                                    /* repurposed below to carry user r12  */
    movq %r12, %r13                 /* r13 <- user r12                     */
    pushq %r12                      /* user rsp                            */
    pushq %r13                      /* user r12                            */
    pushq %r11                      /* user rflags                         */
    pushq %rcx                      /* user rip                            */
    pushq %rax                      /* syscall number                      */
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9
    leaq 64(%rsp), %rdi             /* &frame[0] (r11 is 8 slots above r9)*/

    /*
     * A legitimate ring-3 SYSCALL always carries a user RSP in PML4 slot
     * 254. Anything else means SYSCALL executed in ring 0 or with a
     * corrupted RSP. R14 is scratch here (its user value is on the stack).
     */
    movq %r12, %r14
    shrq $32, %r14
    shrl $7, %r14d
    andl $0x1ff, %r14d
    cmpl $254, %r14d
    jne .entry_invalid

    call syscall_dispatch
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rax
    popq %rcx
    popq %r11
    orl  $0x100, %r11d              /* SYSCALL applied SFMASK (IF cleared)
                                       to R11; SYSRET then rebuilds RFLAGS
                                       from R11, so restore IF to keep user
                                       code timer-preemptible after syscalls */
    popq %r13                       /* r13 <- user r12 (temp)              */
    popq %rsp                       /* RSP <- user rsp (SYSRET does not
                                       touch RSP)                           */
    movq %r13, %r12                 /* R12 <- user r12 (before R13 is
                                       refilled below)                      */
    popq %r13                       /* R13 <- user r13                     */
    popq %r14                       /* R14 <- user r14                     */
    sysretq

.entry_invalid:
    /*
     * Kernel-mode SYSCALL or invalid entry state: invariant violation.
     * If the task was valid, RSP is already the kernel stack and the
     * abort call is safe; if not, a push faulting on the bad user RSP
     * raises a kernel-mode exception and halts through the normal fatal
     * path.
     */
    call syscall_entry_abort
    cli
    hlt

.section .note.GNU-stack,"",@progbits
