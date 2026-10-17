#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# Build tree to certify; passed by the Makefile so BUILD= overrides are honored.
build_dir="${1:-build}"

required_patterns=(
  "zd_ai_broker_init"
  "zd_ai_submit"
  "zd_ai_drain"
  "zd_nlp_parse"
  "zd_nlp_command"
  "zd_nlp_drive_update"
  "zd_automation_init"
  "zd_governor_init"
  "zd_metrics_init"
  "zd_fps_init"
  "zd_lifecycle_init"
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -rnF "$pattern" "$repo_root/userspace/desktop" >/dev/null; then
    echo "stage9-ai-perf-cert: missing required marker: $pattern" >&2
    exit 1
  fi
done

# A grep only proves the name appears in a file. The local intent engine is
# claimed to run without an injected backend, so require it to actually be
# linked into the desktop test binary that desktop-check executes.
desktop_tests="$repo_root/$build_dir/desktop-tests"
if [ ! -x "$desktop_tests" ]; then
  echo "stage9-ai-perf-cert: missing required binary: $desktop_tests" >&2
  exit 1
fi
# A gate that reads a stale artifact is not a gate. When this script is run
# through `make check` the desktop-check prerequisite guarantees freshness,
# but it is also run directly, so refuse to certify a binary older than the
# sources it is supposed to contain.
for src in "$repo_root"/userspace/desktop/src/*.c; do
  if [ "$src" -nt "$desktop_tests" ]; then
    echo "stage9-ai-perf-cert: $desktop_tests is older than $src; rebuild it" >&2
    exit 1
  fi
done

for symbol in zd_nlp_parse zd_nlp_plan zd_nlp_command zd_nlp_drive_update; do
  # No `grep -q` in a pipeline: it SIGPIPEs nm and `set -o pipefail` would
  # report that as a missing symbol.
  if ! nm "$desktop_tests" 2>/dev/null | grep -F " T $symbol" >/dev/null 2>&1; then
    echo "stage9-ai-perf-cert: $symbol is not linked into $desktop_tests" >&2
    exit 1
  fi
done

# Run resource core and scheduler stress verification
"$repo_root/$build_dir/resource-core-test" >/dev/null

echo "stage9-zero-ai-ecosystem-performance-cert: PASS"
