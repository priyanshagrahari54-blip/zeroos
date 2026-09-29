# ZEROOS Hardware Architecture

## Supported-device matrix (Stage 4 baseline)

| Area | Detected / implemented | Operational support | Unsupported / explicitly unclaimed |
|---|---|---|---|
| x86-64 CPU, RAM, serial, PIT/PIC | Yes | Boot, memory management, diagnostics, timer | Other architectures |
| ACPI RSDP/root/MADT | Validated discovery | Interrupt topology discovery and existing APIC path | AML, ACPI power/thermal/battery, suspend/resume |
| PCI segment 0 | Mechanism #1 scan, identity/class, BAR sizing (not display class), bounded capabilities incl. MSI/MSI-X/PCIe; typed resource interval registry (standalone) | Claimed AHCI/NVMe functions: decoding + bus mastering, uncached NX BAR mapping, MSI/MSI-X activation. All others remain unbound | ECAM/MCFG, other segments, BAR assignment, overlap validation of BARs against the resource map, surprise-removal hotplug |
| DMA/IOMMU | No IOMMU backend | Owner-scoped DMA map/unmap API contract with bounded mapping tokens and teardown refusal while mappings remain | Hardware translation/cache-coherency implementation, bounce buffers, isolation/domain setup, device reset integration |
| USB/input/display/audio/network | Event-driven bounded `netif` Ethernet frame queue with required caller-provided IRQ-safe lock callbacks; PS/2 keyboard + mouse (i8042, IRQ1/IRQ12, set-1 decode + 3-byte aux packets → input queue → INPUT_POLL/WAIT); no USB HCD | Descriptor framing validator; fixed-capacity input queue/registry; bounded audio ring; display mode validator; Ethernet/VLAN and ARP framing parsers; IPv4 validation; bounded IPv6 extension-header parsing (Hop-by-Hop, Routing, Fragment, AH, Destination; no ESP, jumbograms, or reassembly); default-deny IPv4 policy and bounded flow tracking/routes; UDP framing; host-tested bounded IPv4 UDP ingress dispatcher with destination checks, default-deny firewall policy and checksum verification; bounded owner-tagged UDP endpoint/receive-queue table with generation handles (no syscalls); checked IPv4/UDP Ethernet transmit builder with DF, IPv4 and UDP checksums via caller-resolved next-hop MAC; host-tested IPv4 ARP request/reply validation/builders and fixed-capacity expiring neighbor cache that admits replies only for outstanding, matching local requests (not connected to a NIC or transmit path); limited TCP connection-state helper/timeouts, DHCP lease-state machine and option parsing, DNS message validator; lifecycle/resource cleanup state machine | Mouse wheel/extended aux protocols (4-byte/ID-prefixed), HID decoding, USB HCD/device enumeration, DMA/audio engine, display scanout/GPU, NIC, IPv6 sockets and user-visible POSIX/BSD socket syscalls, DNS/DHCP live services, ARP/neighbor lookup integration with a NIC/transmit path, routing integration |
| Storage (Stage 3) | AHCI (SATA disks) and NVMe (namespace 1) drivers | MSI/MSI-X completion, NCQ / multi-queue, timeouts, retries, controller reset with in-flight drain, cooperative removal; GPT; ZJFS journaling filesystem; page cache; file syscalls; see STORAGE.md | Legacy IDE/ATAPI, port multipliers, AHCI INTx (polled fallback only), NVMe multi-namespace, TRIM, IOMMU isolation |

## PCI observation contract

`pci_init()` (`kernel/pci.{h,c}`, run once by the storage manager) scans the
256 buses and 32 slots of legacy configuration mechanism #1 on segment 0,
respecting multifunction headers, into a fixed table of
`ZEROOS_PCI_MAX_DEVICES` (64) functions. Overflow is counted and logged, never
silent. For each function it records:
- identity and class, and the INTx line and pin;
- MSI, MSI-X and PCIe capability offsets, from a walk bounded at 48 entries
  so a cyclic list terminates;
- BAR base, size and type. BARs are sized with I/O and memory decoding
  disabled and restored immediately afterwards.

Display controllers (class 03h) are **not** sized, because their
framebuffer is live (`kernel/fb.c`). Their BAR bases are recorded
read-only and cannot be mapped through `pci_map_bar()`.

Every class-01h function is logged (`PCI storage …`). Discovery alone
changes nothing else. Only a driver that claims a function (currently AHCI
and NVMe, Stage 3) enables decoding and bus mastering
(`pci_enable_device`), maps BARs uncached/NX into the kernel MMIO window
(`pci_map_bar`), and programs MSI or MSI-X targeting the BSP LAPIC.
Unclaimed devices, including legacy IDE, stay unbound. PCIe ECAM and
nonzero segments are not supported.

The Stage 4/5 branch briefly had a separate observation-only
`pci_enumerate()`/`pci_inventory` API. When the branches were merged, it
was folded into this single PCI subsystem, so there is exactly one owner
of configuration space.

Known gaps (PARTIAL):
- BAR ranges are not yet validated for overlap against the
  physical-memory/resource map before mapping. Firmware-assigned addresses
  are trusted, and no BAR is ever reassigned.
- Generic discovery sizes BARs without first quiescing the device. It
  disables decoding only for the brief sizing window.
- DMA runs without an IOMMU. The storage drivers DMA only into
  kernel-owned pages that they allocate and free, so a malicious or
  faulty device is not contained.
- MSI/MSI-X is enabled only by drivers that claim a device. Everything
  else stays disabled.

## Driver lifecycle and safety boundary

Future drivers must implement DISCOVER → MATCH → PROBE → RESOURCE ACQUIRE →
DMA/IRQ SETUP → INITIALIZE → REGISTER → SERVE → ERROR RECOVERY → SUSPEND →
RESUME → REMOVE → CLEANUP. Each acquired resource has one owner and one
release path. Unsupported hardware is reported unbound; no universal hardware
support is claimed. Device-specific execution belongs behind stable interfaces,
not in desktop policy.

## Existing boot target

The current target is x86-64 under GRUB/Multiboot2, with serial COM1, physical
page discovery/allocation, x86-64 virtual memory, IDT/ISR entry, 8259 PIC,
PIT, ACPI RSDP/root/MADT validation, and the bounded APIC/SMP functionality
documented in `ACPI.md`. QEMU and physical-device certification are distinct;
no real-hardware certification is implied by compilation.


## 10-stage hardware alignment

Hardware certification is evidence-gated. The Lenovo G560 class is the reference low-resource profile, but exact CPU, RAM, GPU, display and network adapter must be detected per machine. Native 1366x768 is the reference display target; 1080p source playback is a capability test, not a panel-resolution claim. Network throughput is recorded from the actual adapter/link. Thermal, power, HDD, memory-pressure and long-duration results must be recorded in the validation matrix.

Do not convert detection, enumeration or host tests into support claims. Controller/gamepad support is not part of the current ZEROOS hardware target.


## Stage 10 hardware certification profile (2026-09-29)

**No ZEROOS target hardware was attached or certified.** The only observed machine was the sandbox's KVM virtualized Linux build environment, not a Lenovo G560. It exposed 2 logical vCPUs (reported model string `Intel(R) Xeon(R) Processor @ 2.60GHz`), `MemTotal` 4,034,452 kB (~3.85 GiB), and virtual block device `vda` 21.8 GB with guest-visible `ROTA=1`. These values describe the VM/container view, not the physical host; `ROTA=1` is not an RPM measurement and is not proof of an HDD. GPU/VRAM, display/native resolution, actual storage model/SATA mode, Wi-Fi/Ethernet adapters, audio, USB controller, ACPI/BIOS, battery, fans and temperature sensors are **unknown/not exposed**.

The profile is therefore `sandbox-kvm-2026-09-29`, class `BUILD/CI ENVIRONMENT ONLY`, and is not a supported-hardware profile. No G560 model/configuration, 1366x768 panel, 2 GB machine, or 100 Mbps link is claimed. The hardware certification checklist and per-device result template remain open; do not substitute QEMU emulation or enumeration logs for a detected physical profile. See `STAGE_10_REPORT.md`.
