#ifndef ZEROOS_POWER_H
#define ZEROOS_POWER_H
#include "types.h"

/* Orderly system shutdown (docs/POWER.md).
 *
 * power_request() performs, in order: filesystem sync, unmount of every
 * mount (journal commit + CLEAN superblock), device write-cache flush of every
 * AHCI/NVMe disk, NVMe CC.SHN shutdown, then the platform transition (ACPI S5
 * soft-off or reset). It does not return on success. It returns a negative
 * storage errno without side effects when the action is unsupported or a
 * shutdown is already in progress. If the platform ignores the transition
 * after storage is quiesced, the calling CPU halts with a diagnostic; it never
 * returns to a system whose filesystems are unmounted. */
#define POWER_ACTION_POWEROFF 1U
#define POWER_ACTION_REBOOT 2U

/* 0 if `action` can be carried out on this platform, else -SE_NOTSUP/-SE_INVAL. */
int power_check(uint32_t action);
int power_request(uint32_t action, const char *reason);
/* 1 once a shutdown has started (new requests are refused with -SE_BUSY). */
int power_shutdown_in_progress(void);

/* Boot command line (Multiboot2 tag 1). Recognised option:
 *   zeroos.shutdown=poweroff-after-cert
 * powers the machine off through power_request() once the storage and
 * session certifications have both finished (used by CI to prove the clean
 * shutdown path end to end). Unknown options are ignored. */
void power_parse_boot_options(uint64_t multiboot_info);
/* Called from the boot monitor loop; non-blocking. */
void power_monitor_step(int certification_complete);
/* Logs the discovered ACPI power capability. */
void power_report_capability(void);
#endif
