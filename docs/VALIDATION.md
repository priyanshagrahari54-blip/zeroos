# ZEROOS — Stage 5 Validation and Support Matrix
Status: living evidence ledger for Stage 5 and the binding evidence policy for Stages 1-10.

The 10-stage hardening acceptance checklist is authoritative:
docs/ZEROOS_10_STAGE_HARDENING.md

Every row is backed by a command that runs in this repository or by a CI run of a recorded commit. Where evidence does not exist yet, the row says so — no claim is made without it.

## Binding 10-stage rule

For Stages 1-10, a capability can move to Supported only when the corresponding item in docs/ZEROOS_10_STAGE_HARDENING.md has executable evidence at the required execution class.

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
| SECURITY | Enforcement and crypto boundaries | Host coverage exists; kernel enforcement pending |
| RECOVERY | Crash/update/filesystem recovery | Covered paths recorded; full recovery gate pending |
| QEMU | Guest certification | Required |
| REAL-HARDWARE | Physical certified ISO | Not run — no claim |
| PERFORMANCE | CPU/RAM/I/O/GPU/latency/thermal measurements | On-device evidence pending |

## Stage 5 current support limits

- Graphics/compositor/window/input foundations exist; full GPU acceleration is not yet a certified operational path.
- Windows compatibility core and PE validation exist; no Windows runtime claim.
- Android baseline is documented; runtime is absent and unclaimed.
- Browser lifecycle exists; renderer/engine binding remains pending.
- ZERO AI broker exists; backend/inference adapters remain pending.
- Media policy exists; decoder/playback integration remains pending.
- Gaming profiles/metrics exist; live game runtime remains pending.
- Real-hardware validation remains open.

## Release rule

Stage 10 remains blocked until Stages 1-9 have their declared executable evidence and the physical hardware matrix, recovery, security, performance and long-duration soak gates are complete.