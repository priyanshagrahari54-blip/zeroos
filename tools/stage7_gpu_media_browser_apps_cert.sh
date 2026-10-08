#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# Build tree to certify; passed by the Makefile so BUILD= overrides are honored.
build_dir="${1:-build}"

required_patterns=(
  "zd_compositor_present"
  "zd_compositor_damage"
  "zd_browser_open"
  "zd_browser_event"
  "zd_media_init"
  "zd_perf_center_assess"
  "zd_term_write"
  "zd_fm_init"
  "zd_notes_init"
  "zd_pdf_open"
  "zd_study_init"
  "zd_capsule_post"
  "zd_cc_select_section"
)

for pattern in "${required_patterns[@]}"; do
  if ! grep -rnF "$pattern" "$repo_root/userspace/desktop" >/dev/null; then
    echo "stage7-gpu-media-cert: missing required marker: $pattern" >&2
    exit 1
  fi
done

# Run desktop display / audio / input core test binaries
"$repo_root/$build_dir/display-core-test" >/dev/null
"$repo_root/$build_dir/audio-core-test" >/dev/null
"$repo_root/$build_dir/input-core-test" >/dev/null

echo "stage7-gpu-media-browser-apps-cert: PASS"
