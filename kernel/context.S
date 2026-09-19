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
