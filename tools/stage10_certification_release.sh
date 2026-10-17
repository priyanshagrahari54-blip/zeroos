#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# Build tree to certify; passed by the Makefile so BUILD= overrides are honored.
build_dir="${1:-build}"

# Verify kernel ELF image existence and SIMD-cleanliness
test -f "$repo_root/$build_dir/zeroos.elf" || {
  echo "stage10-cert: zeroos.elf missing" >&2
  exit 1
}

# Run the G560 release gate. It compares every declared figure against its
# numeric target and exits non-zero when one is out of range, so the exit code
# has to propagate - the old version discarded it and always reported PASS.
#
# ZEROOS_MEASUREMENTS may point at a JSON file of real observed values (for
# example extracted from a QEMU serial log). Without it the gate still checks
# internal consistency but records the figures as declared, not measured, and
# refuses to stamp the subsystems CERTIFIED.
benchmark_args=("$repo_root/$build_dir/g560_certification_report.json")
if [ -n "${ZEROOS_MEASUREMENTS:-}" ]; then
  test -f "$ZEROOS_MEASUREMENTS" || {
    echo "stage10-cert: ZEROOS_MEASUREMENTS=$ZEROOS_MEASUREMENTS not found" >&2
    exit 1
  }
  benchmark_args+=(--measurements "$ZEROOS_MEASUREMENTS")
fi
python3 "$repo_root/tools/g560_benchmark.py" "${benchmark_args[@]}"

test -f "$repo_root/$build_dir/g560_certification_report.json" || {
  echo "stage10-cert: failed to produce benchmark report" >&2
  exit 1
}

echo "stage10-certification-release: PASS"
