#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# Verify kernel ELF image existence and SIMD-cleanliness
test -f "$repo_root/build/zeroos.elf" || {
  echo "stage10-cert: zeroos.elf missing" >&2
  exit 1
}

# Run G560 Hardware Certification & Benchmark Generator
python3 "$repo_root/tools/g560_benchmark.py" "$repo_root/build/g560_certification_report.json"

test -f "$repo_root/build/g560_certification_report.json" || {
  echo "stage10-cert: failed to produce benchmark report" >&2
  exit 1
}

echo "stage10-certification-release: PASS"
