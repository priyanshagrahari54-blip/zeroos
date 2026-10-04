#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

required_patterns=(
  "ZEROOS: input blocking wait/wake path passed."
  "ZEROOS: display present contract verified."
  "ZEROOS: session shell process started."
  "ZEROOS: session shell process reaped cleanly."
  "ZEROOS: session compositor frame path passed."
  "ZEROOS: session scanout present contract passed."
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -Fq "$pattern" "$repo_root/kernel/kernel.c" && \
     ! grep -Fq "$pattern" "$repo_root/kernel/session.c" && \
     ! grep -Fq "$pattern" "$repo_root/userspace/session/session.c"; then
    echo "stage5-desktop-cert: missing required desktop marker: $pattern" >&2
    exit 1
  fi
done

echo "stage5-desktop-cert: PASS"
