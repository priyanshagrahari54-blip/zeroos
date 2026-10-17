#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=cert_lib.sh
source "$repo_root/tools/cert_lib.sh"

# Build tree to certify; the Makefile passes $(BUILD) so BUILD= overrides work.
build_dir="${1:-build}"
kernel_elf="$(cert_resolve_build "$build_dir")/zeroos.elf"

# Marker strings are the serial contract the guest boot harness matches on.
require_marker "stage1-scheduler-cert" \
  "$repo_root/kernel/kernel.c" "$repo_root/kernel/task.c" -- \
  "ZEROOS: scheduler context bounds self-test passed." \
  "ZEROOS: scheduler deterministic trace: 01-02-04-08-10-20-40." \
  "ZEROOS: scheduler certification passed." \
  "ZEROOS PANIC: scheduler panic diagnostics self-test" \
  "task-table invariant dump"

# Stronger than the markers: these must be in the linked image, proving the
# scheduler is compiled and linked rather than merely present in source.
require_symbols "stage1-scheduler-cert" "$kernel_elf" \
  scheduler_init scheduler_start scheduler_tick scheduler_yield \
  scheduler_tick_remote task_block

echo "stage1-scheduler-cert: PASS"
