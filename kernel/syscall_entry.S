.section .text
.code64

/* Single-CPU entry contract: SFMASK clears IF, TF, DF, NT and AC before
 * entry. The only pre-stack store targets kernel-owned scratch, never user
 * memory. NMI/DF are fatal and do not enter SYSCALL. Once user RSP is pushed,
 * all state belongs to the thread's kernel stack, including across yield.
 * No GPR other than RSP is repurposed before its value has been saved.
 *
 * Frame at RSP: r11, rcx, rax, rdi, rsi, rdx, r10, r8, r9, user_rsp.
 * Ten pushes from a page-aligned kernel stack leave SysV call alignment.
 * This ABI imposes no alignment requirement on the incoming user RSP.
 */
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    movq %rsp, syscall_user_rsp(%rip)
    movq current_task(%rip), %rsp
    testq %rsp, %rsp
    jz .entry_invalid
    movq 24(%rsp), %rsp             /* task.stack_base: asserted in task.h */
    testq %rsp, %rsp
    jz .entry_invalid
    addq $4096, %rsp
    pushq syscall_user_rsp(%rip)
    pushq %r9
    pushq %r8
    pushq %r10
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rax
    pushq %rcx
    pushq %r11
    cld
    movq %rsp, %rdi
    call syscall_dispatch
    /* C validates canonical user return addresses and sanitizes RFLAGS.
     * Callee-saved GPRs are preserved by the C calling convention. */
    popq %r11
    popq %rcx
    popq %rax
    popq %rdi
    popq %rsi
    popq %rdx
    popq %r10
    popq %r8
    popq %r9
    popq %rsp                       /* LAST stack access before SYSRET */
    sysretq

.entry_invalid:
    leaq syscall_emergency_top(%rip), %rsp
    call syscall_entry_abort
1:  cli
    hlt
    jmp 1b

.section .bss
.align 16
syscall_user_rsp:
    .skip 8
.align 16
syscall_emergency_stack:
    .skip 4096
syscall_emergency_top:
.section .note.GNU-stack,"",@progbits
