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

# If both GPT copies are damaged, repair must refuse and leave bytes unchanged.
printf 'XXXX' | dd of="$TMP/disk.img" bs=1 seek=512 conv=notrunc status=none
printf 'Z' | dd of="$TMP/disk.img" bs=1 seek=$((64*1024*1024-512+20)) conv=notrunc status=none
cp "$TMP/disk.img" "$TMP/bad.img"
rc=0
python3 "$ROOT/tools/storage/gpt.py" repair "$TMP/disk.img" || rc=$?
test "$rc" -eq 2
cmp "$TMP/disk.img" "$TMP/bad.img"

echo 'storage host tools self-test: PASS'
