# ZEROOS Stage 10 Production Certification Report

**Status: BLOCKED**
**Assessment date:** 2026-10-01
**Source SHA:** `73275f51df3922d94d5073497942e3f2e6756113`
**Recommendation:** DO NOT RELEASE as production-ready.
**Scope:** source/host tests, existing exact-SHA CI evidence, and available build environment. No physical target hardware was present. The source includes the scancode fix and later VMM changes; see the exact-SHA run records below for the commits each run validates.

## Executive determination

Stage 10 is not complete. On-demand host tests and the exact-SHA QEMU CI workflow pass meaningful subsets of the system, but critical release evidence is missing for physical hardware, low-memory operation, real HDD/network/GPU/media workloads, production security enforcement and signed update lifecycle, measured performance, thermal behavior, and long-duration operation. The current evidence is not sufficient to call the operating system stable, secure, recoverable, performant, or production-ready across its intended matrix.

This report separates:

- **Implemented:** source/interface exists.
- **Tested:** stated test executes and passes in stated environment.
- **Hardware tested:** identified physical hardware was used.
- **Supported:** explicit combination passed the applicable matrix.
- **Production ready:** all release gates for the stated support scope passed.

The only Stage 10 hardware profile recorded is the sandbox environment profile in `HARDWARE.md`, which is not product hardware. The compatibility decision is in `COMPATIBILITY_MATRIX.md`; measured results policy and N/A figures are in `PERFORMANCE_BASELINE.md`.

## Reproduction record

| Evidence | Result |
|---|---|
| `make check` | PASS, exit 0, 2026-10-01, on the current working tree. GCC 12.2.0; GNU ld 2.40; Python 3.11.2. Kernel ELF/SIMD, userspace ABI/runtime, core tests, desktop, compatibility, and storage host self-test all ran |
| Desktop host tests | 120,907 assertions, 0 failures; includes bounded host stress/soak suites, not a long-duration OS soak |
| Compatibility host tests | 107 checks, 0 failures; compatibility-core tests, not Windows application execution |
| Sanitizer follow-up | Initial ASan/UBSan run exposed the E0-prefixed PS/2 table overflow (index 200 into 128 entries). Expanded to 256 slots. The broad desktop/compat/hardware-core sanitizer suites passed after the fix; the focused scancode test was then extended to exhaust all E0-following bytes and 250,000 deterministic mixed-stream bytes and passed under ASan/UBSan. The extended test is included in source commit `a184b4ef00ee52022bd47624d5f441f14499d113` and CI run 36651886021. |
| Storage host tools | GPT/ZJFS fixture create, checksum and fsck/recovery scenarios PASS; not a physical HDD or actual power-loss test |
| Exact-source CI | [Workflow run 36651886021](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36651886021), SUCCESS at source commit `a184b4ef00ee52022bd47624d5f441f14499d113`. Build, `make check`, kernel verification, repeated 2-vCPU QEMU, 4-vCPU QEMU, NX-disabled QEMU, AHCI/NVMe persistence, and the exhaustive/randomized decoder test all passed. |
| Earlier docs CI | [Workflow run 36611709655](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36611709655), SUCCESS on documentation-only commit `fd118ae1e2a141fe25e9df54bc949ac34d06ac3c`; superseded by the exact-source run above. |
| Local ISO packaging | BLOCKED, `grub-mkrescue` missing; no local ISO/QEMU boot |
| New CI dispatch | Attempted for session branch; HTTP 403 `Resource not accessible by integration`, so no new run |
| Physical target | Not available; no Lenovo G560, 2 GB machine or actual network/display/media adapter tested |

## Sanitizer-discovered defect and local fix

A supplementary host run used `make -j3 CC='gcc -fsanitize=address,undefined -fno-omit-frame-pointer' desktop-check compat-check hardware-core-test`. The first run stopped in `tests/scancode_core_test.c`: the E0-prefixed PS/2 key index is `0x80 | scancode`, but `SCANCODER_TABLE_SIZE` was only 128. An extended key (index 200) therefore caused out-of-bounds reads/writes in `kernel/scancode_core.c` and an AddressSanitizer stack-buffer-overflow. Existing functional tests exercised this path but did not detect the memory error.

The fix changes the decoder table to 256 entries (the full base + E0-prefixed index space) and asserts the size in the test. The test now also exercises all 256 possible bytes after E0 and a deterministic 250,000-byte mixed stream. ASan/UBSan passed on that expanded scancode stress test; the broader desktop, compatibility and hardware-core suites passed under ASan/UBSan before that final test-only extension, and `make check` passed after it. The memory-safety fix is committed in `91e30eb7eae80b832f8daecdd1dee8e43424a2f9`; the expanded stream test is in `a184b4ef00ee52022bd47624d5f441f14499d113` and passed in CI run 36651886021. The sandbox cannot install QEMU/GRUB packages because Debian mirror connections failed; guest boot was validated by CI, not locally.

## GCC static-analysis follow-up

`make BUILD=build-analyze EXTRA_CFLAGS=-fanalyzer elf` completed successfully with GCC's `-fanalyzer`, compiling and linking kernel and hardware-core sources under existing `-Werror` settings; the linked-image SIMD safety check also passed. Direct `-fanalyzer -fsyntax-only` checks passed for all desktop sources, compatibility sources, and `kernel/scancode_core.c`.

A broader hosted build that also analyzes `userspace/desktop/tests/test_input.c` stops with GCC `-Wanalyzer-use-of-uninitialized-value` warnings on returned `struct zd_input_delivery` objects from separately compiled APIs. Inspection shows `userspace/desktop/src/input.c` constructs each return through `delivery()`, which assigns `result`, `window`, `local_x`, and `local_y`; GCC reports the call result at the test translation-unit boundary. This is recorded as an analyzer/test-TU warning, not silently called clean. With only that warning downgraded, the full desktop, compatibility, and hardware-core suites execute and pass. The static-analysis signal does not replace further review or runtime testing.

## Certification matrix

| Subsystem | Implemented / tested evidence | Hardware-tested | Assessment |
|---|---|---:|---|
| Kernel / syscall / user boundary | Kernel ELF and boot milestones; CI checks negative syscall/fault/malformed-ELF probes | No | Tested in host/emulation; target security review open |
| Scheduler / interrupts / timers | CI asserts scheduler fairness/latency, timer preemption, process lifecycle and wake/wait paths; run 36860017977 failed the 2-vCPU Boot test, with exact missing marker unknown | No | Intermittent emulated boot failure remains open; longer physical saturation soak also open |
| SMP / TLB | Passing QEMU 2-vCPU and 4-vCPU milestones, per-CPU ownership and remote TLB self-tests; run 36860017977 failed Boot test before storage | No | Emulated evidence includes both passes and an unresolved boot failure; no hardware SMP certification |
| Physical/virtual memory | Host/build and boot allocator self-tests; QEMU VMM owned-frame/refcount, private table-reclaim and shared-MMIO tests (runs 36747020237 / 36748022072 / 36855983261 / 36857163730 / 36858051968 / 36859092757) | No | QEMU-only VMM coverage; no 2 GB pressure, COW, swap, OOM, sustained-memory, or physical SMP lifetime certification |
| Storage / VFS / ZJFS | Host GPT/ZJFS image recovery and CI guest storage/persistence milestones | No | Partial; physical HDD latency/queue/load and safe real power-loss testing absent |
| Drivers / PCI / DMA | Host driver/DMA core suites; CI emulated AHCI/NVMe | No | Adapter-specific hardware matrix absent; IOMMU containment/BAR risk remains per architecture |
| Network | Host protocol/parser/socket core suites | No | No physical NIC, link, packet loss, DNS/DHCP/reconnect or throughput test |
| Graphics / compositor / display | Host desktop/compositor tests and QEMU display/session milestones | No | No GPU backend (including Vulkan) or physical panel/fallback certification |
| Media | Host media policy tests | No | Decoder path and video/audio playback evidence absent |
| Desktop/native applications | Host core suites; session shell path booted under CI | No | Partial shell/probe implementation; no production app compatibility catalogue |
| Windows compatibility | PE/core validation tests (107) | No | Partial parser/policy; no tested Windows process/application support |
| Android compatibility | Baseline documented | No | Unsupported; runtime and APK execution absent |
| Gaming | Policy/profile unit tests | No | No game/runtime/hardware combination measured; unsupported as game compatibility |
| ZERO AI | Host permission/dormancy/failure-policy suite | No | Partial optional broker; no backend linked; no production action lifecycle |
| Security | Crypto vectors and host capability/sandbox/firewall/policy tests | No | Blocked: kernel enforcement/PKI gaps documented; signed package/update and adversarial system-boundary audit not proven |
| Updates / rollback | Host update/snapshot/state-machine suites | No | Partial: no signed installed-system download-to-commit lifecycle, interruption/rollback certification |
| Recovery | Host fsck/recovery and CI guest init-recovery assertions | No | Partial: no target recovery boot/repair or failed-update physical recovery run |
| Thermal / power | Governor policy unit tests only | No | Not tested: no temperature/fan/frequency/throttle/battery/suspend data |
| Long-duration stability | Bounded deterministic host churn tests | No | Not tested: no declared-duration OS-level idle, mixed-workload or media/network/storage soak |

## Stage 1 VMM teardown and shared-MMIO delta (2026-10-01)

The implementation in `4f6df30457b51dc9dbb6679b1afd4ab6931fe21b` orders
active-CPU invalidation before releasing mapped frame references in both
kernel-root and address-space unmaps, reclaims empty private page-table paths,
and preserves shared MMIO page-table pointers. Boot tests assert allocator and
frame-reference accounting plus visibility of a newly-added MMIO subtree in
an address space created earlier. Local `make check` passed. Exact-code CI run
[36747020237](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36747020237)
passed; the workflow was strengthened to require the VMM boot-test markers,
and [run 36748022072](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36748022072)
on `eff20fc7f6345a4541d04497314119f781f102aa` passed those gates, QEMU SMP,
and storage persistence. This is IMPLEMENTED and TESTED in host build/QEMU;
not HARDWARE TESTED, not a production support claim, and not PRODUCTION READY.
Unconditional shootdowns and unlink-before-free ordering reduce the CR3-transition/recycled-table risk, but concurrent map/unmap serialization and physical cross-CPU lifetime stress remain open. 2 GB/HDD behavior and target-hardware evidence remain open.

### VMM permission-isolation follow-up (2026-10-01)

`vmm_protect_page()` now validates the entire leaf path before promoting any
ancestor U/S permissions, and global-root user changes are restricted to the
dedicated user PML4 slot. A boot regression checks failed absent-leaf protect
leaves ancestor permission bits unchanged and rejects attempts to mark a
shared direct-map page as user-accessible. Local `make check` passed; exact-SHA
CI run [36855983261](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36855983261)
on `1070b3ab950092edd9aab93be789f9e4a842c7fd` passed the explicit serial gate, QEMU boot/SMP and storage tests.
This is QEMU-tested only; no physical hardware or production certification is
implied. Stage 10 remains **BLOCKED**.

### VMM table unlink and frame-retirement ordering (2026-10-01)

Page-table parents are cleared before table frames are returned to the
allocator; each unlink is followed by synchronous address-specific invalidation
on every CPU before freeing the frame. Root and address-space unmap broadcast
invalidation before releasing an owned data-frame reference, rather than
trusting a possibly stale active-root snapshot during CR3 transition. Local
`make check` passed. Run [36857163730](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36857163730)
on `206ba31a73fa33836c378a198912e3bc84347836` passed with the earlier full
flush. Run [36858051968](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36858051968)
then failed the q35 storage gate before storage discovery (no panic marker).
Commit `73275f51df3922d94d5073497942e3f2e6756113` changed the hierarchy-unlink
flush to targeted invalidation; exact-SHA run
[36859092757](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36859092757)
passed boot/SMP and both AHCI/NVMe persistence boots. Both failures remain unresolved reliability signals: run 36858051968 failed q35 storage persistence, and run 36860017977 failed Boot test before storage (the log tail stopped during scheduler timer output without a panic marker; the missed assertion is unknown). The workflow now reports recent non-timer milestones on boot failure. Repeated stress is still needed. This is QEMU evidence only.
Concurrent page-table writers are not yet serialized/stress-tested, and no
physical-hardware or production-readiness claim follows.

## Workload and test gaps

The following required areas have no qualifying target evidence: cold/warm physical boot and desktop-ready timing; 2 GB boot/idle/multiple apps/browser/media/cache/reclaim/OOM/isolation; sequential/random HDD I/O, metadata, concurrent I/O, queue depth/latency/CPU/RAM cache; actual Ethernet/Wi-Fi setup/recovery/throughput/loss; GPU fallback and actual compositor frame pacing/occlusion; 720p/1080p playback and AV sync under pressure; thermal and battery behavior; installed-system recovery boot; signed update failure/interruption/rollback; performance regression comparison; sustained operational soak. `VALIDATION.md` contains the precise gate disposition.

## Failure and recovery matrix

This is an evidence classification, not a claim that every failure mode has been injected on target hardware.

| Subsystem | NORMAL | FAILURE considered in current evidence | Detection | Recovery evidenced | User impact / open risk |
|---|---|---|---|---|---|
| Kernel / scheduler | CI boot and scheduler assertions | QEMU panic and negative/fault probes are gated | Serial milestones/panic grep in workflow | Reboot/recovery behavior is asserted in selected tests | No physical/extended soak; unobserved deadlock/race remains possible |
| SMP | QEMU 2/4 vCPU | CI checks startup and TLB/scheduler assertions | Serial milestones | startup/recovery contract self-tests | Hardware topology/firmware differences untested |
| Memory | Boot allocator checks; host resource policies | Host limits/fault suites | Test counters/return codes | Host lifecycle/reclaim policy paths | Real 2 GB reclaim/OOM and data-integrity effects unknown |
| Filesystem/storage | Host ZJFS fixture and emulated guest persistence | Corrupt GPT/superblock and unclean image scenarios | fsck/checksum/error reporting | Host image repair/replay; CI persistence across boots | Real device errors, HDD starvation and power loss untested |
| Network | Host stack/parser tests | Malformed packet/error input in cores | Unit assertions | No physical reconnect recovery result | Actual connectivity/recovery unsupported by evidence |
| Graphics | Host compositor/display contracts; CI session | Host fault/degraded display paths | Test result and guest milestone | Degraded path is covered at core/session contract level | Physical GPU failure/fallback/display recovery unknown |
| Security | Host crypto/policy operations | Tamper/wrong-key and permission negatives in cores | Test assertions/counters | Fail-closed behavior in those host components | Kernel-wide enforcement, keys/PKI, privilege-escalation audit open |
| Update/recovery | Host update/snapshot state machine | Host download/verify/health-failure paths | State/result code | Host rollback contract | No signed installed update/recovery-boot proof; release blocked |
| Compatibility | Core parser/policy suites | Malformed PE/policy cases | Host assertion results | No application-level recovery evidence | No Windows/Android app support claim |
| ZERO AI | Dormant/permission policy cores | Host permission, queue, failure cases | Broker state/test assertions | Return to dormant tested at core level | No runtime backend/action integration certification |
| Thermal/power/soak | No target normal run | None at physical target | Sensors unavailable | None evidenced | Risk unknown; release blocked |

## Known limitations / blocking issues

1. No certified reference hardware profile or physically tested support matrix.
2. No 2 GB-class resource-pressure certification.
3. No real HDD or power-loss certification; CI storage evidence is emulated.
4. No real network adapter, graphics device/display, or media decoder throughput/latency measurement.
5. Security policy/crypto tests do not establish kernel-wide policy enforcement, secure boot, package signing trust, or production update-key lifecycle. Architecture notes existing kernel enforcement and PKI gaps.
6. Windows code is partial compatibility-core/PE work; no Windows application is demonstrated. Android runtime is absent. No game compatibility or production media playback is certified.
7. ZERO AI has tested broker policy components but no backend linked; it must remain optional and dormant.
8. No thermal, battery, long-duration system soak, or published target performance baseline/regression comparison.
9. Local ISO packaging could not be completed in the sandbox because GRUB/QEMU packages could not be installed from unreachable Debian mirrors. GitHub workflow dispatch was denied by API permissions, but pushing the session branch triggered the passing exact-source run 36651886021 and later VMM-gated runs 36747020237, 36748022072 and 36855983261.

These are release blockers due to missing evidence and incomplete production scope, not claims that a specific defect was reproduced. No user impact may be represented as safe based on absence of a test.

## Final status block

- **STAGE 10 STATUS:** BLOCKED
- **CERTIFIED HARDWARE:** None. Sandbox KVM environment is build/test-only; see `HARDWARE.md`.
- **CERTIFIED FEATURES:** Host unit-test suites and exact-SHA emulated CI subsets only; see evidence table. No physical production feature set certified.
- **PARTIAL FEATURES:** Native session/probe path, Windows PE/compatibility core, browser lifecycle/policy, ZERO AI broker policy, storage recovery paths, security/update policy cores.
- **UNSUPPORTED FEATURES:** Android runtime/APK execution; production Windows app execution; general game compatibility; production media playback; universal GPU/Vulkan support.
- **CRITICAL ISSUES:** Required hardware, security, recovery, performance, media/network, memory, thermal, and soak evidence gates remain open.
- **PERFORMANCE RESULTS:** No production benchmark results; host `make check` is functional verification, not a benchmark.
- **THERMAL RESULTS:** Not measured.
- **SECURITY RESULTS:** Host crypto/policy checks pass; system-level enforcement and signing certification not established.
- **RECOVERY RESULTS:** Host filesystem recovery and emulated guest recovery/persistence subsets pass; physical recovery/update rollback not certified.
- **KNOWN LIMITATIONS:** See this report and `COMPATIBILITY_MATRIX.md`.
- **RELEASE RECOMMENDATION:** DO NOT RELEASE as production-ready.
- **COMMIT SHA (tested code):** `73275f51df3922d94d5073497942e3f2e6756113`
