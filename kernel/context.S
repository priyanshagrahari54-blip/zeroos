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
 * Initial entry for user-mode threads.
 *
 * The task's address space (CR3, PCID-tagged when available) and TSS.RSP0
 * are installed by the scheduler before this code runs, so the iretq below
 * executes directly in the process's ring-3 address space. The frame is
 * fetched from the task's per-slot user_entry_frame (see task.c); it is
 * consumed exactly once. RDI carries the process parameter.
 *
 * Alignment: the synthetic context resumes with RSP 8 mod 16. The subq
 * restores 16 mod 16 for the call, and the five frame pushes leave RSP
 * 8 mod 16 at iretq, which is consumed exactly.
 */
.global user_task_entry
.type user_task_entry, @function
user_task_entry:
    subq $8, %rsp
    call task_current
    movq %rax, %rdi
    call task_get_user_frame
    movq 0(%rax), %r10     /* user rip */
    movq 8(%rax), %r11     /* user cs  */
    movq 16(%rax), %r9     /* rflags   */
    movq 24(%rax), %r8     /* user rsp */
    movq 32(%rax), %r12    /* user ss  */
    movq 40(%rax), %rdi    /* argument */
    pushq %r12
    pushq %r8
    pushq %r9
    pushq %r11
    pushq %r10
    iretq

.global context_switch
.type context_switch, @function
context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq (%rsi), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    retq

.section .note.GNU-stack,"",@progbits
