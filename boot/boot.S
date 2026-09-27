.section .multiboot2,"a"
.align 8
mb2_header:
    .long 0xe85250d6
    .long 0
    .long mb2_header_end - mb2_header
    .long -(0xe85250d6 + 0 + (mb2_header_end - mb2_header))
    # Optional framebuffer request (header tag type 5, flags=0): ask GRUB
    # for a linear 1024x768x32 mode; boots fine when unsupported. Size 20
    # with 4 pad bytes keeps the next tag 8-byte aligned.
    .short 5
    .short 0
    .long 20
    .long 1024
    .long 768
    .long 32
    .long 0

    .short 0
    .short 0
    .long 8
mb2_header_end:

.section .text
.code32
.global _start
.type _start, @function
_start:
    cli

    # Preserve Multiboot2 handoff before any diagnostic code can clobber EAX/EBX.
    mov %ebx, %edi
    mov %eax, %esi

    # Debug-port marker: GRUB reached our 32-bit entry.
    mov $'B', %al
    mov $0xE9, %dx
    out %al, %dx

    # Early UART initialization and marker.
    mov $0x00, %al
    mov $0x3f9, %dx
    out %al, %dx
    mov $0x80, %al
    mov $0x3fb, %dx
    out %al, %dx
    mov $0x03, %al
    mov $0x3f8, %dx
    out %al, %dx
    mov $0x00, %al
    mov $0x3f9, %dx
    out %al, %dx
    mov $0x03, %al
    mov $0x3fb, %dx
    out %al, %dx
    mov $0xc7, %al
    mov $0x3fa, %dx
    out %al, %dx
    mov $0x0b, %al
    mov $0x3fc, %dx
    out %al, %dx
    mov $'B', %al
    mov $0x3f8, %dx
    out %al, %dx

    mov $stack_top, %esp
    mov $pml4_table, %eax
    mov %eax, %cr3
    mov %cr4, %eax
    # PAE + OSFXSR + OSXMMEXCPT for compiler-generated SSE instructions.
    or $0x620, %eax
    mov %eax, %cr4
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr
    # Enable paging and establish the x86-64 floating-point/SSE baseline
    # expected by compiler-generated code in long mode.
    mov %cr0, %eax
    # Clear reset cache-disable/NW state and establish MP/NE/WP.
    and $~0x60000004, %eax
    or $0x00010022, %eax
    or $0x80000000, %eax
    mov %eax, %cr0
    lgdt gdt64_ptr
    ljmp $0x08, $long_mode

.code64
long_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $stack_top, %rsp
    and $-16, %rsp

    # Debug-port marker: long mode is active.
    mov $'L', %al
    mov $0xE9, %dx
    out %al, %dx

    # Preserve the existing UART-visible marker as well.
    mov $'L', %al
    mov $0x3f8, %dx
    out %al, %dx

    # Establish a guard at the bottom of the bootstrap stack before C code.
    movabs $0x5a45524f424f4f54, %rax
    movq %rax, stack_bottom

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

.global boot_stack_guard_ok
.type boot_stack_guard_ok, @function
boot_stack_guard_ok:
    movq stack_bottom, %rax
    movabs $0x5a45524f424f4f54, %rdx
    cmpq %rdx, %rax
    sete %al
    movzbq %al, %rax
    retq

.section .data
.align 8
gdt64:
    .quad 0
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.align 4096
pml4_table:
    .quad pdpt_table + 0x03
    .fill 511, 8, 0
.align 4096
pdpt_table:
    .quad pd_table + 0x03
    .fill 511, 8, 0
.align 4096
pd_table:
    # Identity-map the first 1 GiB with 2 MiB pages.  This keeps the
    # Multiboot information and early firmware data reachable after the
    # long-mode transition without assuming GRUB placed them below 2 MiB.
    .set pd_index, 0
    .rept 512
        .quad (pd_index << 21) | 0x83
        .set pd_index, pd_index + 1
    .endr

.section .bss
.align 16
stack_bottom:
    .quad 0
    .skip 16376
stack_top:

.section .note.GNU-stack,"",@progbits
