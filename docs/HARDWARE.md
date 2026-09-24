# ZEROOS Hardware Architecture

## Supported-device matrix (Stage 4 baseline)

| Area | Detected / implemented | Operational support | Unsupported / explicitly unclaimed |
|---|---|---|---|
| x86-64 CPU, RAM, serial, PIT/PIC | Yes | Boot, memory management, diagnostics, timer | Other architectures |
| ACPI RSDP/root/MADT | Validated discovery | Interrupt topology discovery and existing APIC path | AML, ACPI power/thermal/battery, suspend/resume |
| PCI segment 0 | Mechanism #1 bus/function scan, IDs/classes, BAR address/type snapshots, bounded conventional capability list | Observation only; devices remain unbound | ECAM/MCFG, additional segments, BAR sizing/assignment, resource mapping, MSI/MSI-X activation, hotplug, driver matching |
| DMA/IOMMU | No IOMMU backend | Owner-scoped DMA map/unmap API contract with bounded mapping tokens and teardown refusal while mappings remain | Hardware translation/cache-coherency implementation, bounce buffers, isolation/domain setup, device reset integration |
| USB/input/display/audio/network/storage | No device driver | Descriptor framing validator; fixed-capacity input queue/registry; bounded audio ring; display mode validator; Ethernet/VLAN and ARP framing parsers; IPv4/IPv6 base-header validation; default-deny IPv4 policy and bounded flow tracking/routes; UDP framing, limited TCP connection-state helper/timeouts, DHCP option and DNS message validators; lifecycle/resource cleanup state machine | HCD/device enumeration, HID decoding, IRQ-backed input, DMA/audio engine, display scanout/GPU, NIC, IPv6/TCP/UDP sockets, DNS/DHCP, neighbor/ARP, routing integration, storage |

## PCI observation contract

`pci_enumerate()` scans the 256 buses and 32 slots of legacy PCI configuration
mechanism #1, respecting multifunction headers. It records vendor/device and
class identity, snapshots currently assigned BAR addresses/types without
writing configuration space, and follows conventional capability pointers
with cycle and bounds checks. It reports MSI/MSI-X capability metadata only;
these capabilities are never enabled, and table offsets cannot be trusted until
BAR sizing/range validation exists. The inventory is fixed-size; overflow is
reported as truncated. This is not PCIe ECAM support and does not enumerate
nonzero PCI segments. It does not enable bus mastering, allocate resources,
map device MMIO, or activate interrupts. A listed device is discovered, not
supported. No driver is bound and no hardware behavior is simulated.

Future resource ownership must validate address arithmetic, alignment, range
and overlap against the physical-memory/resource map before mapping or use.
BAR sizing requires an exclusive quiesced-device transaction and is not
performed during generic discovery. MSI/MSI-X and DMA stay disabled until
interrupt ownership and IOMMU/DMA lifetime contracts exist.

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
