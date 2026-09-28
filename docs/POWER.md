# ZEROOS Power Management: Orderly Shutdown and Power-Off

Status: implemented and certified in QEMU (q35 AHCI + NVMe, CI step
"Clean shutdown certification"). Items marked **PARTIAL** have a stable
contract but an incomplete implementation.

Code: `kernel/power.c` (orchestration, ACPI S5, reset), `kernel/acpi.c`
(FADT/DSDT discovery, `\_S5_` parser), `storage_shutdown()` in
`kernel/storage/storage.c`, `ZEROOS_SYS_POWER` in `kernel/syscall.c`.

## 1. Contract

`power_request(action, reason)` is the only path to power-off or reboot.
It runs these steps in order:

1. **Admission.** `power_check()` validates the action and platform support
   *before* anything is touched. Power-off requires a discovered ACPI S5
   object. An unsupported request returns `-ENOTSUP` with no side effects.
   A second concurrent request returns `-EBUSY`; exactly one shutdown runs.
2. **Filesystem sync.** `vfs_sync_all()` writes back dirty page-cache data
   and commits every filesystem.
3. **Unmount.** `vfs_unmount_all()` unmounts every mount. For ZJFS this
   commits the journal, writes the CLEAN superblock state and flushes the
   device. Mounts still referenced by live processes are force-unmounted,
   and the forced unmount is logged.
4. **Device cache flush.** Each AHCI and NVMe disk gets `block_flush()`
   (ATA FLUSH CACHE [EXT] / NVMe Flush).
5. **NVMe shutdown.** `nvme_shutdown_all()` runs the CC.SHN normal-shutdown
   handshake and waits for SHST=complete within the controller timeout.
6. **Platform transition:**
   - power-off: ACPI S5 (§3);
   - reboot: the FADT reset register if present, then the i8042 reset pulse,
     then a triple fault.

Steps 2–5 always all run, even after an earlier failure. Each result is
logged, and a single summary line states either `storage quiesced cleanly`
or that errors were reported. A storage error does not block power-off:
ZJFS is journaled, so the next mount recovers. The log states that recovery
will run.

If the platform ignores the transition after storage is quiesced (S5 write
without effect within 5 s, or all reset methods failed), the calling CPU
logs a diagnostic and halts. It never returns to a system whose
filesystems are unmounted.

## 2. Triggers

| Trigger | Who | Notes |
|---|---|---|
| `ZEROOS_SYS_POWER` (ID 57) | Ring 3, uid 0 only | See §4 |
| Boot option `zeroos.shutdown=poweroff-after-cert` | Kernel command line (Multiboot2 tag 1) | Once storage **and** session certification have both finished, a kernel task calls `power_request(POWEROFF)`. Used by CI. Unknown options are ignored. |

The kernel does not freeze user tasks before quiescing storage (**PARTIAL**).
The caller (init/service manager) is responsible for stopping services
first. The service manager already delivers a shutdown event. Files still
open when the unmount runs are force-unmounted.

## 3. ACPI S5 discovery and entry

Discovery (`acpi_discover`) reads, through the boot identity map:

- **FADT (`FACP`):**
  - PM1a/PM1b control blocks: the `X_` GAS form if it is in system I/O
    space, else the 32-bit legacy fields. `PM1_CNT_LEN` must be ≥ 2.
  - `SMI_CMD` and `ACPI_ENABLE`.
  - `Flags`: `HW_REDUCED_ACPI` means S5 is unavailable (**PARTIAL**, no
    SLEEP_CONTROL_REG support). `RESET_REG_SUP` together with an I/O-space
    `RESET_REG` enables the ACPI reset.
  - DSDT (`X_DSDT` preferred).
- **DSDT:** the `Name(_S5_, Package{SLP_TYPa, SLP_TYPb, ...})` object,
  parsed by `acpi_parse_s5()`. It accepts `NameOp` with an optional root
  prefix, a bounds-checked `PkgLength` (1–4 bytes), and integer elements
  (`Zero`, `One`, `Byte`/`Word`/`DWordPrefix`). Values wider than 3 bits,
  truncated packages and non-integer elements are rejected as malformed.
  `_S5_` in SSDTs or computed by methods is not supported (**PARTIAL**).

Every table must be inside the identity-mapped window and pass its checksum.
Only the resulting I/O ports and values are stored. The shutdown path never
dereferences firmware memory. At boot the kernel logs either
`ACPI power: S5 soft-off available (pm1a=… slp_typ=…)` or the reason S5 is
unavailable. The AML parser runs a self-test at boot against good,
root-prefixed, NameOp-less, truncated and out-of-range vectors. Power-off is
refused (`ENOTSUP`) unless that self-test has passed.

Entry follows ACPI 6.5 §16.1.7:

1. If `SCI_EN` is clear and the FADT provides an SMI handoff, write
   `ACPI_ENABLE` to `SMI_CMD` and wait up to 3 s for `SCI_EN`.
2. Program `SLP_TYP` into PM1a (and PM1b).
3. Set `SLP_EN`.

## 4. System call ABI (`ZEROOS_SYS_POWER`, ID 57)

- Feature bit `ZEROOS_ABI_FEATURE_POWER` (bit 13). Additive to ABI v1: no
  existing call changes.
- `zeroos_power(uint32_t action, uint64_t flags)`:
  - `action`: `ZEROOS_POWER_POWEROFF` (1) or `ZEROOS_POWER_REBOOT` (2).
  - `flags`: `ZEROOS_POWER_FLAG_CHECK` validates authority, arguments and
    platform support and returns 0 *without acting*.
- Checks, in order:
  1. `EPERM` if the caller's uid ≠ 0 (authority is checked before
     arguments, so unprivileged callers learn nothing about platform
     support);
  2. `EINVAL` for an unknown action or flag;
  3. `EBUSY` if a shutdown is in progress;
  4. `ENOTSUP` if power-off is requested without ACPI S5.
- On success a real request does not return.
- IDs 55 and 56 (and feature bits 11/12) are reserved for
  `SYSTEM_INFO`/`CHILD_IMAGE`, which are in review. They return `ENOSYS`
  until they land. The ID table stays contiguous
  (`syscall_debug_validate`).

**Migration:** none required. Existing binaries are unaffected. New code
must test `ZEROOS_ABI_FEATURE_POWER` before calling ID 57.

## 5. Evidence

| Test | Where | Result |
|---|---|---|
| q35, 4 vCPU, AHCI + NVMe, `poweroff-after-cert`, no `-no-shutdown` | CI step "Clean shutdown certification"; local QEMU 11.0.2 | QEMU exits 0 by itself; logs `mounts=3`, `disks=2, failed=0`, `nvme0: shutdown complete`, `storage quiesced cleanly`; host `zjfs info` shows `state=CLEAN journal=clean` on both disks; `fsck` clean without repair; the Ring-3 write is present |
| Ring-3 authority/argument checks | storage userspace probe (every boot) | root CHECK power-off 0 (or ENOTSUP without S5), root CHECK reboot 0, bad action/flags EINVAL, uid 1000 power-off/reboot EPERM |
| S5 unavailable (`-machine pc,acpi=off`) | local QEMU | capability logged as unavailable, probe sees ENOTSUP, automatic power-off refused with `-95` and **zero** unmounts; the boot completes |
| AML parser vectors | boot self-test | pass |

Before this change every shutdown was an unclean power cut, recovered by
journal replay.

## 6. Limitations (PARTIAL)

- No task freeze before the storage quiesce (see §2).
- Hardware-reduced ACPI (SLEEP_CONTROL_REG), `_S5_` outside the DSDT,
  and `_PTS`/`_GTS` AML methods are not supported. There is no AML
  interpreter, so firmware that depends on `_PTS` side effects may not
  power off cleanly.
- AHCI disks get FLUSH CACHE but no STANDBY IMMEDIATE.
- Other CPUs are not parked before the transition. This is not required
  for S5 or reset.
- Only tested on QEMU (PIIX4 and ICH9 PM). No real-hardware validation.
