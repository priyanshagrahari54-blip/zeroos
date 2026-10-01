# ZEROOS Stage 10 Release Certification

- **Certification status:** BLOCKED
- **Assessment date:** 2026-10-01
- **Assessed source:** `206ba31a73fa33836c378a198912e3bc84347836`
- **Recommendation:** DO NOT RELEASE as a production-ready operating system. A development/evaluation build may be circulated only with its limitations clearly stated.

This report distinguishes implementation from evidence and from support. A host test is not a target-machine test; QEMU is not bare metal; detection is not operation; and passing a subset does not close unrelated release gates.

## Evidence labels

- **IMPLEMENTED** — code or an interface exists in this checkout.
- **TESTED** — the stated test ran successfully in its stated environment.
- **HARDWARE TESTED** — exercised on identified physical hardware with a reproducible profile.
- **SUPPORTED** — a documented hardware/software combination passes the required functional and failure tests.
- **PRODUCTION READY** — all applicable release, recovery, security, performance, and soak gates pass.

These labels are independent. No hardware combination has Stage 10 hardware certification in this assessment.

The code fix discovered by ASan/UBSan landed in `91e30eb7eae80b832f8daecdd1dee8e43424a2f9`; the decoder stress-test follow-up is in `a184b4ef00ee52022bd47624d5f441f14499d113`. GitHub Actions run 36651886021 passed on that exact source/test commit. The remaining certification blockers concern physical hardware and production-scope evidence.

## Reproducible evidence recorded

1. `make check` on this checkout: PASS, exit 0 (2026-10-01). Host GCC 12.2.0, GNU ld 2.40, Python 3.11.2. The target included kernel ELF compilation and SIMD-instruction check, ABI/runtime consistency, hosted core/desktop/compatibility tests and storage GPT/ZJFS image recovery self-test. Desktop: 120,907 assertions, zero failures; compatibility core: 107 checks, zero failures. See `VALIDATION.md` for the command/output scope.
2. GitHub Actions [run 36651886021](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36651886021): SUCCESS on source/test commit `a184b4ef00ee52022bd47624d5f441f14499d113`, including the scancode bounds fix and malformed-stream regression. Reported successful steps: kernel/ISO build, `make check`, ELF verification, QEMU boot tests (repeated 2-vCPU, 4-vCPU, NX-disabled), and AHCI/NVMe persistence certification. Emulated CI only.
3. Earlier documentation run [36611709655](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36611709655) also passed. For the current VMM changes, see exact-code run 36747020237 and the VMM-marker-gated follow-up 36748022072 in item 8.
4. GCC `-fanalyzer`: kernel/hardware-core ELF build and SIMD gate passed; source-only desktop/compatibility/scancode checks passed. Full desktop analyzer test compilation emits cross-translation-unit aggregate-return warnings in test call sites; with that warning downgraded, host suites pass. See `STAGE_10_REPORT.md`; this does not establish absence of defects.
5. Local `make`: BLOCKED at ISO packaging with exit 2 because `grub-mkrescue` is missing. This environment also has no `qemu-system-x86_64` or `xorriso`. An attempt to install CI-equivalent packages failed because Debian mirror connections were unavailable; local boot was instead covered by CI. The kernel ELF had been built by `make check`; local guest boot not run.
6. A workflow dispatch on `arena/01a0ee2b-zeroos` initially returned HTTP 403 (`Resource not accessible by integration`); no run was created by that request. Pushing the session branch subsequently triggered the passing runs above.
7. No physical G560/reference machine, physical sensors, or production workload equipment was available. See `HARDWARE.md` and `PERFORMANCE_BASELINE.md`.
8. VMM teardown and shared-MMIO lifetime changes were implemented in `4f6df30457b51dc9dbb6679b1afd4ab6931fe21b`; local `make check` passed. CI run [36747020237](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36747020237) passed on that exact code commit, including QEMU boot/SMP and q35 AHCI/NVMe two-boot storage. The workflow was then strengthened to require VMM boot-self-test markers; run [36748022072](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36748022072) passed on `eff20fc7f6345a4541d04497314119f781f102aa`. A follow-up fix for failed-protect permission promotion and its negative boot test landed in `1070b3ab950092edd9aab93be789f9e4a842c7fd`; exact-SHA CI run [36855983261](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36855983261) passed. The latest change unlinks empty table pages before freeing them, flushes page-walk caches, and broadcasts invalidations before frame release; exact-SHA run [36857163730](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36857163730) passed on `206ba31a73fa33836c378a198912e3bc84347836`. Both runs include build, host gates, QEMU boot/SMP and storage. These are QEMU/host results, not physical hardware or production certification.

## Release gates

| Gate | Current evidence | Status |
|---|---|---|
| Reproducible release build | ELF build and exact-SHA CI build pass; no independent bit-for-bit rebuild comparison in this assessment | Partial |
| CI | Exact-code VMM runs 36747020237, 36748022072 and negative-permission runs 36855983261 and 36857163730 PASS; dispatch API restricted but branch push trigger worked | Tested (CI) |
| Kernel / scheduler / SMP | Exact-code VMM run 36747020237 and gated follow-up 36748022072 pass QEMU boot/SMP at 2/4 vCPU; no physical SMP certification | Tested (emulation) |
| VMM unmap/TLB/frame lifetime, page-table reclamation and protect permissions | Boot tests assert owned-frame refcounts, private table reclamation, shared MMIO propagation, failed-protect isolation and table unlink-before-free; CI requires the markers | Tested (QEMU); physical SMP lifetime stress and production readiness open |
| Memory pressure / 2 GB behavior | No measured 2 GB machine run, reclaim/OOM/desktop/browser scenario | Open |
| Filesystem recovery | Host image fsck/recovery plus QEMU persistence evidence | Partial; no real power-loss/HDD validation |
| HDD stress | No identified physical HDD, queue/latency/throughput/temperature measurements | Open |
| Networking | Protocol-core unit tests only; no actual NIC, link, packet loss, reconnect or throughput test | Open |
| Graphics / display | Host desktop tests and QEMU session/display milestones; no identified GPU/panel or physical fallback certification | Open |
| Media | Policy/core tests; no decoder backend and no measured 720p/1080p playback soak established by this evidence | Open |
| Security | Host crypto/policy tests; documented kernel enforcement/PKI gaps; no adversarial target audit | Open (release blocker) |
| Update / rollback | Host update state-machine tests; no signed installed-system lifecycle, interruption and rollback test evidence | Open (release blocker) |
| Recovery | ZJFS host recovery and guest init recovery milestone; no recovery-boot/repair test on target hardware | Partial |
| Thermal / power | No physical sensors, fan/frequency/throttling/battery measurements | Open |
| Long-duration soak | Unit-level bounded churn tests only; no qualifying operational soak | Open |
| ZERO AI | Broker policy/dormancy host tests; no linked backend and no full runtime action lifecycle certification | Partial |
| Compatibility | See `COMPATIBILITY_MATRIX.md`; no Windows/Android application runtime support claim | Open |
| Hardware matrix | Sandbox profile only; reference physical profile absent | Open |
| Performance baseline | No certified workload measurements or previous baseline comparison | Open |
| Critical issue disposition | Multiple required gates have no evidence; this blocks release even without an observed kernel defect | Blocking evidence gaps |

## Release decision

**DO NOT RELEASE as production-ready.** Do not mark Stage 10 COMPLETE. Treat the output as an engineering prototype unless and until each applicable gate is closed with reproducible evidence and the supported matrix is explicitly narrowed to combinations actually tested. The exact-SHA CI pass is valuable implementation/virtual-platform evidence, not a substitute for missing gates.

See `STAGE_10_REPORT.md` for subsystem classifications, workload matrix and recovery behavior; `PERFORMANCE_BASELINE.md` for benchmark data policy; `HARDWARE.md` for what was and was not detected.
