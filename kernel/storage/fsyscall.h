#ifndef ZEROOS_STORAGE_FSYSCALL_H
#define ZEROOS_STORAGE_FSYSCALL_H
#include "../types.h"

/* File/VFS syscalls (ZEROOS_SYS_OPEN .. ZEROOS_SYS_CHOWN, feature
 * ZEROOS_ABI_FEATURE_FILES). Semantics: docs/VFS.md "System call
 * interface". Runs in the calling thread's syscall context and may block
 * (mutexes, page-cache I/O); never called from IRQ context. */
struct interrupt_frame;
struct process;
void fsyscall_dispatch(struct interrupt_frame *frame, struct process *process);

/* Process teardown hook (called by process_reap outside process_lock):
 * queues descriptor-table and mmap-region release for the storage worker.
 * Non-blocking. */
void storage_process_exit(uint64_t pid, uint32_t generation);
/* Storage-worker side of the above. */
void fsyscall_deferred_work(void);

#endif
