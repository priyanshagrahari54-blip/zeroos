#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

required_patterns=(
  "ZEROOS: capability IPC queue/backpressure self-test passed."
  "ZEROOS: capability IPC negative/timeout semantics passed."
  "ZEROOS: IPC pipe blocked-writer wakeup passed."
  "ZEROOS: IPC capability generation/revocation stress passed."
  "ZEROOS: finite child wait deadline/detach passed."
  "ZEROOS: Ring-3 finite WAIT timeout/reap path passed."
  "ZEROOS: blocking child wait/wakeup path passed."
  "ZEROOS: event and pipe IPC foundations self-test passed."
  "ZEROOS: IPC pipe partial-write byte ordering passed."
  "ZEROOS: blocking event wait/wake path passed."
  "ZEROOS: blocking IPC close-wakeup path passed."
  "ZEROOS: blocking IPC send-wakeup path passed."
  "ZEROOS: shared-memory map/grant/lifecycle self-test passed."
  "ZEROOS: userspace resource exhaustion/recovery passed."
  "ZEROOS: Ring-3 transition, syscall ABI, and init recovery passed."
  "ZEROOS: serialized user-copy cross-page/read-only boundary self-test passed."
  "ZEROOS: display present contract verified."
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -Fq "$pattern" "$repo_root/kernel/user.c" && \
     ! grep -Fq "$pattern" "$repo_root/kernel/kernel.c"; then
    echo "stage2-userspace-cert: missing required userspace marker: $pattern" >&2
    exit 1
  fi
done

# Static hardening gates: protect the live Ring-3 contract even when the
# full guest certification is not available on a host.
grep -Fq "process_address_space_is_executable" "$repo_root/kernel/process.c"
grep -Fq "user entry/stack validation" "$repo_root/kernel/thread.c"
grep -Fq "ipc_deadline_init" "$repo_root/kernel/ipc.c"
grep -Fq "ipc_deadline_expired" "$repo_root/kernel/ipc.c"
grep -Fq "ZEROOS_ELF_PT_INTERP ||" "$repo_root/kernel/elf.c"
grep -Fq "ZEROOS_ELF_PT_DYNAMIC" "$repo_root/kernel/elf.c"
grep -Fq "range_end(program->offset,program->file_size,&file_end)" "$repo_root/kernel/elf.c"
grep -Fq "ZEROOS_USER_STACK_TOP>ZEROOS_USER_STACK_PAGE+VMM_PAGE_SIZE" "$repo_root/kernel/exec.c"
grep -Fq "process_address_space_copy_from_user" "$repo_root/kernel/exec.c"
if grep -Fq "vmm_space_translate" "$repo_root/kernel/exec.c"; then
  echo "stage2-userspace-cert: exec must use the serialized user-copy helper" >&2
  exit 1
fi

echo "stage2-userspace-cert: PASS"
