# ZEROOS Compatibility Matrix

**Assessment date:** 2026-09-29
**Source SHA:** `91e30eb7eae80b832f8daecdd1dee8e43424a2f9`
**Rule:** parser/policy presence is not runtime support. Each row distinguishes implemented, tested and operational status. Compatibility is per exact application, runtime build, hardware, driver and workload; no universal claim is made.

## Application/runtime matrix

| Platform / workload | Classification | Implemented evidence | Tested evidence | Missing evidence / limitation |
|---|---|---|---|---|
| ZEROOS native shell/session | PARTIAL | Embedded Ring-3 session/session display/compositor/input path and storage probe sources; userspace ABI/runtime code | Host ABI checks; exact-SHA CI boot checks session shell and storage probe milestones | Not a general native application framework or application catalog; limited embedded probes, no physical desktop workload certification |
| Windows PE compatibility core | PARTIAL | PE validation and compatibility policy/lifecycle core in `userspace/compat/` | `make compat-check`: 107 checks, 0 failures; freestanding compile included in target | No demonstrated PE process execution, import/DLL loading, Win32 process/thread/sync APIs, real filesystem/network/graphics app, or named app passing end-to-end. **No Windows application is SUPPORTED.** |
| Android runtime / APK | UNSUPPORTED | AOSP 14 baseline/interface is documented in architecture | No APK/runtime/device test evidenced; documented tested matrix is empty | No integrated runtime; installation, permissions, lifecycle, graphics/audio/input/network, suspend/resume, update/uninstall untested |
| Browser/web application | PARTIAL | Host-tested browser tab/lifecycle/navigation policy cores | Desktop unit/stress tests | Browser renderer/engine binding and real site compatibility are not established; not certified as web browser support |
| Media playback | UNSUPPORTED for production playback support | Media policy/source/rights gates | Host policy tests | No decoder backend or measured playback path established by this certification; no 720p/1080p/fullscreen/AV-sync/soak results |
| Gaming | UNSUPPORTED for game compatibility | Gaming policy/profile/FPS-monitor cores | Host policy tests | No named game/runtime+GPU+driver combination tested; no FPS/frame-time/thermal soak evidence. Controller/gamepad support remains out of scope |
| ZERO AI | PARTIAL (optional policy core) | Broker lifecycle/permissions/dormancy policy core | Host AI broker tests including permission, bounds and failure paths | No inference backend linked and no full operation with actual tool/action integrations. Not required to boot or operate the OS |
| General native desktop applications | UNSUPPORTED as a production application ecosystem | Desktop/platform components and UI policy cores exist | Host suites plus boot session milestones | No published, named app compatibility list with end-to-end hardware evidence; shell chrome/app framework remains incomplete per existing roadmap |

## Hardware/software matrix

| Profile | Classification | Evidence |
|---|---|---|
| `sandbox-kvm-2026-09-29` | TEST ENVIRONMENT ONLY, not supported hardware | Build host reported KVM, 2 logical CPUs, ~3.85 GiB MemTotal and a 21.8 GB virtual `vda`; no target GPU/panel/network/sensors exposed |
| Lenovo G560-class | NOT TESTED / NOT CERTIFIED | Exact machine configuration unknown; no physical unit attached or profile recorded |
| 2 GB RAM target | NOT TESTED | No 2 GB boot/idle/application/memory-pressure run |
| Native 1366x768 panel | NOT TESTED | No panel identified; 1366x768 is a target only if that is the actual panel |
| Physical HDD/SATA | NOT TESTED | CI emulated AHCI persistence is not physical HDD performance/reliability evidence |
| Ethernet/Wi-Fi adapters | NOT TESTED | No physical adapter identity, negotiated link, DHCP/DNS/reconnect, packet loss or throughput recorded |
| GPU / Vulkan / software fallback | NOT CERTIFIED | No target GPU/backend enumeration, render correctness or resource/performance evidence |

## Application-level support claims

**SUPPORTED:** none established by this Stage 10 assessment.
**PARTIAL:** native session/probe path; Windows PE/compatibility core; browser lifecycle/policy; optional ZERO AI broker policy. These are explicitly partial components, not supported products.
**UNSUPPORTED:** Android application runtime; production media playback; game compatibility; general production native app ecosystem. This means no support claim is justified by current evidence; it is not a statement that every future implementation is impossible.

A supported entry must name exact app and version, runtime, hardware profile, kernel commit/build configuration, functional/negative/stress/recovery results and known limitations. Update this matrix only with linked reproducible evidence; do not promote on parser tests, detection, mocks, or architectural plans.
