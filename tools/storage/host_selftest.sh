#!/usr/bin/env bash
# Host-side GPT/ZJFS image creation, data integrity, fallback, and repair tests.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

truncate -s 64M "$TMP/disk.img"
python3 "$ROOT/tools/storage/gpt.py" create "$TMP/disk.img" \
  --part zjfs:40:data --part scratch:rest:scratch
python3 "$ROOT/tools/storage/zjfs.py" --part 1 "$TMP/disk.img" mkfs --label ci
head -c 3000000 /dev/urandom > "$TMP/blob"
python3 "$ROOT/tools/storage/zjfs.py" --part 1 "$TMP/disk.img" mkdir /d
python3 "$ROOT/tools/storage/zjfs.py" --part 1 "$TMP/disk.img" put "$TMP/blob" /d/blob
python3 "$ROOT/tools/storage/zjfs.py" --part 1 "$TMP/disk.img" cat /d/blob | cmp - "$TMP/blob"
python3 "$ROOT/tools/storage/zjfs.py" --part 1 "$TMP/disk.img" fsck

# Corrupt primary ZJFS superblock: fsck must fall back and repair it.
OFFSET=$(python3 "$ROOT/tools/storage/gpt.py" offset "$TMP/disk.img" 1 | cut -d' ' -f1)
printf 'XXXX' | dd of="$TMP/disk.img" bs=1 seek="$OFFSET" conv=notrunc status=none
python3 "$ROOT/tools/storage/zjfs.py" --part 1 "$TMP/disk.img" fsck >"$TMP/fsck-backup.log" 2>&1
grep -q 'using backup' "$TMP/fsck-backup.log"
python3 "$ROOT/tools/storage/zjfs.py" --part 1 "$TMP/disk.img" fsck --repair
python3 "$ROOT/tools/storage/zjfs.py" --part 1 "$TMP/disk.img" fsck >"$TMP/fsck-clean.log" 2>&1
grep -q 'RESULT clean' "$TMP/fsck-clean.log"

# Damaged primary GPT: backup fallback and byte-exact repair.
cp "$TMP/disk.img" "$TMP/good.img"
printf 'XXXX' | dd of="$TMP/disk.img" bs=1 seek=512 conv=notrunc status=none
python3 "$ROOT/tools/storage/gpt.py" list "$TMP/disk.img" >"$TMP/gpt-backup.log" 2>&1
grep -q 'using backup' "$TMP/gpt-backup.log"
python3 "$ROOT/tools/storage/gpt.py" repair "$TMP/disk.img" >"$TMP/gpt-repair.log" 2>&1
grep -q 'primary GPT rebuilt from backup' "$TMP/gpt-repair.log"
cmp "$TMP/disk.img" "$TMP/good.img"

# Individually valid but divergent GPT copies must not be silently trusted.
# Case A: backup disk GUID differs, with a valid recomputed header CRC.
cp "$TMP/good.img" "$TMP/gpt-mismatch-guid.img"
python3 - "$TMP/gpt-mismatch-guid.img" <<'PY'
import struct, sys, zlib
path = sys.argv[1]
with open(path, "r+b") as f:
    f.seek(-512, 2)
    header = bytearray(f.read(512))
    header[56] ^= 1
    struct.pack_into("<I", header, 16, 0)
    struct.pack_into("<I", header, 16, zlib.crc32(header[:92]) & 0xffffffff)
    f.seek(-512, 2)
    f.write(header)
PY
cp "$TMP/gpt-mismatch-guid.img" "$TMP/gpt-mismatch-guid.before"
rc=0
python3 "$ROOT/tools/storage/gpt.py" list "$TMP/gpt-mismatch-guid.img" >"$TMP/gpt-mismatch-guid.log" 2>&1 || rc=$?
test "$rc" -eq 1
grep -q 'copies disagree' "$TMP/gpt-mismatch-guid.log"
rc=0
python3 "$ROOT/tools/storage/gpt.py" repair "$TMP/gpt-mismatch-guid.img" >"$TMP/gpt-mismatch-guid-repair.log" 2>&1 || rc=$?
test "$rc" -eq 2
cmp "$TMP/gpt-mismatch-guid.img" "$TMP/gpt-mismatch-guid.before"

# Case B: backup entry array differs, with valid entry-array and header CRCs.
cp "$TMP/good.img" "$TMP/gpt-mismatch-entries.img"
python3 - "$TMP/gpt-mismatch-entries.img" <<'PY'
import os, struct, sys, zlib
path = sys.argv[1]
sector_size = 512
with open(path, "r+b") as f:
    f.seek(0, os.SEEK_END)
    sectors = f.tell() // sector_size
    entry_sectors = 32
    entries_lba = sectors - 1 - entry_sectors
    f.seek(entries_lba * sector_size)
    entries = bytearray(f.read(entry_sectors * sector_size))
    struct.pack_into("<Q", entries, 32, 2049)  # valid, non-overlapping start
    entries_crc = zlib.crc32(entries) & 0xffffffff
    f.seek(entries_lba * sector_size)
    f.write(entries)
    f.seek((sectors - 1) * sector_size)
    header = bytearray(f.read(sector_size))
    struct.pack_into("<I", header, 88, entries_crc)
    struct.pack_into("<I", header, 16, 0)
    struct.pack_into("<I", header, 16, zlib.crc32(header[:92]) & 0xffffffff)
    f.seek((sectors - 1) * sector_size)
    f.write(header)
PY
cp "$TMP/gpt-mismatch-entries.img" "$TMP/gpt-mismatch-entries.before"
rc=0
python3 "$ROOT/tools/storage/gpt.py" list "$TMP/gpt-mismatch-entries.img" >"$TMP/gpt-mismatch-entries.log" 2>&1 || rc=$?
test "$rc" -eq 1
grep -q 'copies disagree' "$TMP/gpt-mismatch-entries.log"
rc=0
python3 "$ROOT/tools/storage/gpt.py" repair "$TMP/gpt-mismatch-entries.img" >"$TMP/gpt-mismatch-entries-repair.log" 2>&1 || rc=$?
test "$rc" -eq 2
cmp "$TMP/gpt-mismatch-entries.img" "$TMP/gpt-mismatch-entries.before"

# If both GPT copies are damaged, repair must refuse and leave bytes unchanged.
printf 'XXXX' | dd of="$TMP/disk.img" bs=1 seek=512 conv=notrunc status=none
printf 'Z' | dd of="$TMP/disk.img" bs=1 seek=$((64*1024*1024-512+20)) conv=notrunc status=none
cp "$TMP/disk.img" "$TMP/bad.img"
rc=0
python3 "$ROOT/tools/storage/gpt.py" repair "$TMP/disk.img" || rc=$?
test "$rc" -eq 2
cmp "$TMP/disk.img" "$TMP/bad.img"

echo 'storage host tools self-test: PASS'
