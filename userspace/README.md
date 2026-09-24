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

`include/zeroos/runtime.h` and `runtime.c` provide the first freestanding
policy layer above those wrappers. `zeroos_runtime_init` negotiates the
versioned ABI, `zeroos_runtime_write` chunks output at the advertised transfer
limit, and the IPC/spawn/wait helpers enforce public bounds before entering the
kernel. They deliberately do not pretend to be libc or a dynamic linker.

Run `make userspace-abi-check userspace-runtime-check` to compile the
representative consumer and runtime with warnings-as-errors. This is an
ABI/runtime-source gate, not a claim that a full libc, dynamic linker, or
application runtime exists; those remain Stage 2 work.
