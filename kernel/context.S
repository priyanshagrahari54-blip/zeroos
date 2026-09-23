.text

/*
 * Initial task entry wrapper.
 *
 * task_prepare_stack() starts the first cooperative context immediately below
 * the synthetic register frame. Without a downward stack reservation here,
 * the first hardware IRQ can push its frame over that synthetic frame before
 * the task has established a normal C stack frame. Keep a 264-byte interrupt
 * headroom gap; 264 preserves SysV alignment for the call into C.
 */
.global task_trampoline
.type task_trampoline, @function
task_trampoline:
    subq $264, %rsp
    call task_trampoline_body
1:
    cli
    hlt
    jmp 1b

/*
 * void context_switch_ex(uint64_t *old_sp,
 *                        const uint64_t *new_sp,
 *                        struct interrupt_frame *new_frame);
 *
 * The single scheduler handoff primitive. It always saves the SysV
 * callee-saved register set (RBP, RBX, R12-R15) plus the return address of
 * this call onto the outgoing task's stack and publishes RSP through *old_sp.
 * The saved layout is exactly the one task_prepare_stack() fabricates for a
 * fresh task and the one the cooperative IRQ-exit epilogue restores:
 *
 *     *old_sp -> r15, r14, r13, r12, rbx, rbp, retq-continuation
 *
 * Successor selection accepts both resumable context forms:
 *
 *   - new_frame != 0: the successor was preempted and owns a live hardware
 *     interrupt frame. That frame is exclusively owned by this dispatch from
 *     the moment its task->interrupt_frame pointer is retired by the caller,
 *     and is resumed with the architectural pop/iretq sequence. This path
 *     never returns.
 *
 *   - new_frame == 0: the successor suspended cooperatively through this
 *     same function. Restore its callee-saved registers and retq into its
 *     frozen C continuation (the context_switch_ex call site).
 *
 * Callers must run with interrupts disabled and must not hold scheduler
 * metadata locks across this call.
 */
.global context_switch_ex
.type context_switch_ex, @function
context_switch_ex:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    testq %rdx, %rdx
    jnz .Lresume_frame
    movq (%rsi), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    retq

.Lresume_frame:
    /* The old task may be a zombie. Clear its per-CPU handoff quarantine
     * while the old stack is still active, immediately before switching to
     * the destination architectural frame. */
    movq %rdx, %r12
    movq %r12, %rdi
    subq $8, %rsp
    call task_handoff_complete
    addq $8, %rsp
    movq %r12, %rdx
    /* Identical restore sequence to the isr_common frame epilogue. */
    movq %rdx, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

.section .note.GNU-stack,"",@progbits
