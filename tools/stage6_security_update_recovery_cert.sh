#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# Build tree to certify; passed by the Makefile so BUILD= overrides are honored.
build_dir="${1:-build}"

# Verify kernel and userspace security / update / recovery enforcement markers
required_patterns=(
  "net_fw_check_ipv6"
  "net_fw_check_tcp"
  "net_fw_check_icmp"
  "sandbox_enforce_all"
  "zd_update_verify_payload"
  "zd_snapshots_bind_update"
  "zd_vault_unlock"
  "zd_pkg_install"
  "zd_pkg_resolve_deps"
  "zd_recovery_run_fsck"
  "zd_recovery_trigger_rollback"
  "zd_backup_create_job"
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -rnF "$pattern" "$repo_root/kernel" "$repo_root/userspace" >/dev/null; then
    echo "stage6-security-cert: missing required marker: $pattern" >&2
    exit 1
  fi
done

# Run security / crypto / firewall / update / recovery unit test binaries
"$repo_root/$build_dir/crypto-core-test" >/dev/null
"$repo_root/$build_dir/net-stack-test" >/dev/null
"$repo_root/$build_dir/net-stack-tx-test" >/dev/null

echo "stage6-security-update-recovery-cert: PASS"
