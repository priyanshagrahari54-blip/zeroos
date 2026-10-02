# ZEROOS — STAGE 4 IMPLEMENTATION PROMPT
Version: 1.0 — Production-Grade Hardware and Networking
Repository: https://github.com/priyanshagrahari54-blip/zeroos

## Mission
Continue from current `main`. Build the production hardware/device and networking layer. Inspect existing drivers and preserve stable contracts. Unsupported hardware must be detected and reported, never simulated.

## 1. PCI/PCIe
Implement:
- enumeration
- vendor/device/class identification
- BAR discovery and safe mapping
- capability parsing
- MSI/MSI-X
- resource assignment
- driver matching
- error reporting
- device lifetime/hotplug architecture.

Validate every MMIO/resource range.

## 2. ACPI
Build architecture for:
- device discovery
- power-state information
- interrupt routing
- thermal information
- system configuration
- suspend/resume.

Malformed firmware tables must fail safely.

## 3. DMA/IOMMU
Define ownership and APIs for:
- DMA mapping/unmapping
- buffer lifetime
- cache coherency
- bounce buffers where required
- IOMMU integration
- isolation policy
- device reset/recovery.

No DMA buffer may outlive its owner.

## 4. USB
Build:
- host-controller abstraction
- device enumeration
- descriptor validation
- endpoint management
- transfer queues
- completion/error handling
- timeout/cancellation
- hotplug
- HID integration.

Support architecture must remain extensible to multiple controller types.

## 5. Input
Create stable event APIs for:
- keyboard
- mouse
- touch
- future input devices.

Handle hotplug, device removal, malformed input and event backpressure.

## 6. Display/GPU hardware abstraction
Implement architecture for:
- display discovery
- modes
- refresh rate
- multi-monitor
- framebuffer fallback
- GPU memory
- acceleration
- synchronization
- hotplug
- capability detection.

Do not put desktop policy in kernel.

## 7. Audio
Build:
application → audio API → session manager → mixer → device engine → codec/DSP → hardware.

Support:
- playback/capture
- per-app volume
- device switching
- hotplug
- low-latency mode
- bounded buffers
- underrun/overrun recovery
- hardware acceleration where available
- AV timing hooks.

Do not claim proprietary Dolby/etc. support without licensing.

## 8. Networking
Build:
NIC → Ethernet → IP → TCP/UDP → DNS/DHCP → sockets → network manager.

Implement/mature:
- interface management
- IPv4
- IPv6 architecture
- routing
- ARP/neighbor handling
- TCP state machine
- UDP
- DNS
- DHCP
- sockets
- connection timeouts/retransmission
- congestion handling
- packet/error counters
- diagnostics
- firewall hooks.

No busy polling when interrupt/event-driven operation is possible.

## 9. Firewall/security hooks
Create auditable packet-policy boundaries with:
- explicit default policy
- connection/state tracking where required
- rule lifecycle
- logging
- rate limits
- resource bounds
- permission integration.

Do not claim a full antivirus from a packet firewall.

## 10. Power/thermal
Implement architecture for:
- CPU idle/frequency policy
- device power states
- thermal monitoring
- fan policy
- battery/AC state
- suspend/resume
- wake-source accounting.

Adapt workloads under thermal pressure.

## 11. Driver lifecycle
Every driver follows:
DISCOVER → MATCH → PROBE → RESOURCE ACQUIRE → DMA/IRQ SETUP → INITIALIZE → REGISTER → SERVE → ERROR RECOVERY → SUSPEND → RESUME → REMOVE → CLEANUP.

All failure paths must release resources exactly once.

## 12. Cross-cutting resource model
Use capability detection and profiles:
powerful → richer acceleration
mid-range → balanced
low-end → simplified
thermal pressure → reduced optional work.

Do not promise universal hardware support.

## 13. Tests
Cover:
- malformed PCI/ACPI data
- BAR/resource conflicts
- interrupt storms
- MSI/MSI-X
- DMA mapping errors
- device reset
- USB hotplug/removal
- malformed descriptors
- network packet errors
- connection timeout
- route changes
- DNS/DHCP failure
- firewall policy
- audio underrun
- display hotplug
- suspend/resume
- thermal throttling
- driver crash/isolation.

Use QEMU plus real hardware where applicable.

## 14. Completion
Update HARDWARE, ARCHITECTURE, TECHSPEC, RULES and relevant subsystem docs. Record supported-device matrix and unsupported cases. Zero warnings, regression tests and CI evidence required.

Definition of done: supported hardware is accessed through stable interfaces, device failures are isolated/recoverable, and networking/audio/input/display foundations are real and measurable.
