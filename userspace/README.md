# ZEROOS userspace ABI surface

`include/zeroos/syscall.h` is the public freestanding C wrapper layer for
Syscall ABI v1. It is intentionally independent of `kernel/` headers: a Ring-3
runtime can include it without importing kernel structures or implementation
locks.

The wrappers preserve the register contract (`RAX`, `RDI`, `RSI`, `RDX`, `R10`,
`R8`, `R9`) and return signed `-ZEROOS_E*` values. Pointer arguments remain
owned by the caller; the kernel validates every range before copying or
mapping. IPC, record pipes, coalescing events, shared-memory capabilities,
spawn, wait, diagnostics, and process identity all have named wrappers.

Run `make userspace-abi-check` to compile the representative consumer under
`tests/abi_compile.c` with warnings-as-errors. This is an ABI/header gate, not
a claim that a full libc, dynamic linker, or application runtime exists; those
remain Stage 2 work.
