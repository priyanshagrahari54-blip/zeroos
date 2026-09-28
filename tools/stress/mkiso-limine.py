#!/usr/bin/env python3
"""Build a BIOS-bootable Limine ISO (multiboot2) for local ZEROOS testing.

Usage: mkiso-limine.py KERNEL.ELF OUT.ISO [KERNEL_CMDLINE]
"""
import io, os, sys
# pycdlib: pip install pycdlib (or put it on PYTHONPATH)
import pycdlib
elf, out = sys.argv[1], sys.argv[2]
cmdline = sys.argv[3] if len(sys.argv) > 3 else ""
L = os.environ.get('LIMINE_DIR', 'limine').rstrip('/') + '/'
conf = ("timeout: 0\n\n/ZEROOS\n    protocol: multiboot2\n    path: boot():/boot/zeroos.elf\n"
        + (f"    cmdline: {cmdline}\n" if cmdline else "")).encode()
iso = pycdlib.PyCdlib()
iso.new(interchange_level=3, rock_ridge='1.09', joliet=3)
for d in ('/BOOT', '/BOOT/LIMINE'):
    iso.add_directory(d, rr_name=d.rsplit('/', 1)[1].lower(), joliet_path=d.lower())
def add(data, path, rr):
    iso.add_fp(io.BytesIO(data), len(data), path + ';1', rr_name=rr,
               joliet_path='/' + '/'.join(p.lower() for p in path.strip('/').split('/')[:-1] + [rr]))
add(open(elf, 'rb').read(), '/BOOT/ZEROOS.ELF', 'zeroos.elf')
add(conf, '/BOOT/LIMINE/LIMINE.CONF', 'limine.conf')
add(open(L + 'limine-bios.sys', 'rb').read(), '/BOOT/LIMINE/LIMINE_BIOS.SYS', 'limine-bios.sys')
add(open(L + 'limine-bios-cd.bin', 'rb').read(), '/BOOT/LIMINE/LIMINE_BIOS_CD.BIN', 'limine-bios-cd.bin')
iso.add_eltorito('/BOOT/LIMINE/LIMINE_BIOS_CD.BIN;1', media_name='noemul',
                 boot_load_size=4, boot_info_table=True)
iso.write(out)
iso.close()
