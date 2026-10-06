#!/usr/bin/env python3
"""ZEROOS Multiboot2 header conformance checker.

A Multiboot2 kernel that boots in QEMU can still fail on real firmware or a
different GRUB because of the header, and the header cannot be exercised
without booting. This script validates the header directly against the
Multiboot2 specification (version 3.1.2 and 3.1.10) by reading the linked ELF:

  * the header is present and 8-byte aligned,
  * magic is 0xE85250D6 and the architecture field is 0 (i386 protected mode),
  * magic + architecture + header_length + checksum == 0 (mod 2^32),
  * the header lies wholly inside a loaded program segment, and inside the
    first 32768 bytes of that segment's file image, which is where a bootloader
    is required to search,
  * the tag list is well formed and terminated by the end tag,
  * if an address tag is present its entry address equals the ELF entry point,
  * if a framebuffer tag is present it matches the mode grub.cfg requests.

Exit status is 0 only when every check passes.

Usage: verify_multiboot2.py <kernel.elf> [--expect-wxhxb WxHxB]
"""

import struct
import sys

MAGIC = 0xE85250D6
MAX_SEARCH = 32768

TAG_NAMES = {
    0: "end",
    1: "information request",
    2: "address",
    3: "entry address",
    4: "console flags",
    5: "framebuffer",
    6: "module align",
    7: "relocatable",
    10: "EFI boot services",
    11: "entry address (EFI 32)",
    12: "entry address (EFI 64)",
    13: "EFI PE image",
}


class Report:
    def __init__(self):
        self.failures = []

    def check(self, cond, ok_msg, fail_msg):
        print(f"  {'ok  ' if cond else 'FAIL'} {ok_msg if cond else fail_msg}")
        if not cond:
            self.failures.append(fail_msg)
        return bool(cond)

    def ok(self, msg):
        print(f"  ok   {msg}")


def parse_elf64(blob):
    """Return (entry, [(type, flags, offset, vaddr, filesz, memsz)], sections)."""
    if blob[:4] != b"\x7fELF":
        raise SystemExit("not an ELF file")
    if blob[4] != 2:
        raise SystemExit("not a 64-bit ELF")
    e_entry = struct.unpack_from("<Q", blob, 24)[0]
    e_phoff = struct.unpack_from("<Q", blob, 32)[0]
    e_shoff = struct.unpack_from("<Q", blob, 40)[0]
    e_phentsize = struct.unpack_from("<H", blob, 54)[0]
    e_phnum = struct.unpack_from("<H", blob, 56)[0]
    e_shentsize = struct.unpack_from("<H", blob, 58)[0]
    e_shnum = struct.unpack_from("<H", blob, 60)[0]
    e_shstrndx = struct.unpack_from("<H", blob, 62)[0]

    segments = []
    for i in range(e_phnum):
        base = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", blob, base)
        p_offset, p_vaddr = struct.unpack_from("<QQ", blob, base + 8)
        struct.unpack_from("<Q", blob, base + 24)          # p_paddr
        p_filesz, p_memsz = struct.unpack_from("<QQ", blob, base + 32)
        segments.append((p_type, p_flags, p_offset, p_vaddr, p_filesz, p_memsz))

    # Section headers, to locate .multiboot2 by name.
    shstr_base = e_shoff + e_shstrndx * e_shentsize
    shstr_off = struct.unpack_from("<Q", blob, shstr_base + 24)[0]
    sections = {}
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        name_off = struct.unpack_from("<I", blob, base)[0]
        sh_type = struct.unpack_from("<I", blob, base + 4)[0]
        sh_flags = struct.unpack_from("<Q", blob, base + 8)[0]
        sh_addr = struct.unpack_from("<Q", blob, base + 16)[0]
        sh_offset = struct.unpack_from("<Q", blob, base + 24)[0]
        sh_size = struct.unpack_from("<Q", blob, base + 32)[0]
        end = blob.index(b"\x00", shstr_off + name_off)
        name = blob[shstr_off + name_off:end].decode("ascii", "replace")
        sections[name] = (sh_type, sh_flags, sh_addr, sh_offset, sh_size)
    return e_entry, segments, sections


def read_header(blob, sections, rep):
    """Locate the Multiboot2 header and return (vaddr, file_offset, bytes)."""
    sec = sections.get(".multiboot2")
    if sec is None:
        # Fall back to searching the first 32 KiB, which is where the spec says
        # a bootloader looks.
        for off in range(0, min(MAX_SEARCH, len(blob) - 16), 8):
            if struct.unpack_from("<I", blob, off)[0] == MAGIC:
                rep.ok(f"header found by scan at file offset 0x{off:x}")
                return None, off
        rep.check(False, "", "no .multiboot2 section and no magic in the first 32 KiB")
        return None, None
    sh_type, _flags, sh_addr, sh_offset, sh_size = sec
    rep.ok(f".multiboot2 section: addr=0x{sh_addr:x} offset=0x{sh_offset:x} size={sh_size}")
    if struct.unpack_from("<I", blob, sh_offset)[0] != MAGIC:
        rep.check(False, "", f".multiboot2 section does not start with the magic 0x{MAGIC:08x}")
        return None, None
    return sh_addr, sh_offset


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[1]
    expect_fb = None
    if "--expect-wxhxb" in argv:
        expect_fb = argv[argv.index("--expect-wxhxb") + 1]

    with open(path, "rb") as fh:
        blob = fh.read()
    print(f"verify_multiboot2: {path} ({len(blob)} bytes)")

    rep = Report()
    e_entry, segments, sections = parse_elf64(blob)
    rep.ok(f"ELF entry point: 0x{e_entry:x}")

    hdr_vaddr, hdr_off = read_header(blob, sections, rep)
    if hdr_off is None:
        return 1 if rep.failures else 0

    magic, arch, hdr_len, checksum = struct.unpack_from("<IIII", blob, hdr_off)
    rep.check(magic == MAGIC, f"magic = 0x{magic:08x}", f"magic = 0x{magic:08x}, expected 0x{MAGIC:08x}")
    rep.check(arch == 0, f"architecture = {arch} (i386 protected mode)",
              f"architecture = {arch}, expected 0 (i386 protected mode)")
    rep.check(hdr_len >= 16, f"header_length = {hdr_len} bytes",
              f"header_length = {hdr_len}, must be at least 16")
    rep.check(hdr_len % 8 == 0, "header_length is 8-byte aligned",
              f"header_length {hdr_len} is not a multiple of 8")
    total = (magic + arch + hdr_len + checksum) & 0xFFFFFFFF
    rep.check(total == 0, f"checksum valid (sum mod 2^32 = {total})",
              f"checksum invalid: magic+arch+len+checksum = 0x{total:08x}, expected 0")

    # Alignment of the header itself within the image.
    rep.check(hdr_off % 8 == 0, f"header starts at 8-byte aligned offset 0x{hdr_off:x}",
              f"header offset 0x{hdr_off:x} is not 8-byte aligned")

    # The header must live inside a loaded segment, within the first 32 KiB of
    # the image as the bootloader sees it.
    load_segs = [s for s in segments if s[0] == 1]          # PT_LOAD
    containing = [s for s in load_segs
                  if s[2] <= hdr_off < s[2] + s[4]]
    rep.check(bool(containing),
              "header lies inside a PT_LOAD segment",
              "header is not inside any PT_LOAD segment - a bootloader loading "
              "program headers would never see it")
    for seg in containing:
        rel = hdr_off - seg[2]
        rep.check(rel + hdr_len <= MAX_SEARCH,
                  f"header ends {rel + hdr_len} bytes into the segment (< {MAX_SEARCH})",
                  f"header ends {rel + hdr_len} bytes into the segment, beyond the "
                  f"{MAX_SEARCH}-byte window a bootloader is required to search")
        rep.check(seg[4] > 0, "containing segment has file content",
                  "containing segment has zero file size")

    # Walk the tag list.
    tags = []
    pos = hdr_off + 16
    end = hdr_off + hdr_len
    well_formed = True
    while pos + 8 <= end:
        # Tag layout is u16 type, u16 flags, u32 size - the size is a 32-bit
        # field at offset +4, not the second 16-bit field.
        t_type, _t_flags = struct.unpack_from("<HH", blob, pos)
        t_size = struct.unpack_from("<I", blob, pos + 4)[0]
        if t_size < 8 or pos + t_size > end:
            well_formed = False
            break
        tags.append((t_type, pos, t_size))
        if t_type == 0:
            break
        pos += (t_size + 7) & ~7                       # tags are 8-byte aligned
    rep.check(well_formed, "tag list is well formed and 8-byte aligned",
              "tag list is malformed: a tag size is < 8 or overruns the header")
    rep.check(bool(tags) and tags[-1][0] == 0, "tag list terminated by the end tag",
              "tag list is not terminated by the end tag (type 0)")

    for t_type, t_pos, t_size in tags:
        name = TAG_NAMES.get(t_type, f"type {t_type}")
        rep.ok(f"tag: {name} (size {t_size})")
        if t_type == 3 and t_size >= 12:               # entry address
            entry = struct.unpack_from("<I", blob, t_pos + 8)[0]
            rep.check(entry == (e_entry & 0xFFFFFFFF),
                      f"entry address tag 0x{entry:x} matches the ELF entry point",
                      f"entry address tag 0x{entry:x} does not match ELF entry 0x{e_entry:x}")
        # A framebuffer tag is 20 bytes when it carries no colour info and 32
        # when it does; both are valid per the spec.
        if t_type == 5 and t_size >= 20:               # framebuffer
            width, height, depth = struct.unpack_from("<III", blob, t_pos + 8)
            rep.ok(f"framebuffer tag: {width}x{height}x{depth}")
            if expect_fb:
                want = expect_fb.lower().split("x")
                got = [str(width), str(height), str(depth)]
                rep.check(got == want,
                          f"framebuffer {width}x{height}x{depth} matches grub.cfg "
                          f"gfxpayload {expect_fb}",
                          f"framebuffer {width}x{height}x{depth} does not match "
                          f"grub.cfg gfxpayload {expect_fb}")

    print()
    if rep.failures:
        print(f"verify_multiboot2: FAIL ({len(rep.failures)} check(s) failed)")
        return 1
    print("verify_multiboot2: PASS - header conforms to the Multiboot2 specification")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
