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
| 6 | `IPC_CREATE` | create a bounded channel pair and return generation-checked handles |
| 7 | `IPC_GRANT` | copy a selected subset of capability rights to a generation-checked live process (`R10`, zero means all source rights) |
| 8 | `IPC_CLOSE` | revoke the caller's capability |
| 9 | `IPC_SEND` | bounded message copy into the peer queue |
| 10 | `IPC_RECEIVE` | receive or peek one bounded message |
| 11 | `SPAWN` | copy a bounded static ELF plus argv/envp into a child process |
| 12 | `WAIT` | wait for and reap an owned child, returning its generation-tagged PID |

`SPAWN` accepts bounded vectors (16 arguments and 16 environment strings, each
at most 128 bytes) and constructs an initial stack containing `argc`, argv,
envp, and a bounded auxiliary vector (`AT_ENTRY`, `AT_PHNUM`, `AT_PHENT`,
`AT_PAGESZ`, `AT_BASE`, and the explicit static-loader `AT_PHDR=0` policy).
The child gets a private address space and one user thread; all image-copy,
stack-map, ELF-load, and thread-publication failures roll back transactionally.
`WAIT` validates child ownership before sleeping/polling, supports a bounded
`R10` tick timeout and nonblocking mode, and reaps all zombie threads before
destroying the child address space.

IPC handles are process-scoped capabilities, not global file-like integers.
The kernel checks owner, generation, rights and endpoint lifetime on every
operation. Queues have a fixed depth and message size, so exhaustion returns
`-ZEROOS_EAGAIN` for `NONBLOCK` rather than allocating unbounded kernel memory.
A closed peer returns `-ZEROOS_EPIPE`.

Blocking send/receive paths publish the current task on an endpoint wait queue
while holding the IPC condition lock, then perform the ordinary scheduler
block transition. Enqueue/dequeue and endpoint destruction wake the opposite
waiter class, so a full or empty queue cannot lose a wakeup and capability
revocation cancels blocked operations. The timed variants use the `R9` syscall
argument as a bounded tick timeout (`R9 == 0` means no timeout for ABI
compatibility); expiry returns `-ZEROOS_ETIMEDOUT`, and a failed scheduler
block returns `-ZEROOS_EINTR`. `PEEK` does not wake blocked senders because it
does not free queue capacity. The public kernel helpers expose both infinite
and timed forms so service code and fault tests use the same semantics.

The kernel never trusts a user pointer, user length, file descriptor, or
syscall ID. Unsupported IDs return `-ZEROOS_ENOSYS`; invalid pointers return
`-ZEROOS_EFAULT`; oversized transfers return `-ZEROOS_EOVERFLOW`.

## Executable loading

`kernel/elf.c` validates ELF64 little-endian x86-64 images before allocating
anything: checked header/program-header bounds, supported type, PT_LOAD
overflow and alignment rules, `filesz <= memsz`, canonical/user PML4 range,
entry-in-an-executable-segment, non-overlapping pages, and W^X segment flags.
ET_EXEC is loaded at its declared address and ET_DYN receives a fixed bias at
the ZEROOS user base. PT_INTERP is rejected until a dynamic-loader policy is
implemented; accepting an interpreter-less static binary is explicit rather
than an accidental jump into unvalidated bytes.

Each load is transactional. Pages are zero-filled, mapped with segment-derived
permissions, populated only from validated file ranges, and rolled back through
the process-owned VMM accounting on any allocation or mapping failure. The
loader workspace is serialized and bounded so the 4 KiB kernel task stack is
not used as an unbounded program-header scratch area. The bootstrap init image
is now built as a real two-segment ELF (RX code/data header and RW NX data),
then receives its separately mapped RW NX stack.

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

## Init service manager and recovery bootstrap

The bootstrap monitor now supervises a real isolated service image in addition
to init. It creates a controller process with an endpoint pair, grants only
the worker-side send/receive/close rights to a separately loaded Ring-3
service, and validates the reply after the worker exits. The service first
blocks in `IPC_RECEIVE`; the monitor observes the scheduler blocked state and
sends the request, which exercises the atomic wait-queue publication and wake
path. Attempt one deliberately exits with a failure status after delivering
its bounded IPC reply. The monitor reaps the thread and address space, revokes
the worker capability, closes the controller endpoints, and launches attempt
two with a fresh generation-checked channel. Attempt two must deliver the
reply and exit successfully before init is considered recovered. This
exercises cross-process capability transfer, least-privilege rights,
backpressure/endpoint lifetime, blocking wakeup, failure detection, restart,
address-space teardown, and no-stale-capability cleanup in the normal
scheduler path.

The service image is a static ET_EXEC with the same RX code and RW/NX data
policy as init. The worker's IPC handle and restart-specific status are patched
into the image before loading; no kernel pointer or ambient global endpoint is
exposed to Ring 3. Controller and worker resource limits are explicit, and
all setup failures roll back unpublished processes and mapped pages.

## Remaining Stage 2 work

This is not the Stage 2 exit claim. The remaining production gates are:

- ELF process construction with argv/env/auxv, executable identity and a
  defined relocation/dynamic-loader policy;
- a persistent userspace init/service-manager process rather than only the
  bootstrap supervisor, including dependency ordering, health checks, crash
  diagnostics, shutdown policy and multi-service resource accounting;
- capability credentials/rights policy and a public userspace runtime library;
- pipes, shared-memory lifecycle, events and socket foundations built on the
  bounded/backpressure and cancellation contracts;
- negative, fault-injection, timeout, cancellation, resource-exhaustion and
  multi-CPU stress coverage for the live syscall paths.

The implementation must pass the Stage 2 exit gate only when multiple isolated
services can execute, communicate, fail, restart and cleanly terminate under
all required functional, negative, stress, security, recovery, documentation,
CI and QEMU gates.
