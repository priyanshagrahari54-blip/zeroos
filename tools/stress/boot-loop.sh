#!/bin/bash
# Repeated-boot stress harness for intermittent SMP/storage failures.
#
#   tools/stress/boot-loop.sh MODE ISO N [OUTDIR]
#     MODE  smp2 | smp4       plain -cdrom boot, checked against every
#                             Boot-test serial pattern in build.yml
#           storage           q35 -smp 4 AHCI+NVMe with fresh ZJFS disks per
#                             boot, checked like the CI persistence gate
#   Env: QEMU (default qemu-system-x86_64), TIMEOUT seconds (default 150).
#
# Boots run sequentially: parallel QEMUs on a small host oversubscribe the
# vCPU threads and hide cross-CPU races. QEMU is stopped as soon as the boot
# completes or panics. Failing serial logs are kept in OUTDIR.
set -u
mode=$1; iso=$2; n=$3; out=${4:-build/stress-$mode}
qemu=${QEMU:-qemu-system-x86_64}; to=${TIMEOUT:-150}
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../.." && pwd)
done_re='storage Stage 3 certification complete|ZEROOS PANIC|FAILED'
mkdir -p "$out"; : > "$out/results.txt"

if [ "$mode" = storage ]; then
  for d in sata nvme; do
    rm -f "$out/$d.pristine"; truncate -s 64M "$out/$d.pristine"
    python3 "$repo/tools/storage/gpt.py" create "$out/$d.pristine" --part zjfs:40:data --part scratch:rest:scratch >/dev/null
    python3 "$repo/tools/storage/zjfs.py" --part 1 "$out/$d.pristine" mkfs --label $d >/dev/null
  done
  echo "hello from the host" > "$out/hello.txt"
  python3 "$repo/tools/storage/zjfs.py" --part 1 "$out/sata.pristine" put "$out/hello.txt" /hello.txt >/dev/null
fi

for i in $(seq 1 "$n"); do
  log="$out/$i.log"; rm -f "$log"
  case $mode in
    smp2|smp4) args=(-smp "${mode#smp}" -cdrom "$iso") ;;
    storage)
      cp "$out/sata.pristine" "$out/sata.img"; cp "$out/nvme.pristine" "$out/nvme.img"
      args=(-machine q35 -smp 4 -m 256M -cdrom "$iso" -boot d
            -drive file="$out/sata.img",if=none,id=d0,format=raw -device ide-hd,drive=d0,bus=ide.0
            -drive file="$out/nvme.img",if=none,id=n0,format=raw -device nvme,serial=zeroos1,drive=n0) ;;
    *) echo "unknown mode $mode" >&2; exit 2 ;;
  esac
  $qemu "${args[@]}" -serial file:"$log" -display none -no-reboot -no-shutdown >/dev/null 2>&1 &
  pid=$!; end=$((SECONDS+to))
  while kill -0 $pid 2>/dev/null && [ $SECONDS -lt $end ]; do
    if [ -f "$log" ] && grep -aqE "$done_re" "$log"; then sleep 1; break; fi
    sleep 0.5
  done
  kill $pid 2>/dev/null; wait $pid 2>/dev/null
  if [ "$mode" = storage ]; then
    if ! grep -aqE 'ZEROOS PANIC|FAILED' "$log" &&
       grep -aq 'storage Stage 3 certification complete' "$log" &&
       grep -aq 'storage persistence check passed' "$log"; then ok=1; else ok=0; fi
  else
    python3 "$here/cicheck.py" "$repo/.github/workflows/build.yml" "$log" >/dev/null 2>&1 && ok=1 || ok=0
  fi
  if [ $ok = 1 ]; then echo "$i PASS" >> "$out/results.txt"; rm -f "$log"
  else echo "$i FAIL $(grep -aoE 'ZEROOS PANIC:[^\r]*|[^\r]*FAILED[^\r]*' "$log" | head -1)" >> "$out/results.txt"; fi
done
p=$(grep -c PASS "$out/results.txt"); f=$(grep -c FAIL "$out/results.txt")
echo "$mode: $p pass, $f fail of $n"
[ "$f" = 0 ]
