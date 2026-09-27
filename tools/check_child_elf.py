#!/usr/bin/env python3
"""Validate a ZEROOS Ring-3 ELF image the way the kernel loader does.

The kernel refuses an image whose program headers do not describe a usable
W^X user mapping, and it does so at spawn time -- i.e. in the guest, during
boot certification.  A malformed linker script therefore costs a full CI
run to discover.  This gate applies the same structural rules on the host:

  * ELF64, little endian, ET_EXEC, x86-64
  * at least one PT_LOAD
  * every PT_LOAD inside the user window, and below the user stack page
  * no segment both writable and executable (W^X)
  * the entry point inside an executable PT_LOAD

Usage: tools/check_child_elf.py <elf>
"""
import struct
import sys

USER_BASE = 0x00007F0000000000
USER_STACK_PAGE = 0x00007F00001FF000

ET_EXEC = 2
EM_X86_64 = 62
PT_LOAD = 1
PF_X = 1
PF_W = 2


def fail(message):
    sys.stderr.write('child-elf-check: %s\n' % message)
    return 1


def main(path):
    with open(path, 'rb') as handle:
        image = handle.read()
    if len(image) < 64 or image[:4] != b'\x7fELF':
        return fail('not an ELF image')
    if image[4] != 2 or image[5] != 1:
        return fail('expected ELF64 little-endian')
    (e_type, e_machine, _, _, e_phoff, _, _, _, e_phentsize, e_phnum, _, _,
     _) = struct.unpack_from('<HHIQQQIHHHHHH', image, 16)
    if e_type != ET_EXEC:
        return fail('e_type=%d, expected ET_EXEC' % e_type)
    if e_machine != EM_X86_64:
        return fail('e_machine=%d, expected x86-64' % e_machine)
    if e_phnum == 0:
        return fail('no program headers')
    entry = struct.unpack_from('<Q', image, 24)[0]
    executable = []
    loads = 0
    for index in range(e_phnum):
        offset = e_phoff + index * e_phentsize
        p_type, p_flags, p_offset, p_vaddr, _, _, p_filesz, p_memsz, _ = \
            struct.unpack_from('<IIQQQQQQQ', image, offset)
        if p_type != PT_LOAD:
            continue
        loads += 1
        if p_vaddr < USER_BASE:
            return fail('PT_LOAD %d at 0x%x is below the user base' %
                        (index, p_vaddr))
        if p_vaddr + p_memsz > USER_STACK_PAGE:
            return fail('PT_LOAD %d at 0x%x reaches the user stack page' %
                        (index, p_vaddr))
        if p_flags & PF_W and p_flags & PF_X:
            return fail('PT_LOAD %d is writable and executable' % index)
        if p_flags & PF_X:
            executable.append((p_vaddr, p_memsz))
    if loads == 0:
        return fail('no PT_LOAD segments')
    for vaddr, memsz in executable:
        if vaddr <= entry < vaddr + memsz:
            return 0
    return fail('entry 0x%x is not inside an executable PT_LOAD' % entry)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1]))
