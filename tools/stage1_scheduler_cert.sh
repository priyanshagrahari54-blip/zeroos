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
  "ZEROOS: SMP startup recovery contract self-test passed." \
  "ZEROOS: scheduler context bounds self-test passed." \
  "ZEROOS: scheduler policy and affinity self-test passed." \
  "ZEROOS: timer-only preemption stress passed." \
  "ZEROOS: scheduler fairness and latency stress passed." \
  "ZEROOS: zombie reaping and slot-reuse stress passed." \
  "ZEROOS: interrupt-frame ownership invariant verified." \
  "ZEROOS: scheduler certification passed." \
  "ZEROOS: per-CPU scheduler ownership verified." \
  "ZEROOS: CPU hot-offline queue evacuation and parking passed." \
  "ZEROOS: per-task kernel-stack/TSS handoff self-test passed." \
  "ZEROOS PANIC: scheduler panic diagnostics self-test" \
  "task-table invariant dump"

# Stronger than the markers: these must be in the linked image, proving the
# scheduler is compiled and linked rather than merely present in source.
require_symbols "stage1-scheduler-cert" "$kernel_elf" \
  scheduler_init scheduler_start scheduler_tick scheduler_yield \
  scheduler_tick_remote scheduler_sleep_until scheduler_sleep_ticks \
  task_block task_scheduler_tick task_debug_validate task_frame_resume_count \
  task_cpu_offline task_scheduler_task_cpu_mask

echo "stage1-scheduler-cert: PASS"
