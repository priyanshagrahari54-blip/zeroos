#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=cert_lib.sh
source "$repo_root/tools/cert_lib.sh"

# Build tree to certify; the Makefile passes $(BUILD) so BUILD= overrides work.
build_dir="${1:-build}"
kernel_elf="$(cert_resolve_build "$build_dir")/zeroos.elf"

require_marker "stage3-storage-cert" \
  "$repo_root/kernel/storage/storage.c" "$repo_root/kernel/kernel.c" -- \
  "ZEROOS: storage block layer scheduler/fault/timeout self-test passed." \
  "ZEROOS: storage GPT validation/recovery self-test passed." \
  "ZEROOS: storage VFS/ZJFS/page-cache self-test passed." \
  "ZEROOS: storage crash-consistency (power-cut/replay/fsck) self-test passed." \
  "ZEROOS: storage stack certification passed" \
  "ZEROOS: storage Stage 3 certification complete."

require_symbols "stage3-storage-cert" "$kernel_elf" \
  block_device_alloc block_add_partition block_complete

# Real executable check: build a GPT/ZJFS image on the host, write and read
# data back, corrupt the primary superblock and confirm fsck recovers it.
bash "$repo_root/tools/storage/host_selftest.sh" >/dev/null

echo "stage3-storage-cert: PASS"
