#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=cert_lib.sh
source "$repo_root/tools/cert_lib.sh"

# Build tree to certify; the Makefile passes $(BUILD) so BUILD= overrides work.
build_dir="${1:-build}"
kernel_elf="$(cert_resolve_build "$build_dir")/zeroos.elf"

require_marker "stage5-desktop-cert" \
  "$repo_root/kernel/kernel.c" "$repo_root/kernel/session.c" \
  "$repo_root/userspace/session/session.c" -- \
  "ZEROOS: input blocking wait/wake path passed." \
  "ZEROOS: display present contract verified." \
  "ZEROOS: session shell process started." \
  "ZEROOS: session shell process reaped cleanly." \
  "ZEROOS: session compositor frame path passed." \
  "ZEROOS: session scanout present contract passed."

require_symbols "stage5-desktop-cert" "$kernel_elf" \
  session_start session_finished

# Real executable check: the full desktop/compositor/UI suite, which also
# covers the Ring-3 session lifecycle paths.
require_binary "stage5-desktop-cert" "$build_dir" desktop-tests

echo "stage5-desktop-cert: PASS"
