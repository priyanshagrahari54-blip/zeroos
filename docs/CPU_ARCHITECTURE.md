# ZEROOS CPU Architecture Contract

## Scope

The current Stage 1 supported matrix is x86-64 QEMU/PC hardware with a GRUB
Multiboot2 handoff. The CPU layer has a bounded per-CPU record array and an AP
startup boundary; an AP is published only after it installs its own GS base,
GDT/TSS, IDT, interrupt-controller state and TLB registration. Multi-vCPU
runtime certification is still a separate gate, and this document does not
claim universal hardware support.

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
IA32_EFER.NXE when CPUID proves support on the BSP and independently on every
AP trampoline; callers still treat capability absence as a reason to disable
an optional protection rather than to write an unsupported MSR. The VMM never
places the NX page-table bit in hardware mappings when the capability is
absent.

## Floating-point baseline

The kernel clears CR0.EM and reset cache-disable/NW state, sets CR0.MP,
CR0.NE and CR0.WP, and enables CR4.OSFXSR and CR4.OSXMMEXCPT. APs normalize
the same CR0 policy during their protected-mode transition. Extended XSAVE/AVX
state is not enabled until a future per-thread FPU ownership policy is
present; this prevents silently corrupting architectural state.

## Per-CPU state

`struct cpu_local` is the ownership boundary for:

- logical CPU ID and APIC ID;
- interrupt nesting depth;
- interrupt count;
- scheduler epoch.

Records are statically allocated for the bounded supported CPU capacity.
`cpu_prepare_local()` reserves an AP record, while `cpu_mark_online()`
publishes its GS base only during the AP entry handshake. The AP then installs
its per-CPU GDT/TSS and IDT, enables its Local APIC state, and registers with
the TLB protocol before the SMP startup boundary acknowledges it. The BSP
waits only for a bounded interval, retries a failed AP at most once with a
new generation token, and removes failed APs from the TLB target mask before
entering BSP-only recovery. APs remain out of the BSP scheduler until
per-CPU scheduling and device-IRQ ownership have passed their own gates.

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
AP startup requires a usable LAPIC and validated MADT processor records, while
non-timer routes and per-CPU device-controller ownership remain separate gates.

## Security and failure policy

Unsupported or malformed capability data must produce an explicit diagnostic.
No code assumes that APIC, NX, invariant TSC, SMEP, SMAP or PCID exists. An
unsupported optional feature is disabled; page-table NX flags are conditional
on the probed capability; a missing mandatory SSE2 baseline fails CPU
initialization before scheduler startup.

## Validation

The boot certification checks:

- CPU capability milestone;
- allocator and VMM W^X/overflow negative paths;
- synchronization try/bounded paths and rwlock operations;
- APIC/PIC capability report;
- timer clocksource report;
- generation-tagged AP acknowledgement, bounded retry, stale-token quarantine,
  failed-dispatch fault injection and TLB-mask recovery;
- AP CR0/EFER/CR3/IDT policy and shared-root invariants;
- scheduler priority, affinity and starvation-aging metadata;
- repeated QEMU scheduler/process stress boots;
- four-vCPU SMP startup and remote-shootdown certification;
- CPU hot-offline queue evacuation and AP parking certification;
- NX-disabled two-vCPU boot certification.

## Remaining Stage 1 boundary

Full IOAPIC IRQ ownership beyond the timer route, CPU hot-offline evacuation,
remote TLB-shootdown stress beyond boot certification, extended FPU state
switching, and supported-hardware multi-vCPU validation remain required before
claiming complete SMP hardware support. The TLB request/acknowledgement
contract and AP startup handshake are implemented and fail closed when an AP
cannot reach the published state. These capabilities are deliberately
isolated behind explicit gates rather than represented by a fake single-CPU
success path.
