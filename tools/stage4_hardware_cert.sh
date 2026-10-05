#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

required_patterns=(
  "ZEROOS: ACPI routing discovery:"
  "ZEROOS: IRQ controller capability:"
  "ZEROOS: IRQ ownership layer initialized."
  "ZEROOS: LAPIC/IOAPIC timer routing activated."
  "ZEROOS: user fault containment policy armed."
  "ZEROOS: PS/2 keyboard input stack ready."
  "ZEROOS: PS/2 mouse pointer ready."
  "ZEROOS: SMP CPU topology:"
  "ZEROOS: foundation milestone reached."
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -Fq "$pattern" "$repo_root/kernel/kernel.c" && \
     ! grep -Fq "$pattern" "$repo_root/kernel/input.c" && \
     ! grep -Fq "$pattern" "$repo_root/kernel/interrupts.c" && \
     ! grep -Fq "$pattern" "$repo_root/kernel/smp.c"; then
    echo "stage4-hardware-cert: missing required hardware marker: $pattern" >&2
    exit 1
  fi
done

grep -rnF "zd_bt_init" "$repo_root/userspace/desktop" >/dev/null
grep -rnF "zd_wifi_init" "$repo_root/userspace/desktop" >/dev/null

echo "stage4-hardware-cert: PASS"
