# Shared helpers for the ZEROOS stage certification gates.
#
# Sourced, not executed. Every helper fails the gate loudly and explains what
# it expected, so a gate that passes is passing for a stated reason.

cert_repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[1]}")/.." && pwd)"

# cert_resolve_build <build_dir>
#   Accepts either a path relative to the repository root (what the Makefile
#   passes) or an absolute path, and echoes the resolved directory. Without
#   this an absolute BUILD= produced "$repo//tmp/whatever".
cert_resolve_build() {
  case "$1" in
    /*) printf '%s' "$1" ;;
    *)  printf '%s' "$cert_repo_root/$1" ;;
  esac
}

# require_marker <label> <file>... -- <pattern>...
#   Original behaviour: the pattern must appear in at least one of the files.
#   Kept, because the marker strings are the serial contract the guest boot
#   harness matches on.
require_marker() {
  local label="$1"; shift
  local files=()
  while [ "$1" != "--" ]; do files+=("$1"); shift; done
  shift
  local pattern
  for pattern in "$@"; do
    local found=0 f
    for f in "${files[@]}"; do
      # -r so a directory argument is searched recursively; the original gates
      # used `grep -rnF` for the userspace trees and plain `grep -Fq` for
      # single files, and both forms are passed here.
      if grep -rqF "$pattern" "$f"; then found=1; break; fi
    done
    if [ "$found" -eq 0 ]; then
      echo "$label: missing required marker: $pattern" >&2
      return 1
    fi
  done
}

# require_symbols <label> <elf> <symbol>...
#   Stronger than a source grep: the symbol must be present in the linked
#   image, which proves the code was compiled and actually linked in rather
#   than merely written somewhere in the tree.
require_symbols() {
  local label="$1"; shift
  local elf="$1"; shift
  if [ ! -f "$elf" ]; then
    echo "$label: linked kernel image not found: $elf" >&2
    return 1
  fi
  local missing=() sym
  local present
  present="$(nm -g --defined-only "$elf" 2>/dev/null || nm --defined-only "$elf")"
  for sym in "$@"; do
    if ! grep -qE "(^|[[:space:]])${sym}\$" <<<"$present"; then
      missing+=("$sym")
    fi
  done
  if [ "${#missing[@]}" -gt 0 ]; then
    echo "$label: symbols absent from $elf: ${missing[*]}" >&2
    return 1
  fi
}

# require_binary <label> <build_dir> <name>...
#   Runs each named host test binary and fails on a non-zero exit.
require_binary() {
  local label="$1"; shift
  local build_dir="$1"; shift
  local name
  for name in "$@"; do
    local path
    path="$(cert_resolve_build "$build_dir")/$name"
    if [ ! -x "$path" ]; then
      echo "$label: test binary not built: $path" >&2
      echo "$label: build it first (hardware-core-test / desktop-check / compat-check)" >&2
      return 1
    fi
    if ! "$path" >/dev/null; then
      echo "$label: test binary failed: $path" >&2
      return 1
    fi
  done
}
