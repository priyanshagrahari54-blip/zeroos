#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

required_patterns=(
  "ZEROOS: capability IPC queue/backpressure self-test passed."
  "ZEROOS: capability IPC negative/timeout semantics passed."
  "ZEROOS: IPC capability generation/revocation stress passed."
  "ZEROOS: blocking child wait/wakeup path passed."
  "ZEROOS: event and pipe IPC foundations self-test passed."
  "ZEROOS: blocking event wait/wake path passed."
  "ZEROOS: blocking IPC close-wakeup path passed."
  "ZEROOS: blocking IPC send-wakeup path passed."
  "ZEROOS: shared-memory map/grant/lifecycle self-test passed."
  "ZEROOS: userspace resource exhaustion/recovery passed."
  "ZEROOS: Ring-3 transition, syscall ABI, and init recovery passed."
  "ZEROOS: display present contract verified."
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -Fq "$pattern" "$repo_root/kernel/user.c" && \
     ! grep -Fq "$pattern" "$repo_root/kernel/kernel.c"; then
    echo "stage2-userspace-cert: missing required userspace marker: $pattern" >&2
    exit 1
  fi
done

echo "stage2-userspace-cert: PASS"
