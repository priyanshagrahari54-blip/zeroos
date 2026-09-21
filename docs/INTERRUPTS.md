# ZEROOS interrupt boundary

The x86-64 IDT/TSS/frame formats are hardware standards. ZEROOS owns the
normalization, dispatch, fault policy, IRQ binding and scheduling rules.
See [X86_BOUNDARY.md](X86_BOUNDARY.md) for exact offsets and encodings.

## Entry and return

There are 256 16-byte IDT gates. The common assembly entry normalizes the
hardware/software error-code distinction and saves all 15 general registers.
The 176-byte frame contains GPRs, vector, error, RIP, CS, RFLAGS, RSP and SS.
In 64-bit mode the CPU frame includes SS:RSP even for same-CPL delivery;
IRETQ restores them. An IST switch does **not** add an IST-index word.
Kernel C is compiled general-register-only.

An early fatal IDT precedes allocator/VMM initialization. Both fatal paths
report vector, error, saved RIP and CR2 for page faults, then halt. QEMU tests
assert exact fault addresses/codes, not merely the presence of a message.

## Emergency stacks

The #DF gate uses IST1; the NMI gate uses IST2. These are separate permanent
16 KiB stacks, independent of task RSP0. The IST index belongs in the low three
bits of **gate byte 4**, not the access/type byte. The complete 104-byte TSS
contains the stack pointers and an I/O-map offset beyond its descriptor limit,
which denies user port I/O.

NMI and #DF are fatal regardless of interrupted CPL. They never become an
ordinary reschedulable user fault. The CI NMI case invokes the NMI gate in
software to test its frame/stack path; it does not emulate every asynchronous
NMI interleaving. A separate case causes a genuine double fault.

## User fault containment

For other exceptions, saved **CS.RPL** identifies the interrupted privilege
level. A CPL3 exception marks the process/thread terminal with a fault-derived
exit code and requests a switch at IRQ exit. A CPL0 exception is fatal.
Interrupt gates and the dispatcher keep IRQs disabled while the task becomes
terminal, avoiding an intermediate non-running current task visible to a tick.
The selected task's IRETQ frame restores its own flags.

TSS.RSP0 is updated on every task switch to that task's kernel-stack entry
point (with 512 bytes of headroom reserved). CPL3 IRQs/exceptions therefore do
not use a user-supplied stack. Syscalls separately select the thread's trusted
kernel stack; they do not rely on the SYSCALL instruction to switch RSP.

## IRQ binding and timer

The 8259 PIC supplies vectors 32–47. `irq_register` binds one handler/context
pair per IRQ; `irq_unregister` requires that same pair. Dispatch calls the
bound handler and sends EOI. Device code does not own PIC details.

The PIT supplies a 100 Hz bootstrap tick. Its handler accounts ticks and calls
a bounded tick hook. The scheduler accounts time, wakes expired sleepers,
reclaims eligible terminal tasks and requests preemption; the actual frame
switch occurs at common IRQ exit, not inside the timer C call chain.

Before `task_start_first`, slot 0 is a bootstrap context on the boot stack.
Both the tick hook and IRQ-exit scheduler exempt it from task-stack checks.
A fault-instrumented boot deliberately receives two ticks in this state to
regress the previously observed stack-guard panic. Real tasks retain all guard
and frame checks.

The current scope is one CPU, PIC/PIT, bounded wait/sleep and IRQ-exit
preemption. APIC routing, SMP, shared IRQ policy, deferred device work and
high-resolution clocks are future work. See VALIDATION.md for tested scope.
