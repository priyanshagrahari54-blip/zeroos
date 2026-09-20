# ZEROOS System Call ABI (Stage 1)

## Mechanism

ZEROOS uses the x86-64 SYSCALL / SYSRET pair. The kernel enables
`IA32_EFER.SCE` and programs:

| MSR | Value | Meaning |
|---|---|---|
| `IA32_LSTAR` | address of `syscall_entry` | 64-bit syscall target |
| `IA32_STAR` | kernel CS `0x08` in bits 32-47; SYSRET base `0x10` in bits 48-63 | SYSCALL enters CS 0x08; SYSRET returns CS 0x23 / SS 0x1B (RPL 3) |
| `IA32_SFMASK` | `0x10300` (IF, DF, AC) | flags cleared on entry |

On SYSCALL the CPU saves the user RIP into RCX and the user RFLAGS into R11,
loads RIP from LSTAR, and masks RFLAGS (SFMASK clears IF, DF, AC). **RSP is
not changed** by the instruction itself. The entry trampoline
(`kernel/syscall_entry.S`) immediately moves to the top of the current task's
kernel stack — before any memory operation — and builds the syscall frame
there, so kernel state never lands in user-writable memory. On return the
trampoline restores the user RSP and sets IF in R11 before SYSRET, so user
code always resumes with interrupts enabled (DF and AC are cleared).

## Register convention

```
number : RAX
args   : RDI, RSI, RDX, R10, R8, R9      (SysV order)
result : RAX   (0 or per-call value on success; -1 on failure)
```

Clobbered by every syscall: `RAX`, `RCX`, `R11`. Argument registers are not
preserved. Callee-saved registers (`RBX`, `RBP`, `R12-R15`) are preserved.
On entry RSP must follow SysV alignment (8 mod 16 inside a function frame).

## Syscall table (Stage 1)

| Number | Name | Arguments | Result |
|---:|---|---|---|
| 0 | `exit` | RDI = exit code | never returns |
| 1 | `yield` | none | 0 |
| 2 | `write` | RDI = fd (only 1 = debug console), RSI = user buffer, RDX = length | bytes written, or -1 |
| 3 | `getpid` | none | process ID |
| 4 | `gettid` | none | thread (task) ID |

`write` rejects `length > 512` with -1 instead of capping it: capping would
turn an overflowed or malformed length into a silently different transfer.

The table is deliberately small and numbered for extension; reserved numbers
return -1.

## User-pointer rules (mandatory)

The kernel never dereferences a user address directly. Every user pointer is
processed by `copy_from_user()` / `copy_to_user()` after whole-range
validation in `vmm_space_is_user_range()`:

1. the address and `address + length - 1` are canonical, with overflow of
   `address + length` rejected before arithmetic;
2. the entire range lies in the process's user region (PML4 slot 254);
3. every page of the range is mapped in the calling process's address space
   (page-by-page walk, so the final byte of a multi-page buffer is checked);
4. the page carries the access rights the operation requires (read or
   write).

A page fault or general protection taken in user mode is contained: the
kernel reports the fault, kills the current process (zombie), and continues
scheduling other tasks. Kernel-mode exceptions remain fatal.

## Memory permissions (W^X)

User mappings created by the VMM must be either executable or writable, never
both. The process layout is:

```
0x7f0000000000  code   user, executable, not writable
0x7f0000002000  data   user, writable,   not executable
0x7f0000004000  stack  user, writable,   not executable
```

## Certification evidence (QEMU)

The boot self-test sequence exercises, from a genuine CPL-3 user program:
hello (write/getpid/gettid/exit), isolated data-page store/read across two
processes sharing the same virtual address, a contained page fault from an
unmapped user page, a contained general protection from a privileged
instruction, and rejection of kernel pointers, overflowed lengths, and
unmapped user pages by `write`.
