# ZEROOS — Validation and Support Matrix (Stages 1–10)
Status: living evidence ledger; capability claims remain qualified by their required execution class.

The 10-stage hardening acceptance checklist is authoritative:
[ZEROOS_10_STAGE_HARDENING.md](./ZEROOS_10_STAGE_HARDENING.md)

Every row is backed by a command that runs in this repository or by a CI run of a recorded commit. Where evidence does not exist yet, the row says so — no claim is made without it.

## Binding 10-stage rule

For Stages 1-10, a capability can move to Supported only when the corresponding item in [ZEROOS_10_STAGE_HARDENING.md](./ZEROOS_10_STAGE_HARDENING.md) has executable evidence at the required execution class.

A host test cannot close a QEMU gate. QEMU cannot close a real-hardware gate. Detection, parsing, an architectural contract, or a mock cannot close an operational-support claim.

Never claim literal zero CPU/RAM/latency, universal compatibility, or untested hardware support.

## Evidence classes

| Class | Evidence | Status |
|---|---|---|
| UNIT | Host unit suites | Recorded green where listed |
| INTEGRATION | Full guest boot and service lifecycle | Recorded green where listed |
| NEGATIVE | Error/denial paths | Required |
| FAULT | Injected failures and recovery | Required |
| STRESS | Capacity/concurrency loops | Required |
| SOAK | Long-duration stability | Required; hardware pending |
| SECURITY | Enforcement and crypto boundaries | IPv4/UDP directional policy is packet-stack-bound and host-tested; live-path and other protocol gates remain pending |
| RECOVERY | Crash/update/filesystem recovery | Covered paths recorded; full recovery gate pending |
| QEMU | Guest certification | Required |
| REAL-HARDWARE | Physical certified ISO | Not run — no claim |
| PERFORMANCE | CPU/RAM/I/O/GPU/latency/thermal measurements | On-device evidence pending |

## Host memory-safety sweep

`make sanitize-check` runs the host-testable hardware/input cores, desktop
platform tests, and compatibility tests with GCC AddressSanitizer and
UndefinedBehaviorSanitizer. The current working-tree run passed after fixing an
out-of-bounds keyboard-decoder access: E0-prefixed set-1 keys use a separate
7-bit namespace, so the decoder state table is now 256 entries and the test
covers extended Delete make/repeat/break. This is HOST-only evidence. It does
not close any QEMU or real-hardware gate. The workflow now runs the same
command; CI has not yet run against this working-tree change.

## Local verification — 2026-10-05

The current working tree passed `make check`, including the Stage 1–5 local
certification scripts, the kernel/userspace build and the host suites. The
run reported 121,651 desktop checks and 9,352 compatibility checks with zero
failures. `make sanitize-check` also passed the desktop, compatibility and
hardware-core suites under AddressSanitizer and UndefinedBehaviorSanitizer;
`make elf` linked the kernel and reported no FP/SIMD instructions.

These are local build/host results, not a CI run or guest boot. QEMU/ISO
certification and all real-hardware gates remain open; do not infer that the
Stage 2 Ring-3 WAIT marker has passed at runtime from the source marker or
ELF build alone.

## Stage 1 current support limits

- `stage1-scheduler-cert.sh` passes its local gate, but it is not a QEMU SMP
  soak. The residual multi-vCPU ownership/frame/scheduler failures recorded in
  `STORAGE.md` remain unresolved and unverified by this working-tree run.

## Stage 2 current support limits

- `stage2-userspace-cert.sh`, ABI checks and the kernel ELF build pass locally.
  Copy-in/copy-out now holds the process accounting lock across range checks,
  translation and the bounded copy; a kernel self-test exercises cross-page,
  read-only, unmapped and overflow cases. Its serial marker has not been
  observed in a guest; source checks and linking do not close that runtime gate.
- Guest lifecycle, syscall-boundary negatives, IPC races and resource-exhaustion
  recovery still require the declared QEMU evidence.

## Stage 3 current support limits

- VFS unmount now holds a temporary superblock reference across mount lookup
  and root-inode acquisition, and refuses forced teardown while file/path/mmap
  references remain. A kernel storage self-test covers refusal plus continued
  mount usability.
- `make check` compiles that self-test but does not boot the kernel. QEMU
  execution and the intermittent mount-after-abort/SMP failures documented in
  `STORAGE.md` remain open.

## Stage 4 current support limits

- `stage4-hardware-cert.sh` and the host hardware-core suites pass locally.
  DHCP parser/client tests exercise duplicate-option rejection, server-ID
  binding during select/request/renew, rebinding behavior, tick wraparound,
  512 arbitrary exact-bounds datagrams and 512 mutated option streams. These
  do not prove live packet-path behavior, PCI/DMA/IRQ operation, driver
  recovery, or support for a physical network/audio/display adapter; the
  hardware matrix is still open.

## Stage 5 current support limits

- Graphics/compositor/window/input foundations exist; full GPU acceleration is not yet a certified operational path.
- Browser URL tests cover retained query/fragment-only suffixes, encoded-host rejection, DNS hyphen-label rules, invalid query escapes, path backslashes, and all 255 non-NUL byte values in a path and hostname position. File-manager tests cover hostile/unterminated provider names and over-cap source counts; downloads tests cover stable totals, progress bounds and INT_MIN callback errors. These remain host-core evidence only.
- Windows compatibility core and PE validation exist; no Windows runtime claim.
- Android baseline is documented; runtime is absent and unclaimed.
- Browser lifecycle exists; renderer/engine binding remains pending.
- ZERO AI broker exists; backend/inference adapters remain pending.
- Media policy exists; decoder/playback integration remains pending.
- Gaming profiles/metrics exist; live game runtime remains pending.
- Real-hardware validation remains open.

## Stage 6 current support limits

- The snapshot/update component is a bounded, injected-hook host model. Tests
  verify active rollback-point protection from public discard and capacity
  pruning, alternate-slot capture, exact-name rollback, and suppression of
  recursive state-machine events during side-effect hooks.
- No persistent snapshot backend, signed package verification, power-loss
  safe update journal, independently bootable recovery environment, or
  Stage 6 security-boundary integration is established by those tests.
- Stage 6 remains in progress until its security, transactional-update,
  recovery and adversarial test requirements have executable evidence at the
  required execution class.

## Stage 7 current support limits

- Window/compositor models, browser tab lifecycle and media policy have
  host-tested foundations, but there is no GPU command submission, media
  decoder/playback pipeline or browser engine integration.
- GPU, frame-time, media, browser-isolation and real-hardware certification
  remain open; no accelerated or fully functional browser/media claim is made.

## Stage 8 current support limits

- The PE32+ validator checks power-of-two/sub-page alignment rules, bounded
  optional-header directory counts and ranges (including the security
  directory's file-offset form), aligned image/header/section extents,
  non-overlapping ranges and executable entry mapping. Its host suite runs all
  1,025 file-size truncation boundaries using exact-sized buffers plus 8,192
  one-bit image mutations; `make compat-check` reports 9,352 assertions.
  Sanitizer coverage is recorded in `make sanitize-check`.
- No image loader, relocation/import handling, Windows or Android runtime,
  graphics translation or compatibility certification is claimed.

## Stage 9 current support limits

- The host-tested AI broker and bounded automation core provide policy
  primitives, not a complete AI service. AI tests cover permission revalidation,
  nested-drain suppression and payload wiping; automation tests cover
  synchronous callback-loop suppression and cooldown behavior under a
  regressing tick.
- No inference backend, live minimum-context providers, persistent workflow
  scheduler, async loop prevention, or system-wide idle/active performance
  baseline is established; Stage 9 remains in progress.

## Stage 10 current support limits

- No physical hardware certification, Lenovo G560 matrix, performance/thermal
  baseline, long soak, repeated reboot/recovery cycle or stable release evidence
  has been performed. QEMU/ISO tools are unavailable in this sandbox, and
  physical certification requires actual supported hardware.

## Release rule

Stage 10 remains blocked until Stages 1-9 have their declared executable evidence and the physical hardware matrix, recovery, security, performance and long-duration soak gates are complete.

## Current implementation status — 2026-10-05
For the live evidence-weighted stage percentages, recent hardening and remaining-gate rationale, see `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md`. Current estimate: ~44% implemented (~56% remaining); S1 92%, S2 82%, S3 45%, S4 39%, S5 41%, S6 20%, S7 12%, S8 12%, S9 12%, S10 6%.
