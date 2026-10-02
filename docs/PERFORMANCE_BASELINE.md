# ZEROOS Performance Baseline

**Status: NO PRODUCTION PERFORMANCE BASELINE**
**Assessment date:** 2026-10-01
**Source SHA:** `8e02c4804b84922fb50ee075e55e2a0a18d42c07`

## Measurement policy

A valid performance result must include the physical hardware profile, kernel commit, build configuration, compiler/toolchain, workload and its configuration, repetitions, temperature/power state where relevant, result units, median and variance (and p95/p99 where useful). CI assertions, host unit loops, tool wall time, or a modeled governor tier are not performance measurements. Comparisons are meaningful only between equivalent hardware and workload configurations.

## Environment and build provenance

| Field | Observed value | Caveat |
|---|---|---|
| Source | `8e02c4804b84922fb50ee075e55e2a0a18d42c07` | Exact code/diagnostic source SHA for this evidence snapshot; later documentation-only changes do not alter the tested code |
| Compiler | GCC 12.2.0 | Local sandbox |
| Linker | GNU ld 2.40 | Local sandbox |
| Python | 3.11.2 | Storage tooling |
| Build flags | `Makefile` freestanding x86-64 flags; `-O2`, `-Werror`, `-mgeneral-regs-only`, no red zone | Actual configuration is Makefile defaults; no target-profile build variant recorded |
| Host | KVM virtualized Linux, 2 exposed logical CPUs, `MemTotal` 4,034,452 kB | Not ZEROOS or G560 hardware |
| Storage | VM-visible 21.8 GB `vda`, `ROTA=1` | Not an identified physical HDD; RPM, model and SATA mode unknown |
| GPU/display/network | Not exposed/identified | No GPU, native-resolution, decoder or link benchmark possible |
| Temperature/power | Not measured | No physical sensor or thermal envelope |

## Available test outcomes (not benchmarks)

| Test | Result | What it does not measure |
|---|---|---|
| `make check` | PASS, exit 0 on the current code/diagnostic source; two observed tool wall durations were 42.3 s and 33.1 s | Not a controlled kernel boot-time or performance benchmark; concurrent environment and workload variance are uncontrolled |
| Desktop host suite | 120,907 assertions, 0 failures | No frame-time/FPS or real desktop residency measurement |
| Compatibility host suite | 107 checks, 0 failures | No application runtime performance |
| Storage host self-test | PASS, including 3,000,000-byte fixture and fsck checks | Not HDD throughput, IOPS, seek latency, queue depth, CPU cost or power-loss behavior |
| GitHub Actions | Exact-SHA runs 36747020237, 36748022072, 36855983261, 36857163730, 36859092757 and 36860832315 PASS; runs 36858051968, 36861689464 and 36873757374 failed q35 storage, while 36860017977 failed Boot test (storage skipped); diagnostics-only follow-up run 36876632155 passed the full workflow | One follow-up PASS is not a stability claim; emulated timings are not recorded here and are not hardware performance data |
| VMM self-tests | Owned-page lifetime, page-table reclamation, shared-MMIO visibility and failed-protect permission-isolation assertions are included in QEMU boot tests | Functional checks only; no TLB latency, allocator, RSS or workload-performance measurement |

## Certified workload results

No numeric production result is currently certified. `N/A` means not measured, not zero.

| Metric | Hardware/workload | Result | Variance | Status |
|---|---|---:|---:|---|
| Cold/warm boot, kernel init, desktop ready | Physical target | N/A | N/A | Not measured |
| Idle CPU/RAM, disk/network/GPU activity, wakeups | Physical target, AI/Android/compat inactive | N/A | N/A | Not measured |
| 2 GB memory pressure, reclaim, swap/OOM behavior | Identified 2 GB target | N/A | N/A | Not measured |
| HDD sequential/random I/O, metadata, latency, IOPS, queue depth | Identified rotating disk | N/A | N/A | Not measured |
| Network throughput, latency, loss, CPU cost | Actual adapter and link | N/A | N/A | Not measured |
| Graphics frame time, frame pacing, damage/occlusion | Actual graphics path/display | N/A | N/A | Not measured |
| 720p/1080p playback, sync, dropped frames, CPU/GPU/RAM | Actual media backend | N/A | N/A | Not measured |
| Application launch and multi-app pressure | Supported application matrix | N/A | N/A | Not measured |
| Thermal/frequency/fan behavior under mixed load | Physical target | N/A | N/A | Not measured |
| Long-duration resource drift | Physical target and declared soak duration | N/A | N/A | Not measured |

## Regression policy and baseline status

There is no previous certified hardware baseline to compare against; automatic regression thresholds cannot honestly be declared. For the next certification run, preserve raw logs and repeat each workload on the same identified machine/configuration. Establish medians and spread over repeated runs before setting thresholds. Keep build-time, host test duration and QEMU functional assertions separate from target performance. Until a physical baseline and comparison exist, performance regression certification is **BLOCKED**.
