# ZEROOS STAGE 10 — CERTIFICATION, HARDWARE MATRIX & RELEASE

Status: FINAL EXECUTION/CERTIFICATION STAGE — specification and release contract.

## Mission
Prove the OS on real hardware and publish only claims backed by reproducible evidence.

## 10.1 Reference hardware
Maintain a Lenovo G560-class reference profile where available: x86-64, 1366x768, HDD and low-RAM configurations. Exact CPU/GPU/Wi-Fi/audio/storage/firmware must be recorded for every test. The reference profile does not imply every G560 revision is supported.

## 10.2 Certification matrix
For each machine record:
- CPU/model/features
- RAM
- GPU/driver
- display
- storage/controller/filesystem
- Ethernet/Wi-Fi/Bluetooth
- audio
- firmware/boot mode
- ACPI/power/thermal sensors
- ZEROOS build/commit

Capabilities use DESIGNED, IMPLEMENTED, TESTED, CERTIFIED, REGRESSION-LOCKED.

## 10.3 Functional certification
Boot, login/session, filesystem, applications, network, audio, display, sleep/resume, shutdown/reboot, update, rollback, recovery and security boundaries must pass.

## 10.4 Performance certification
Measure boot time, idle CPU/RAM, wakeups, app launch, scheduler/context switch, syscall/IPC, disk throughput/latency, network throughput/latency, compositor frame time, media decode, browser workload, AI invocation, suspend/resume and thermal behavior.

No "zero resource" claim: publish idle/background overhead and active-work cost separately.

## 10.5 1080p media
Validate 1080p playback only on hardware that has sufficient decode/scale capability. Record codec, bitrate, frame drops, CPU/GPU load, RAM, temperature and power behavior. Native 1366x768 remains the G560 panel target.

## 10.6 Network certification
Report physical link speed, negotiated mode, driver, throughput, latency, packet loss and CPU cost. Never state a fixed 100 Mbps capability independent of hardware/link.

## 10.7 Thermal/power
Run cold boot, sustained CPU, sustained GPU, media, browser, storage and mixed workloads. Test battery/AC transitions, thermal throttling, suspend/resume, critical battery shutdown and power-loss recovery where hardware permits.

## 10.8 Reliability/soak
Long-duration mixed workload, repeated boot/shutdown, suspend/resume loops, application crash loops, network loss/reconnect, storage stress and update/recovery cycles. Record all failures, not only successful runs.

## 10.9 Security release gate
No known critical privilege escape, package signature bypass, update rollback bypass, sandbox escape or data-integrity defect in the declared release scope. Security exceptions must be documented and block release when critical.

## 10.10 Compatibility release gate
Windows/Android/browser/app support is published only from tested matrix entries. Unsupported combinations are explicitly listed.

## 10.11 Recovery release gate
At minimum demonstrate failed update recovery, filesystem/reboot recovery, safe mode, rollback and recovery environment startup on supported profiles.

## 10.12 Release channels
Development -> Preview -> Stable. Each channel has explicit entry/exit criteria. Stable requires green CI, documented hardware matrix, reproducible artifacts, security review, recovery validation, performance baseline and no known critical crashes in declared scope.

## 10.13 Release artifacts
Produce checksums/signatures, SBOM, build metadata, source revision, test report, hardware matrix, known limitations, recovery instructions and rollback instructions.

## Exit gate
Stage 10 is complete only when the declared release has reproducible build/test evidence, hardware certification, performance baseline, security/recovery validation and honest compatibility claims.
