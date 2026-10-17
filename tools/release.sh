#!/usr/bin/env bash
#
# ZEROOS release builder: produces the production and testing ISOs and proves
# they boot.
#
# Run this on a real host, not in a container without an emulator. It is the
# only path to a bootable image: the El Torito boot image comes from GRUB, and
# nothing in this repository can synthesise one.
#
# What it does, in order:
#   1. checks the toolchain (installs it with --install-toolchain)
#   2. runs `make check` - all ten stage gates plus the host suites
#   3. builds build/zeroos.iso        (persistent desktop)
#   4. builds build-test/zeroos.iso   (finite probes, exits - for testing)
#   5. verifies both images (ISO 9660 / El Torito structure, Multiboot2 header)
#   6. boots both in QEMU and asserts every milestone in boot_milestones.txt
#
# It stops at the first failure and says which step failed. Nothing is reported
# as passing unless the command that proves it exited zero.
#
# Usage:
#   tools/release.sh [--install-toolchain] [--skip-boot-test] [--iterations N]
#
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

install_toolchain=0
skip_boot_test=0
iterations=3

while [ $# -gt 0 ]; do
  case "$1" in
    --install-toolchain) install_toolchain=1 ;;
    --skip-boot-test)    skip_boot_test=1 ;;
    --iterations)        iterations="$2"; shift ;;
    -h|--help)           sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "release: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

step() { echo; echo "===== $* ====="; }
die()  { echo; echo "release: FAILED at: $1" >&2; exit 1; }

# ---------------------------------------------------------------- toolchain
step "1/7 toolchain"
need=(gcc ld python3)
bootable_toolchain=(grub-mkrescue xorriso qemu-system-x86_64)
missing=()
for t in "${need[@]}"; do
  command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
[ "${#missing[@]}" -eq 0 ] || die "essential tools missing: ${missing[*]}"

missing_boot=()
for t in "${bootable_toolchain[@]}"; do
  command -v "$t" >/dev/null 2>&1 || missing_boot+=("$t")
done

if [ "${#missing_boot[@]}" -gt 0 ]; then
  if [ "$install_toolchain" -eq 1 ]; then
    echo "installing: ${missing_boot[*]}"
    if command -v apt-get >/dev/null 2>&1; then
      sudo apt-get update
      sudo apt-get install -y grub-pc-bin grub-common xorriso mtools qemu-system-x86
    elif command -v dnf >/dev/null 2>&1; then
      sudo dnf install -y grub2-tools xorriso qemu-system-x86-core
    else
      die "no supported package manager; install manually: ${missing_boot[*]}"
    fi
    missing_boot=()
    for t in "${bootable_toolchain[@]}"; do
      command -v "$t" >/dev/null 2>&1 || missing_boot+=("$t")
    done
  fi
  if [ "${#missing_boot[@]}" -gt 0 ]; then
    cat >&2 <<EOF
release: these tools are missing: ${missing_boot[*]}

Without grub-mkrescue the image cannot be made bootable, and without QEMU it
cannot be proven to boot. Re-run with --install-toolchain to install them, or
install them yourself and re-run.

To build the (non-bootable) images anyway:
    make iso iso-test
EOF
    exit 1
  fi
fi
for t in "${need[@]}" "${bootable_toolchain[@]}"; do
  echo "  ok  $t -> $(command -v "$t")"
done

# ---------------------------------------------------------------- gates
step "2/7 make check (all ten stage gates)"
make check || die "make check"

# ---------------------------------------------------------------- images
step "3/7 release hygiene: freestanding, provenance, determinism"
make repro-check

step "4/7 production image"
make iso || die "make iso"

step "5/7 testing image"
make iso-test || die "make iso-test"

# ---------------------------------------------------------------- structure
step "6/7 image structure"
# --require-bios-boot: the El Torito platform id must be 0x00 (x86 BIOS). A
# pre-UEFI machine cannot boot an EFI-only image no matter how correct its
# ISO 9660 layout is, and that failure is invisible without this check.
python3 tools/verify_iso.py build/zeroos.iso --require-bios-boot \
  || die "verify_iso (production) is not BIOS-bootable"
python3 tools/verify_iso.py build-test/zeroos.iso --require-bios-boot \
  || die "verify_iso (testing) is not BIOS-bootable"
python3 tools/verify_multiboot2.py build/zeroos.elf --expect-wxhxb 1024x768x32 \
  || die "verify_multiboot2 (production)"
python3 tools/verify_multiboot2.py build-test/zeroos.elf --expect-wxhxb 1024x768x32 \
  || die "verify_multiboot2 (testing)"

# ---------------------------------------------------------------- boot
if [ "$skip_boot_test" -eq 1 ]; then
  echo
  echo "release: boot test SKIPPED by --skip-boot-test."
  echo "release: the images are structurally valid but NOT proven to boot."
  exit 0
fi

step "7/7 boot test - production image"
bash tools/boot_test.sh --build-dir build --iterations "$iterations" \
  || die "boot_test (production)"

step "7/7 boot test - testing image"
# The certification flavour runs its finite probes and exits, so one boot is
# enough evidence that it reached the end of the probe sequence.
bash tools/boot_test.sh --build-dir build-test --iterations 1 \
  || die "boot_test (testing)"

cat <<EOF

release: COMPLETE

  production ISO : $repo_root/build/zeroos.iso
                   persistent desktop session
  testing ISO    : $repo_root/build-test/zeroos.iso
                   finite probes, exits after the probe sequence

Both were built by grub-mkrescue, verified structurally, and booted in QEMU
with every milestone in tools/boot_milestones.txt asserted.

Write to a USB stick (DESTROYS the target device - check the device name):
    sudo dd if=build/zeroos.iso of=/dev/sdX bs=4M status=progress oflag=sync

Install alongside your current OS:
    sudo tools/install/dualboot_install.sh --dry-run
    sudo tools/install/dualboot_install.sh
EOF
