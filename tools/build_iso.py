#!/usr/bin/env python3
"""ZEROOS ISO-9660 image builder.

Fallback image writer for hosts that have neither grub-mkrescue nor xorriso.
It produces a spec-conformant ECMA-119 (ISO 9660 Level 1) image with a real
directory hierarchy, L/M path tables, and - when a boot image is supplied - a
complete El Torito boot record and boot catalog.

The previous version of this script flattened every path into the root
directory (boot/zeroos.elf -> BOOT_ZEROOS.ELF;1), dropped subdirectories
entirely, and emitted no El Torito descriptors at all, so the resulting image
looked like an ISO to `file` but could not be booted by any firmware. That is
why --eltorito is required: without a boot image this script refuses to write
an image that would silently fail to boot. Use --no-boot to force a data-only
image.

Usage:
  build_iso.py <output.iso> <source_dir> [--eltorito BOOT_IMAGE]
                                       [--boot-sectors N] [--vol-id ID]
                                       [--no-boot]
"""

import argparse
import os
import re
import struct
import sys

SECTOR = 2048
ROOT_LBA_RESERVED_START = 16

_ALLOWED = re.compile(r"[^A-Z0-9_]")


def le16(v):
    return struct.pack("<H", v)


def be16(v):
    return struct.pack(">H", v)


def le32(v):
    return struct.pack("<I", v)


def be32(v):
    return struct.pack(">I", v)


def both16(v):
    """ECMA-119 stores many integers little-endian immediately followed by big-endian."""
    return le16(v) + be16(v)


def both32(v):
    return le32(v) + be32(v)


def pad_to_sector(data):
    rem = len(data) % SECTOR
    return data + b"\x00" * (SECTOR - rem) if rem else data


def iso_dir_name(name):
    """ECMA-119 Level 1 directory identifier: 1-8 chars from [A-Z0-9_]."""
    cleaned = _ALLOWED.sub("_", name.upper())[:8]
    if not cleaned:
        raise ValueError(f"directory name {name!r} has no ISO 9660 Level 1 representation")
    return cleaned


def iso_file_name(name):
    """ECMA-119 Level 1 file identifier: NAME[.EXT];1 (8.3, uppercase)."""
    upper = name.upper()
    if "." in upper:
        stem, ext = upper.rsplit(".", 1)
    else:
        stem, ext = upper, ""
    stem = _ALLOWED.sub("_", stem)[:8]
    ext = _ALLOWED.sub("_", ext)[:3]
    if not stem:
        raise ValueError(f"file name {name!r} has no ISO 9660 Level 1 representation")
    ident = stem + ("." + ext if ext else "")
    return ident + ";1"


class Dir:
    def __init__(self, ident, parent):
        self.ident = ident
        self.parent = parent
        self.children = []      # Dir and File, ordered
        self.lba = 0
        self.record_len = 0     # padded to a sector multiple
        self.number = 0         # path table number, assigned breadth-first


class File:
    def __init__(self, ident, path, size):
        self.ident = ident
        self.path = path
        self.size = size
        self.lba = 0


def scan(src_dir):
    """Build the directory tree from the source directory."""
    root = Dir("", None)

    def walk(node, abs_path):
        for entry in sorted(os.listdir(abs_path)):
            full = os.path.join(abs_path, entry)
            if os.path.isdir(full):
                child = Dir(iso_dir_name(entry), node)
                node.children.append(child)
                walk(child, full)
            elif os.path.isfile(full):
                node.children.append(File(iso_file_name(entry), full, os.path.getsize(full)))
            else:
                raise ValueError(f"unsupported filesystem entry: {full}")

    walk(root, src_dir)

    # Detect identifier collisions inside each directory: two distinct names can
    # collapse to the same 8.3 identifier, which would silently drop a file.
    for node in iter_dirs(root):
        seen = {}
        for child in node.children:
            if child.ident in seen:
                raise ValueError(
                    f"ISO 9660 Level 1 collision: {seen[child.ident]} and "
                    f"{child.ident} both map to {child.ident!r}; "
                    f"rename one of them or build with grub-mkrescue (Joliet/Rock Ridge)")
            seen[child.ident] = getattr(child, "path", child.ident)
    return root


def iter_dirs(node):
    yield node
    for child in node.children:
        if isinstance(child, Dir):
            yield from iter_dirs(child)


def iter_files(node):
    for child in node.children:
        if isinstance(child, File):
            yield child
        else:
            yield from iter_files(child)


def build_time():
    """Timestamp used for every field in the image.

    SOURCE_DATE_EPOCH is honoured so two builds of the same inputs produce
    byte-identical images; without it every image differs in eleven date fields
    and "reproducible image" cannot be claimed.
    """
    import os
    import time
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if epoch:
        try:
            return time.gmtime(int(epoch))
        except ValueError:
            raise SystemExit(f"SOURCE_DATE_EPOCH is not an integer: {epoch!r}")
    return time.gmtime()


def dir_date_record():
    """ECMA-119 9.1.5 recording date: 7 bytes (year-1900, mon, mday, h, m, s, GMT)."""
    t = build_time()
    return bytes([t.tm_year - 1900, t.tm_mon, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec, 0])


def pvd_date_stamp():
    """ECMA-119 8.4.26.1 volume date: 17 bytes (YYYYMMDDHHMMSSCC + GMT offset)."""
    t = build_time()
    return (f"{t.tm_year:04d}{t.tm_mon:02d}{t.tm_mday:02d}"
            f"{t.tm_hour:02d}{t.tm_min:02d}{t.tm_sec:02d}00").encode("ascii") + b"\x00"


# Directory records and the PVD use different date encodings; keep them distinct
# because mixing them silently corrupts every directory record length.
def date_record():
    return dir_date_record()


def dir_record(ident_bytes, extent, length, is_dir, date_bytes):
    """ECMA-119 9.1 directory record."""
    if len(date_bytes) != 7:
        raise AssertionError(
            f"directory record date must be 7 bytes (ECMA-119 9.1.5), got {len(date_bytes)}")
    rec = bytearray()
    rec += bytes([0])                    # LEN-DR placeholder
    rec += bytes([0])                    # extended attribute record length
    rec += both32(extent)
    rec += both32(length)
    rec += date_bytes
    rec += bytes([2 if is_dir else 0])   # file flags: bit1 = directory
    rec += bytes([0])                    # file unit size
    rec += bytes([0])                    # interleave gap
    rec += both16(1)                     # volume sequence number
    rec += bytes([len(ident_bytes)])
    rec += ident_bytes
    if len(rec) % 2 != 0:                # LEN-DR must be even
        rec += b"\x00"
    rec[0] = len(rec)
    fixed = 33 + len(ident_bytes)        # ECMA-119 9.1: 33-byte fixed part + identifier
    if rec[0] not in (fixed, fixed + 1):
        raise AssertionError(f"directory record length {rec[0]} is not {fixed} or {fixed + 1}")
    return bytes(rec)


def build_dir_extent(node, date_bytes):
    """Serialize a directory: '.' entry, '..' entry, then children."""
    blob = bytearray()
    blob += dir_record(b"\x00", node.lba, node.record_len, True, date_bytes)
    parent = node.parent if node.parent is not None else node
    blob += dir_record(b"\x01", parent.lba, parent.record_len, True, date_bytes)
    for child in node.children:
        is_dir = isinstance(child, Dir)
        length = child.record_len if is_dir else child.size
        blob += dir_record(child.ident.encode("ascii"), child.lba, length, is_dir, date_bytes)
    return pad_to_sector(bytes(blob))


def assign_path_numbers(root):
    """Breadth-first numbering, root = 1 (ECMA-119 9.4)."""
    order = []
    queue = [root]
    while queue:
        node = queue.pop(0)
        node.number = len(order) + 1
        order.append(node)
        queue.extend(c for c in node.children if isinstance(c, Dir))
    return order


def path_table(order, big_endian):
    """ECMA-119 9.4 path table; M-type is the big-endian mirror."""
    u32 = be32 if big_endian else le32
    u16 = be16 if big_endian else le16
    blob = bytearray()
    for node in order:
        ident = b"\x00" if node is order[0] else node.ident.encode("ascii")
        rec = bytearray()
        rec += bytes([len(ident)])       # length of directory identifier
        rec += bytes([0])                # extended attribute record length
        rec += u32(node.lba)
        rec += u16(node.parent.number if node.parent is not None else 1)
        rec += ident
        if len(ident) % 2 != 0:          # pad so the next record is word aligned
            rec += b"\x00"
        blob += rec
    return bytes(blob)


def boot_catalog(catalog_lba_unused, image_lba, boot_sectors, platform=0):
    """El Torito validation entry + initial/default entry."""
    validation = bytearray(32)
    validation[0] = 0x01                                    # header id
    validation[1] = platform                                # 0 = x86 BIOS, 0xEF = EFI
    validation[2:4] = b"\x00\x00"                           # reserved
    validation[4:28] = b"ZEROOS".ljust(24, b"\x00")         # manufacturer id
    # bytes 28-29 hold the checksum, filled in below
    validation[30] = 0x55                                   # key byte 1
    validation[31] = 0xAA                                   # key byte 2
    # The 16-bit words of the validation entry must sum to zero mod 0x10000.
    total = sum(struct.unpack_from("<H", validation, i)[0] for i in range(0, 32, 2))
    struct.pack_into("<H", validation, 28, (0x10000 - total) & 0xFFFF)

    initial = bytearray(32)
    initial[0] = 0x88                                       # bootable
    initial[1] = 0x00                                       # media type: no emulation
    initial[2:4] = le16(0)                                  # load segment (0 -> 0x7C00)
    initial[4] = 0x00                                       # system type
    initial[5] = 0x00                                       # unused
    initial[6:8] = le16(boot_sectors)                       # sectors to load
    initial[8:12] = le32(image_lba)                         # load RBA
    return pad_to_sector(bytes(validation) + bytes(initial))


def build_iso(out_path, src_dir, eltorito=None, boot_sectors=4, vol_id="ZEROOS_BOOT", no_boot=False):
    if not os.path.isdir(src_dir):
        raise ValueError(f"source directory does not exist: {src_dir}")
    if not no_boot and not eltorito:
        raise SystemExit(
            "build_iso.py: refusing to write a non-bootable image.\n"
            "  The fallback writer cannot synthesise a GRUB boot image, so an image\n"
            "  built without one would be accepted by tooling but never boot.\n"
            "  Install the real toolchain instead:\n"
            "      sudo apt-get install grub-pc-bin grub-common xorriso mtools\n"
            "  then re-run `make iso` (it prefers grub-mkrescue automatically).\n"
            "  Pass --eltorito <boot_image> to supply one directly, or --no-boot to\n"
            "  force a data-only image.")

    root = scan(src_dir)
    date_bytes = date_record()
    order = assign_path_numbers(root)

    boot_image = None
    if eltorito:
        with open(eltorito, "rb") as fh:
            boot_image = fh.read()
        if not boot_image.strip(b"\x00"):
            raise SystemExit(f"build_iso.py: boot image is empty: {eltorito}")

    # ---- volume descriptor set layout -----------------------------------
    lba = ROOT_LBA_RESERVED_START
    pvd_lba = lba
    lba += 1
    boot_rec_lba = None
    if boot_image is not None:
        boot_rec_lba = lba
        lba += 1
    terminator_lba = lba
    lba += 1

    # Path tables come next. Their SIZE depends only on identifiers, but their
    # CONTENT embeds every directory's extent LBA, so size them now with a
    # probe pass and serialise them only once all LBAs are final.
    ltype_probe = path_table(order, big_endian=False)
    mtype_probe = path_table(order, big_endian=True)
    path_l_lba = lba
    lba += (len(ltype_probe) + SECTOR - 1) // SECTOR
    path_m_lba = lba
    lba += (len(mtype_probe) + SECTOR - 1) // SECTOR

    # Directory extents. record_len must be known before serialisation because
    # each directory stores its own length and its children's lengths, so fix
    # up the sizes first by serialising with a provisional layout.
    for node in iter_dirs(root):
        provisional = build_dir_extent(node, date_bytes)
        node.record_len = len(provisional)

    for node in iter_dirs(root):
        node.lba = lba
        lba += node.record_len // SECTOR

    # File LBAs MUST be assigned before the directory extents are serialised:
    # every directory record embeds its children's extent LBAs. Serialising
    # first writes zeros into the file extents and the image looks valid while
    # every file points at the system area.
    file_blobs = []
    for f in iter_files(root):
        f.lba = lba
        with open(f.path, "rb") as fh:
            data = fh.read()
        if len(data) != f.size:
            raise AssertionError(f"{f.path} changed size while building the image")
        padded = pad_to_sector(data)
        file_blobs.append((f, padded))
        lba += len(padded) // SECTOR

    # Re-serialise now that every LBA is final, and confirm the sizes held.
    extents = {}
    for node in iter_dirs(root):
        blob = build_dir_extent(node, date_bytes)
        if len(blob) != node.record_len:
            raise AssertionError(
                f"directory {node.ident or '/'} extent changed size after LBA assignment "
                f"({node.record_len} -> {len(blob)})")
        extents[node.lba] = blob

    # Serialise the path tables now that every directory LBA is final.
    ltype = path_table(order, big_endian=False)
    mtype = path_table(order, big_endian=True)
    if len(ltype) != len(ltype_probe) or len(mtype) != len(mtype_probe):
        raise AssertionError("path table size changed after LBA assignment")

    # El Torito boot catalog and boot image live at the end of the volume.
    catalog_lba = image_lba = None
    if boot_image is not None:
        catalog_lba = lba
        lba += 1
        image_lba = lba
        lba += (len(boot_image) + SECTOR - 1) // SECTOR

    total_sectors = lba

    # No extent may point into the system area or the descriptor set; that is
    # the failure mode a mis-ordered layout produces silently.
    for node in order:
        if node.lba <= terminator_lba:
            raise AssertionError(
                f"path table entry {node.ident or '/'} has extent LBA {node.lba}, "
                f"which is not a final directory location")
    lowest = min([d.lba for d in iter_dirs(root)] + [f.lba for f, _ in file_blobs])
    if lowest <= terminator_lba:
        raise AssertionError(
            f"extent LBA {lowest} collides with the volume descriptor set "
            f"(terminator at {terminator_lba}); the layout is mis-ordered")

    # ---- Primary Volume Descriptor --------------------------------------
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"ZEROOS".ljust(32, b" ")                  # system identifier
    pvd[40:72] = vol_id.encode("ascii")[:32].ljust(32, b" ")
    pvd[80:88] = both32(total_sectors)                     # volume space size
    pvd[120:124] = both16(1)                               # volume set size
    pvd[124:128] = both16(1)                               # volume sequence number
    pvd[128:132] = both16(SECTOR)                          # logical block size
    pvd[132:140] = both32(len(ltype))                      # path table size
    pvd[140:144] = le32(path_l_lba)
    pvd[144:148] = le32(0)                                 # optional L path table
    pvd[148:152] = be32(path_m_lba)
    pvd[152:156] = be32(0)                                 # optional M path table
    root_rec = dir_record(b"\x00", root.lba, root.record_len, True, date_bytes)
    if len(root_rec) > 34:
        raise AssertionError("root directory record exceeds the 34 bytes reserved in the PVD")
    pvd[156:156 + len(root_rec)] = root_rec
    pvd[190:318] = b"ZEROOS".ljust(128, b" ")              # volume set identifier
    pvd[318:446] = b"ZEROOS PROJECT".ljust(128, b" ")      # publisher identifier
    pvd[446:574] = b"build_iso.py".ljust(128, b" ")        # data preparer
    pvd[574:702] = b"ZEROOS BUILD".ljust(128, b" ")        # application identifier
    stamp = pvd_date_stamp()
    if len(stamp) != 17:
        raise AssertionError("PVD date stamp must be exactly 17 bytes")
    # ECMA-119 8.4.26-8.4.30: the four volume dates start at byte 813, the file
    # structure version is byte 881 and byte 882 is unused (must stay zero).
    # These are one byte earlier than the intuitive 814/882 offsets; writing
    # them one byte late puts the version number into the reserved byte and
    # strict parsers reject the descriptor.
    pvd[813:830] = stamp                                   # creation
    pvd[830:847] = stamp                                   # modification
    pvd[847:864] = b"0" * 16 + b"\x00"                     # expiration (none)
    pvd[864:881] = b"0" * 16 + b"\x00"                     # effective (now)
    pvd[881] = 1                                           # file structure version
    pvd[882] = 0                                           # unused, must be zero

    # ---- Boot Record Volume Descriptor (El Torito) ----------------------
    boot_rec = None
    if boot_image is not None:
        boot_rec = bytearray(SECTOR)
        boot_rec[0] = 0
        boot_rec[1:6] = b"CD001"
        boot_rec[6] = 1
        boot_rec[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
        boot_rec[71:75] = le32(catalog_lba)

    terminator = bytearray(SECTOR)
    terminator[0] = 255
    terminator[1:6] = b"CD001"
    terminator[6] = 1

    # ---- assemble --------------------------------------------------------
    image = bytearray(SECTOR * ROOT_LBA_RESERVED_START)   # system area
    image += pvd
    if boot_rec is not None:
        image += boot_rec
    image += terminator
    image += pad_to_sector(ltype)
    image += pad_to_sector(mtype)
    for node_lba in sorted(extents):
        image += extents[node_lba]
    for _f, blob in file_blobs:
        image += blob
    if boot_image is not None:
        image += boot_catalog(catalog_lba, image_lba, boot_sectors)
        image += pad_to_sector(boot_image)

    expected = total_sectors * SECTOR
    if len(image) != expected:
        raise AssertionError(f"image size {len(image)} != declared {expected}")

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as fh:
        fh.write(image)

    files = list(iter_files(root))
    dirs = [d for d in iter_dirs(root) if d is not root]
    print(f"Created ISO: {out_path} ({len(image)} bytes, {total_sectors} sectors, "
          f"{len(dirs)} directories, {len(files)} files, "
          f"{'El Torito bootable' if boot_image is not None else 'DATA ONLY - NOT BOOTABLE'})")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description="Build a ZEROOS ISO 9660 image.")
    parser.add_argument("output")
    parser.add_argument("source_dir")
    parser.add_argument("--eltorito", help="boot image referenced by the El Torito catalog")
    parser.add_argument("--boot-sectors", type=int, default=4,
                        help="512-byte sectors of the boot image to load (default 4)")
    parser.add_argument("--vol-id", default="ZEROOS_BOOT")
    parser.add_argument("--no-boot", action="store_true",
                        help="write a data-only image (will not boot)")
    args = parser.parse_args(argv[1:])

    # Accept the historical positional order too: build_iso.py <out> <src>
    rc = build_iso(args.output, args.source_dir, eltorito=args.eltorito,
                   boot_sectors=args.boot_sectors, vol_id=args.vol_id,
                   no_boot=args.no_boot)
    if args.no_boot:
        print("WARNING: --no-boot was used; this image has no El Torito boot record "
              "and cannot be booted by firmware.", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
