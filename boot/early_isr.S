/*
 * Early fatal-exception IDT stubs.
 *
 * Installed by early_idt_install() before the heap phase, i.e. while the
 * kernel runs with paging enabled but without the full IDT (installed by
 * interrupts_init much later). Any CPU exception in that window used to
 * triple-fault with no information; instead each stub normalizes the
 * frame (vector word, error-code word, then the architectural push
 * SS,RSP,RFLAGS,CS,RIP) and calls early_fatal_dispatch() in C, which
 * prints the vector, error code, faulting RIP and CR2 to the serial
 * console and halts.
 *
 * All gates use IST=0: no TSS is loaded at this stage, so an IST gate
 * would fault the load itself. Vector 8 (double fault) is handled with a
 * plain stack frame; if the stack is already exhausted the CPU triple
 * faults, which is the correct last resort.
 */
.section .text
.code64

/* Vectors without a hardware error code: push 0 so the frame layout is
 * uniform across all 32 vectors. */
.macro early_stub_noerr n
.global early_stub_\n
.type early_stub_\n, @function
early_stub_\n:
    pushq $0
    pushq $\n
    jmp early_common
.endm

/* Vectors that push a hardware error code: pass it through so the
 * dispatch sees the real code. */
.macro early_stub_err n
.global early_stub_\n
.type early_stub_\n, @function
early_stub_\n:
    pushq $\n
    jmp early_common
.endm

early_stub_noerr 0
early_stub_noerr 1
early_stub_noerr 2
early_stub_noerr 3
early_stub_noerr 4
early_stub_noerr 5
early_stub_noerr 6
early_stub_noerr 7
early_stub_err   8
early_stub_noerr 9
early_stub_err   10
early_stub_err   11
early_stub_err   12
early_stub_err   13
early_stub_err   14
early_stub_noerr 15
early_stub_noerr 16
early_stub_err   17
early_stub_noerr 18
early_stub_noerr 19
early_stub_noerr 20
early_stub_noerr 21
early_stub_noerr 22
early_stub_noerr 23
early_stub_noerr 24
early_stub_noerr 25
early_stub_noerr 26
early_stub_noerr 27
early_stub_noerr 28
early_stub_noerr 29
early_stub_noerr 30
early_stub_noerr 31

/*
 * On entry (uniform for every vector thanks to the stubs), low to high:
 *   [rsp+0]  = vector      (stub-pushed)
 *   [rsp+8]  = error code  (stub-pushed 0 or the hardware code)
 *   [rsp+16] = SS
 *   [rsp+24] = RSP
 *   [rsp+32] = RFLAGS
 *   [rsp+40] = CS
 *   [rsp+48] = RIP
 */
.global early_common
.type early_common, @function
early_common:
    /*
     * Fault-proof first report, before any stack read or C call: marker,
     * vector, then the faulting RIP as 16 hex digits on the isa-debugcon
     * (port 0xe9), which CI captures in debug.log. A CPU exception cannot
     * be raised by outb in CPL0, so this cannot re-fault.
     */
    movb  $'E', %al
    outb  %al, $0xe9
    movzbl 0(%rsp), %eax
    outb  %al, $0xe9
    movq  48(%rsp), %r11
    movq  $16, %r12
1:  movq  %r11, %r10
    shrq  $60, %r10
    andq  $0xf, %r10
    addq  $'0', %r10
    cmpq  $'9', %r10
    jbe   2f
    addq  $6, %r10
2:  movq  %r10, %rax
    outb  %al, $0xe9
    shrq  $4, %r11
    decq  %r12
    jnz   1b

    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbx
    pushq %rbp
    /* After these six pushes the frame words are at +48. SysV AMD64:
     * arg1=rdi arg2=rsi arg3=rdx arg4=rcx. */
    movq  48(%rsp), %rdi   /* vector */
    movq  56(%rsp), %rsi   /* error code */
    movq  96(%rsp), %rdx   /* faulting RIP */
    movq  %cr2, %rcx       /* faulting linear address (#PF; 0 otherwise) */
    callq early_fatal_dispatch
    cli
1:  hlt
    jmp 1b
.section .note.GNU-stack,"",@progbits
