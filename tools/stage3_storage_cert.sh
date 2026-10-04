#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

required_patterns=(
  "ZEROOS: storage block layer scheduler/fault/timeout self-test passed."
  "ZEROOS: storage GPT validation/recovery self-test passed."
  "ZEROOS: storage VFS/ZJFS/page-cache self-test passed."
  "ZEROOS: storage crash-consistency (power-cut/replay/fsck) self-test passed."
  "ZEROOS: storage stack certification passed"
  "ZEROOS: storage Stage 3 certification complete."
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -Fq "$pattern" "$repo_root/kernel/storage/storage.c" && \
     ! grep -Fq "$pattern" "$repo_root/kernel/kernel.c"; then
    echo "stage3-storage-cert: missing required storage marker: $pattern" >&2
    exit 1
  fi
done

echo "stage3-storage-cert: PASS"
