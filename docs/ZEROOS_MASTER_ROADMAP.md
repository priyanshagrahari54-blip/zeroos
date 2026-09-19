# ZEROOS MASTER ROADMAP

## Execution rule

Every milestone follows:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

Never create a basic version merely to replace it later.

## Current state

Already substantially implemented:
- Multiboot2 bootstrap
- x86-64 long mode
- bootstrap paging
- serial/debug diagnostics
- physical page bitmap allocator
- 4-level VMM
- huge-page splitting
- spinlocks and atomics
- IDT and ISR stubs
- PIC/PIT
- timer
- IRQ registration
- task creation
- cooperative context switching
- idle task
- timer preemption framework
- wait queues
- timed sleep
- zombie reaping
- preempt_count
- stack guards
- scheduler diagnostics
- QEMU CI boot path

Current priority:
Scheduler/context lifecycle must be proven stable before moving to unrelated major kernel work.

Previous failure:
invalid opcode at RIP 0x1bf and impossible task id 0x21 after scheduler activity.

Hardening already added:
- stale interrupt-frame invalidation
- tagged IRQ exit
- consumed-frame retirement
- frame bounds validation
- saved-stack validation
- current_task pointer/state validation
- scheduler diagnostics

## NEXT 50-STEP BATCH

1. Verify latest HEAD.
2. Verify latest CI run.
3. Inspect build logs.
4. Inspect serial output.
5. Inspect debug port.
6. Inspect QEMU reset output.
7. Verify scheduler reaches 100 ticks.
8. Verify worker completion.
9. Verify wait completion.
10. Verify sleep completion.
11. Verify idle fallback.
12. Audit current_task writes.
13. Audit interrupt_frame writes.
14. Audit interrupt_frame clears.
15. Audit saved_stack writes.
16. Audit state transitions.
17. Audit every scheduler selection.
18. Verify IRQ selection mode.
19. Verify cooperative selection mode.
20. Verify frame stack bounds.
21. Verify saved stack bounds.
22. Verify task ID/slot consistency.
23. Verify wait queue membership.
24. Verify sleep queue membership.
25. Prevent wait+sleep double membership.
26. Verify exit removes queue membership.
27. Verify zombie reaping.
28. Verify page_free locking interaction.
29. Verify task_lock IRQ safety.
30. Stress RBX.
31. Stress RBP.
32. Stress R12.
33. Stress R13.
34. Stress R14.
35. Stress R15.
36. Stress repeated cooperative yields.
37. Stress timer preemption.
38. Stress mixed yield/preemption.
39. Stress sleep/preemption.
40. Stress wait/preemption.
41. Stress wake/timeout race.
42. Stress exit during scheduling.
43. Stress idle transitions.
44. Add deterministic scheduler trace.
45. Add context-switch sequence numbers.
46. Add per-task transition counters.
47. Add panic context dump.
48. Re-run QEMU.
49. Re-run CI.
50. Declare scheduler stable only after evidence.

## PHASE K — Kernel maturity

- process object
- thread object
- PID/TID
- parent/child
- wait/exit
- exec
- file descriptor table
- credentials
- syscall ABI
- user stack
- syscall entry/exit
- safe user copies
- page fault path
- signal/event model
- IPC
- resource limits

## PHASE M — Memory maturity

- kernel object allocator
- slab/object caches
- per-CPU page caches
- page refcounts
- ownership
- higher-order allocation
- fragmentation tracking
- reclaim
- page cache
- swap
- higher-half kernel
- physical direct map
- user address spaces
- page faults
- demand paging
- COW
- VMAs
- guard pages
- ASLR
- PCID
- TLB shootdowns
- page table reclamation

## PHASE S — Scheduler maturity

- scheduler entities
- priorities
- runqueue abstraction
- scheduling class interface
- fair scheduler
- virtual runtime/eligibility
- real-time scheduler
- deadline evaluation
- per-CPU runqueues
- CPU affinity
- load balancing
- CPU hotplug
- tickless operation
- latency tracing
- wakeup latency benchmark
- context switch benchmark
- lock contention benchmark

## PHASE D — Drivers

- device objects
- bus objects
- driver registry
- driver matching
- PCI
- ACPI
- DMA
- MMIO
- IRQ routing
- USB
- HID
- storage
- network
- graphics
- audio
- power
- thermal

## PHASE F — Storage

- block layer
- request queues
- HDD-aware scheduling
- partitioning
- VFS
- filesystem
- journaling
- page cache
- buffered I/O
- direct I/O
- async I/O
- fsck/recovery
- snapshots
- checksums
- encryption

## PHASE N — Networking

- net device API
- packet buffers
- Ethernet
- IPv4
- IPv6
- ARP/ND
- UDP
- TCP
- DNS
- DHCP
- firewall
- socket API
- TLS integration
- network diagnostics
- Wi-Fi

## PHASE U — Userspace

- init
- service manager
- process supervisor
- IPC
- logging
- device manager
- storage manager
- network manager
- audio service
- settings service
- notification service
- package service
- update service

## PHASE G — Graphics and UI

- framebuffer
- display abstraction
- input subsystem
- GPU abstraction
- compositor
- surfaces
- window manager
- UI toolkit
- font system
- taskbar
- launcher
- universal search
- control center
- notifications
- workspace manager
- settings
- performance center
- file manager
- terminal
- screenshot/annotation
- accessibility

## PHASE A — Native applications

- ZERO Files
- ZERO Terminal
- Browser
- Notes
- PDF viewer
- Calculator
- Dictionary
- Study Center
- Code Editor
- Media Player
- Settings
- Diagnostics
- Performance Center

## PHASE T — Study platform

- notes
- rich text
- markdown
- PDF annotation
- highlighting
- flashcards
- quizzes
- revision planner
- focus timer
- dictionary
- calculator
- AI study assistant
- offline-first support

## PHASE C — Compatibility

Windows compatibility:
- user-space
- isolated
- sandboxed
- resource controlled

Android:
- user-space
- isolated
- resource controlled
- unified launcher

## PHASE X — Security

- privilege separation
- capabilities
- permissions
- sandboxing
- NX
- W^X
- ASLR
- stack protection
- package signing
- update signing
- secure boot
- TPM
- encrypted storage
- secrets service
- audit

## PHASE R — Recovery

- recovery environment
- boot repair
- filesystem check
- snapshots
- update rollback
- safe mode
- driver isolation
- recovery terminal
- network recovery
- reset

## PHASE P — Performance

Benchmark:
- boot
- idle RAM
- idle CPU
- process creation
- context switch
- syscall
- allocation
- page fault
- filesystem
- disk
- network
- UI frame latency
- app launch
- suspend/resume

Always publish methodology with results.

## PHASE Z — Release

Development:
- functionality

Preview:
- regression stability
- recovery
- update rollback
- measured resources

Stable:
- security review
- hardware matrix
- performance baseline
- recovery validation
- update validation
- documentation
- no known critical crashes

## Final dream

ZEROOS becomes a complete ecosystem:
Kernel + Drivers + Security + Storage + Network + Desktop + Native Apps + Study Platform + Compatibility + Recovery + Updates + Forge AI

while remaining fast, lightweight, native, reliable and measurable.
