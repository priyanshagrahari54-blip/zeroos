# Exhaustive Low-Level Tuning & Network Acceleration Manual — 120 Features

**Source PDF:** `120 Low-Level System, Network & Browser Optimization Catalog.pdf`  
**Relationship:** This is the separate 120-item catalog. Items 1–100 overlap conceptually with the 100-feature catalog; items 101–120 focus on browser/runtime behavior. This is an engineering evaluation document, not an implementation-status claim.

## Hardware boundary: Lenovo G560

The supplied source describes G560 variants with 10/100 Ethernet and 802.11n Wi-Fi around 150 Mbps maximum (exact NIC/card varies by SKU). Software cannot exceed the physical NIC, Wi-Fi radio, router, ISP or remote-server limits. ZEROOS should detect the actual device and negotiate supported link modes; never promise a 100 Mbps/150 Mbps/1 Gbps rate independent of hardware.

## 1. Storage acceleration, I/O caching and disk lifespan (1–25)

| # | Feature | ZEROOS implementation guidance |
|---:|---|---|
| 1 | bcache Dynamic Tiering | Optional hot-block cache when supported cache/backing devices exist. |
| 2 | EnhanceIO Cache Engine | Legacy cache candidate; audit provenance, maintenance and write semantics. |
| 3 | dm-cache Target | Linux device-mapper concept; adapt only if equivalent ZEROOS layer exists. |
| 4 | bcachefs COW Filesystem | Separate filesystem candidate, not a drop-in replacement for ZJFS. |
| 5 | tmpfs RAM Storage | Bounded, reclaimable temporary storage only. |
| 6 | ramfs Fixed Buffer | Avoid unbounded unswappable allocations on low-RAM profiles. |
| 7 | io_uring Ring Queues | Linux-specific API; evaluate async-I/O concept against ZEROOS ABI. |
| 8 | BFQ Budget Fair Queue | Candidate rotational-HDD policy with foreground fairness. |
| 9 | Kyber Low-Latency Scheduler | Primarily fast block devices; detect hardware/workload. |
| 10 | mq-deadline | Bounded latency and starvation control for supported queues. |
| 11 | vm.dirty_background_ratio | Adapt writeback thresholds to RAM and storage latency. |
| 12 | vm.dirty_ratio | Bound dirty pages to prevent UI stalls and excessive data-loss windows. |
| 13 | vm.dirty_expire_centisecs | Age-based writeback; coordinate with crash consistency. |
| 14 | noatime | Optional timestamp-write reduction if semantics permit. |
| 15 | nodiratime | Optional where directory access timestamps exist. |
| 16 | ext4 commit=60 | ext4-only; do not copy to ZJFS without equivalent journal semantics. |
| 17 | ext4 data=writeback | Integrity trade-off; not a default. |
| 18 | 4096 KB read-ahead | Use bounded adaptive read-ahead; 4 MiB may be too large for 2 GB RAM/random reads. |
| 19 | EROFS | Consider for immutable compressed OS/app images. |
| 20 | SquashFS + ZSTD | Read-only image candidate; benchmark decompression cost. |
| 21 | fallocate | Preallocate when supported; cannot universally guarantee zero fragmentation. |
| 22 | TRIM automation | SSD/discard-capable devices only. |
| 23 | hdparm spindown | Drive-specific; avoid repeated spin-up/down wear and latency. |
| 24 | SATA NCQ depth | Tune by device capability and measured seek/queue latency. |
| 25 | NVMe queue overrides | NVMe only; not applicable to SATA HDD systems. |

## 2. RAM compression, virtual memory and cache pressure (26–50)

| # | Feature | ZEROOS implementation guidance |
|---:|---|---|
| 26 | zram compressed swap | Compressed in-RAM paging pool; effective capacity depends on data compressibility and CPU. |
| 27 | zswap write-back cache | Linux-specific; concept requires compatible swap backend. |
| 28 | ZSTD | Higher compression; benchmark CPU cost on legacy processors. |
| 29 | LZ4 | Fast compression/decompression candidate for cold pages. |
| 30 | LZO-RLE | Compare against LZ4 on representative sparse pages. |
| 31 | vm.swappiness 10–20 | Linux sysctl; implement equivalent policy only if measured beneficial. |
| 32 | vm.vfs_cache_pressure=50 | Linux sysctl; use adaptive metadata cache reclaim instead. |
| 33 | watermark_boost_factor | Early reclaim, bounded to avoid needless background work. |
| 34 | min_free_kbytes | Preserve allocation reserves for interrupt/DMA paths. |
| 35 | KSM | Optional deduplication; scan overhead and side-channel/security costs. |
| 36 | page-cluster=0 | Linux swap behavior; tune granularity by measurements. |
| 37 | Transparent Huge Pages | Avoid default use on small-RAM systems; check hardware and workload. |
| 38 | MADV_HUGEPAGE mode | Opt-in huge pages for suitable mappings. |
| 39 | PSI | Event-driven resource-stall metrics and pressure response. |
| 40 | earlyoom | Prefer freeze/reclaim before termination; isolate critical services. |
| 41 | systemd-oomd | Linux-specific manager; equivalent native resource supervisor if available. |
| 42 | mlockall | Strict capability and memory quota for pinned pages. |
| 43 | HugeTLBFS | Specialized workload, not desktop default. |
| 44 | CMA | Contiguous DMA allocation where hardware requires it. |
| 45 | KPTI | Security-first decision with measured performance. |
| 46 | Proactive compaction | Trigger on pressure/request, not continuous scanning. |
| 47 | NUMA zone reclaim | Only where actual NUMA topology exists. |
| 48 | SLUB merging | Linux allocator detail; analogous optimization only if applicable. |
| 49 | vm.overcommit_memory | Explicit commit accounting and deterministic failure behavior. |
| 50 | vm.overcommit_ratio | Bound commitments only with tested allocation accounting. |

## 3. CPU scheduling, core isolation and cache locality (51–75)

| # | Feature | ZEROOS implementation guidance |
|---:|---|---|
| 51 | EEVDF scheduler | Evaluate scheduler design; do not claim upstream Linux scheduler is present. |
| 52 | taskset affinity | Optional affinity where profiling proves benefit. |
| 53 | cgroups v2 resource bands | ZEROOS-native quotas/groups needed; Linux API is not directly available. |
| 54 | cpufreq performance mode | Hardware-aware and thermal-safe; not a permanent max-frequency default. |
| 55 | sched_setaffinity | Requires supported API and topology detection. |
| 56 | irqbalance | SMP-only benefit; avoid overhead on single-core systems. |
| 57 | dedicated IRQ core | Pin only after measuring contention and latency. |
| 58 | isolcpus | Linux boot option; not a general desktop default. |
| 59 | nohz_full | Requires tickless timer/scheduler architecture. |
| 60 | Intel/AMD P-state | CPU-model and firmware-specific driver work. |
| 61 | EPP | Safe capability-gated hardware hint. |
| 62 | Turbo Boost | Firmware/thermal limits remain authoritative. |
| 63 | C-state prevention | Do not disable deep idle by default; balance wake latency and power. |
| 64 | sched_migration_cost_ns | Tune only from measured cache locality. |
| 65 | sched_nr_migrate | Avoid static values without workload evidence. |
| 66 | NUMA balancing | Only for multi-node systems. |
| 67 | numactl local binding | Specialized NUMA systems only. |
| 68 | SCHED_FIFO | Privileged, bounded execution to prevent starvation. |
| 69 | chrt | Requires scheduler-control permission checks. |
| 70 | nice/renice | Foreground priority with fairness/anti-starvation. |
| 71 | CPU microcode | Authenticated platform-supported update and recovery path. |
| 72 | speculative execution tuning | Safe mitigations by default; no silent security weakening. |
| 73 | PGO | Representative workload profiles and reproducible builds. |
| 74 | LTO | Toolchain-gated; measure binary size, performance and debugability. |
| 75 | AVX/SSE vectorization | Runtime ISA detection; G560 CPU variants may not support newer vectors. |

## 4. Kernel networking, TCP/IP and IPC (76–100)

| # | Feature | ZEROOS implementation guidance |
|---:|---|---|
| 76 | TCP BBR | Requires complete TCP congestion-control and test coverage. |
| 77 | TCP Fast Open | Replay/security review before enablement. |
| 78 | somaxconn | Bound accept backlog to memory and abuse limits. |
| 79 | TCP read/write buffers | Autotune within per-socket and global memory budgets. |
| 80 | TCP low latency | Workload-specific; benchmark throughput and fairness. |
| 81 | eBPF/XDP | Linux-specific; equivalent programmable filters require a verifier/sandbox. |
| 82 | RPS/RFS | SMP and measured receive bottleneck only. |
| 83 | TSO/GSO | NIC-offload capability checks and descriptor validation. |
| 84 | GRO | Requires correct packet coalescing and protocol regression tests. |
| 85 | Unix socket buffers | Bound queues and preserve backpressure. |
| 86 | shared-memory IPC | Capability-limited maps, lifetime/revocation and fault tests. |
| 87 | memfd_create | Linux API; adapt anonymous shared object semantics if needed. |
| 88 | systemd service pruning | Use ZEROOS service lifecycle; do not disable security/required services blindly. |
| 89 | journald size capping | Bounded logs, rotation, preserve crash-critical events. |
| 90 | hardware rule optimization | Cache validated device matches; handle hotplug and reprobe. |
| 91 | compressed initramfs | Measure decompression vs HDD reads on target CPU. |
| 92 | kernel.pid_max | Native process/thread limits with clear exhaustion errors. |
| 93 | fs.file-max | Per-process/global handle quotas and leak observability. |
| 94 | PCIe ASPM | Device/firmware quirk table; do not blanket-disable power saving. |
| 95 | SATA ALPM | Hardware-aware power/latency policy. |
| 96 | DMA buffer tuning | Alignment, mapping, bounds, ownership and cache-coherency validation. |
| 97 | PCI latency timer | Legacy-specific; no change without device evidence. |
| 98 | NMI watchdog disable | Do not disable reliability instrumentation just to chase cycles. |
| 99 | Btrfs compression | Filesystem-specific; evaluate ZJFS separately. |
| 100 | CachyOS/XanMod kernel | Linux kernel distributions, not drop-in ZEROOS kernels; evaluate individual techniques. |

## 5. Low-RAM browser optimization (101–120)

| # | Feature | ZEROOS implementation guidance |
|---:|---|---|
| 101 | Parallel downloads | Use bounded concurrent range requests only when server supports them; respect server policy. |
| 102 | GPU rasterization | Enable only after GPU/driver capability and correctness checks. |
| 103 | Zero-copy raster writer | Requires compatible compositor/GPU buffer-sharing path; never assume zero-copy. |
| 104 | Tab discarding / Memory Saver | Implement lifecycle ACTIVE → IDLE → FROZEN → DISCARDED with state restoration. |
| 105 | uBlock Origin Lite / content filtering | Prefer user-controlled, reputable filtering; don't promise a fixed RAM saving. |
| 106 | JavaScript JIT tuning | Engine-specific; prioritize memory caps, warmup and correctness. |
| 107 | HTTP/3 QUIC | Negotiate when peer/network supports it; fallback to HTTP/2 or HTTP/1.1. |
| 108 | Disable DoH | Do not disable encrypted DNS blindly; configurable policy with trusted resolver/cache. |
| 109 | Disable WebRTC/telemetry | Privacy and feature policy; no unnecessary background connections. |
| 110 | Process-per-site | Balance isolation/security against process memory; site grouping has trade-offs. |
| 111 | Browser cache in tmpfs | Avoid by default on 2 GB systems; consumes RAM and loses cache on reboot. Prefer bounded disk cache plus hot in-memory cache. |
| 112 | Back-forward cache | Keep under a strict memory budget; evict on pressure. |
| 113 | Preload key resources | Conservative, same-origin/policy-aware; avoid speculative network and battery waste. |
| 114 | JS heap cap | Adaptive per-tab/process memory budget, not a universal fixed value. |
| 115 | Disable background apps | Stop background work when window closes unless user-authorized background task remains. |
| 116 | Smooth scrolling off | Respect reduced-motion/accessibility settings and hardware budget. |
| 117 | Hardware video decoding | Detect actual codec/profile/bit-depth support; prefer H.264 where compatible. Do not promise AV1/VP9 acceleration unless confirmed. |
| 118 | QUIC experiment | Prefer negotiated stable protocol; do not force experimental flags. |
| 119 | Lazy image/iframe loading | Defer offscreen work while preserving web semantics and accessibility. |
| 120 | Auto tab suspend | Lifecycle-driven freeze/discard with session restore; use time and pressure signals rather than a rigid five-minute rule. |

## Source setup summary — interpreted safely

The source proposes RAM-backed browser cache, BBR, larger TCP buffers and compressed swap. ZEROOS must not copy these as unconditional defaults: RAM cache can worsen pressure on 2 GB systems; large network buffers can amplify memory use; BBR requires a complete TCP implementation; compression ratios are workload-dependent. Apply only after feature detection, resource caps and target-hardware benchmarks.

## Required validation

- Test actual Lenovo G560 SKU, RAM, GPU/decoder, NIC, Wi-Fi card, SATA drive and firmware.
- Network throughput is bounded by physical link and remote path; report measured throughput, not theoretical headline rates.
- Browser acceptance: cold/warm launch, 5/10/20 tabs, memory-pressure freeze/discard and restore, HTTPS/DNS privacy, HTTP protocol fallback, video codec/decoder path, dropped frames and A/V sync.
- System acceptance: boot-to-interactive, idle CPU/RAM, HDD I/O latency, swap/compression CPU cost, foreground responsiveness, thermal stability, security and filesystem recovery.
- Catalog presence is not implementation evidence. Track each item as `Not assessed / Planned / In progress / Implemented / Tested / Hardware-verified`.
