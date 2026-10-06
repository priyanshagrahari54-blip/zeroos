#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=cert_lib.sh
source "$repo_root/tools/cert_lib.sh"

# Build tree to certify; the Makefile passes $(BUILD) so BUILD= overrides work.
build_dir="${1:-build}"
kernel_elf="$(cert_resolve_build "$build_dir")/zeroos.elf"

require_marker "stage4-hardware-cert" \
  "$repo_root/kernel/kernel.c" "$repo_root/kernel/input.c" \
  "$repo_root/kernel/interrupts.c" "$repo_root/kernel/smp.c" -- \
  "ZEROOS: ACPI routing discovery:" \
  "ZEROOS: IRQ controller capability:" \
  "ZEROOS: IRQ ownership layer initialized." \
  "ZEROOS: LAPIC/IOAPIC timer routing activated." \
  "ZEROOS: user fault containment policy armed." \
  "ZEROOS: PS/2 keyboard input stack ready." \
  "ZEROOS: PS/2 mouse pointer ready." \
  "ZEROOS: SMP CPU topology:" \
  "ZEROOS: foundation milestone reached."

require_marker "stage4-hardware-cert" "$repo_root/userspace/desktop" -- \
  "zd_bt_init" "zd_wifi_init"

require_symbols "stage4-hardware-cert" "$kernel_elf" \
  fb_init display_mode_valid fb_present_active

# Real executable checks over the hardware core suites.
require_binary "stage4-hardware-cert" "$build_dir" \
  display-core-test input-core-test usb-core-test driver-core-test dma-test

echo "stage4-hardware-cert: PASS"
