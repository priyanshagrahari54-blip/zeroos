# Complete Low-Level System Tuning Catalog — 100 Features

**Source PDF:** `100 Low-Level System Optimizations Catalog.pdf`  
**Purpose:** ZEROOS engineering reference for evaluating storage, memory, CPU, networking, IPC, boot and hardware tuning. This is a transcription/index of the supplied catalog, not a claim that these features are implemented.

## Engineering rule for ZEROOS

Do not apply Linux-specific knobs, daemon names, sysctls, kernel modules or filesystem options verbatim to ZEROOS. For each item, record: applicability to ZEROOS's native kernel; reusable upstream implementation/license; hardware capability; security and data-integrity impact; RAM/CPU/I/O cost; fallback; measurable acceptance test. Prefer capability detection and measured policy over fixed global settings. Never trade filesystem integrity or security for benchmark speed.

## 1. Storage I/O acceleration, caching and disk lifespan (1–25)

| # | Feature | Subsystem | ZEROOS evaluation |
|---:|---|---|---|
| 1 | bcache Hybrid Tiering | Kernel block/cache driver | Consider only when supported backing device and cache device exist. |
| 2 | EnhanceIO Dynamic Cache | Block cache | Legacy/external implementation candidate; audit maintenance and license. |
| 3 | dm-cache Target | Device mapper | Linux-specific; adapt concept only if ZEROOS gains equivalent mapping layer. |
| 4 | bcachefs COW Cache | Filesystem | Do not assume compatibility with ZEROOS ZJFS; evaluate as a separate filesystem/import. |
| 5 | tmpfs RAM Filesystem | Virtual filesystem | Useful for bounded temporary data; enforce memory ceilings and reclaim. |
| 6 | ramfs Unswappable Storage | Virtual filesystem | Risky on low RAM; prefer bounded, reclaimable memory-backed storage. |
| 7 | io_uring Ring Buffer | Async I/O | Linux API; reuse concept only after ZEROOS async-I/O ABI exists. |
| 8 | BFQ Budget Fair Queue | I/O scheduler | Candidate policy for rotational HDD foreground fairness. |
| 9 | Kyber Multiqueue | I/O scheduler | Primarily low-latency block devices; hardware/workload gated. |
| 10 | mq-deadline Queue | I/O scheduler | Candidate for bounded latency and starvation control. |
| 11 | vm.dirty_background_ratio | Writeback policy | Linux sysctl; implement analogous byte/time thresholds if needed. |
| 12 | vm.dirty_ratio Ceiling | Writeback policy | Bound dirty memory to prevent stalls and data-loss exposure. |
| 13 | vm.dirty_expire_centisecs | Writeback policy | Use bounded writeback age with crash-consistency requirements. |
| 14 | noatime | VFS metadata | Optional mount policy; consider semantics and compatibility. |
| 15 | nodiratime | VFS metadata | Only if directory access timestamps exist and can safely be disabled. |
| 16 | ext4 commit=60 | Filesystem journal | Linux/ext4-specific; never copy blindly into ZJFS. |
| 17 | ext4 data=writeback | Filesystem journal | Integrity risk; not a default recommendation. |
| 18 | Block read-ahead | Block layer | Adaptive and bounded; smaller defaults for 2 GB RAM/HDD. |
| 19 | EROFS | Read-only filesystem | Evaluate for immutable system/application images. |
| 20 | SquashFS + ZSTD | Compressed image | Evaluate for read-only distribution images; measure boot CPU vs I/O. |
| 21 | fallocate preallocation | Filesystem allocation | Useful when supported; does not guarantee zero fragmentation on all media. |
| 22 | TRIM automation | SSD maintenance | Run only on devices/filesystems that support discard; not for HDD. |
| 23 | hdparm spindown management | Drive power | Hardware/drive-specific; avoid aggressive spin cycles. |
| 24 | SATA NCQ depth | SATA controller | Tune from controller/device capability and latency measurements. |
| 25 | NVMe queue depth | NVMe controller | NVMe-specific; not relevant to SATA-only systems. |

## 2. RAM compression, virtual memory and cache pressure (26–50)

| # | Feature | Subsystem | ZEROOS evaluation |
|---:|---|---|---|
| 26 | zram compressed swap | Memory compression | Consider an in-RAM compressed page pool; not a literal RAM multiplier guarantee. |
| 27 | zswap write-back cache | Swap buffer | Linux-specific; analogous compressed cache only if swap architecture supports it. |
| 28 | Zstandard (ZSTD) | Compression | Consider for cold pages/assets; measure CPU and latency on legacy CPUs. |
| 29 | LZ4 | Compression | Candidate for fast page/cache compression. |
| 30 | LZO-RLE | Compression | Compare against LZ4 for sparse pages; don't enable without benchmarks. |
| 31 | vm.swappiness | VM policy | Linux sysctl; ZEROOS needs workload-aware reclaim policy. |
| 32 | vm.vfs_cache_pressure | VFS cache | Use adaptive metadata-cache reclaim, not fixed Linux knob. |
| 33 | watermark boost | Memory pressure | Reclaim early enough to avoid allocation stalls, without wasteful background work. |
| 34 | min_free_kbytes | Allocation reserve | Preserve bounded reserves for interrupt/DMA-critical allocations. |
| 35 | KSM page deduplication | Memory dedup | Optional; scanning CPU/security side-channel costs must be evaluated. |
| 36 | page-cluster=0 | Swap | Linux-specific; evaluate transfer granularity for compressed swap. |
| 37 | Transparent Huge Pages | MMU/TLB | Hardware/app-dependent; avoid memory bloat on 2 GB machines. |
| 38 | MADV_HUGEPAGE-style opt-in | MMU/TLB | Prefer explicit opt-in for large aligned mappings. |
| 39 | PSI resource pressure metrics | Observability | Build bounded event-driven CPU/RAM/I/O pressure signals. |
| 40 | earlyoom | User-space OOM | ZEROOS-native reclaim/termination policy should prefer freeze/reclaim before kill. |
| 41 | systemd-oomd | cgroup OOM | Linux-specific; analogous resource-group supervisor if groups exist. |
| 42 | mlockall / pinned memory | Memory API | Restrict to trusted critical buffers with strict quotas. |
| 43 | HugeTLBFS | Huge pages | Specialized workload only; not default desktop policy. |
| 44 | CMA | Contiguous memory | Useful if supported DMA devices require contiguous physical buffers. |
| 45 | KPTI | Kernel security | Security architecture decision; benchmark only after threat-model review. |
| 46 | proactive compaction | Physical memory | Run on demand/pressure, not as an unbounded background task. |
| 47 | NUMA zone reclaim | NUMA | Usually irrelevant to single-socket legacy laptops; detect topology. |
| 48 | SLUB cache merging | Kernel allocator | Linux implementation detail; evaluate equivalent slab deduplication if allocator supports it. |
| 49 | vm.overcommit_memory | VM allocation | Define explicit commit accounting and failure semantics. |
| 50 | vm.overcommit_ratio | VM allocation | Use policy limits only with tested allocation accounting. |

## 3. CPU scheduling, cache locality and process isolation (51–75)

| # | Feature | Subsystem | ZEROOS evaluation |
|---:|---|---|---|
| 51 | EEVDF scheduler | Scheduler | Study scheduling ideas; do not claim Linux EEVDF is present in ZEROOS. |
| 52 | taskset affinity | Process affinity | Expose only where affinity improves measured workloads. |
| 53 | cgroups v2 CPU/RAM limits | Resource control | Linux-specific; ZEROOS needs native resource groups/quotas. |
| 54 | cpufreq performance mode | Frequency driver | Capability-gated; balance latency, power and thermals. |
| 55 | sched_setaffinity | Scheduler API | Requires stable process/thread ABI and topology awareness. |
| 56 | irqbalance | IRQ routing | Distribute interrupts only where SMP and device support exist. |
| 57 | dedicated IRQ core pinning | Interrupts | Measure contention; avoid pinning on single-core systems. |
| 58 | isolcpus | Boot policy | Linux-specific; not a default general desktop optimization. |
| 59 | nohz_full tickless CPUs | Timer/scheduler | Requires scheduler/timer support; idle should already avoid unnecessary ticks. |
| 60 | Intel/AMD P-state drivers | CPU power | Implement only with CPU model, firmware and safe register support. |
| 61 | EPP | CPU power | Hardware/firmware gated; preserve thermal safety. |
| 62 | Turbo Boost | CPU frequency | Firmware-controlled; never force beyond safe platform policy. |
| 63 | C-state sleep policy | CPU idle | Do not disable deep idle by default; wake latency vs power trade-off. |
| 64 | scheduler migration cost | Scheduler | Tune from measured cache locality and responsiveness. |
| 65 | scheduler migration count | Scheduler | Avoid static tuning without workload data. |
| 66 | automatic NUMA balancing | NUMA | Only where multiple NUMA nodes exist. |
| 67 | numactl binding | NUMA | Specialized multi-node systems only. |
| 68 | real-time FIFO/RR scheduling | Scheduler | Restrict privileges and bound runtime to prevent starvation. |
| 69 | chrt utility | Process control | Requires secure scheduler-control permissions. |
| 70 | nice/renice | Priority | Implement foreground/background priorities with anti-starvation guarantees. |
| 71 | CPU microcode updates | Firmware | Signed/authenticated platform-supported updates; rollback/recovery plan. |
| 72 | speculative execution mitigations | Security | Default to safe mitigations; explicit, informed policy for any trade-off. |
| 73 | PGO | Compiler | Apply only after representative profiling and reproducible builds. |
| 74 | LTO | Compiler/linker | Enable where toolchain supports it; verify size, boot and debug impact. |
| 75 | SIMD/vectorization | CPU ISA | Runtime/compile-time dispatch; do not assume AVX/AVX-512 on G560. |

## 4. Network, IPC, init and hardware management (76–100)

| # | Feature | Subsystem | ZEROOS evaluation |
|---:|---|---|---|
| 76 | TCP BBR | Congestion control | Requires complete TCP congestion-control implementation and testing. |
| 77 | TCP Fast Open | TCP | Security/replay implications; enable only after protocol support and tests. |
| 78 | listen backlog sizing | Socket layer | Bound queues to RAM; prevent connection floods. |
| 79 | TCP read/write buffer autotuning | Network buffers | Adaptive per-connection caps; avoid huge fixed buffers on low RAM. |
| 80 | TCP low-latency mode | TCP | Workload-dependent; test throughput and fairness. |
| 81 | eBPF/XDP filtering | Packet path | Linux-specific; ZEROOS equivalent needs verifier/sandbox before untrusted programs. |
| 82 | RPS/RFS | Network processing | Useful only with SMP and measured receive bottlenecks. |
| 83 | TSO/GSO | NIC offload | Enable only when NIC/driver advertises support and validates descriptors. |
| 84 | GRO | Receive path | Requires protocol correctness and packet-boundary testing. |
| 85 | Unix-domain socket buffers | IPC | Bound buffers and preserve backpressure. |
| 86 | shared-memory IPC | IPC | Capability-scoped mappings, revocation and lifetime tests required. |
| 87 | memfd-style anonymous memory files | IPC | Requires secure object lifetime and transfer semantics. |
| 88 | service pruning | Init/services | Event-driven dormant services; never disable required security or device services blindly. |
| 89 | bounded journal/log storage | Logging | Ring buffers, rotation and crash-critical log preservation. |
| 90 | hardware rule optimization | Device manager | Cache only validated device matches; handle hotplug/reprobe. |
| 91 | compressed initramfs | Boot image | Measure decompression CPU vs storage reads on legacy CPU/HDD. |
| 92 | PID limit | Process manager | Explicit, tested limits and recoverable resource exhaustion. |
| 93 | file descriptor limit | VFS | Per-process/global limits with leak diagnostics. |
| 94 | PCIe ASPM | Bus power | Respect firmware/device quirks; performance mode is not always safer/faster. |
| 95 | SATA ALPM | AHCI | Capability/power-policy gated; no universal disable. |
| 96 | DMA buffer tuning | Driver framework | Validate alignment, mapping, IOMMU/capability and bounded allocation. |
| 97 | PCI latency timer | PCI | Legacy/architecture-specific; do not change without device-specific evidence. |
| 98 | watchdog policy | Reliability | Keep watchdog protection; tune only with measured false positives. |
| 99 | transparent filesystem compression | Filesystem | Depends on filesystem; measure CPU/I/O and preserve recovery semantics. |
| 100 | CachyOS/XanMod kernel techniques | Kernel build | Linux distribution kernels, not drop-in ZEROOS code; evaluate individual patches only. |

## Acceptance gates

- No feature is marked implemented solely because it appears in this catalog.
- Every enabled optimization has a capability check, safe fallback, resource ceiling, security/integrity review and benchmark.
- Test at least legacy 2 GB RAM + HDD, a modern SSD/NVMe system where supported, SMP, memory pressure, I/O contention, power/thermal throttling, and failure/recovery.
- Publish measured boot-to-interactive, idle RAM/CPU, app-switch latency, HDD queue latency, memory-pressure recovery and integrity/recovery results.
