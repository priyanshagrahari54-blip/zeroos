.text
.code64

/*
 * Enter a validated user image.
 *
 * SysV arguments:
 *   rdi = user RIP
 *   rsi = user RSP
 *
 * The caller has already activated the process CR3 and installed the current
 * task's kernel stack in TSS.RSP0.  The transition is one-way; user exit and
 * fault paths return through the syscall/exception dispatcher and never back
 * into this function.
 */
.global zeroos_user_enter
.type zeroos_user_enter, @function
zeroos_user_enter:
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es

    pushq $0x23
    pushq %rsi
    pushfq
    orq $0x200, (%rsp)
    pushq $0x1b
    pushq %rdi
    iretq

.size zeroos_user_enter, .-zeroos_user_enter
.section .note.GNU-stack,"",@progbits
