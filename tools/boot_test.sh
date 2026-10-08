#!/usr/bin/env bash
#
# ZEROOS real boot test.
#
# Boots the ISO in QEMU and asserts the guest serial log against every boot
# milestone in tools/boot_milestones.txt. This is the only gate in the project
# that exercises the kernel as it actually runs - the host suites and the stage
# certifications never execute kernel code, so a kernel that compiles and links
# but hangs at boot passes all of them and fails here.
#
# The milestone list is shared with CI (tools/boot_milestones.txt) so the two
# cannot drift.
#
# Usage:
#   tools/boot_test.sh [--build-dir DIR] [--iterations N] [--smp N] [--timeout S]
#   tools/boot_test.sh --serial-log FILE        # assert an existing log, no QEMU
#
# --serial-log exists so the assertion logic itself can be exercised on a host
# without QEMU; it is not a substitute for a real boot.
#
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
milestones="$repo_root/tools/boot_milestones.txt"

build_dir=build
iterations=3
smp=2
timeout_s=45
serial_log=""

while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir)  build_dir="$2"; shift 2 ;;
    --iterations) iterations="$2"; shift 2 ;;
    --smp)        smp="$2"; shift 2 ;;
    --timeout)    timeout_s="$2"; shift 2 ;;
    --serial-log) serial_log="$2"; shift 2 ;;
    -h|--help)    sed -n '2,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "boot_test: unknown option: $1" >&2; exit 2 ;;
  esac
done

case "$build_dir" in
  /*) build_root="$build_dir" ;;
  *)  build_root="$repo_root/$build_dir" ;;
esac

[ -f "$milestones" ] || { echo "boot_test: milestone list missing: $milestones" >&2; exit 1; }

# assert_log <log> <label>
#   Checks every milestone. Reports all missing ones, not just the first, so a
#   failing boot shows how far the kernel actually got.
assert_log() {
  local log="$1" label="$2"
  local total=0 missing=0 violated=0 line
  local -a missing_list=() violated_list=()

  [ -f "$log" ] || { echo "$label: serial log not produced: $log" >&2; return 1; }

  while IFS= read -r line; do
    case "$line" in ''|'#'*) continue ;; esac
    if [ "${line#never:}" != "$line" ]; then
      # A negative gate: presence means the boot failed or panicked.
      if grep -q "${line#never:}" "$log"; then
        violated=$((violated + 1))
        violated_list+=("${line#never:}")
      fi
      continue
    fi
    total=$((total + 1))
    if [ "${line#re:}" != "$line" ]; then
      grep -Eq "${line#re:}" "$log" || { missing=$((missing + 1)); missing_list+=("re: ${line#re:}"); }
    else
      # BRE, not -F: CI greps these with plain `grep -q`, so `.*` is a wildcard
      # (e.g. "PIT timer configured at .*clocksource="), not literal text.
      grep -q "$line" "$log" || { missing=$((missing + 1)); missing_list+=("$line"); }
    fi
  done < "$milestones"

  if [ "$missing" -gt 0 ] || [ "$violated" -gt 0 ]; then
    echo "$label: FAIL - $missing of $total milestones absent, $violated failure marker(s) present" >&2
    local m
    for m in ${missing_list[@]+"${missing_list[@]}"}; do echo "    missing: $m" >&2; done
    for m in ${violated_list[@]+"${violated_list[@]}"}; do echo "    failure marker present: $m" >&2; done
    echo "    --- last 20 lines of $log ---" >&2
    tail -20 "$log" >&2 || true
    return 1
  fi
  echo "$label: PASS - all $total milestones present, no failure markers"
}

# Offline mode: assert a log somebody else produced.
if [ -n "$serial_log" ]; then
  assert_log "$serial_log" "boot_test (offline log check)"
  exit 0
fi

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  cat >&2 <<'EOF'
boot_test: qemu-system-x86_64 not found.

This gate is a real boot, and there is no way to fake it: without an x86-64
system emulator nothing here executes kernel code. Install it and re-run:

    Debian/Ubuntu : sudo apt-get install qemu-system-x86
    Fedora        : sudo dnf install qemu-system-x86-core

To check only the assertion logic against a log captured elsewhere:

    tools/boot_test.sh --serial-log path/to/serial.log
EOF
  exit 1
fi

iso="$build_root/zeroos.iso"
[ -f "$iso" ] || { echo "boot_test: image not found: $iso (run: make iso)" >&2; exit 1; }

# The production image blocks on the interactive input wait and never exits, so
# the boot test uses QEMU's timeout as its stop condition - exactly as CI does.
for iteration in $(seq 1 "$iterations"); do
  echo "===== ZEROOS boot certification iteration ${iteration}/${iterations} ====="
  timeout "${timeout_s}s" qemu-system-x86_64 \
    -smp "$smp" \
    -cdrom "$iso" \
    -serial "file:$build_root/serial.log" \
    -display none -no-reboot -no-shutdown \
    -debugcon "file:$build_root/debug.log" \
    -global isa-debugcon.iobase=0xe9 \
    -d int,guest_errors,cpu_reset \
    -D "$build_root/qemu.log" || true
  assert_log "$build_root/serial.log" "boot_test iteration ${iteration}"
done

# boot/boot.S writes 'B' at the 32-bit entry and 'L' in long mode; both must
# have reached the debug port, which proves the transition really happened.
if [ -f "$build_root/debug.log" ]; then
  for marker in B L; do
    grep -q "$marker" "$build_root/debug.log" || {
      echo "boot_test: debug port marker '$marker' missing from $build_root/debug.log" >&2
      exit 1
    }
  done
  echo "boot_test: debug port markers B (32-bit entry) and L (long mode) present"
fi

echo "boot_test: PASS - $iterations/$iterations boots reached every milestone"
