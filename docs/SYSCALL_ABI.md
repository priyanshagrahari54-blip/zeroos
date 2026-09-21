# ZEROOS system-call ABI (Stage 1)

## Boundary and ownership

The CPU standard defines SYSCALL/SYSRET and their MSRs; ZEROOS defines the
call numbers, register convention, validation and process-lifetime policy.
SYSCALL saves user RIP in RCX and user RFLAGS in R11 but does not switch RSP.
The entry uses a kernel-owned single-CPU scratch word to save user RSP, then
loads the current thread's trusted kernel stack. It never accesses user RSP
as memory. Once saved on that stack, all call state belongs to the thread,
including across a cooperative yield. NMI/DF do not enter this trampoline.
Multi-CPU support will require per-CPU scratch storage.

| MSR | Configuration |
|---|---|
| EFER | preserve existing bits, enable SCE |
| LSTAR | `syscall_entry` |
| STAR | `0x0010000800000000`: kernel CS 0x08, SYSRET CS 0x23 / SS 0x1B |
| SFMASK | `0x44700`: clear TF, IF, DF, NT, AC on entry |

Only the return path restores user RSP, as its **last stack access** before
SYSRET. C validates that RCX and RSP are canonical and mapped inside the
process's user slot, with a writable RSP. Invalid state terminates the thread
and marks a contained process fault; it is never passed to SYSRET. Return
RFLAGS retains arithmetic flags, sets IF and bit 1, and clears other flags.
A user-looking RSP is not proof of caller CPL; this entry is reserved for
live user threads, while trusted kernel code must not invoke SYSCALL.

## Public register convention

- Number/result: RAX; arguments: RDI, RSI, RDX, R10, R8, R9.
- Success: per-call value; failure: exactly `-1` (all bits set).
- Clobbers: RAX, RCX, R11 and argument registers.
- Preserved: RBX, RBP, R12–R15 and user RSP.
- No user-stack alignment requirement for the instruction. Kernel C-call
  alignment is established independently on the trusted stack.

| Number | Name | Arguments | Result |
|---:|---|---|---|
| 0 | exit | RDI = exit code | does not return |
| 1 | yield | none | 0 |
| 2 | write-debug | RDI = 1, RSI = buffer, RDX = length | bytes written or -1 |
| 3 | getpid | none | process ID |
| 4 | gettid | none | thread ID |

Unknown numbers fail. Write-debug rejects lengths above 512. Zero length
returns zero without dereferencing the buffer. Nonzero writes validate the
whole range, copy exactly that length, and emit exactly those bytes (the UART
adds CR before LF). Embedded NUL does not terminate a write. It is not a
NUL-terminated string API and never scans beyond the local copy.

## Initial thread state and instruction policy

The built-in image starts with RDI equal to its entry argument and every other
GPR zero. DS/ES use the user data selector; FS/GS are null. RFLAGS starts at
`0x202`; the supplied RSP points inside a writable NX stack page. This is not
an ELF/argc/argv entry contract; no general executable loader exists yet.

Stage 1 is **integer-only**. CR0.TS remains set; x87/MMX/SSE attempts cause a
contained #NM rather than accessing inherited extended state. Kernel C uses
general registers only, with an ELF instruction scan as a regression check.
Future floating-point/vector support must introduce per-thread state ownership
before relaxing this policy. No FP/vector-preservation ABI is promised today.

SYSCALL is the sole supported system-call instruction. If CPUID advertises
SYSENTER, its CS/ESP/EIP MSRs are zeroed to prevent an inherited alternate
entry path. A user SYSENTER or invalid software interrupt is contained as
#GP/#UD as appropriate for the CPU. These are not alternate syscall APIs.

## User-pointer rules

`copy_from_user` uses `vmm_space_is_user_range` before copying any byte:
canonical start/end; overflow rejection; both endpoints in PML4 slot 254;
present/user permission at every table level; write permission at every
level when requested; and every page checked including the final byte.
Unsupported upper-level huge mappings are rejected, not treated as tables.
The copy walks translations and reads through the kernel physical mapping.
No `copy_to_user` API is implemented yet (the initial calls do not need one).

## Tests and limitations

`make host-test` executes the actual range walker and syscall C dispatcher
with hardware/scheduler side effects replaced by test stubs. It checks
multi-page boundaries, parent permissions, canonicality, overflow, unknown
calls, binary writes at the 512-byte limit, no partial copy on rejection,
and containment of invalid RCX/RSP. This is not proof of assembly entry.

The built-in ring-3 program checks CS.RPL, IF/TF, positive return values,
callee-saved GPR canaries and unchanged RSP across write/getpid/gettid/yield.
Its CI marker is `CPL3 and syscall register ABI verified`. Additional real
user cases assert initial register clearing, disabled x87/MMX/SSE, noncanonical
and kernel-address RSP rejection, RX-code writes, NX data/stack execution,
kernel reads, port I/O and unsupported entry instructions. Hardware results
and certification status are recorded separately in VALIDATION.md.
