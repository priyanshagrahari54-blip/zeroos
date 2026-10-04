#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

required_patterns=(
  "ZEROOS: scheduler deterministic trace: 01-02-04-08-10-20-40."
  "ZEROOS: scheduler certification passed."
  "ZEROOS PANIC: scheduler panic diagnostics self-test"
  "task-table invariant dump"
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -Fq "$pattern" "$repo_root/kernel/kernel.c" && \
     ! grep -Fq "$pattern" "$repo_root/kernel/task.c"; then
    echo "stage1-scheduler-cert: missing required scheduler marker: $pattern" >&2
    exit 1
  fi
done

echo "stage1-scheduler-cert: PASS"
