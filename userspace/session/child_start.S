/* Ring-3 shell-child entry: the kernel provides a 16-byte aligned stack;
 * call child_main and exit with its status. */
    .section .text.start, "ax"
    .globl _start
_start:
    movq (%rsp), %rdi          /* argc */
    leaq 8(%rsp), %rsi         /* argv */
    andq $-16, %rsp
    call child_main
    movq %rax, %rdi
    movq $1, %rax          /* ZEROOS_SYS_EXIT */
    int $0x80
1:  jmp 1b
    .section .note.GNU-stack, "", @progbits
