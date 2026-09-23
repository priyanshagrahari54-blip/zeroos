.section .text.ap_trampoline,"ax"

.global ap_trampoline_start
.global ap_trampoline_end
.global ap_trampoline_cr3
.global ap_trampoline_entry
.global ap_trampoline_stack
.global ap_trampoline_cpu_id
.global ap_trampoline_gdt_ptr

.code16
ap_trampoline_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /* Compute the physical address of the copied trampoline from CS:0. */
    xorl %eax, %eax
    movw %cs, %ax
    shll $4, %eax
    movl %eax, %edx
    addl $(ap_trampoline_gdt_ptr-ap_trampoline_start+2), %edx
    movl %eax, (%edx)

    movl %eax, %edx
    addl $(ap_trampoline_gdt_ptr-ap_trampoline_start), %edx
    lgdt (%edx)

    movl %eax, %edx
    addl $(ap_trampoline_cr3-ap_trampoline_start), %edx
    movl (%edx), %eax
    movl %eax, %cr3

    movl %cr0, %eax
    orl $0x00000001, %eax
    movl %eax, %cr0
    ljmpl $0x08, $(ap_trampoline_protected-ap_trampoline_start)

.code32
ap_trampoline_protected:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movl %cr4, %eax
    orl $0x00000620, %eax
    movl %eax, %cr4

    movl $0xc0000080, %ecx
    rdmsr
    orl $0x00000100, %eax
    wrmsr

    movl %cr0, %eax
    andl $~0x00000004, %eax
    orl $0x80000022, %eax
    movl %eax, %cr0
    ljmpl $0x18, $(ap_trampoline_long-ap_trampoline_start)

.code64
ap_trampoline_long:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    leaq ap_trampoline_stack(%rip), %rsi
    movq (%rsi), %rsp
    andq $-16, %rsp
    subq $8, %rsp

    leaq ap_trampoline_cpu_id(%rip), %rsi
    movl (%rsi), %edi

    leaq ap_trampoline_entry(%rip), %rsi
    movq (%rsi), %rax
    jmp *%rax

.align 8
ap_trampoline_cr3:
    .quad 0
ap_trampoline_entry:
    .quad 0
ap_trampoline_stack:
    .quad 0
ap_trampoline_cpu_id:
    .long 0
    .long 0

.align 8
ap_trampoline_gdt_ptr:
    .word ap_trampoline_gdt_end-ap_trampoline_gdt-1
    .long 0

.align 8
ap_trampoline_gdt:
    .quad 0x0000000000000000
    /* 32-bit protected-mode code. */
    .quad 0x00cf9a000000ffff
    /* Flat data. */
    .quad 0x00cf92000000ffff
    /* 64-bit long-mode code. */
    .quad 0x00af9a000000ffff
ap_trampoline_gdt_end:

ap_trampoline_end:
.section .note.GNU-stack,"",@progbits
