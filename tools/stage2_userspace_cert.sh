#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=cert_lib.sh
source "$repo_root/tools/cert_lib.sh"

# Build tree to certify; the Makefile passes $(BUILD) so BUILD= overrides work.
build_dir="${1:-build}"
kernel_elf="$(cert_resolve_build "$build_dir")/zeroos.elf"

require_marker "stage2-userspace-cert" \
  "$repo_root/kernel/user.c" "$repo_root/kernel/kernel.c" -- \
  "ZEROOS: capability IPC queue/backpressure self-test passed." \
  "ZEROOS: capability IPC negative/timeout semantics passed." \
  "ZEROOS: IPC pipe blocked-writer wakeup passed." \
  "ZEROOS: IPC capability generation/revocation stress passed." \
  "ZEROOS: blocking child wait/wakeup path passed." \
  "ZEROOS: event and pipe IPC foundations self-test passed." \
  "ZEROOS: IPC pipe partial-write byte ordering passed." \
  "ZEROOS: blocking event wait/wake path passed." \
  "ZEROOS: blocking IPC close-wakeup path passed." \
  "ZEROOS: blocking IPC send-wakeup path passed." \
  "ZEROOS: shared-memory map/grant/lifecycle self-test passed." \
  "ZEROOS: userspace resource exhaustion/recovery passed." \
  "ZEROOS: Ring-3 transition, syscall ABI, and init recovery passed." \
  "ZEROOS: display present contract verified."

# Static hardening gates: protect the live Ring-3 contract even when the full
# guest certification is not available on a host.
require_marker "stage2-userspace-cert" "$repo_root/kernel/process.c" -- \
  "process_address_space_is_executable"
require_marker "stage2-userspace-cert" "$repo_root/kernel/thread.c" -- \
  "user entry/stack validation"
require_marker "stage2-userspace-cert" "$repo_root/kernel/ipc.c" -- \
  "ipc_deadline_init" "ipc_deadline_expired"
require_marker "stage2-userspace-cert" "$repo_root/kernel/elf.c" -- \
  "ZEROOS_ELF_PT_INTERP ||" "ZEROOS_ELF_PT_DYNAMIC" \
  "range_end(program->offset,program->file_size,&file_end)"
require_marker "stage2-userspace-cert" "$repo_root/kernel/exec.c" -- \
  "ZEROOS_USER_STACK_TOP>ZEROOS_USER_STACK_PAGE+VMM_PAGE_SIZE"

require_symbols "stage2-userspace-cert" "$kernel_elf" \
  process_address_space_is_executable elf_load_image process_create \
  ipc_send ipc_receive

# Real executable checks: the public/kernel ABI must agree, and the public
# headers must compile against the ABI test.
python3 "$repo_root/userspace/tests/abi_consistency.py"
gcc -std=c11 -Wall -Wextra -Werror -m64 -I"$repo_root/userspace/include" \
  -fsyntax-only "$repo_root/userspace/tests/abi_compile.c"

echo "stage2-userspace-cert: PASS"
