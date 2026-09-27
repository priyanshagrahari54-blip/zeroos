#!/usr/bin/env python3
"""ZEROOS ZJFS host tool (stdlib only). On-disk format: docs/ZJFS.md and
kernel/storage/zjfs.h (format version 1).

  zjfs.py [--part N | --offset BYTES [--length BYTES]] IMAGE COMMAND ...

Commands:
  mkfs [--label L] [--inodes N] [--journal-blocks N] [--force]
  info
  fsck [--repair]          exit 0 clean, 1 errors (repaired with --repair),
                           2 fatal/unrecoverable, 3 cannot open
  ls [PATH]
  cat PATH
  put LOCAL PATH           create or replace a regular file
  mkdir PATH
  get-counter              print /.zeroos-boot-count (0 if absent)

Read-only commands apply a committed-but-unreplayed journal transaction
as an in-memory overlay (the result a ZEROOS mount would see). Commands
that modify the image replay it to disk first, refuse to touch a
filesystem with the ERROR flag or pending orphans (run fsck --repair / a
ZEROOS mount), and mark the superblock DIRTY while working so an
interrupted host edit is detected.
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpt  # noqa: E402

BS = 4096
MAGIC = 0x53464A5A
VERSION = 1
INODE_SIZE = 256
IPB = BS // INODE_SIZE
BITS = 4088 * 8
BITMAP_MAGIC = 0x4D424A5A
DIR_MAGIC = 0x52444A5A
JSB_MAGIC = 0x534A4A5A
DESC_MAGIC = 0x44544A5A
COMMIT_MAGIC = 0x43544A5A
PTRS = 1022
DIRECT = 12
ROOT = 1
LOSTFOUND = 2
DESC_MAX = 500
DIR_HEADER = 16
MAX_FILE_BLOCKS = DIRECT + PTRS + PTRS * PTRS
S_IFMT, S_IFREG, S_IFDIR = 0xF000, 0x8000, 0x4000
FT_REG, FT_DIR = 1, 2
STATE_CLEAN, STATE_DIRTY, STATE_ERROR = 1, 2, 4
IF_ORPHAN = 1

# ---------------------------------------------------------------- CRC-32C
_T = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ 0x82F63B78 if _c & 1 else _c >> 1
    _T.append(_c)


def crc32c(data, crc=0):
    crc ^= 0xFFFFFFFF
    t = _T
    for b in data:
        crc = t[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


assert crc32c(b"123456789") == 0xE3069283


def meta_csum(block, number):
    return crc32c(block[:4092]) ^ (number & 0xFFFFFFFF)


def dir_csum(block, number):
    c = crc32c(bytes(block[:8]) + b"\0\0\0\0" + bytes(block[12:BS]))
    return c ^ (number & 0xFFFFFFFF)


def inode_csum(raw, ino):
    return crc32c(raw[:252]) ^ (ino & 0xFFFFFFFF)


def rd32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def wr32(b, o, v):
    struct.pack_into("<I", b, o, v & 0xFFFFFFFF)


def div_up(a, b):
    return (a + b - 1) // b


# ------------------------------------------------------------- structures
SB_FIELDS = ("magic version block_size inode_size total_blocks inode_count free_blocks "
             "free_inodes journal_start journal_blocks inode_bitmap_start "
             "inode_bitmap_blocks block_bitmap_start block_bitmap_blocks "
             "inode_table_start inode_table_blocks data_start root_ino state "
             "features_compat features_incompat features_ro_compat uuid label "
             "mount_count last_mount_time last_write_time last_check_time error_count "
             "last_error_code last_error_block last_error_time orphan_head "
             "generation_counter lost_found_ino").split()
SB_FMT = "<IIII13QII3Q16s32s4QII5Q"
assert struct.calcsize(SB_FMT) == 280

DI_FMT = "<HHIIIQQQQQQII12IIIII116sI"
assert struct.calcsize(DI_FMT) == 256


class Corrupt(Exception):
    pass


class Inode:
    __slots__ = ("ino", "mode", "links", "uid", "gid", "flags", "size", "blocks", "atime",
                 "mtime", "ctime", "crtime", "generation", "orphan_next", "direct",
                 "indirect", "dindirect", "tindirect", "parent")

    @classmethod
    def unpack(cls, ino, raw):
        v = struct.unpack(DI_FMT, raw)
        i = cls()
        i.ino = ino
        (i.mode, i.links, i.uid, i.gid, i.flags, i.size, i.blocks, i.atime, i.mtime,
         i.ctime, i.crtime, i.generation, i.orphan_next) = v[:13]
        i.direct = list(v[13:25])
        i.indirect, i.dindirect, i.tindirect, i.parent = v[25:29]
        return i

    @classmethod
    def new(cls, ino, mode, now, generation, parent=0):
        i = cls()
        i.ino = ino
        i.mode, i.links, i.uid, i.gid, i.flags = mode, 1, 0, 0, 0
        i.size = i.blocks = 0
        i.atime = i.mtime = i.ctime = i.crtime = now
        i.generation, i.orphan_next = generation, 0
        i.direct = [0] * DIRECT
        i.indirect = i.dindirect = i.tindirect = 0
        i.parent = parent
        return i

    def pack(self):
        raw = bytearray(struct.pack(DI_FMT, self.mode, self.links, self.uid, self.gid,
                                    self.flags, self.size, self.blocks, self.atime,
                                    self.mtime, self.ctime, self.crtime, self.generation,
                                    self.orphan_next, *self.direct, self.indirect,
                                    self.dindirect, self.tindirect, self.parent,
                                    bytes(116), 0))
        wr32(raw, 252, inode_csum(raw, self.ino))
        return bytes(raw)

    def is_dir(self):
        return self.mode & S_IFMT == S_IFDIR

    def is_reg(self):
        return self.mode & S_IFMT == S_IFREG


class Image:
    def __init__(self, path, offset=0, length=None, writable=False):
        self.f = open(path, "r+b" if writable else "rb")
        self.offset = offset
        size = os.path.getsize(path) if length is None else length
        if length is None:
            size -= offset
        self.blocks = size // BS

    def read(self, n):
        if n >= self.blocks:
            raise Corrupt("block %d beyond device" % n)
        self.f.seek(self.offset + n * BS)
        data = self.f.read(BS)
        return data + bytes(BS - len(data))

    def write(self, n, data):
        assert len(data) == BS and n < self.blocks
        self.f.seek(self.offset + n * BS)
        self.f.write(data)

    def flush(self):
        self.f.flush()
        os.fsync(self.f.fileno())


def super_valid(raw, number, device_blocks):
    s = dict(zip(SB_FIELDS, struct.unpack_from(SB_FMT, raw)))
    if s["magic"] != MAGIC or rd32(raw, 4092) != meta_csum(raw, number):
        return None
    if (s["version"] != VERSION or s["block_size"] != BS or s["inode_size"] != INODE_SIZE
            or s["features_incompat"]):
        return None
    t = s["total_blocks"]
    if t < 256 or t > device_blocks or t > 0xFFFFFFFF:
        return None
    ic = s["inode_count"]
    if ic < 64 or ic % IPB or ic > 0xFFFFFFFF:
        return None
    j = s["journal_blocks"]
    if s["journal_start"] != 1 or j < 16 or j > t // 2:
        return None
    if (s["inode_bitmap_start"] != 1 + j or s["inode_bitmap_blocks"] != div_up(ic, BITS)
            or s["block_bitmap_start"] != s["inode_bitmap_start"] + s["inode_bitmap_blocks"]
            or s["block_bitmap_blocks"] != div_up(t, BITS)
            or s["inode_table_start"] != s["block_bitmap_start"] + s["block_bitmap_blocks"]
            or s["inode_table_blocks"] != ic // IPB
            or s["data_start"] != s["inode_table_start"] + s["inode_table_blocks"]
            or s["data_start"] + 2 >= t):
        return None
    if (s["root_ino"] != ROOT or s["free_blocks"] > t or s["free_inodes"] > ic
            or s["orphan_head"] >= ic):
        return None
    return s


def pack_super(s, raw_template, number):
    raw = bytearray(raw_template)
    struct.pack_into(SB_FMT, raw, 0, *[s[k] for k in SB_FIELDS])
    wr32(raw, 4092, meta_csum(raw, number))
    return bytes(raw)


# ------------------------------------------------------------------ mkfs
def mkfs(img, label="zeroos", inodes=0, journal=0, force=False):
    total = min(img.blocks, 0xFFFFFFFF)
    if total < 256:
        raise Corrupt("device too small (%d blocks)" % total)
    if not force:
        for n in (0, img.blocks - 1):
            if super_valid(img.read(n), n, img.blocks):
                raise Corrupt("a valid ZJFS already exists (use --force to overwrite)")
    if not journal:
        journal = 1024 if total // 32 > 1024 else (64 if total // 32 < 64 else total // 32)
    if not inodes:
        inodes = total // 4
    inodes = max(inodes, 64)
    inodes = div_up(inodes, IPB) * IPB
    if journal < 16 or journal > total // 4:
        raise Corrupt("invalid journal size")
    s = dict.fromkeys(SB_FIELDS, 0)
    s.update(magic=MAGIC, version=VERSION, block_size=BS, inode_size=INODE_SIZE,
             total_blocks=total, inode_count=inodes, journal_start=1, journal_blocks=journal)
    s["inode_bitmap_start"] = 1 + journal
    s["inode_bitmap_blocks"] = div_up(inodes, BITS)
    s["block_bitmap_start"] = s["inode_bitmap_start"] + s["inode_bitmap_blocks"]
    s["block_bitmap_blocks"] = div_up(total, BITS)
    s["inode_table_start"] = s["block_bitmap_start"] + s["block_bitmap_blocks"]
    s["inode_table_blocks"] = inodes // IPB
    s["data_start"] = s["inode_table_start"] + s["inode_table_blocks"]
    if s["data_start"] + 16 >= total:
        raise Corrupt("device too small for metadata")
    root_block, lf_block = s["data_start"], s["data_start"] + 1
    s.update(root_ino=ROOT, state=STATE_CLEAN, free_blocks=total - (s["data_start"] + 2) - 1,
             free_inodes=inodes - 3, uuid=os.urandom(16),
             label=label.encode()[:31].ljust(32, b"\0"), generation_counter=2,
             lost_found_ino=LOSTFOUND)
    now = time.time_ns()
    s["last_check_time"] = now
    jsb = bytearray(BS)
    struct.pack_into("<IIQQI", jsb, 0, JSB_MAGIC, 1, 1, journal, 0)
    wr32(jsb, 4092, meta_csum(jsb, 1))
    img.write(1, bytes(jsb))
    img.write(2, bytes(BS))
    for i in range(s["inode_bitmap_blocks"]):
        b = bytearray(BS)
        base = i * BITS
        for bit in range(BITS):
            if base + bit <= LOSTFOUND or base + bit >= inodes:
                b[bit >> 3] |= 1 << (bit & 7)
        wr32(b, 4088, BITMAP_MAGIC)
        wr32(b, 4092, meta_csum(b, s["inode_bitmap_start"] + i))
        img.write(s["inode_bitmap_start"] + i, bytes(b))
    for i in range(s["block_bitmap_blocks"]):
        b = bytearray(BS)
        base = i * BITS
        for bit in range(BITS):
            n = base + bit
            if n <= lf_block or n >= total - 1:
                b[bit >> 3] |= 1 << (bit & 7)
        wr32(b, 4088, BITMAP_MAGIC)
        wr32(b, 4092, meta_csum(b, s["block_bitmap_start"] + i))
        img.write(s["block_bitmap_start"] + i, bytes(b))
    for i in range(s["inode_table_blocks"]):
        b = bytearray(BS)
        if i == 0:
            r = Inode.new(ROOT, S_IFDIR | 0o755, now, 1, ROOT)
            r.links, r.size, r.blocks, r.direct[0] = 3, BS, 1, root_block
            lf = Inode.new(LOSTFOUND, S_IFDIR | 0o700, now, 2, ROOT)
            lf.links, lf.size, lf.blocks, lf.direct[0] = 2, BS, 1, lf_block
            b[ROOT * INODE_SIZE:(ROOT + 1) * INODE_SIZE] = r.pack()
            b[LOSTFOUND * INODE_SIZE:(LOSTFOUND + 1) * INODE_SIZE] = lf.pack()
        img.write(s["inode_table_start"] + i, bytes(b))
    d = new_dir_block(ROOT)
    rec_fill(d, DIR_HEADER, LOSTFOUND, BS - DIR_HEADER, b"lost+found", FT_DIR)
    img.write(root_block, seal_dir(d, root_block))
    img.write(lf_block, seal_dir(new_dir_block(LOSTFOUND), lf_block))
    img.write(total - 1, pack_super(s, bytes(BS), total - 1))
    img.write(0, pack_super(s, bytes(BS), 0))
    img.flush()
    return s


def new_dir_block(owner):
    d = bytearray(BS)
    wr32(d, 0, DIR_MAGIC)
    wr32(d, 4, owner)
    wr32(d, DIR_HEADER, 0)
    struct.pack_into("<H", d, DIR_HEADER + 4, BS - DIR_HEADER)
    return d


def seal_dir(d, number):
    wr32(d, 8, dir_csum(d, number))
    return bytes(d)


def rec_fill(d, off, ino, rec_len, name, ftype):
    struct.pack_into("<IHBB", d, off, ino, rec_len, len(name), ftype)
    d[off + 8:off + 8 + len(name)] = name
    d[off + 8 + len(name):off + rec_len] = bytes(rec_len - 8 - len(name))


def rec_align(n):
    return (n + 7) & ~7


# -------------------------------------------------------------------- FS
class FS:
    def __init__(self, img, writable=False):
        self.img = img
        self.overlay = {}
        self.dirty = {}
        self.used_backup = False
        self.replayed = None
        raw = img.read(0)
        s = super_valid(raw, 0, img.blocks)
        if not s:
            n = img.blocks - 1
            raw = img.read(n)
            s = super_valid(raw, n, img.blocks)
            if not s or s["total_blocks"] != img.blocks:
                raise Corrupt("no valid ZJFS superblock (primary and backup invalid)")
            self.used_backup = True
        self.s = s
        self.sb_raw = raw
        self.writable = writable
        self.pending_journal = self._journal(apply=False)

    # block I/O ---------------------------------------------------------
    def read(self, n):
        if n in self.dirty:
            return self.dirty[n]
        if n in self.overlay:
            return self.overlay[n]
        return self.img.read(n)

    def put(self, n, data):
        assert self.writable
        self.dirty[n] = bytes(data)

    def commit(self, clean=True):
        """Write dirty blocks, then both superblocks (the SB goes last)."""
        for n in sorted(self.dirty):
            self.img.write(n, self.dirty[n])
        self.dirty.clear()
        self.img.flush()
        self.write_super(clean)

    def write_super(self, clean):
        s = self.s
        s["state"] = (s["state"] & STATE_ERROR) | (STATE_CLEAN if clean else STATE_DIRTY)
        s["last_write_time"] = time.time_ns()
        t = s["total_blocks"]
        self.img.write(0, pack_super(s, self.sb_raw, 0))
        if clean:
            self.img.write(t - 1, pack_super(s, self.sb_raw, t - 1))
        self.img.flush()

    # journal -----------------------------------------------------------
    def _journal(self, apply):
        s = self.s
        j0, jn = s["journal_start"], s["journal_blocks"]
        jsb = self.img.read(j0)
        magic, _, seq, blocks = struct.unpack_from("<IIQQ", jsb)
        if magic != JSB_MAGIC or rd32(jsb, 4092) != meta_csum(jsb, j0) or blocks != jn:
            raise Corrupt("journal superblock invalid")
        desc = self.img.read(j0 + 1)
        dmagic, count, dseq = struct.unpack_from("<IIQ", desc)
        mx = min(jn - 3, DESC_MAX)
        if (dmagic != DESC_MAGIC or rd32(desc, 4092) != meta_csum(desc, j0 + 1)
                or count == 0 or count > mx or dseq < seq):
            return None
        commit = self.img.read(j0 + 2 + count)
        cmagic, ccount, cseq, dcrc = struct.unpack_from("<IIQI", commit)
        if (cmagic != COMMIT_MAGIC or rd32(commit, 4092) != meta_csum(commit, j0 + 2 + count)
                or cseq != dseq or ccount != count):
            return None
        targets = struct.unpack_from("<%dQ" % count, desc, 16)
        for t in targets:
            if t >= s["total_blocks"] or j0 <= t < j0 + jn:
                raise Corrupt("journal target %d out of range" % t)
        images = [self.img.read(j0 + 2 + i) for i in range(count)]
        crc = 0
        for im in images:
            crc = crc32c(im, crc)
        if crc != dcrc:
            return None                     # torn journal write: discard
        info = (dseq, list(zip(targets, images)))
        if not apply:
            for t, im in info[1]:
                self.overlay[t] = im
            # the journaled SB image is authoritative once replayed
            if 0 in self.overlay:
                s2 = super_valid(self.overlay[0], 0, self.img.blocks)
                if s2:
                    self.s, self.sb_raw = s2, self.overlay[0]
        return info

    def replay(self):
        """Apply a pending committed transaction to disk and retire it."""
        if not self.pending_journal:
            return 0
        seq, pairs = self.pending_journal
        for t, im in pairs:
            self.img.write(t, im)
        self.img.flush()
        s = self.s
        jsb = bytearray(BS)
        struct.pack_into("<IIQQI", jsb, 0, JSB_MAGIC, 1, seq + 1, s["journal_blocks"], 0)
        wr32(jsb, 4092, meta_csum(jsb, s["journal_start"]))
        self.img.write(s["journal_start"], bytes(jsb))
        self.img.flush()
        self.overlay.clear()
        self.pending_journal = None
        self.replayed = (seq, len(pairs))
        return len(pairs)

    # inodes ------------------------------------------------------------
    def iget(self, ino):
        if ino == 0 or ino >= self.s["inode_count"]:
            raise Corrupt("inode %d out of range" % ino)
        blk = self.s["inode_table_start"] + ino // IPB
        off = (ino % IPB) * INODE_SIZE
        raw = self.read(blk)[off:off + INODE_SIZE]
        if raw == bytes(INODE_SIZE):
            return None
        if rd32(raw, 252) != inode_csum(raw, ino):
            raise Corrupt("inode %d checksum mismatch" % ino)
        i = Inode.unpack(ino, raw)
        if not (i.is_dir() or i.is_reg()) or i.tindirect or \
                i.size > MAX_FILE_BLOCKS * BS or i.blocks > self.s["total_blocks"]:
            raise Corrupt("inode %d invalid" % ino)
        return i

    def iput(self, inode, clear=False):
        blk = self.s["inode_table_start"] + inode.ino // IPB
        off = (inode.ino % IPB) * INODE_SIZE
        b = bytearray(self.read(blk))
        b[off:off + INODE_SIZE] = bytes(INODE_SIZE) if clear else inode.pack()
        self.put(blk, b)

    def block_ok(self, n):
        return self.s["data_start"] <= n < self.s["total_blocks"] - 1

    def indirect(self, n, owner):
        if not self.block_ok(n):
            raise Corrupt("inode %d references invalid block %d" % (owner, n))
        b = self.read(n)
        if rd32(b, 4088) != owner or rd32(b, 4092) != meta_csum(b, n):
            raise Corrupt("indirect block %d (inode %d) checksum/owner mismatch" % (n, owner))
        return list(struct.unpack_from("<%dI" % PTRS, b))

    def bmap(self, inode, index):
        if index < DIRECT:
            return inode.direct[index]
        index -= DIRECT
        if index < PTRS:
            return self.indirect(inode.indirect, inode.ino)[index] if inode.indirect else 0
        index -= PTRS
        if not inode.dindirect:
            return 0
        mid = self.indirect(inode.dindirect, inode.ino)[index // PTRS]
        return self.indirect(mid, inode.ino)[index % PTRS] if mid else 0

    def read_file(self, inode):
        out = bytearray()
        for idx in range(div_up(inode.size, BS)):
            n = self.bmap(inode, idx)
            if n and not self.block_ok(n):
                raise Corrupt("inode %d data block %d invalid" % (inode.ino, n))
            out += self.read(n) if n else bytes(BS)
        return bytes(out[:inode.size])

    # directories -------------------------------------------------------
    def dir_block(self, dino, n):
        b = self.read(n)
        if rd32(b, 0) != DIR_MAGIC or rd32(b, 4) != dino or rd32(b, 8) != dir_csum(b, n):
            raise Corrupt("directory %d block %d checksum/owner mismatch" % (dino, n))
        return b

    def rec_valid(self, d, off):
        ino, rec_len, name_len = struct.unpack_from("<IHB", d, off)
        if (rec_len < 16 or rec_len & 7 or off + rec_len > BS
                or (ino and (name_len == 0 or 8 + name_len > rec_len))
                or ino >= self.s["inode_count"]):
            return 0
        return rec_len

    def entries(self, d_inode):
        for idx in range(d_inode.size // BS):
            n = self.bmap(d_inode, idx)
            if not n:
                raise Corrupt("directory %d has a hole at %d" % (d_inode.ino, idx))
            d = self.dir_block(d_inode.ino, n)
            off = DIR_HEADER
            while off < BS:
                rec_len = self.rec_valid(d, off)
                if not rec_len:
                    raise Corrupt("malformed directory block %d" % n)
                ino = rd32(d, off)
                if ino:
                    ln = d[off + 6]
                    yield bytes(d[off + 8:off + 8 + ln]), ino, d[off + 7]
                off += rec_len

    def lookup(self, path):
        inode = self.iget(ROOT)
        parts = [p for p in path.split("/") if p]
        for p in parts:
            if not inode.is_dir():
                raise Corrupt("%s: not a directory" % path)
            want = p.encode()
            for name, ino, _ in self.entries(inode):
                if name == want:
                    inode = self.iget(ino)
                    break
            else:
                raise FileNotFoundError(path)
        return inode

    # allocation (host edits) -------------------------------------------
    def _bitmap_alloc(self, start, nblocks, nbits, lo):
        for bi in range(nblocks):
            n = start + bi
            b = bytearray(self.read(n))
            if rd32(b, 4088) != BITMAP_MAGIC or rd32(b, 4092) != meta_csum(b, n):
                raise Corrupt("bitmap block %d checksum mismatch" % n)
            base = bi * BITS
            for byte in range(4088):
                if b[byte] == 0xFF:
                    continue
                for bit in range(8):
                    idx = base + byte * 8 + bit
                    if idx < lo or idx >= nbits or b[byte] & (1 << bit):
                        continue
                    b[byte] |= 1 << bit
                    wr32(b, 4092, meta_csum(b, n))
                    self.put(n, b)
                    return idx
        raise OSError(28, "No space left on device")

    def _bitmap_clear(self, start, idx):
        n = start + idx // BITS
        b = bytearray(self.read(n))
        bit = idx % BITS
        b[bit >> 3] &= ~(1 << (bit & 7)) & 0xFF
        wr32(b, 4092, meta_csum(b, n))
        self.put(n, b)

    def alloc_block(self):
        if self.s["free_blocks"] == 0:
            raise OSError(28, "No space left on device")
        n = self._bitmap_alloc(self.s["block_bitmap_start"], self.s["block_bitmap_blocks"],
                               self.s["total_blocks"] - 1, self.s["data_start"])
        self.s["free_blocks"] -= 1
        return n

    def free_block(self, n):
        self._bitmap_clear(self.s["block_bitmap_start"], n)
        self.s["free_blocks"] += 1

    def alloc_inode(self):
        if self.s["free_inodes"] == 0:
            raise OSError(28, "No space left on device (inodes)")
        n = self._bitmap_alloc(self.s["inode_bitmap_start"], self.s["inode_bitmap_blocks"],
                               self.s["inode_count"], LOSTFOUND + 1)
        self.s["free_inodes"] -= 1
        self.s["generation_counter"] += 1
        return n, self.s["generation_counter"] & 0xFFFFFFFF

    def _new_indirect(self, owner):
        n = self.alloc_block()
        return n, [0] * PTRS

    def _write_indirect(self, n, owner, ptrs):
        b = bytearray(BS)
        struct.pack_into("<%dI" % PTRS, b, 0, *ptrs)
        wr32(b, 4088, owner)
        wr32(b, 4092, meta_csum(b, n))
        self.put(n, b)

    def set_blocks(self, inode, blocks):
        """Assign a fresh block list (inode must currently have none)."""
        if len(blocks) > MAX_FILE_BLOCKS:
            raise OSError(27, "File too large")
        for i in range(min(DIRECT, len(blocks))):
            inode.direct[i] = blocks[i]
        rest = blocks[DIRECT:]
        count = len(blocks)
        if rest:
            leaf = rest[:PTRS]
            inode.indirect, _ = self._new_indirect(inode.ino)
            self._write_indirect(inode.indirect, inode.ino, leaf + [0] * (PTRS - len(leaf)))
            count += 1
            rest = rest[PTRS:]
        if rest:
            inode.dindirect, _ = self._new_indirect(inode.ino)
            count += 1
            mids = []
            while rest:
                leaf, rest = rest[:PTRS], rest[PTRS:]
                m, _ = self._new_indirect(inode.ino)
                self._write_indirect(m, inode.ino, leaf + [0] * (PTRS - len(leaf)))
                mids.append(m)
                count += 1
            self._write_indirect(inode.dindirect, inode.ino, mids + [0] * (PTRS - len(mids)))
        inode.blocks = count

    def all_blocks(self, inode):
        out = [b for b in inode.direct if b]
        if inode.indirect:
            out.append(inode.indirect)
            out += [b for b in self.indirect(inode.indirect, inode.ino) if b]
        if inode.dindirect:
            out.append(inode.dindirect)
            for m in self.indirect(inode.dindirect, inode.ino):
                if m:
                    out.append(m)
                    out += [b for b in self.indirect(m, inode.ino) if b]
        return out

    def dir_add(self, d_inode, name, ino, ftype):
        need = rec_align(8 + len(name))
        for idx in range(d_inode.size // BS):
            n = self.bmap(d_inode, idx)
            d = bytearray(self.dir_block(d_inode.ino, n))
            off = DIR_HEADER
            while off < BS:
                rec_len = self.rec_valid(d, off)
                if not rec_len:
                    raise Corrupt("malformed directory block %d" % n)
                rino = rd32(d, off)
                if not rino and rec_len >= need:
                    rec_fill(d, off, ino, rec_len, name, ftype)
                    self.put(n, seal_dir(d, n))
                    return
                if rino:
                    used = rec_align(8 + d[off + 6])
                    if rec_len - used >= need and rec_len - used >= 16:
                        struct.pack_into("<H", d, off + 4, used)
                        rec_fill(d, off + used, ino, rec_len - used, name, ftype)
                        self.put(n, seal_dir(d, n))
                        return
                off += rec_len
        idx = d_inode.size // BS
        if idx >= DIRECT:
            raise OSError(27, "host tool: directory larger than %d blocks" % DIRECT)
        n = self.alloc_block()
        d = new_dir_block(d_inode.ino)
        rec_fill(d, DIR_HEADER, ino, BS - DIR_HEADER, name, ftype)
        self.put(n, seal_dir(d, n))
        d_inode.direct[idx] = n
        d_inode.blocks += 1
        d_inode.size += BS

    def dir_remove(self, d_inode, name):
        for idx in range(d_inode.size // BS):
            n = self.bmap(d_inode, idx)
            d = bytearray(self.dir_block(d_inode.ino, n))
            off, prev = DIR_HEADER, None
            while off < BS:
                rec_len = self.rec_valid(d, off)
                if not rec_len:
                    raise Corrupt("malformed directory block %d" % n)
                if rd32(d, off) and bytes(d[off + 8:off + 8 + d[off + 6]]) == name:
                    if prev is None:
                        wr32(d, off, 0)
                    else:
                        plen = struct.unpack_from("<H", d, prev + 4)[0]
                        struct.pack_into("<H", d, prev + 4, plen + rec_len)
                    self.put(n, seal_dir(d, n))
                    return
                prev = off
                off += rec_len
        raise FileNotFoundError(name)

    def split(self, path):
        parts = [p for p in path.split("/") if p]
        if not parts:
            raise OSError(22, "invalid path")
        name = parts[-1].encode()
        if len(name) > 255 or name in (b".", b".."):
            raise OSError(22, "invalid name")
        parent = self.lookup("/".join(parts[:-1]))
        if not parent.is_dir():
            raise OSError(20, "not a directory")
        return parent, name

    def begin_edit(self):
        s = self.s
        if s["state"] & STATE_ERROR:
            raise Corrupt("filesystem has the ERROR flag set; run fsck --repair first")
        if self.replay():
            print("zjfs: replayed pending journal transaction %d (%d blocks)" % self.replayed)
        if s["orphan_head"]:
            raise Corrupt("filesystem has pending orphans; mount in ZEROOS or fsck --repair")
        self.write_super(clean=False)

    def put_file(self, path, data):
        now = time.time_ns()
        parent, name = self.split(path)
        existing = None
        for n, ino, _ in self.entries(parent):
            if n == name:
                existing = self.iget(ino)
        if existing is not None:
            if not existing.is_reg():
                raise OSError(21, "is a directory")
            if existing.links != 1:
                raise OSError(95, "host tool: replacing multiply-linked files unsupported")
            for b in self.all_blocks(existing):
                self.free_block(b)
            inode = existing
            inode.direct = [0] * DIRECT
            inode.indirect = inode.dindirect = 0
            inode.mtime = inode.ctime = now
        else:
            ino, gen = self.alloc_inode()
            inode = Inode.new(ino, S_IFREG | 0o644, now, gen)
        blocks = []
        nblocks = div_up(len(data), BS)
        for i in range(nblocks):
            chunk = data[i * BS:(i + 1) * BS]
            n = self.alloc_block()
            self.put(n, chunk + bytes(BS - len(chunk)))
            blocks.append(n)
        self.set_blocks(inode, blocks)
        inode.size = len(data)
        self.iput(inode)
        if existing is None:
            self.dir_add(parent, name, inode.ino, FT_REG)
            parent.mtime = parent.ctime = now
            self.iput(parent)
        return inode

    def mkdir(self, path):
        now = time.time_ns()
        parent, name = self.split(path)
        for n, _, _ in self.entries(parent):
            if n == name:
                raise FileExistsError(path)
        ino, gen = self.alloc_inode()
        inode = Inode.new(ino, S_IFDIR | 0o755, now, gen, parent.ino)
        n = self.alloc_block()
        self.put(n, seal_dir(new_dir_block(ino), n))
        inode.direct[0], inode.size, inode.blocks, inode.links = n, BS, 1, 2
        self.iput(inode)
        self.dir_add(parent, name, ino, FT_DIR)
        parent.links += 1
        parent.mtime = parent.ctime = now
        self.iput(parent)

    # fsck --------------------------------------------------------------
    def fsck(self, repair, log):
        s = self.s
        r = dict(checksum_errors=0, structure_errors=0, leaked_blocks=0, leaked_inodes=0,
                 link_errors=0, counter_errors=0, repaired=0, fatal=0, inodes_used=0,
                 blocks_used=0)
        total, ninodes = s["total_blocks"], s["inode_count"]
        used = bytearray(total)
        for b in range(s["data_start"]):
            used[b] = 1
        used[total - 1] = 1
        inodes = {}

        def ibit(ino):
            n = s["inode_bitmap_start"] + ino // BITS
            b = self.read(n)
            if rd32(b, 4088) != BITMAP_MAGIC or rd32(b, 4092) != meta_csum(b, n):
                raise Corrupt("inode bitmap block %d checksum mismatch" % n)
            bit = ino % BITS
            return (b[bit >> 3] >> (bit & 7)) & 1

        def set_ibit(ino, v):
            n = s["inode_bitmap_start"] + ino // BITS
            b = bytearray(self.read(n))
            bit = ino % BITS
            if v:
                b[bit >> 3] |= 1 << (bit & 7)
            else:
                b[bit >> 3] &= ~(1 << (bit & 7)) & 0xFF
            wr32(b, 4092, meta_csum(b, n))
            self.put(n, b)

        def mark(block, ino):
            if not self.block_ok(block):
                r["structure_errors"] += 1
                log("inode %d references invalid block %d" % (ino, block))
                return False
            if used[block]:
                r["structure_errors"] += 1
                r["fatal"] += 1
                log("block %d cross-linked (inode %d)" % (block, ino))
                return False
            used[block] = 1
            return True

        # Pass 1: inodes
        for ino in range(1, ninodes):
            inb = ibit(ino)
            try:
                inode = self.iget(ino)
            except Corrupt as e:
                r["checksum_errors"] += 1
                r["fatal"] += 1
                log(str(e))
                continue
            if inode is None:
                if inb and ino > LOSTFOUND:
                    r["leaked_inodes"] += 1
                    log("inode %d marked used but free" % ino)
                    if repair:
                        set_ibit(ino, 0)
                        r["repaired"] += 1
                continue
            inodes[ino] = inode
            r["inodes_used"] += 1
            if not inb:
                r["structure_errors"] += 1
                log("inode %d in use but free in bitmap" % ino)
                if repair:
                    set_ibit(ino, 1)
                    r["repaired"] += 1
            count = 0
            for b in inode.direct:
                if b and mark(b, ino):
                    count += 1
            try:
                if inode.indirect and mark(inode.indirect, ino):
                    count += 1
                    count += sum(1 for b in self.indirect(inode.indirect, ino) if b and mark(b, ino))
                if inode.dindirect and mark(inode.dindirect, ino):
                    count += 1
                    for m in self.indirect(inode.dindirect, ino):
                        if m and mark(m, ino):
                            count += 1
                            count += sum(1 for b in self.indirect(m, ino) if b and mark(b, ino))
            except Corrupt as e:
                r["checksum_errors"] += 1
                r["fatal"] += 1
                log(str(e))
            if count != inode.blocks:
                r["counter_errors"] += 1
                log("inode %d blocks=%d counted=%d" % (ino, inode.blocks, count))
                if repair:
                    inode.blocks = count
                    self.iput(inode)
                    r["repaired"] += 1
            if inode.is_dir() and inode.size % BS:
                r["structure_errors"] += 1
                r["fatal"] += 1
                log("directory %d size %d not block-aligned" % (ino, inode.size))
        # Pass 2: directory tree
        refs = {ROOT: 1}
        subdirs = {}
        queue = [ROOT]
        seen_dirs = {ROOT}
        while queue:
            dino = queue.pop(0)
            d = inodes.get(dino)
            if d is None or not d.is_dir():
                if dino == ROOT:
                    r["fatal"] += 1
                    log("root directory missing or not a directory")
                continue
            try:
                for name, target, ftype in list(self.entries(d)):
                    if target not in inodes:
                        r["structure_errors"] += 1
                        r["fatal"] += 1
                        log("dir %d entry %r points to free/invalid inode %d" % (dino, name, target))
                        continue
                    if ftype == FT_DIR:
                        if target in seen_dirs:
                            r["structure_errors"] += 1
                            r["fatal"] += 1
                            log("directory %d linked twice" % target)
                        else:
                            seen_dirs.add(target)
                            queue.append(target)
                            subdirs[dino] = subdirs.get(dino, 0) + 1
                    refs[target] = refs.get(target, 0) + 1
            except Corrupt as e:
                r["checksum_errors"] += 1
                r["fatal"] += 1
                log(str(e))
        # Pass 3: link counts / reachability
        lf = inodes.get(LOSTFOUND)
        for ino, inode in sorted(inodes.items()):
            if refs.get(ino, 0) == 0:
                if inode.flags & IF_ORPHAN:
                    continue
                r["leaked_inodes"] += 1
                log("inode %d unreachable" % ino)
                if repair and lf is not None and lf.is_dir():
                    ftype = FT_DIR if inode.is_dir() else FT_REG
                    self.dir_add(lf, ("#%d" % ino).encode(), ino, ftype)
                    if inode.is_dir():
                        inode.parent = LOSTFOUND
                        lf.links += 1
                        inode.links = 2 + subdirs.get(ino, 0)
                    else:
                        inode.links = 1
                    self.iput(lf)
                    self.iput(inode)
                    r["repaired"] += 1
                continue
            expected = 2 + subdirs.get(ino, 0) if inode.is_dir() else refs[ino]
            if inode.links != expected:
                r["link_errors"] += 1
                log("inode %d links=%d expected=%d" % (ino, inode.links, expected))
                if repair:
                    inode.links = expected
                    self.iput(inode)
                    r["repaired"] += 1
        # Pass 4: block bitmap
        for bi in range(s["block_bitmap_blocks"]):
            n = s["block_bitmap_start"] + bi
            b = bytearray(self.read(n))
            if rd32(b, 4088) != BITMAP_MAGIC or rd32(b, 4092) != meta_csum(b, n):
                r["checksum_errors"] += 1
                r["fatal"] += 1
                log("block bitmap %d checksum mismatch" % n)
                continue
            changed = False
            for bit in range(BITS):
                blk = bi * BITS + bit
                if blk >= total:
                    break
                have = (b[bit >> 3] >> (bit & 7)) & 1
                want = used[blk]
                if want:
                    r["blocks_used"] += 1
                if have == want:
                    continue
                if have:
                    r["leaked_blocks"] += 1
                else:
                    r["structure_errors"] += 1
                    log("block %d in use but free in bitmap" % blk)
                if repair:
                    if want:
                        b[bit >> 3] |= 1 << (bit & 7)
                    else:
                        b[bit >> 3] &= ~(1 << (bit & 7)) & 0xFF
                    changed = True
                    r["repaired"] += 1
            if changed:
                wr32(b, 4092, meta_csum(b, n))
                self.put(n, b)
        if r["leaked_blocks"]:
            log("%d leaked blocks" % r["leaked_blocks"])
        free_blocks = total - r["blocks_used"]
        free_inodes = sum(1 for ino in range(ninodes) if not ibit(ino))
        if s["free_blocks"] != free_blocks or s["free_inodes"] != free_inodes:
            r["counter_errors"] += 1
            log("superblock counters free_blocks=%d/%d free_inodes=%d/%d (stored/actual)" % (
                s["free_blocks"], free_blocks, s["free_inodes"], free_inodes))
            if repair:
                s["free_blocks"], s["free_inodes"] = free_blocks, free_inodes
                r["repaired"] += 1
        return r


# ----------------------------------------------------------------- main
def open_image(a, writable):
    offset, length = 0, None
    if a.part:
        offset, length = gpt.partition_extent(a.image, a.part)
    elif a.offset is not None:
        offset, length = a.offset, a.length
    return Image(a.image, offset, length, writable)


def main(argv):
    ap = argparse.ArgumentParser(description="ZEROOS ZJFS host tool")
    ap.add_argument("--part", type=int, help="GPT partition number inside IMAGE")
    ap.add_argument("--offset", type=int)
    ap.add_argument("--length", type=int)
    ap.add_argument("image")
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("mkfs")
    m.add_argument("--label", default="zeroos")
    m.add_argument("--inodes", type=int, default=0)
    m.add_argument("--journal-blocks", type=int, default=0)
    m.add_argument("--force", action="store_true")
    sub.add_parser("info")
    f = sub.add_parser("fsck")
    f.add_argument("--repair", action="store_true")
    ls = sub.add_parser("ls")
    ls.add_argument("path", nargs="?", default="/")
    c = sub.add_parser("cat")
    c.add_argument("path")
    p = sub.add_parser("put")
    p.add_argument("local")
    p.add_argument("path")
    md = sub.add_parser("mkdir")
    md.add_argument("path")
    sub.add_parser("get-counter")
    a = ap.parse_args(argv)
    writes = a.cmd in ("mkfs", "put", "mkdir") or (a.cmd == "fsck" and a.repair)
    try:
        img = open_image(a, writes)
    except (OSError, gpt.GptError) as e:
        print("zjfs: cannot open: %s" % e, file=sys.stderr)
        return 3
    try:
        if a.cmd == "mkfs":
            s = mkfs(img, a.label, a.inodes, a.journal_blocks, a.force)
            print("zjfs: formatted %d blocks, %d inodes, journal %d blocks, data_start %d" % (
                s["total_blocks"], s["inode_count"], s["journal_blocks"], s["data_start"]))
            return 0
        fs = FS(img, writes)
        if a.cmd == "info":
            s = fs.s
            st = [n for n, bit in (("CLEAN", 1), ("DIRTY", 2), ("ERROR", 4)) if s["state"] & bit]
            print("label=%s blocks=%d free_blocks=%d inodes=%d free_inodes=%d state=%s "
                  "mounts=%d errors=%d journal=%s backup_sb=%s" % (
                      s["label"].rstrip(b"\0").decode(errors="replace"), s["total_blocks"],
                      s["free_blocks"], s["inode_count"], s["free_inodes"], "|".join(st) or "0",
                      s["mount_count"], s["error_count"],
                      "pending seq %d (%d blocks)" % (fs.pending_journal[0],
                                                       len(fs.pending_journal[1]))
                      if fs.pending_journal else "clean",
                      "used" if fs.used_backup else "no"))
            return 0
        if a.cmd == "fsck":
            if fs.used_backup:
                print("zjfs fsck: primary superblock invalid; using backup")
            if fs.pending_journal:
                seq, pairs = fs.pending_journal
                if a.repair:
                    fs.replay()
                    print("zjfs fsck: replayed journal transaction %d (%d blocks)" % (seq, len(pairs)))
                else:
                    print("zjfs fsck: journal transaction %d pending (%d blocks); checking the "
                          "replayed view read-only" % (seq, len(pairs)))
            msgs = []
            r = fs.fsck(a.repair, msgs.append)
            for msg in msgs[:50]:
                print("zjfs fsck: " + msg)
            if len(msgs) > 50:
                print("zjfs fsck: ... %d more" % (len(msgs) - 50))
            errors = (r["checksum_errors"] + r["structure_errors"] + r["leaked_blocks"] +
                      r["leaked_inodes"] + r["link_errors"] + r["counter_errors"])
            error_flag = bool(fs.s["state"] & STATE_ERROR)
            unclean = not (fs.s["state"] & STATE_CLEAN)
            if a.repair:
                if r["fatal"] == 0:
                    fs.s["state"] &= ~STATE_ERROR
                    fs.s["last_check_time"] = time.time_ns()
                    fs.commit(clean=True)
                else:
                    fs.commit(clean=False)
            print("zjfs fsck: inodes_used=%d blocks_used=%d checksum=%d structure=%d leaked_blocks=%d "
                  "leaked_inodes=%d links=%d counters=%d repaired=%d fatal=%d error_flag=%d unclean=%d" % (
                      r["inodes_used"], r["blocks_used"], r["checksum_errors"],
                      r["structure_errors"], r["leaked_blocks"], r["leaked_inodes"],
                      r["link_errors"], r["counter_errors"], r["repaired"], r["fatal"],
                      int(error_flag), int(unclean)))
            if r["fatal"]:
                print("zjfs fsck: RESULT fatal")
                return 2
            if errors or (error_flag and not a.repair):
                print("zjfs fsck: RESULT %s" % ("repaired" if a.repair else "errors"))
                return 1
            print("zjfs fsck: RESULT clean")
            return 0
        if a.cmd == "ls":
            inode = fs.lookup(a.path)
            if not inode.is_dir():
                print("%10d %s" % (inode.size, a.path))
                return 0
            for name, ino, ftype in fs.entries(inode):
                child = fs.iget(ino)
                print("%s %6o %4d %10d %s" % ("d" if ftype == FT_DIR else "-",
                                              child.mode & 0o7777, child.links, child.size,
                                              name.decode(errors="replace")))
            return 0
        if a.cmd == "cat":
            inode = fs.lookup(a.path)
            if not inode.is_reg():
                raise OSError(21, "is a directory")
            sys.stdout.buffer.write(fs.read_file(inode))
            return 0
        if a.cmd == "get-counter":
            try:
                data = fs.read_file(fs.lookup("/.zeroos-boot-count"))
                print(int(data.strip() or b"0"))
            except FileNotFoundError:
                print(0)
            return 0
        if a.cmd in ("put", "mkdir"):
            fs.begin_edit()
            if a.cmd == "put":
                with open(a.local, "rb") as fh:
                    data = fh.read()
                inode = fs.put_file(a.path, data)
                print("zjfs: wrote %s (%d bytes, inode %d, crc32c=0x%x)" % (
                    a.path, len(data), inode.ino, crc32c(data)))
            else:
                fs.mkdir(a.path)
                print("zjfs: created directory %s" % a.path)
            fs.commit(clean=True)
            return 0
    except FileNotFoundError as e:
        print("zjfs: no such file: %s" % e, file=sys.stderr)
        return 1
    except Corrupt as e:
        print("zjfs: corrupt or unsupported filesystem: %s" % e, file=sys.stderr)
        return 2
    except OSError as e:
        print("zjfs: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
