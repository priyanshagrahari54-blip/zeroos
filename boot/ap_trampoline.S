.section .text.ap_trampoline,"ax"

.global ap_trampoline_start
.global ap_trampoline_end
.global ap_trampoline_cr3
.global ap_trampoline_entry
.global ap_trampoline_stack
.global ap_trampoline_cpu_id
.global ap_trampoline_gdt_ptr
.global ap_trampoline_gdt
.global ap_trampoline_protected
.global ap_trampoline_protected_far

.code16
ap_trampoline_start:
    cli
    movw $0xe9, %dx
    movb $'r', %al
    outb %al, %dx
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
    lgdtl (%edx)
    movl %eax, %ebx
    movw $0xe9, %dx
    movb $'g', %al
    outb %al, %dx
    movl %ebx, %eax

    movl %eax, %edx
    addl $(ap_trampoline_cr3-ap_trampoline_start), %edx
    movl (%edx), %eax
    movl %eax, %cr3
    movw $0xe9, %dx
    movb $'q', %al
    outb %al, %dx

    /* Keep an absolute protected-mode stack ready before PE is enabled. */
    movl %ebx, %esp
    addl $0x1f0, %esp
    movw $0xe9, %dx
    movb $'e', %al
    outb %al, %dx
    movl %ebx, %edx
    addl $(ap_trampoline_protected_far-ap_trampoline_start), %edx
    movl %edx, %edi
    movw $0xe9, %dx
    movb $'h', %al
    outb %al, %dx

    /* Emit the pointer address and its patched target for transition
     * diagnostics without relying on a second console. */
    movl %edi, %esi
    movl $8, %ecx
1:
    movl %esi, %eax
    shrl $28, %eax
    andl $0x0f, %eax
    cmpb $10, %al
    jb 2f
    addb $('a'-10), %al
    jmp 3f
2:
    addb $'0', %al
3:
    movw $0xe9, %dx
    outb %al, %dx
    shll $4, %esi
    decl %ecx
    jnz 1b

    movw $0xe9, %dx
    movb $':', %al
    outb %al, %dx
    movl (%edi), %esi
    movl $8, %ecx
4:
    movl %esi, %eax
    shrl $28, %eax
    andl $0x0f, %eax
    cmpb $10, %al
    jb 5f
    addb $('a'-10), %al
    jmp 6f
5:
    addb $'0', %al
6:
    movw $0xe9, %dx
    outb %al, %dx
    shll $4, %esi
    decl %ecx
    jnz 4b

    movl %edi, %edx
    movl %cr0, %eax
    orl $0x00000001, %eax
    movl %eax, %cr0
    ljmpl *(%edx)

.code32
ap_trampoline_protected:
    movw $0xe9, %dx
    movb $'p', %al
    outb %al, %dx
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

    /* Code and data descriptors are flat; recover the copied page's physical
     * address from the absolute protected-mode instruction pointer. */
    call 1f
1:
    popl %eax
    subl $(1b-ap_trampoline_start), %eax
    addl $(ap_trampoline_long-ap_trampoline_start), %eax
    pushl $0x18
    pushl %eax
    lret

.code64
ap_trampoline_long:
    movw $0xe9, %dx
    movb $'l', %al
    outb %al, %dx
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movw $0xe9, %dx
    movb $'c', %al
    outb %al, %dx

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
ap_trampoline_protected_far:
    .long 0
    .word 0

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
