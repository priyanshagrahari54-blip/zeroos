# ZEROOS Stage 2 — Userspace Core

## Scope

Stage 2 begins only after the Stage 1 scheduler/process/SMP certification
boundary. The current implementation establishes the first production
privilege and ABI boundary without treating a kernel task as a user process.

The ownership chain is:

    process
      -> private vmm_space
      -> user pages (code/data/stack)
      -> user thread
      -> scheduler task
      -> TSS.RSP0 kernel stack

A scheduler task is not itself a process. A user thread is marked explicitly
and carries a user RIP/RSP pair; kernel threads continue to use a C entry
function. This prevents an accidental kernel function pointer from becoming a
Ring-3 entry point.

## Address-space and CR3 contract

Each process owns a private PML4. The kernel identity mapping is shared through
PML4 slot 0; user mappings are accepted only in the isolated PML4 slot 254,
which covers the `0x00007f0000000000` bootstrap user layout.

The VMM tracks the active CR3 root per CPU. A scheduler handoff activates the
selected thread's process root before the architectural return, or reactivates
the kernel root for a kernel task. Address-space destruction is rejected while
any CPU still has that root active. User page mapping retains a physical-page
reference and process accounting is checked against the VMM counter during
teardown.

The current bootstrap user image has one executable code page, one read-only
(non-writable) data page, and one writable NX stack page. W^X is enforced by
the VMM mapping contract. The loader-facing validation checks that the entry
page is user-accessible and executable and that the initial stack is user and
writable.

## Ring-3 transition

The runtime GDT exposes:

| Selector | Meaning |
|---:|---|
| `0x08` | kernel code |
| `0x10` | kernel data |
| `0x1b` | user code, DPL3 |
| `0x23` | user data/stack, DPL3 |

`zeroos_user_enter()` constructs an architectural IRET frame and enters the
validated user RIP/RSP. The user stack is never reused as a kernel stack.
Each scheduler task owns its own kernel stack and the scheduler publishes it to
TSS.RSP0 before a task handoff. A user timer or syscall frame therefore lands
on the owning task's kernel stack and can be suspended/resumed using the
existing interrupt-frame ownership invariant.

User exceptions remain fail-closed: containable faults terminate the owning
thread through the normal zombie path; malformed frames, platform-fatal
exceptions and missing thread ownership remain kernel-fatal.

## Syscall ABI v1

Userspace enters through `int 0x80` (IDT vector 128, DPL3). Every other IDT
gate remains DPL0. The register ABI is:

- `RAX`: syscall ID on entry and signed result on return;
- `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9`: arguments;
- negative results are `-ZEROOS_E*` error values;
- user pointers are validated against the current process address space before
  copy-in/copy-out;
- length arithmetic is bounded and the write transfer limit is explicit.

`ZEROOS_SYS_ABI_INFO` returns a structure containing an ABI version, structure
size, feature bitmap and maximum transfer size. This is the extension point:
future structures must carry their own size/version fields rather than silently
changing v1 layouts.

The initial v1 calls are:

| ID | Call | Contract |
|---:|---|---|
| 0 | `ABI_INFO` | copy a versioned ABI descriptor to a writable user buffer |
| 1 | `EXIT` | terminate the calling thread with a status; does not return |
| 2 | `WRITE` | bounded fd 1/2 diagnostic output after safe copy-in |
| 3 | `GETPID` | return the owning generation-tagged PID |
| 4 | `GETTID` | return the owning generation-tagged TID |
| 5 | `YIELD` | request interrupt-return rescheduling; no cooperative switch on an ISR stack |

The kernel never trusts a user pointer, user length, file descriptor, or
syscall ID. Unsupported IDs return `-ZEROOS_ENOSYS`; invalid pointers return
`-ZEROOS_EFAULT`; oversized transfers return `-ZEROOS_EOVERFLOW`.

## Init bootstrap and recovery

After the Stage 1 exit gate, the BSP scheduler monitor creates a real process,
maps the built-in statically linked init image, and publishes a user thread.
The image writes a diagnostic through the syscall path and exits. The monitor
then:

1. waits for the thread's normal zombie transition;
2. reaps the thread descriptor;
3. activates the kernel root on the current CPU;
4. reaps the process and destroys its private address space;
5. publishes the Ring-3/syscall/init recovery certificate.

This path is intentionally exercised after SMP hot-offline certification and
runs on the normal scheduler rather than a boot-time direct call. It verifies
that CR3 activation, TSS.RSP0, user pointer validation, syscall return, user
exit, task zombie reclamation, thread reaping, and process address-space
teardown compose without kernel corruption.

## Remaining Stage 2 work

This is not the Stage 2 exit claim. The remaining production gates are:

- ELF segment loader with overflow/permission checks, stack/argv/env/auxv
  construction and a defined relocation/dynamic-loader policy;
- blocking syscall paths with cancellation and timeout semantics;
- capability credentials and secure generation-checked handles;
- pipes, bounded message queues, events, shared-memory lifecycle and socket
  foundations with backpressure;
- init/service manager dependency ordering, supervision, health checks, crash
  diagnostics and restart/shutdown policy;
- userspace runtime wrappers and negative/fault-injection/stress coverage.

The implementation must pass the Stage 2 exit gate only when multiple isolated
services can execute, communicate, fail, restart and cleanly terminate.
