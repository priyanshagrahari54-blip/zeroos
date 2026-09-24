# ZEROOS GDT and TSS Architecture

## Purpose

ZEROOS now has a runtime x86-64 Global Descriptor Table and Task State
Segment installed by the kernel after physical memory initialization.

The runtime GDT is separate from the bootstrap GDT in boot/boot.S. The
bootstrap GDT is only responsible for entering long mode. The runtime GDT is
the long-lived descriptor table used by the kernel and future Ring-3 entry.

## Descriptor layout

| Index | Selector | Purpose |
|---:|---:|---|
| 0 | 0x00 | Null descriptor |
| 1 | 0x08 | Kernel 64-bit code |
| 2 | 0x10 | Kernel data/stack |
| 3 | 0x1B | User 64-bit code, DPL3 |
| 4 | 0x23 | User data/stack, DPL3 |
| 5-6 | 0x28 | 64-bit available TSS descriptor |

The kernel code descriptor uses long-mode code semantics (L=1, D=0). The
user code descriptor has the same long-mode encoding with DPL3. Data
descriptors retain normal writable data-segment encoding.

## TSS

The current TSS contains:
- RSP0 for privilege transitions
- reserved RSP1/RSP2 fields
- IST1 double-fault stack, IST2 NMI stack, IST3 machine-check stack,
  IST4 page-fault stack and IST5 segment/protection-fault stack;
- IST6-IST7 reserved for later architecture-specific paths;
- I/O-map base positioned at the end of the TSS

A dedicated runtime entry-stack page is allocated during GDT initialization.
Its aligned top is installed as the initial RSP0. Five additional
allocator-backed, guard-marked IST pages are allocated for fatal/diagnostic
exception classes. The bootstrap task retains the RSP0 value as its
kernel-entry stack.

Every scheduler task owns one allocator-backed, guard-checked kernel stack
page. The scheduler publishes the selected task's aligned stack top through
`gdt_set_kernel_stack()` before the context handoff, so a privilege transition
can never land on the previously running task's stack. This is the protected
kernel-stack layer; a future user thread will add a separate user stack and
user-mode frame without reusing the kernel stack page.

## User transition boundary

Future Ring-3 execution will require:

    Ring 3
       |
       | interrupt/exception
       v
    CPU loads TSS.RSP0
       |
       v
    kernel entry stack
       |
       v
    normalized ISR / fault path

The scheduler now allocates and owns the protected kernel stack for every
schedulable task and updates TSS.RSP0 at each context transition. Actual
Ring-3 privilege entry, separate user stacks, user interrupt entry and user
fault containment remain later Stage 2/Stage 1-boundary work; this module does
not claim that Ring 3 is active.

## Kernel/user selectors

The runtime GDT exposes stable selector constants through:
- gdt_kernel_code_selector()
- gdt_kernel_data_selector()
- gdt_user_code_selector()
- gdt_user_data_selector()
- gdt_tss_selector()

Keeping these selectors in one architecture module prevents duplicated magic
constants in future interrupt and Ring-3 code.

## Initialization contract

The kernel initializes the runtime GDT/TSS after physical memory is available
and before the scheduler self-test enables the long-lived timer path.

Initialization performs:
1. allocate the runtime entry stack;
2. build kernel and user descriptors;
3. build the 64-bit TSS descriptor;
4. load the GDT with LGDT;
5. reload kernel data segments;
6. load the task register with LTR;
7. record the aligned RSP0 value.

## Certification

The IDT assigns the IST slots through `gdt_exception_ist()`: double fault,
NMI, machine check, page fault and segment/protection faults do not reuse the
possibly damaged current stack. Each IST top is aligned and validated during
boot.

The runtime self-test verifies:
- LGDT took effect;
- GDTR base matches the runtime table;
- the table contains the expected descriptor span;
- TR contains the runtime TSS selector;
- CS and DS remain the expected kernel selectors;
- user selectors differ from kernel selectors;
- TSS.RSP0 is non-zero and 16-byte aligned.

The self-test does not claim Ring-3 execution yet. Scheduler validation now
also checks per-task kernel-stack metadata and publishes each selected stack to
TSS.RSP0. Actual privilege transition, separate user stacks, user interrupt
entry and user fault containment remain subsequent user-mode milestones.