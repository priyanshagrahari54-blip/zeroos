/* Ring-3 session/shell-process entry: the kernel provides a 16-byte
 * aligned stack in the single user stack page; call session_main and
 * exit with its status. */
    .section .text.start, "ax"
    .globl _start
_start:
    andq $-16, %rsp
    call session_main
    movq %rax, %rdi
    movq $1, %rax          /* ZEROOS_SYS_EXIT */
    int $0x80
1:  jmp 1b
    .section .note.GNU-stack, "", @progbits
