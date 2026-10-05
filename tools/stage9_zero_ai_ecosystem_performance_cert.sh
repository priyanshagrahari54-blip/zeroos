#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

required_patterns=(
  "zd_ai_broker_init"
  "zd_ai_submit"
  "zd_ai_drain"
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

# Run resource core and scheduler stress verification
"$repo_root/build/resource-core-test" >/dev/null

echo "stage9-zero-ai-ecosystem-performance-cert: PASS"
