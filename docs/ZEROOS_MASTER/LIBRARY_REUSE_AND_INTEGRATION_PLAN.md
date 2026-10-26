# ZEROOS Library Reuse, Research Adoption & Integration Plan
Updated: 2026-10-26
Status: research-to-engineering plan; not evidence that listed components are already imported.

## Goal

Use the user's ChatGPT Library research and suitable upstream implementations to make ZEROOS more complete without replacing its native x86-64 identity, weakening security, importing incompatible Linux assumptions, or treating a PDF/catalog entry as working code.

**Policy:** audit current code → identify exact gap → choose the smallest mature compatible component → pin source/version and license → adapt behind a ZEROOS-owned API → test in isolation → integrate → measure → enable by capability. Do not import an archive wholesale.

## Library items examined

| Library item | What it is | Action |
|---|---|---|
| `100 Low-Level System Optimizations Catalog` (duplicate copies) | Idea catalog for CPU, memory, storage, scheduling and I/O | Deduplicate; use the existing per-technique audit ledger; validate each idea against code and benchmarks |
| `120 Low-Level System, Network & Browser Optimization Catalog` (duplicate copies) | Idea catalog for network, IPC, browser and media optimizations | Deduplicate; adapt safe concepts; reject forced protocol/codec and magic-value defaults |
| `Operating Systems Ultimate Encyclopedia` | Broad reference/project directory | Extract concrete upstream projects, then inspect official repositories and licenses individually |
| `Operating Systems Absolute Omega Vault` | Broad OS engineering reference | Use as research leads, not source-code provenance |
| `Extreme Hardware Optimization & Low-Level OS Engineering Manual.pdf` | Identified by repository upload metadata; content not yet fully extracted in this pass | **Pending full-text/page audit**; do not claim reviewed or adopt its prescriptions yet |
| `Master 100+ Hardware, Kernel & Virtual VRAM Handbook.pdf` | Identified by repository upload metadata; content not yet fully extracted in this pass | **Pending full-text/page audit**; do not claim reviewed or adopt its prescriptions yet |
| `RECOVER-X-10K-master-stage-package.zip` | Inspected archive: `STATUS.md`, `MASTER_10K_ROADMAP.md`, SHA-256 manifest, and a large JSON stage manifest; **no kernel/source tree in this ZIP** | Treat as planning/manifest data, not importable implementation. Use only as an external requirements reference after validating each item against ZEROOS |
| Lenovo G560 optimization ZIPs | Hardware/profile notes or configuration artifacts | Extract as profile/test inputs only after inspecting every file; do not treat as drivers or kernel implementation |
| `ZEROOS_MASTER_CONTEXT.md` | Project handoff/context | Preserve as context; repository code and current CI remain authoritative |

## Candidate reuse/adaptation matrix

This is a **candidate list**, not an import record. No third-party implementation is considered adopted until a pinned source, license, build target, tests, and provenance are committed.

| Component / idea | Best ZEROOS integration point | Decision | Gate / constraints |
|---|---|---|---|
| musl-class libc | Stage 2 userspace ABI/runtime, `userspace/libc` | High priority candidate | First define POSIX-like syscall/ABI contract; port syscalls and TLS/thread model; keep kernel APIs ZEROOS-native |
| LZ4 library | ZAMF compressed cold pages, bounded cache/log compression, image/cache tools | High priority candidate | Import library portion only; upstream repository distinguishes BSD-2-Clause library from GPL-2.0-or-later programs; verify file-level notices |
| zlib/miniz-class DEFLATE | ZIP/package/archive support, PNG and compressed assets | High priority candidate | Select exact implementation/license; malformed-input fuzzing and bounded memory |
| FreeType | Text/font service for shell, Files, browser and documents | High priority candidate | Use a supported maintained release; keep font parsing out of kernel; fuzz untrusted fonts |
| HarfBuzz | Unicode/OpenType shaping, Hindi/Devanagari and complex scripts | High priority candidate | Keep as userspace text service; trim optional backends; license and dependency review |
| lwIP-class TCP/IP | Compare against current native network stack; optional userspace/network service adapter | Evaluate, do not blindly replace | Current ZEROOS already has network foundations; compare completeness, porting cost, licensing, sockets, IPv6, firewall integration and tests first |
| mbedTLS/wolfSSL-class crypto | TLS for browser/package/update services | Candidate after threat-model review | Current kernel has crypto primitives; do not put a large TLS stack in kernel. Prefer userspace service, pinned version, CVE process, strict certificate validation |
| LLVM/Clang toolchain | Reproducible cross-build and CI | High priority tooling candidate | Keep existing GCC path until a second toolchain builds/boots/tests; pin versions and compare artifacts |
| Mesa/GPU stack | Future accelerated graphics/user-mode driver integration | Long-term only | Large dependency/driver-model gap; not a quick import; framebuffer fallback remains required |
| AOSP/ART | Optional Android runtime, Stage 8 | Long-term isolated runtime | Not kernel code; requires compatible libc/Bionic, linker, Binder-like IPC, graphics, permissions and service stack; no “Android supported” claim until apps run end-to-end |
| Wine/Windows compatibility components | Stage 8 compatibility service | Long-term isolated runtime | Requires a real userspace ABI, loader, syscalls, graphics/audio/network adapters; keep isolated; no universal app compatibility claims |
| Browser engine (e.g. Chromium/Firefox components) | Browser service | Long-term, high cost | Huge dependency and security-update surface; do not build a fake webview and call it a browser; first secure libc, threads, graphics, network, TLS, sandbox |
| FFmpeg-class codecs | Media service / video player | Evaluate after audio/video ABI | License/codec patent scope varies by configuration and jurisdiction; hardware decode must be detected; codec parsing sandboxed |
| SQLite | Settings, app metadata, local indexes, history | Candidate for user-space data stores | Small local DB only where transactions matter; WAL/durability tests; do not add DB dependency to kernel; bound caches and writeback |
| Existing RECOVER-X artifacts | Recovery tooling only if archive contains a separable portable component | Pending archive inventory | Verify archive file list, source, provenance, license, threat model, and whether it is a binary/model/data package rather than source; isolated adapter and tests required |

## ZEROOS-owned pieces — do not outsource the policy core

Keep these native and authoritative even when implementation libraries are reused:
- scheduler/task lifecycle and interrupt-frame ownership;
- address-space/page ownership and process cleanup;
- syscall ABI, capability and permission broker;
- ZERO Adaptive Memory Fabric (ZAMF) lifecycle/reclaim policy;
- resource/thermal governor and foreground-vs-background policy;
- event-driven, damage-driven, zero-render-idle compositor policy;
- update/snapshot/rollback trust and recovery decisions;
- AI action broker, consent, scoped permissions, audit, cancellation and emergency STOP;
- hardware capability profiles, including Lenovo G560 safe fallback.

Libraries provide mechanisms; ZEROOS retains policy, lifecycle and security ownership.

## Apply the optimization catalog safely

1. **HDD / filesystem:** bounded read-ahead, sequential boot reads, foreground I/O priority, coalesced writes with correct flush ordering, bounded dirty pages, metadata locality. Do not sacrifice crash consistency for speed.
2. **Memory:** demand paging, shared read-only pages, COW, discardable caches, pressure-triggered compression/reclaim. Do not put all browser cache in RAM on 2 GB hardware; no fixed compression ratio claims.
3. **Scheduler:** fairness, latency budgets, priority inheritance where needed, starvation prevention and event-driven wakeups. Benchmark before/after; no blind CPU pinning.
4. **Network:** socket-buffer budgets, safe congestion control, negotiated protocol fallback, capability-gated NIC offloads. Do not force BBR/QUIC/TFO or copy Linux sysctl values.
5. **Browser:** lazy images/iframes, bounded JS heaps, background throttling, bfcache-like retained state, freeze/discard lifecycle. Preserve site isolation; do not merge unrelated sites just to save RAM.
6. **Video:** detect codec/profile and hardware decoder, bounded frame queue, frame pacing, A/V clock, overlay/direct scanout when supported. G560 target is H.264/AVC 1080p30 certification, not a guaranteed result before physical testing.
7. **Boot/services:** minimal deterministic boot path, parallelize only independent init, lazy-start optional services, keep recovery path independent.
8. **Build:** reproducible flags, LTO/PGO only after deterministic baseline and benchmark; target CPU features must be detected and legacy x86-64 safe.
9. **Security:** no disabling NX/W^X, firewall, TLS verification, watchdog, mitigations, or audit records for benchmark wins.
10. **Telemetry:** event-driven counters, bounded ring/logs, opt-in sampling; measure idle wakeups and CPU cost.

## Import checklist (required per component)

- [ ] exact upstream URL, pinned tag/commit and source checksum;
- [ ] SPDX/license and file-level license notices;
- [ ] SBOM/provenance entry and local patch series;
- [ ] intended user/kernel boundary and stable ZEROOS adapter;
- [ ] supported architecture, compiler, ABI and dependency list;
- [ ] explicit feature/capability flag and fallback;
- [ ] unit, malformed-input, negative, fuzz, resource-exhaustion and fault tests;
- [ ] QEMU boot/integration tests plus real-hardware tests where relevant;
- [ ] before/after RAM, CPU, idle wakeups, boot, I/O and latency measurements;
- [ ] update/CVE ownership, rollback procedure and docs;
- [ ] only then mark IMPORTED / INTEGRATED / TESTED.

## Work order

P0. Run current-main clean build, `make check`, ISO verification and repeated `make boot-test`; fix failures first.
P1. Finish Stage 1/2 process, exec, thread, timer, IPC, address-space ownership and failure cleanup with stress/fault tests.
P2. Prove persistent storage, filesystem recovery, disk-full and interrupted-write behavior.
P3. Make the native session useful end-to-end: input, display, shell, Files, Terminal, Settings and service restart.
P4. Add libc/runtime and compression/font dependencies through separate, reproducible ports.
P5. Improve drivers/network/audio/video and test the actual G560 hardware envelope.
P6. Integrate TLS/browser/media and data stores as isolated user-space services.
P7. Build signed update, independent recovery/install, backup/restore and rollback.
P8. Add Android/Windows runtimes and full AI automation only after their required ABI/service boundaries are real.
P9. Enable optimizations one by one from measured evidence; finish hardware, security, soak and release certification.

## Current truth and completion accounting

- The PDF audit and this plan are documentation/research outputs, **not code completion**.
- Existing host tests or a source file do not prove the feature is wired into the boot path; use the current completion matrix and boot milestone log.
- Mark each feature with one of: SPEC_ONLY, PROTOTYPE, IMPLEMENTED, INTEGRATED, TESTED, QEMU_CERTIFIED, HARDWARE_VERIFIED.
- A feature is 100% only after its required implementation, negative/fault/stress tests, boot-path integration, resource measurement and documentation all pass.
- Do not publish a whole-project percentage increase solely because research docs were added.

## Primary upstream references to verify during import

- LZ4: https://github.com/lz4/lz4 (library code is BSD-2-Clause; programs have separate licensing).
- FreeType: https://freetype.org/ (choose and record the applicable license).
- HarfBuzz: https://github.com/harfbuzz/harfbuzz (MIT license in upstream COPYING).
- musl: https://musl.libc.org/
- lwIP: https://savannah.nongnu.org/projects/lwip/
- SQLite: https://www.sqlite.org/copyright.html
- FFmpeg: https://ffmpeg.org/legal.html
- LLVM: https://llvm.org/
- AOSP: https://source.android.com/
- Wine: https://www.winehq.org/

Links are discovery starting points, not an assertion that a component has already passed ZEROOS integration tests.
