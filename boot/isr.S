.section .text
.code64

.macro ISR_NOERR n
.global isr_stub_\\n
.type isr_stub_\\n, @function
isr_stub_\\n:
    pushq $0
    pushq $\\n
    jmp isr_common
.endm

.macro ISR_ERR n
.global isr_stub_\\n
.type isr_stub_\\n, @function
isr_stub_\\n:
    pushq $\\n
    jmp isr_common
.endm

.set n, 0
.rept 256
.if n == 8
    ISR_ERR n
.elseif n == 10
    ISR_ERR n
.elseif n == 11
    ISR_ERR n
.elseif n == 12
    ISR_ERR n
.elseif n == 13
    ISR_ERR n
.elseif n == 14
    ISR_ERR n
.elseif n == 17
    ISR_ERR n
.elseif n == 21
    ISR_ERR n
.elseif n == 29
    ISR_ERR n
.elseif n == 30
    ISR_ERR n
.else
    ISR_NOERR n
.endif
.set n, n + 1
.endr

.global isr_common
.type isr_common, @function
isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    mov %rsp, %rdi
    call interrupt_dispatch

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

.section .rodata
.align 8
.global isr_stub_table
isr_stub_table:
.set t, 0
.rept 256
    .quad isr_stub_\\t
.set t, t + 1
.endr
