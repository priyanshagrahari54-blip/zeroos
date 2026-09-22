# ZEROOS CPU Architecture Contract

## Scope

The current Stage 1 supported matrix is x86-64 QEMU/PC hardware with one
online CPU and a GRUB Multiboot2 handoff. The implementation does not claim
SMP activation or universal hardware support. The CPU layer is nevertheless
SMP-safe in ownership shape: CPU capabilities, the bootstrap APIC ID and a
per-CPU record are explicit rather than hidden in scheduler globals.

## Capability discovery

`kernel/cpu.c` performs CPUID discovery before higher-level initialization and
records:

- vendor-independent family, model and stepping;
- bootstrap APIC ID and logical-processor count;
- physical and virtual address-width limits;
- SSE2/FPU baseline;
- NX, APIC, x2APIC, TSC-deadline, invariant-TSC, PCID, SMEP, SMAP, OSXSAVE,
  one-GiB-page and RDRAND capability bits;
- TSC frequency from CPUID leaves 0x15/0x16 where firmware exposes it.

ZEROOS requires SSE2 for the compiler/runtime ABI. NX is enabled through
IA32_EFER.NXE when CPUID proves support; callers still treat capability
absence as a reason to disable an optional protection rather than to write an
unsupported MSR.

## Floating-point baseline

The kernel clears CR0.EM, sets CR0.MP, and enables CR4.OSFXSR and
CR4.OSXMMEXCPT. Extended XSAVE/AVX state is not enabled until a future
per-thread FPU ownership policy is present; this prevents silently corrupting
architectural state.

## Per-CPU state

`struct cpu_local` is the ownership boundary for:

- logical CPU ID and APIC ID;
- interrupt nesting depth;
- interrupt count;
- scheduler epoch.

The current UP build has one statically allocated record. AP startup must add
records and publish them only after the AP has installed its GDT, IDT, stack,
interrupt controller route and scheduler state.

## Time source

The timer layer uses invariant TSC nanoseconds when the frequency and
invariant-TSC capability are available, with the PIT tick as a deterministic
fallback. The PIT remains the bootstrap clock-event source at 100 Hz. CMOS RTC
reads provide a wall-clock Unix-seconds sample; wall clock is never used for
scheduler deadlines.

## Interrupt controller boundary

`kernel/acpi.c` validates the Multiboot2 ACPI RSDP, RSDT/XSDT and MADT
checksum/length chains and records enabled processors, IOAPICs and interrupt
source overrides. `kernel/apic.c` probes the Local APIC MSR and version
register, maps validated controller MMIO pages after VMM initialization, and
publishes a timer-only LAPIC/IOAPIC route only after GSI and redirection
validation. The discovery record retains bounded processor, IOAPIC and
source-override descriptors, not just aggregate counts, so routing is built
from validated firmware records. A failed activation retains the 8259 PIC;
AP startup, non-timer routes and per-CPU controller ownership remain
unsupported until their own gates pass.

## Security and failure policy

Unsupported or malformed capability data must produce an explicit diagnostic.
No code assumes that APIC, NX, invariant TSC, SMEP, SMAP or PCID exists. An
unsupported optional feature is disabled; a missing mandatory SSE2 baseline
fails CPU initialization before scheduler startup.

## Validation

The boot certification checks:

- CPU capability milestone;
- allocator and VMM W^X/overflow negative paths;
- synchronization try/bounded paths and rwlock operations;
- APIC/PIC capability report;
- timer clocksource report;
- scheduler priority, affinity and starvation-aging metadata;
- repeated QEMU scheduler/process stress boots.

## Remaining Stage 1 boundary

Full IOAPIC IRQ ownership beyond the timer route, AP startup, per-CPU
runqueues, TLB shootdowns and extended FPU state switching remain required
before claiming SMP hardware support. They are deliberately isolated behind
this contract rather than represented by a fake single-CPU success path.
