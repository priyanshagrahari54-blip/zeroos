#!/usr/bin/env bash
#
# ZEROOS dual-boot installer (host-side, Linux + GRUB 2).
#
# ZeroOS is a self-contained Multiboot2 kernel: the storage probe and the
# Ring-3 desktop session are embedded in the ELF (kernel/storage_probe_image.S,
# kernel/session_probe_image.S), so it needs no initrd, no root partition and
# no bootloader of its own to start. That makes the honest dual-boot path a
# GRUB menu entry on the machine you already boot, rather than a partitioning
# installer: GRUB loads /zeroos/zeroos.elf with `multiboot2` and hands over.
#
# This script writes the kernel next to the host's other kernels and installs
# /etc/grub.d/40_zeroos with two entries - the persistent desktop and the
# finite-probe testing build - then regenerates grub.cfg.
#
# It is deliberately conservative: it never touches partition tables, never
# reformats anything, and every file it writes is listed and removable with
# --uninstall.
#
# Usage:
#   sudo tools/install/dualboot_install.sh [--dry-run] [--force]
#   sudo tools/install/dualboot_install.sh --uninstall
#
set -euo pipefail

INSTALL_DIR=/boot/zeroos
KERNEL_NAME=zeroos.elf
GRUB_FRAGMENT=/etc/grub.d/40_zeroos

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
prod_elf="$repo_root/build/zeroos.elf"
test_elf="$repo_root/build-test/zeroos.elf"

dry_run=0
force=0
uninstall=0

usage() { sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run)   dry_run=1 ;;
    --force)     force=1 ;;
    --uninstall) uninstall=1 ;;
    -h|--help)   usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

run() {
  if [ "$dry_run" -eq 1 ]; then
    echo "  [dry-run] $*"
  else
    "$@"
  fi
}

need_root() {
  # --dry-run only prints what would happen, so it must work unprivileged;
  # that is the whole point of previewing an installer that touches /boot.
  if [ "$dry_run" -eq 1 ]; then
    return 0
  fi
  if [ "$(id -u)" -ne 0 ]; then
    echo "error: this script writes to /boot and /etc/grub.d; re-run with sudo" >&2
    exit 1
  fi
}

require_grub_multiboot2() {
  # GRUB only understands `multiboot2` from 2.02 onwards; an older GRUB would
  # write a menu entry that fails at boot time with "unknown command".
  if ! command -v grub-mkconfig >/dev/null 2>&1; then
    echo "error: grub-mkconfig not found. Install GRUB 2 first:" >&2
    echo "         Debian/Ubuntu: sudo apt-get install grub-common grub-pc-bin" >&2
    exit 1
  fi
  local version
  version="$(grub-mkconfig --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
  if [ -z "$version" ]; then
    echo "warning: could not determine the GRUB version; multiboot2 needs GRUB >= 2.02" >&2
  else
    local major minor
    major="${version%%.*}"; minor="${version##*.}"
    if [ "$major" -lt 2 ] || { [ "$major" -eq 2 ] && [ "$minor" -lt 2 ]; }; then
      echo "error: GRUB $version is too old for multiboot2 (need >= 2.02)" >&2
      exit 1
    fi
    echo "GRUB $version: multiboot2 supported"
  fi
}

write_fragment() {
  cat <<'FRAGMENT'
#!/bin/sh
# ZEROOS dual-boot entry, written by tools/install/dualboot_install.sh.
# Remove with: sudo tools/install/dualboot_install.sh --uninstall
set -e

cat <<'ENTRY'
menuentry 'ZEROOS' --class os {
	insmod all_video
	set gfxpayload=1024x768x32
	multiboot2 /zeroos/zeroos.elf
}
ENTRY

if test -f /zeroos/zeroos-test.elf; then
	cat <<'ENTRY'
menuentry 'ZEROOS (testing mode - finite probes, exits)' --class os {
	insmod all_video
	set gfxpayload=1024x768x32
	multiboot2 /zeroos/zeroos-test.elf
}
ENTRY
fi
FRAGMENT
}

do_uninstall() {
  need_root
  echo "removing ZEROOS dual-boot entry"
  run rm -f "$GRUB_FRAGMENT"
  run rm -rf "$INSTALL_DIR"
  echo "regenerating grub.cfg"
  run update-grub
  echo "done. ZEROOS is no longer in the boot menu."
}

if [ "$uninstall" -eq 1 ]; then
  do_uninstall
  exit 0
fi

echo "ZEROOS dual-boot installer"
echo "  production kernel : $prod_elf"
echo "  testing kernel    : $test_elf"
echo

if [ ! -f "$prod_elf" ]; then
  echo "error: $prod_elf not found. Build it first:" >&2
  echo "         make iso            (production image + kernel)" >&2
  echo "         make iso-test       (optional testing-mode kernel)" >&2
  exit 1
fi

require_grub_multiboot2

if [ ! -f "$test_elf" ]; then
  echo "note: $test_elf absent - only the production entry will be installed."
  echo "      Run 'make iso-test' to add the testing-mode entry."
fi

if [ -e "$GRUB_FRAGMENT" ] && [ "$force" -eq 0 ]; then
  echo "error: $GRUB_FRAGMENT already exists. Re-run with --force to overwrite." >&2
  exit 1
fi

need_root

echo "writing $INSTALL_DIR/$KERNEL_NAME"
run mkdir -p "$INSTALL_DIR"
run cp "$prod_elf" "$INSTALL_DIR/$KERNEL_NAME"
run chmod 0644 "$INSTALL_DIR/$KERNEL_NAME"

if [ -f "$test_elf" ]; then
  echo "writing $INSTALL_DIR/zeroos-test.elf"
  run cp "$test_elf" "$INSTALL_DIR/zeroos-test.elf"
  run chmod 0644 "$INSTALL_DIR/zeroos-test.elf"
fi

echo "writing $GRUB_FRAGMENT"
if [ "$dry_run" -eq 1 ]; then
  echo "  [dry-run] would write the GRUB menu fragment"
else
  write_fragment > "$GRUB_FRAGMENT"
  chmod 0755 "$GRUB_FRAGMENT"
fi

echo "regenerating grub.cfg"
run update-grub

echo
echo "Installed. Reboot and pick ZEROOS from the GRUB menu."
echo "Roll back with: sudo tools/install/dualboot_install.sh --uninstall"
