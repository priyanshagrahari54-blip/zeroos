.section .multiboot2
.align 8
mb2_header:
    .long 0xe85250d6
    .long 0
    .long mb2_header_end - mb2_header
    .long -(0xe85250d6 + 0 + (mb2_header_end - mb2_header))
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
    # Multiboot2: EAX = magic, EBX = info structure.
    # Keep both values in registers across the long-mode transition.
    mov %ebx, %edi
    mov %eax, %esi
    mov $stack_top, %esp

    mov $pml4_table, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    mov %cr0, %eax
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

    # SysV x86-64 ABI: RDI=info address, RSI=magic.
    # The 32-bit writes above already zero-extended both registers.
    mov $stack_top, %rsp
    and $-16, %rsp
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

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

# Bootstrap page tables contain non-zero addresses/flags, so they
# belong in a data section rather than .bss.
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
    .quad 0x00000083
    .fill 511, 8, 0

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:
