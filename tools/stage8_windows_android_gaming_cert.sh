#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

required_patterns=(
  "zpe_validate"
  "zcompat_translate_path"
  "zcompat_reg_resolve"
  "zcompat_dll_load"
  "zandroid_runtime_wake"
  "zandroid_install_apk"
  "zandroid_translate_path"
  "zandroid_grant_permission"
  "zd_gaming_init"
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -rnF "$pattern" "$repo_root/userspace" >/dev/null; then
    echo "stage8-compat-cert: missing required marker: $pattern" >&2
    exit 1
  fi
done

# Run Windows and Android compatibility test suite
"$repo_root/build/compat-tests" >/dev/null

echo "stage8-windows-android-gaming-cert: PASS"
