#!/usr/bin/env python3
"""ZEROOS ISO bootability verifier.

Independently parses a produced ISO image against ECMA-119 (ISO 9660) and the
El Torito bootable-CD specification. It does not trust the builder: it reads the
bytes back off the image.

A bootable ZeroOS ISO must have all of:
  1. a Primary Volume Descriptor with the CD001 identifier and a 2048-byte
     logical block size,
  2. a volume descriptor set terminator,
  3. a walkable root directory containing the kernel and the GRUB config,
  4. a Boot Record Volume Descriptor ("EL TORITO SPECIFICATION") pointing at a
     boot catalog,
  5. a boot catalog with a valid header entry (key bytes 0x55 0xAA) and an
     initial/default entry marked bootable (0x88),
  6. a boot image at the LBA the catalog names, which is non-empty.

Exit status is 0 only when every required check passes.

Usage: verify_iso.py <image.iso> [--kernel-path /BOOT/ZEROOS.ELF;1] ...
"""

import struct
import sys

SECTOR = 2048


class Report:
    def __init__(self, allow_unbootable=False):
        self.failures = []
        self.warnings = []
        self.notes = []
        self.allow_unbootable = allow_unbootable

    def ok(self, msg):
        self.notes.append(f"  ok   {msg}")

    def warn(self, msg):
        self.warnings.append(msg)
        self.notes.append(f"  WARN {msg}")

    def fail(self, msg, bootability=False):
        """Record a failure. Bootability failures can be downgraded to a
        warning by --allow-unbootable, for hosts that have no GRUB toolchain
        and only want the ISO 9660 layout validated."""
        if bootability and self.allow_unbootable:
            self.warnings.append(msg)
            self.notes.append(f"  WARN {msg} (bootability waived by --allow-unbootable)")
            return
        self.failures.append(msg)
        self.notes.append(f"  FAIL {msg}")

    def check(self, cond, ok_msg, fail_msg):
        if cond:
            self.ok(ok_msg)
        else:
            self.fail(fail_msg)
        return bool(cond)


def both_endian_32(buf, off):
    """ISO 9660 stores extents as little-endian followed by big-endian."""
    return struct.unpack_from("<I", buf, off)[0]


def both_endian_16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def read_dir_records(blob):
    """Yield (name, extent_lba, data_len, flags) for a directory extent."""
    pos = 0
    out = []
    while pos < len(blob):
        rec_len = blob[pos]
        if rec_len == 0:
            # Padding to the end of the sector.
            nxt = ((pos // SECTOR) + 1) * SECTOR
            if nxt <= pos or nxt >= len(blob):
                break
            pos = nxt
            continue
        if pos + rec_len > len(blob) or rec_len < 33:
            break
        extent = both_endian_32(blob, pos + 2)
        data_len = both_endian_32(blob, pos + 10)
        flags = blob[pos + 25]
        name_len = blob[pos + 32]
        name = blob[pos + 33:pos + 33 + name_len]
        out.append((name, extent, data_len, flags))
        pos += rec_len
    return out


def verify(path, required_files, allow_unbootable=False):
    rep = Report(allow_unbootable)
    with open(path, "rb") as fh:
        img = fh.read()

    print(f"verify_iso: {path} ({len(img)} bytes, {len(img)//SECTOR} sectors)")

    if len(img) < 17 * SECTOR:
        rep.fail("image smaller than the 16-sector system area + PVD")
        return rep

    # ---- Volume descriptor set ------------------------------------------
    pvd_off = None
    boot_record_off = None
    terminator_seen = False
    sec = 16
    while (sec + 1) * SECTOR <= len(img):
        base = sec * SECTOR
        vd_type = img[base]
        ident = img[base + 1:base + 6]
        if ident != b"CD001":
            break
        if vd_type == 1 and pvd_off is None:
            pvd_off = base
        elif vd_type == 0 and boot_record_off is None:
            boot_record_off = base
        elif vd_type == 255:
            terminator_seen = True
            break
        sec += 1

    if not rep.check(pvd_off is not None, "Primary Volume Descriptor present (type 1, CD001)",
                     "no Primary Volume Descriptor at/after sector 16 with CD001 identifier"):
        return rep

    lba_size = both_endian_16(img, pvd_off + 128)
    rep.check(lba_size == SECTOR, f"logical block size = {lba_size}",
              f"logical block size is {lba_size}, expected {SECTOR}")
    rep.check(terminator_seen, "volume descriptor set terminator (type 255) present",
              "volume descriptor set terminator missing")

    vol_id = img[pvd_off + 40:pvd_off + 72].decode("ascii", "replace").strip()
    total_sectors = both_endian_32(img, pvd_off + 80)
    rep.ok(f"volume id = {vol_id!r}, declared sectors = {total_sectors}")
    rep.check(total_sectors * SECTOR <= len(img) + SECTOR,
              "declared volume size is within the image",
              f"declared volume size {total_sectors} sectors exceeds image ({len(img)//SECTOR} sectors)")

    # ---- Root directory --------------------------------------------------
    root_extent = both_endian_32(img, pvd_off + 156 + 2)
    root_len = both_endian_32(img, pvd_off + 156 + 10)
    if not rep.check(root_extent * SECTOR + root_len <= len(img),
                     f"root directory extent at LBA {root_extent}, {root_len} bytes",
                     "root directory extent points outside the image"):
        return rep

    root_blob = img[root_extent * SECTOR:root_extent * SECTOR + max(root_len, SECTOR)]
    root_recs = read_dir_records(root_blob)
    names = [n.decode("latin-1") for n, _, _, _ in root_recs]
    rep.ok(f"root directory entries: {[n if n.isprintable() else hex(ord(n[0])) for n in names]}")
    rep.check(any(name == b"\x00" for name, _, _, _ in root_recs),
            "root '.' self entry present (ECMA-119)",
            "root '.' self entry missing (ECMA-119 requires it)")
    rep.check(any(name == b"\x01" for name, _, _, _ in root_recs),
            "root '..' parent entry present (ECMA-119)",
            "root '..' parent entry missing (ECMA-119 requires it)")

    def walk(dir_recs, dir_blob_base, prefix, depth=0):
        """Return {path: (extent, length)} for a directory tree."""
        found = {}
        if depth > 4:
            return found
        for name, extent, dlen, flags in dir_recs:
            label = name.decode("ascii", "replace")
            if label in ("\x00", "\x01"):
                continue
            path = f"{prefix}/{label}"
            found[path] = (extent, dlen)
            if flags & 0x02:  # directory
                sub = img[extent * SECTOR:extent * SECTOR + max(dlen, SECTOR)]
                found.update(walk(read_dir_records(sub), extent, path, depth + 1))
        return found

    tree = walk(root_recs, root_extent, "")
    for req in required_files:
        hit = None
        for path, (extent, dlen) in tree.items():
            if path.upper().rstrip(";1") == req.upper().rstrip(";1") or path.upper() == req.upper():
                hit = (path, extent, dlen)
                break
        rep.check(hit is not None and hit[2] > 0,
                  f"required file present: {req} (LBA {hit[1]}, {hit[2]} bytes)" if hit else "",
                  f"required file MISSING from the ISO9660 tree: {req}")

    # ---- El Torito --------------------------------------------------------
    if boot_record_off is None:
        rep.fail("NO El Torito Boot Record Volume Descriptor - firmware cannot "
                 "find a boot catalog, image is NOT bootable", bootability=True)
        # Nothing further to check: without a boot record there is no catalog
        # to walk, whether or not the failure was waived.
        return rep

    br_ident = img[boot_record_off + 7:boot_record_off + 39]
    if not br_ident.startswith(b"EL TORITO SPECIFICATION"):
        rep.fail(f"boot record system id is {br_ident[:31]!r}, expected "
                 f"'EL TORITO SPECIFICATION'", bootability=True)
    else:
        rep.ok(f"boot record system id = {br_ident[:31]!r}")

    catalog_lba = struct.unpack_from("<I", img, boot_record_off + 71)[0]
    if catalog_lba * SECTOR + 512 > len(img):
        rep.fail("boot catalog LBA points outside the image", bootability=True)
        if rep.failures:
            return rep
    rep.ok(f"boot catalog at LBA {catalog_lba}")

    cat = img[catalog_lba * SECTOR:catalog_lba * SECTOR + SECTOR]
    if cat[0] != 0x01:
        rep.fail(f"boot catalog validation header id = 0x{cat[0]:02x}, expected 0x01",
                 bootability=True)
    else:
        rep.ok("boot catalog validation entry header id = 0x01")
    if not (cat[0x1E] == 0x55 and cat[0x1F] == 0xAA):
        rep.fail(f"boot catalog key bytes = 0x{cat[0x1E]:02x} 0x{cat[0x1F]:02x}, "
                 f"expected 0x55 0xAA", bootability=True)
    else:
        rep.ok("boot catalog validation key bytes = 0x55 0xAA")
    # El Torito requires the 16-bit words of the validation entry to sum to 0.
    checksum = sum(struct.unpack_from("<H", cat, i)[0] for i in range(0, 32, 2)) & 0xFFFF
    if checksum != 0:
        rep.fail(f"boot catalog validation checksum invalid (16-bit word sum = "
                 f"0x{checksum:04x}, expected 0)", bootability=True)
    else:
        rep.ok("boot catalog validation checksum valid (16-bit word sum == 0)")

    initial = cat[32:64]
    if initial[0] != 0x88:
        rep.fail(f"initial/default entry indicator = 0x{initial[0]:02x}, expected "
                 f"0x88 (bootable)", bootability=True)
    else:
        rep.ok("initial/default entry is bootable (indicator 0x88)")
    media = initial[1]
    load_rba = struct.unpack_from("<I", initial, 8)[0]
    sector_count = struct.unpack_from("<H", initial, 6)[0]
    media_names = {0: "no emulation", 1: "1.2MB floppy", 2: "1.44MB floppy", 3: "2.88MB floppy", 4: "hard disk"}
    rep.ok(f"boot media type = {media} ({media_names.get(media, 'unknown')}), "
           f"load RBA = {load_rba}, sector count = {sector_count}")

    boot_bytes = img[load_rba * SECTOR:(load_rba + max(sector_count, 1)) * SECTOR]
    nonzero = boot_bytes.strip(b"\x00")
    if len(nonzero) == 0:
        rep.fail(f"boot image at LBA {load_rba} is missing or entirely zero - "
                 f"firmware would boot to nothing", bootability=True)
    else:
        rep.ok(f"boot image at LBA {load_rba} is non-empty ({len(nonzero)} non-zero bytes)")

    return rep


def cross_check_pycdlib(path, rep):
    """Optional third-party cross-check.

    The checks above are this project's own parser, so they share its blind
    spots. When pycdlib is importable, re-parse the image with it and confirm
    that an independent implementation agrees on the directory tree. This is
    what caught the one-byte PVD date offset and the pre-LBA path table.
    """
    try:
        import pycdlib
    except ImportError:
        rep.warn("pycdlib not installed; independent cross-check skipped "
                 "(pip install pycdlib enables it)")
        return rep

    try:
        iso = pycdlib.PyCdlib()
        iso.open(path)
    except Exception as exc:                                  # noqa: BLE001
        rep.fail(f"independent parser (pycdlib) rejected the image: {exc}")
        return rep

    rep.ok("independent parser (pycdlib) opened the image")
    for _path, dirs, files in iso.walk(iso_path="/"):
        for name in list(dirs) + list(files):
            rep.ok(f"pycdlib sees {_path.rstrip('/')}/{name}")
    if iso.eltorito_boot_catalog is None:
        rep.fail("pycdlib found no El Torito boot catalog", bootability=True)
    else:
        rep.ok("pycdlib parsed an El Torito boot catalog")
    iso.close()
    return rep


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[1]
    required = ["/BOOT/ZEROOS.ELF;1", "/BOOT/GRUB/GRUB.CFG;1"]
    args = argv[2:]
    i = 0
    while i < len(args):
        if args[i] == "--require" and i + 1 < len(args):
            required.append(args[i + 1])
            i += 2
        else:
            i += 1

    allow = "--allow-unbootable" in args
    rep = verify(path, required, allow_unbootable=allow)
    if not rep.failures:
        cross_check_pycdlib(path, rep)
    for line in rep.notes:
        print(line)
    print()
    if rep.failures:
        print(f"verify_iso: FAIL ({len(rep.failures)} check(s) failed)")
        for f in rep.failures:
            print(f"  - {f}")
        return 1
    if rep.warnings:
        print(f"verify_iso: PASS with {len(rep.warnings)} warning(s) - "
              f"ISO 9660 layout is valid but THIS IMAGE WILL NOT BOOT")
        return 0
    print("verify_iso: PASS - image is a structurally bootable ISO 9660 / El Torito image")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
