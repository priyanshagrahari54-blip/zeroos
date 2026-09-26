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
| 13 | `PIPE_CREATE` | create a bounded byte-stream pipe pair (same capability lifetime rules as IPC) |
| 14 | `PIPE_WRITE` | write up to `ZEROOS_SYSCALL_MAX_TRANSFER` bytes with backpressure/timeout |
| 15 | `PIPE_READ` | read up to the requested byte count with peek/timeout semantics |
| 16 | `EVENT_CREATE` | create a signal endpoint and its wait endpoint |
| 17 | `EVENT_SIGNAL` | coalescing, nonblocking notification signal |
| 18 | `EVENT_WAIT` | consume or peek one notification, with nonblocking/timeout behavior |
| 19 | `EVENT_CLOSE` | close an event capability |
| 20 | `SHM_CREATE` | allocate zero-filled, page-granular shared memory and return a capability |
| 21 | `SHM_GRANT` | grant selected map/write/grant/close rights to another live process |
| 22 | `SHM_MAP` | map the whole object at a caller-selected user address with read or read/write permissions |
| 23 | `SHM_UNMAP` | unmap one whole-object mapping and release its page references |
| 24 | `SHM_CLOSE` | close a shared-memory capability; active mappings keep their pages alive |
| 25–50 | file/VFS calls | Stage 3, additive, gated by `ZEROOS_ABI_FEATURE_FILES` (bit 7). Semantics, structures and migration notes are in [VFS.md](VFS.md) §7; wrappers are in `userspace/include/zeroos/storage.h` |
| 51 | `DISPLAY_INFO` | Stage 5, gated by `ZEROOS_ABI_FEATURE_DISPLAY` (bit 8): copy the read-only `struct zeroos_display_info` geometry record (see TECHSPEC.md) |
| 52 | `DISPLAY_PRESENT` | Stage 5, gated by `ZEROOS_ABI_FEATURE_PRESENT` (bit 9): pixel-mapping scanout submit (see TECHSPEC.md) |

The public freestanding wrapper surface is
`userspace/include/zeroos/syscall.h`. It contains the fixed-width ABI
constants, capability/feature declarations, and inline `int 0x80` wrappers for
every v1 call, including pipe, event, and shared-memory operations. The
`userspace-abi-check` Make target compiles a representative consumer with
warnings-as-errors; it does not link or run in the kernel and therefore cannot
hide ABI drift behind kernel-private headers.

`SPAWN` accepts bounded vectors (16 arguments and 16 environment strings, each
at most 128 bytes) and constructs an initial stack containing `argc`, argv,
envp, and a bounded auxiliary vector (`AT_ENTRY`, `AT_PHNUM`, `AT_PHENT`,
`AT_PAGESZ`, `AT_BASE`, and `AT_PHDR`). `AT_PHDR` is the mapped program-header
address when a readable `PT_LOAD` covers the table; otherwise it is explicitly
zero, the no-PHDR value for this static-loader ABI. The child gets a private
address space and one user thread; all image-copy, stack-map, ELF-load, and
thread-publication failures roll back transactionally. The boot child consumes
its initial stack in Ring 3: it checks argc, argv terminators, environment
termination, and representative argument/environment bytes before emitting
its success message, so argv/envp delivery is an executing runtime gate rather
than only a kernel layout check.
`WAIT` validates child ownership before waiting, publishes an indefinite wait
on the parent's child wait queue without a lost-wakeup window, and is woken
when the final child thread exits. Bounded `R10` tick timeouts and
nonblocking mode retain their explicit polling/expiry behavior, while all
successful waits revalidate ownership and reap every zombie thread before
destroying the child address space. The boot gate also exercises the child
wait/wakeup and reap path with a temporary process pair.

IPC handles are process-scoped capabilities, not global file-like integers.
The kernel checks owner, generation, rights and endpoint lifetime on every
operation. Cross-process grants acquire a generation-checked process lifetime
pin before publishing the target capability; reaping and the final thread-exit
transition wait for that pin, so a reused process slot cannot receive a stale
grant. The boot gate repeats close, stale-handle rejection, cross-process
reduced-rights grant, and unpublished-target revocation across 32 generations;
this is a bounded resource/lifetime stress check, not just a single happy-path
assertion. Queues have a fixed depth and message size, so exhaustion returns
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
and timed forms so service code and fault tests use the same semantics. The
boot gate also blocks a real receiver, closes its peer endpoint, requires the
receiver to wake with `-ZEROOS_EPIPE`, and then reaps the temporary process;
peer-close cancellation is therefore covered independently of event signaling. A
second gate fills a bounded queue, blocks a real sender, drains one record,
and requires the sender to complete; this covers the opposite backpressure
wake direction rather than relying only on service receive wakeups.

`PIPE_CREATE`, `PIPE_WRITE`, and `PIPE_READ` expose a bounded byte stream with
`ZEROOS_IPC_PIPE_CAPACITY` bytes of kernel buffering. A write blocks until the
whole requested chunk fits (or returns `EAGAIN`, `ETIMEDOUT`, `EINTR`, or
`EPIPE`); a read returns any available bytes up to the requested capacity and
may split one write across multiple reads. `PEEK` copies without consuming and
therefore does not wake blocked writers. After the peer closes, buffered bytes
remain readable and the empty pipe returns `EPIPE`. Pipe endpoints use the same
generation-tagged handles, rights, wait-queue publication, cancellation, and
rollback rules as message IPC, while the message channel retains its discrete
record semantics.

`EVENT_CREATE` returns a signal handle and a wait handle. `EVENT_SIGNAL` sets a
single pending bit on the peer and wakes one waiter; repeated signals while the
bit is set coalesce and return zero. `EVENT_WAIT` returns one when it consumes a
pending notification, returns one without consuming it with `PEEK`, returns
`-ZEROOS_EAGAIN` for an empty nonblocking wait, `-ZEROOS_ETIMEDOUT` on a timed
empty wait, and `-ZEROOS_EPIPE` after the peer closes. Event endpoints carry no
message queue, so their lifecycle and wait-queue cancellation are validated by
the same endpoint reference accounting as IPC. The boot userspace gate also
creates a temporary kernel-thread-backed process, proves that an event waiter
reaches `TASK_BLOCKED`, signals it from the supervisor, and reaps the process;
this exercises the event wait queue's lost-wakeup and endpoint-lifetime path,
not only nonblocking polling.

`SHM_CREATE` allocates at most 16 zero-filled pages and returns a
process-scoped generation-tagged capability. `SHM_GRANT` can reduce rights;
`SHM_MAP` accepts only a page-aligned address in the isolated user PML4 and
maps the object read-only by default or read/write with the explicit write
right. Shared mappings retain physical-page references independently of the
object capability, are tracked per process, and are removed before process
address-space destruction. Closing a capability with one of the caller's
mappings active returns `-ZEROOS_EBUSY`; the mapping must be unmapped
explicitly first. A mapping in another process keeps its physical pages alive
after the source capability is closed, and process teardown removes every
tracked mapping transactionally. A failed multi-page map rolls back every page
already mapped and a failed output copy rolls back a newly-created capability.

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
the process-owned VMM accounting on any allocation or mapping failure. Unload
preflights every expected physical page and uses accounting-checked range
unmaps, preventing a stale mapping from leaving a half-unloaded executable. The
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
policy as init. The worker's IPC handle, controller PID, and restart-specific
status are patched into the image before loading; no kernel pointer or ambient
global endpoint is exposed to Ring 3. Before receiving, the worker attempts an
`IPC_GRANT` with its transferred capability. Because the grant is deliberately
limited to `SEND|RECV|CLOSE`, this must fail and proves that a service cannot
escalate or delegate its endpoint. Controller and worker resource limits are
explicit, and all setup failures roll back unpublished processes and mapped
pages.

The init image also probes invalid IPC output, invalid wait status, an unmapped
image, and a malformed ELF image; each must return a negative syscall result
before the valid child spawn. The bootstrap self-test temporarily limits child
creation, fills the bounded IPC endpoint table, verifies the expected
`ENOMEM` boundary, and closes every resource before continuing. The loader
rejects non-page-aligned or overlapping segments before mapping and preserves
distinct invalid-image and resource errors through the spawn ABI.

Every assertion in the init image and in the service-manager image jumps to
its own failure stub. The exit status therefore identifies both the check
and the kernel's answer: `status = ((-rax) & 0xffffff) << 8 | (16 + i)`, where
`i` is the check index in emission order and `-rax` is the errno returned by
the failing syscall. For example, `3615 = 14 << 8 | 31` would be check 15
failing with `EFAULT`. Both images must exit 0. The kernel prints any failure
as `init reap failed (stage=..., exit_status=...)`, naming the reap
precondition that failed (`thread-reap`, `process-state`, `process-reap`,
`ipc-validate` or `exit-status`), so a CI log identifies the failing check
without a debugger.

The in-kernel blocking-wakeup self-tests (event, IPC close and IPC send) run
their probe in a kernel thread. The probe publishes its final state before it
returns into `thread_exit()`, so after observing that state the monitor waits,
bounded, for the probe thread and its process to become zombies before it
reaps them. On SMP, or after a preemption between the two steps, sampling the
zombie state immediately would fail spuriously.

After init and the IPC service recovery path, a separate Ring-3 service-manager
process is published. It owns an explicit child limit, launches a worker,
waits for its unhealthy exit status, patches only manager-owned image data for a
fresh restart, launches a new child generation, and requires a healthy exit
before publishing the dependency/restart certificate. It then remains alive as
a Ring-3 daemon blocked on a supervisor-owned shutdown event. The supervisor
signals that event only after observing the persistent wait, then reaps the
manager and closes its controller capabilities. This adds a real userspace
parent/child dependency, restart lifecycle, persistent lifetime, and explicit
shutdown path to the bootstrap monitor's capability-service test; it does not
yet claim a general dynamic service registry.

## Remaining Stage 2 work

This is not the Stage 2 exit claim. The remaining production gates are:

- ELF process construction with argv/env/auxv, executable identity and a
  defined relocation/dynamic-loader policy;
- a general service registry beyond the bounded manager image, including
  dependency graphs, health checks, crash diagnostics, shutdown policy and
  multi-service resource accounting;
- capability credentials/identity policy and a complete userspace runtime
  library beyond the current freestanding syscall/IPC/spawn/wait layer;
- socket foundations built on the bounded/backpressure and cancellation
  contracts (the byte-stream pipe, coalescing-event, and page-granular
  shared-memory ABI is only the first foundation);
- stronger concurrent capability-lifetime proofs and multi-process/multi-CPU
  stress coverage for grant, close, exit, and reaping races;
- negative, fault-injection, timeout, cancellation, resource-exhaustion and
  recovery assertions for every newly exposed syscall path.

The implementation must pass the Stage 2 exit gate only when multiple isolated
services can execute, communicate, fail, restart and cleanly terminate under
all required functional, negative, stress, security, recovery, documentation,
CI and QEMU gates.
