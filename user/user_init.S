# ZEROOS user-mode test program (Stage 1).
#
# Linked into the kernel image in a dedicated .userinit section; the
# process layer copies the blob into each process's user code page
# (ZEROOS_USER_CODE_VA, executable-only through the process page tables).
#
# Convention: RDI carries the test parameter at entry:
#   0 - hello:  write a string, call getpid/gettid, exit(0)
#   1 - marker: store 0xDEADBEEF in the process data page, exit(0)
#   2 - reader: read the data page, exit(0)
#   3 - fault:  touch an unmapped user page -> #PF must be contained
#   4 - gp:     execute a privileged instruction -> #GP must be contained
#
# Syscall ABI (see kernel/syscall.h): RAX number; RDI/RSI/RDX args;
# RAX result. Clobbered: RAX, RCX, R11.

    .section .userinit, "ax"
    .code64

.global user_init_code
user_init_code:
    # Check the complete initial integer-register sanitization contract.
    orq %rbx, %rax
    orq %rcx, %rax
    orq %rdx, %rax
    orq %rsi, %rax
    orq %rbp, %rax
    orq %r8, %rax
    orq %r9, %rax
    orq %r10, %rax
    orq %r11, %rax
    orq %r12, %rax
    orq %r13, %rax
    orq %r14, %rax
    orq %r15, %rax
    jnz .exit_one
    testq %rdi, %rdi
    je  .hello
    cmpq $1, %rdi
    je  .marker_write
    cmpq $2, %rdi
    je  .marker_read
    cmpq $3, %rdi
    je  .fault_test
    cmpq $4, %rdi
    je  .gp_test
    cmpq $5, %rdi
    je  .bad_syscall
    cmpq $6, %rdi
    je .x87_denied
    cmpq $7, %rdi
    je .sse_denied
    cmpq $8, %rdi
    je .mmx_denied
    cmpq $9, %rdi
    je .bad_rsp
    cmpq $10, %rdi
    je .noncanonical_rsp
    cmpq $11, %rdi
    je .write_code
    cmpq $12, %rdi
    je .execute_data
    cmpq $13, %rdi
    je .read_kernel
    cmpq $14, %rdi
    je .port_io
    cmpq $15, %rdi
    je .execute_stack
    cmpq $16, %rdi
    je .invalid_gate
    jmp .exit_zero

.write_code:
    leaq user_init_code(%rip), %rax
    movb $0, (%rax)
    jmp .exit_one
.execute_data:
    movabsq $0x7f0000002000, %rax
    movb $0xc3, (%rax)
    jmp *%rax
.read_kernel:
    movq $0x100000, %rax
    movq (%rax), %rax
    jmp .exit_one
.port_io:
    outb %al, $0xe9
    jmp .exit_one
.execute_stack:
    movb $0xc3, (%rsp)
    jmp *%rsp
.invalid_gate:
    int $0x80
    jmp .exit_one

.x87_denied:
    fninit
    jmp .exit_one
.sse_denied:
    pxor %xmm0, %xmm0
    jmp .exit_one
.mmx_denied:
    pxor %mm0, %mm0
    jmp .exit_one
.bad_rsp:
    movq $0x1000, %rsp
    movq $3, %rax
    syscall
    ud2
.noncanonical_rsp:
    movabsq $0x800000000000, %rsp
    movq $3, %rax
    syscall
    ud2

.hello:
    movw %cs, %ax
    andl $3, %eax
    cmpl $3, %eax
    jne .exit_one
    pushfq
    popq %rax
    testq $0x200, %rax               # user execution must be preemptible
    jz .exit_one
    testq $0x100, %rax               # no accidental single-step flag
    jnz .exit_one
    movabsq $0x7f0000002010, %rax
    movq %rsp, (%rax)
    movq $0x1111, %rbx
    movq $0x2222, %rbp
    movq $0x1212, %r12
    movq $0x1313, %r13
    movq $0x1414, %r14
    movq $0x1515, %r15
    movq $2, %rax
    movq $1, %rdi
    leaq .hello_msg(%rip), %rsi
    movq $.hello_len, %rdx
    std                             # entry must establish DF=0 for C
    syscall
    cmpq $.hello_len, %rax
    jne .exit_one
    movq $3, %rax
    syscall
    testq %rax, %rax
    jz .exit_one
    movq $4, %rax
    syscall
    testq %rax, %rax
    jz .exit_one
    movq $1, %rax                   # yield, then check preserved state
    syscall
    testq %rax, %rax
    jnz .exit_one
    cmpq $0x1111, %rbx
    jne .exit_one
    cmpq $0x2222, %rbp
    jne .exit_one
    cmpq $0x1212, %r12
    jne .exit_one
    cmpq $0x1313, %r13
    jne .exit_one
    cmpq $0x1414, %r14
    jne .exit_one
    cmpq $0x1515, %r15
    jne .exit_one
    movabsq $0x7f0000002010, %rax
    cmpq %rsp, (%rax)
    jne .exit_one
    pushfq
    popq %rax
    testq $0x400, %rax               # public return policy clears DF
    jnz .exit_one
    movq $2, %rax
    movq $1, %rdi
    leaq .abi_msg(%rip), %rsi
    movq $.abi_len, %rdx
    syscall
    jmp .exit_zero

.marker_write:
    movabsq $0x7f0000002000, %rax    # ZEROOS_USER_DATA_VA
    movl $0xdeadbeef, (%rax)
    movq $2, %rax
    movq $1, %rdi
    leaq .marker_msg(%rip), %rsi
    movq $.marker_len, %rdx
    syscall
    jmp .exit_zero

.marker_read:
    movabsq $0x7f0000002000, %rax    # ZEROOS_USER_DATA_VA
    movl (%rax), %eax
    /*
     * This process's data page is a fresh zeroed page. If the read sees
     * anything but zero, this process is aliasing another process's
     * physical page: report the isolation break and fail.
     */
    testl %eax, %eax
    jne .reader_failed
    movq $2, %rax
    movq $1, %rdi
    leaq .reader_msg(%rip), %rsi
    movq $.reader_len, %rdx
    syscall
    jmp .exit_zero

.reader_failed:
    movq $2, %rax
    movq $1, %rdi
    leaq .reader_fail_msg(%rip), %rsi
    movq $.reader_fail_len, %rdx
    syscall
    jmp .exit_one

.fault_test:
    movq $2, %rax
    movq $1, %rdi
    leaq .fault_msg(%rip), %rsi
    movq $.fault_len, %rdx
    syscall
    movabsq $0x7f0000005000, %rax    # unmapped user page
    movl (%rax), %ebx                # #PF at CPL3, must be contained
    hlt                              # unreachable

.gp_test:
    movq $2, %rax
    movq $1, %rdi
    leaq .gp_msg(%rip), %rsi
    movq $.gp_len, %rdx
    syscall
    cli                              # privileged at CPL3 -> #GP, contained
    hlt                              # unreachable

/*
 * Negative syscall validation. Each rejected call must return -1 (sign
 * bit set) without taking the process down; if one is accepted the
 * process reports which check failed and exits with a non-zero code,
 * which the kernel orchestrator treats as a certification failure.
 */
.bad_syscall:
    # (a) kernel pointer: outside the user range entirely
    movq $2, %rax
    movq $1, %rdi
    movabsq $0x1000, %rsi
    movq $8, %rdx
    syscall
    testq %rax, %rax
    jns .bad_a_failed
    # (b) length overflow: address+length wraps
    movq $2, %rax
    movq $1, %rdi
    movabsq $0x7f0000002000, %rsi
    movabsq $0xfffffffffffff000, %rdx
    syscall
    testq %rax, %rax
    jns .bad_b_failed
    # (c) unmapped page inside the user range
    movq $2, %rax
    movq $1, %rdi
    movabsq $0x7f0000003000, %rsi
    movq $4, %rdx
    syscall
    testq %rax, %rax
    jns .bad_c_failed
    # still alive: a valid write must work
    movq $2, %rax
    movq $1, %rdi
    leaq .bad_msg(%rip), %rsi
    movq $.bad_len, %rdx
    syscall
    jmp .exit_zero

.bad_a_failed:
    movq $2, %rax
    movq $1, %rdi
    leaq .bad_a_msg(%rip), %rsi
    movq $.bad_a_len, %rdx
    syscall
    jmp .exit_one
.bad_b_failed:
    movq $2, %rax
    movq $1, %rdi
    leaq .bad_b_msg(%rip), %rsi
    movq $.bad_b_len, %rdx
    syscall
    jmp .exit_one
.bad_c_failed:
    movq $2, %rax
    movq $1, %rdi
    leaq .bad_c_msg(%rip), %rsi
    movq $.bad_c_len, %rdx
    syscall
    jmp .exit_one

.exit_one:
    movq $0, %rax                    # exit
    movl $1, %edi                    # code 1: validation failure
    syscall
    hlt                              # unreachable
    jmp .

.exit_zero:
    movq $0, %rax                    # exit
    xorl %edi, %edi                  # code 0
    syscall
    hlt                              # unreachable
    jmp .

.abi_msg:
    .ascii "ZEROOS: CPL3 and syscall register ABI verified.\n"
.abi_end:
.set .abi_len, .abi_end - .abi_msg

.hello_msg:
    .asciz "ZEROOS: ring-3 user program executed.\n"
.hello_end:
.set .hello_len, .hello_end - .hello_msg - 1

.marker_msg:
    .asciz "ZEROOS: user stored marker in data page.\n"
.marker_end:
.set .marker_len, .marker_end - .marker_msg - 1

.reader_msg:
    .asciz "ZEROOS: user read data page.\n"
.reader_end:
.set .reader_len, .reader_end - .reader_msg - 1

.reader_fail_msg:
    .asciz "ZEROOS: FAIL reader saw foreign data-page contents\n"
.reader_fail_end:
.set .reader_fail_len, .reader_fail_end - .reader_fail_msg - 1

.fault_msg:
    .asciz "ZEROOS: fault test: touching unmapped page...\n"
.fault_end:
.set .fault_len, .fault_end - .fault_msg - 1

.gp_msg:
    .asciz "ZEROOS: gp test: privileged instruction follows...\n"
.gp_end:
.set .gp_len, .gp_end - .gp_msg - 1

.bad_msg:
    .asciz "ZEROOS: negative syscall checks rejected as required.\n"
.bad_end:
.set .bad_len, .bad_end - .bad_msg - 1

.bad_a_msg:
    .asciz "ZEROOS: FAIL kernel pointer accepted by write\n"
.bad_a_end:
.set .bad_a_len, .bad_a_end - .bad_a_msg - 1

.bad_b_msg:
    .asciz "ZEROOS: FAIL overflowed length accepted by write\n"
.bad_b_end:
.set .bad_b_len, .bad_b_end - .bad_b_msg - 1

.bad_c_msg:
    .asciz "ZEROOS: FAIL unmapped user page accepted by write\n"
.bad_c_end:
.set .bad_c_len, .bad_c_end - .bad_c_msg - 1

.global user_init_end
user_init_end:

.section .note.GNU-stack,"",@progbits
