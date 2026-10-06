#!/usr/bin/env bash
#
# Release hygiene checks over the built artifacts. Closes four items from
# docs/ZEROOS_MASTER/ZEROOS_REMAINING_GAP_CLOSURE.md section A that can be
# settled without hardware:
#
#   * Prevent accidental host-library linkage.
#   * Record source revision inside build artifacts.
#   * Verify deterministic filesystem/image creation.
#   * Verify deterministic link ordering.
#
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-build}"
case "$build_dir" in
  /*) build_root="$build_dir" ;;
  *)  build_root="$repo_root/$build_dir" ;;
esac

elf="$build_root/zeroos.elf"
[ -f "$elf" ] || { echo "repro-check: kernel image missing: $elf" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail=0
note() { echo "  ok   $*"; }
bad()  { echo "  FAIL $*" >&2; fail=1; }

# 1. Freestanding: a kernel ELF must not be dynamically linked and must not
#    resolve any symbol from a host C library.
if readelf -d "$elf" 2>/dev/null | grep -E 'NEEDED|SONAME' >/dev/null 2>&1; then
  bad "kernel image has a dynamic section (NEEDED/SONAME) - it links a host library"
else
  note "no dynamic section: kernel is freestanding"
fi
if readelf -h "$elf" | awk '/Type:/{print $2}' | grep DYN >/dev/null 2>&1; then
  bad "kernel image type is DYN (shared object), expected EXEC"
else
  note "image type: $(readelf -h "$elf" | awk '/Type:/{print $2}')"
fi
libc_syms="$(nm -u "$elf" 2>/dev/null | grep -cE '\b(printf|memcpy|memset|malloc|free|strlen|strcmp)\b' || true)"
if [ "$libc_syms" -gt 0 ]; then
  bad "kernel has $libc_syms undefined host-libc symbols"
else
  note "no undefined host-libc symbols"
fi

# 2. Provenance: the revision the image was built from must be inside it.
rev="$(git -C "$repo_root" rev-parse --short HEAD 2>/dev/null || echo unknown)"
dirty=""
if [ -n "$(git -C "$repo_root" status --porcelain 2>/dev/null)" ]; then dirty="-dirty"; fi
# NB: no `grep -q` here - it exits at the first match, SIGPIPEs `strings`, and
# `set -o pipefail` turns that into a false failure.
if strings "$elf" | grep -F "${rev}${dirty}" >/dev/null 2>&1; then
  note "source revision recorded in image: ${rev}${dirty}"
else
  bad "source revision ${rev}${dirty} not found in the image"
fi

# 3. Deterministic link ordering: the object list must be explicit and stable.
# Read the variable twice; `make -p` output is captured to a file first so the
# awk exit does not SIGPIPE `make` (which `pipefail` would turn into a failure).
dump_vars() { make -C "$repo_root" BUILD="$build_dir" -p 2>/dev/null > "$tmp/mk.$$" || true
              awk -F' := ' '/^KERNEL_OBJS/{print $2; exit}' "$tmp/mk.$$"; }
objs_a="$(dump_vars)"
objs_b="$(dump_vars)"
if [ -n "$objs_a" ] && [ "$objs_a" = "$objs_b" ]; then
  note "link order is explicit and stable ($(echo "$objs_a" | wc -w) objects)"
else
  bad "link order is not reproducible across invocations"
fi

# 4. Deterministic image creation: same inputs, same SOURCE_DATE_EPOCH, same bytes.
if [ -d "$build_root/iso" ]; then
  SOURCE_DATE_EPOCH=1700000000 python3 "$repo_root/tools/build_iso.py" \
    "$tmp/a.iso" "$build_root/iso" --no-boot >/dev/null
  sleep 1
  SOURCE_DATE_EPOCH=1700000000 python3 "$repo_root/tools/build_iso.py" \
    "$tmp/b.iso" "$build_root/iso" --no-boot >/dev/null
  if cmp -s "$tmp/a.iso" "$tmp/b.iso"; then
    note "image creation is deterministic (two builds byte-identical)"
  else
    bad "image creation is not deterministic"
  fi
else
  echo "  skip $build_root/iso absent - run 'make iso' to check image determinism"
fi

echo
if [ "$fail" -ne 0 ]; then
  echo "repro-check: FAIL"
  exit 1
fi
echo "repro-check: PASS"
