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
    movb  $'E', %al
    outb  %al, $0xe9
    movb  $\n, %al
    outb  %al, $0xe9
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
    movb  $'E', %al
    outb  %al, $0xe9
    movb  $\n, %al
    outb  %al, $0xe9
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
early_stub_err   21
early_stub_noerr 22
early_stub_noerr 23
early_stub_noerr 24
early_stub_noerr 25
early_stub_noerr 26
early_stub_noerr 27
early_stub_noerr 28
early_stub_err   29
early_stub_err   30
early_stub_noerr 31

/* After normalization, low to high: vector, error, RIP, CS, RFLAGS,
 * RSP, SS. IST does not add a word to the architectural frame.
 * Fatal-only entry: it never restores the interrupted registers.
 */
.global early_common
.type early_common, @function
early_common:
    cli
    cld
    movq  0(%rsp), %rdi
    movq  8(%rsp), %rsi
    movq 16(%rsp), %rdx
    movq %cr2, %rcx
    andq $-16, %rsp
    callq early_fatal_dispatch
1:  cli
    hlt
    jmp 1b
.section .note.GNU-stack,"",@progbits
