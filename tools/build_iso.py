#!/usr/bin/env python3
"""
ZEROOS ISO9660 Image Builder
Generates a valid ISO-9660 Level 1 filesystem image containing the boot tree.
Enables self-contained ISO creation even when host grub-mkrescue is absent.
"""

import os
import struct
import sys
import time

SECTOR_SIZE = 2048

def to_both_endian_16(val):
    return struct.pack("<H", val) + struct.pack(">H", val)

def to_both_endian_32(val):
    return struct.pack("<I", val) + struct.pack(">I", val)

def pad_sector(data):
    rem = len(data) % SECTOR_SIZE
    if rem != 0:
        data += b"\x00" * (SECTOR_SIZE - rem)
    return data

def build_iso(src_dir, out_iso_path):
    # Collect files from src_dir
    entries = []  # list of (rel_path, abs_path, is_dir, size)
    for root, dirs, files in os.walk(src_dir):
        for d in dirs:
            full = os.path.join(root, d)
            rel = os.path.relpath(full, src_dir).replace("\\", "/")
            entries.append((rel, full, True, 0))
        for f in files:
            full = os.path.join(root, f)
            rel = os.path.relpath(full, src_dir).replace("\\", "/")
            size = os.path.getsize(full)
            entries.append((rel, full, False, size))

    entries.sort(key=lambda x: x[0])

    # Layout:
    # Sectors 0..15: System area (32768 bytes, 0s)
    # Sector 16: Primary Volume Descriptor
    # Sector 17: Volume Descriptor Set Terminator
    # Sector 18: Root Directory Sector
    # Subsequent sectors: file data and subdirectories

    current_sector = 19
    file_records = []
    file_data_bytes = bytearray()

    for rel, full, is_dir, size in entries:
        if is_dir:
            continue
        with open(full, "rb") as fh:
            data = fh.read()
        aligned_data = pad_sector(data)
        sec_count = len(aligned_data) // SECTOR_SIZE
        sec_start = current_sector
        current_sector += sec_count
        file_records.append((rel, sec_start, size))
        file_data_bytes.extend(aligned_data)

    # Build directory records for root
    # Format of Directory Record:
    # len (1B), ext_attr_len (1B), loc_extent (8B), data_len (8B), recording_date (7B),
    # flags (1B), file_unit_size (1B), interleave_gap (1B), vol_seq_num (4B),
    # file_id_len (1B), file_id (var), padding (0 or 1B)
    now = time.gmtime()
    date_rec = struct.pack("7B",
                           now.tm_year - 1900, now.tm_mon, now.tm_mday,
                           now.tm_hour, now.tm_min, now.tm_sec, 0)

    def make_dir_record(name_bytes, extent_sec, extent_size, is_directory):
        flags = 2 if is_directory else 0
        name_len = len(name_bytes)
        rec_len = 33 + name_len
        if rec_len % 2 != 0:
            rec_len += 1
            pad = b"\x00"
        else:
            pad = b""
        record = struct.pack("BB", rec_len, 0)
        record += to_both_endian_32(extent_sec)
        record += to_both_endian_32(extent_size)
        record += date_rec
        record += struct.pack("BBB", flags, 0, 0)
        record += to_both_endian_16(1)
        record += struct.pack("B", name_len)
        record += name_bytes
        record += pad
        return record

    # Root dir sector (Sector 18)
    root_dir_records = bytearray()
    root_self = make_dir_record(b"\x00", 18, SECTOR_SIZE, True)
    root_parent = make_dir_record(b"\x01", 18, SECTOR_SIZE, True)
    root_dir_records.extend(root_self)
    root_dir_records.extend(root_parent)

    for rel, sec_start, size in file_records:
        iso_name = rel.replace("/", "_").upper().encode("ascii") + b";1"
        rec = make_dir_record(iso_name, sec_start, size, False)
        root_dir_records.extend(rec)

    root_dir_sector = pad_sector(root_dir_records)

    # Sector 16: Primary Volume Descriptor (PVD)
    total_sectors = current_sector
    pvd = bytearray(SECTOR_SIZE)
    pvd[0] = 1  # Type 1: PVD
    pvd[1:6] = b"CD001"
    pvd[6] = 1  # Version 1
    pvd[8:40] = b"ZEROOS".ljust(32, b" ")
    pvd[40:72] = b"ZEROOS_BOOT".ljust(32, b" ")
    pvd[80:88] = to_both_endian_32(total_sectors)
    pvd[120:124] = to_both_endian_16(1)  # Volume set size
    pvd[124:128] = to_both_endian_16(1)  # Volume sequence number
    pvd[128:132] = to_both_endian_16(SECTOR_SIZE)  # Logical block size

    # Root directory record in PVD (bytes 156..189)
    root_pvd_record = make_dir_record(b"\x00", 18, SECTOR_SIZE, True)
    pvd[156:156 + len(root_pvd_record)] = root_pvd_record

    pvd[190:318] = b"ZEROOS".ljust(128, b" ")  # Volume Set Identifier
    pvd[318:446] = b"ZEROOS FOUNDATION".ljust(128, b" ")  # Publisher Identifier

    # Sector 17: Volume Descriptor Set Terminator
    terminator = bytearray(SECTOR_SIZE)
    terminator[0] = 255  # Terminator
    terminator[1:6] = b"CD001"
    terminator[6] = 1

    # Write out full ISO
    os.makedirs(os.path.dirname(out_iso_path), exist_ok=True)
    with open(out_iso_path, "wb") as out:
        # Sectors 0..15
        out.write(b"\x00" * (16 * SECTOR_SIZE))
        # Sector 16
        out.write(pvd)
        # Sector 17
        out.write(terminator)
        # Sector 18
        out.write(root_dir_sector)
        # Sectors 19..N
        out.write(file_data_bytes)

    print(f"Created ISO: {out_iso_path} ({os.path.getsize(out_iso_path)} bytes, {total_sectors} sectors)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: build_iso.py <output.iso> <source_dir>")
        sys.exit(1)
    build_iso(sys.argv[2], sys.argv[1])
