#!/usr/bin/env python3
"""ZEROOS GPT host tool (stdlib only).

  gpt.py create IMAGE [--sector-size 512] --part TYPE:SIZE[:NAME] ...
  gpt.py list IMAGE [--sector-size 512]
  gpt.py offset IMAGE N          # print "<byte offset> <byte length>" of partition N
  gpt.py repair IMAGE            # rebuild a damaged primary/backup from the valid copy

Exit codes: 0 success (repair: nothing to do or repaired), 1 usage/I/O
error, 2 (repair) both copies invalid - nothing is written.

TYPE is one of zjfs, scratch, or a GUID string. SIZE is in MiB, or "rest"
for the remaining usable space (last partition only). Partitions are
1 MiB-aligned. The layout matches what the kernel's GPT validator
(kernel/storage/gpt.c) accepts: protective MBR, primary header at LBA 1,
128 x 128-byte entries, backup entries + backup header at the end.
"""
import argparse
import os
import struct
import sys
import uuid
import zlib

TYPE_ZJFS = uuid.UUID("8F3D2A10-5A4A-4653-9A2E-5A45524F4F53")
TYPE_SCRATCH = uuid.UUID("8F3D2A11-5A4A-4653-9A2E-5A45524F4F53")
TYPES = {"zjfs": TYPE_ZJFS, "scratch": TYPE_SCRATCH}
NAMES = {TYPE_ZJFS: "zeroos-zjfs", TYPE_SCRATCH: "zeroos-scratch"}
ENTRIES = 128
ENTRY_SIZE = 128
SIGNATURE = b"EFI PART"


class GptError(Exception):
    pass


def _crc(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def _header(ss, my_lba, alt_lba, first, last, disk_guid, entries_lba, entries_crc):
    h = bytearray(92)
    struct.pack_into("<8sIIIIQQQQ16sQIII", h, 0, SIGNATURE, 0x00010000, 92, 0, 0,
                     my_lba, alt_lba, first, last, disk_guid.bytes_le, entries_lba,
                     ENTRIES, ENTRY_SIZE, entries_crc)
    struct.pack_into("<I", h, 16, _crc(bytes(h)))
    return bytes(h) + bytes(ss - 92)


def create(path, parts, ss=512):
    size = os.path.getsize(path)
    if size % ss:
        raise GptError("image size is not a multiple of the sector size")
    sectors = size // ss
    entry_sectors = ENTRIES * ENTRY_SIZE // ss
    first_usable = 2 + entry_sectors
    last_usable = sectors - 2 - entry_sectors
    align = (1 << 20) // ss
    lba = max(align, first_usable)
    table = bytearray(ENTRIES * ENTRY_SIZE)
    out = []
    for i, (ptype, mib, name) in enumerate(parts):
        lba = (lba + align - 1) // align * align
        if mib == "rest":
            end = last_usable
        else:
            end = lba + int(mib) * ((1 << 20) // ss) - 1
        if end > last_usable or end < lba:
            raise GptError("partition %d does not fit" % (i + 1))
        encoded = name.encode("utf-16-le")[:72]
        struct.pack_into("<16s16sQQQ72s", table, i * ENTRY_SIZE, ptype.bytes_le,
                         uuid.uuid4().bytes_le, lba, end, 0, encoded)
        out.append((i + 1, ptype, lba, end, name))
        lba = end + 1
    disk_guid = uuid.uuid4()
    ecrc = _crc(bytes(table))
    mbr = bytearray(ss)
    # Boot code: "int 18h; jmp $" -> firmware tries the next boot device
    # instead of executing zeros from a data disk.
    mbr[0:4] = b"\xcd\x18\xeb\xfe"
    struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\x00\x02\x00", 0xEE, b"\xff\xff\xff",
                     1, min(sectors - 1, 0xFFFFFFFF))
    mbr[510:512] = b"\x55\xaa"
    with open(path, "r+b") as f:
        f.seek(0)
        f.write(mbr)
        f.write(_header(ss, 1, sectors - 1, first_usable, last_usable, disk_guid, 2, ecrc))
        f.write(table)
        f.seek((sectors - 1 - entry_sectors) * ss)
        f.write(table)
        f.write(_header(ss, sectors - 1, 1, first_usable, last_usable, disk_guid,
                        sectors - 1 - entry_sectors, ecrc))
    return out


def _load(f, ss, sectors, primary):
    """Validate one GPT copy the way kernel/storage/gpt.c does (subset:
    signature, revision, size, header CRC, MyLBA, usable range, entry
    geometry and entry-array CRC). Returns (fields, table) or raises."""
    which = "primary" if primary else "backup"
    lba = 1 if primary else sectors - 1
    f.seek(lba * ss)
    h = f.read(92)
    if len(h) < 92 or h[:8] != SIGNATURE:
        raise GptError("%s GPT header signature missing" % which)
    (_, rev, hsize, hcrc, _, my_lba, alt, first, last, guid, elba, count, esize,
     ecrc) = struct.unpack("<8sIIIIQQQQ16sQIII", h)
    check = bytearray(h)
    struct.pack_into("<I", check, 16, 0)
    if rev != 0x00010000 or hsize != 92 or _crc(bytes(check)) != hcrc:
        raise GptError("%s GPT header revision/size/checksum invalid" % which)
    if my_lba != lba or alt != (sectors - 1 if primary else 1):
        raise GptError("%s GPT header MyLBA/AlternateLBA invalid" % which)
    if esize != ENTRY_SIZE or count == 0 or count > 1024:
        raise GptError("%s GPT entry geometry unsupported" % which)
    esec = (count * esize + ss - 1) // ss
    if not (first <= last < sectors - 1) or elba + esec > sectors:
        raise GptError("%s GPT usable range invalid" % which)
    f.seek(elba * ss)
    table = f.read(count * esize)
    if _crc(table) != ecrc:
        raise GptError("%s GPT entry array checksum invalid" % which)
    return (first, last, uuid.UUID(bytes_le=guid), elba, count, esize, ecrc), table


def read(path, ss=512):
    """Returns [(number, type_uuid, first_lba, last_lba, name, attributes)].
    Uses the primary GPT, or the backup (with a warning) if the primary is
    damaged - the same recovery policy as the kernel."""
    sectors = os.path.getsize(path) // ss
    with open(path, "rb") as f:
        try:
            fields, table = _load(f, ss, sectors, True)
        except GptError as e:
            fields, table = _load(f, ss, sectors, False)
            print("gpt.py: warning: %s; using backup (run gpt.py repair)" % e,
                  file=sys.stderr)
    first, last, _, _, count, esize, _ = fields
    result = []
    for i in range(count):
        e = table[i * esize:(i + 1) * esize]
        if e[:16] == bytes(16):
            continue
        t, _, lo, hi, attrs, name = struct.unpack("<16s16sQQQ72s", e)
        if lo < first or hi > last or hi < lo:
            raise GptError("partition %d outside usable range" % (i + 1))
        result.append((i + 1, uuid.UUID(bytes_le=t), lo, hi,
                       name.decode("utf-16-le").rstrip("\x00"), attrs))
    return result


def repair(path, ss=512):
    """Rewrite whichever GPT copy is invalid from the valid one. Returns a
    status string; raises GptError if neither copy validates."""
    sectors = os.path.getsize(path) // ss
    with open(path, "r+b") as f:
        good = {}
        errors = {}
        for primary in (True, False):
            try:
                good[primary] = _load(f, ss, sectors, primary)
            except GptError as e:
                errors[primary] = str(e)
        if len(good) == 2:
            return "both GPT copies valid; nothing to repair"
        if not good:
            raise GptError("both GPT copies invalid (%s; %s); refusing to write" %
                           (errors[True], errors[False]))
        src_primary = True in good
        (first, last, guid, _, count, esize, ecrc), table = good[src_primary]
        if count != ENTRIES or esize != ENTRY_SIZE:
            raise GptError("valid copy has non-default entry geometry; not rebuilt")
        esec = count * esize // ss
        if src_primary:
            f.seek((sectors - 1 - esec) * ss)
            f.write(table)
            f.write(_header(ss, sectors - 1, 1, first, last, guid, sectors - 1 - esec, ecrc))
            fixed = "backup"
        else:
            f.seek(ss)
            f.write(_header(ss, 1, sectors - 1, first, last, guid, 2, ecrc))
            f.write(table)
            fixed = "primary"
        f.flush()
        os.fsync(f.fileno())
        return "%s GPT rebuilt from %s (%s)" % (
            fixed, "primary" if src_primary else "backup", errors[not src_primary])


def partition_extent(path, number, ss=512):
    for n, _, lo, hi, _, _ in read(path, ss):
        if n == number:
            return lo * ss, (hi - lo + 1) * ss
    raise GptError("partition %d not found" % number)


def _parse_part(text):
    fields = text.split(":")
    if len(fields) < 2:
        raise argparse.ArgumentTypeError("expected TYPE:SIZE[:NAME]")
    ptype = TYPES.get(fields[0].lower())
    if ptype is None:
        ptype = uuid.UUID(fields[0])
    size = fields[1] if fields[1] == "rest" else int(fields[1])
    name = fields[2] if len(fields) > 2 else NAMES.get(ptype, "data")
    return ptype, size, name


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("create")
    c.add_argument("image")
    c.add_argument("--sector-size", type=int, default=512)
    c.add_argument("--part", type=_parse_part, action="append", required=True)
    l = sub.add_parser("list")
    l.add_argument("image")
    l.add_argument("--sector-size", type=int, default=512)
    o = sub.add_parser("offset")
    o.add_argument("image")
    o.add_argument("number", type=int)
    o.add_argument("--sector-size", type=int, default=512)
    r = sub.add_parser("repair")
    r.add_argument("image")
    r.add_argument("--sector-size", type=int, default=512)
    a = ap.parse_args(argv)
    try:
        if a.cmd == "create":
            for n, t, lo, hi, name in create(a.image, a.part, a.sector_size):
                print("p%d %s lba %d-%d %s" % (n, NAMES.get(t, str(t)), lo, hi, name))
        elif a.cmd == "list":
            for n, t, lo, hi, name, attrs in read(a.image, a.sector_size):
                print("p%d %s lba %d-%d attrs=0x%x %s" % (n, NAMES.get(t, str(t)), lo, hi,
                                                          attrs, name))
        elif a.cmd == "repair":
            try:
                print("gpt.py: " + repair(a.image, a.sector_size))
            except GptError as e:
                print("gpt.py: error: %s" % e, file=sys.stderr)
                return 2
        else:
            off, length = partition_extent(a.image, a.number, a.sector_size)
            print(off, length)
    except (GptError, OSError) as e:
        print("gpt.py: error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
