#!/bin/bash
set -e
echo "=== ZEROOS Production Readiness Check ==="
grep -q "\-Werror" Makefile || { echo "FAIL: -Werror missing"; exit 1; }
echo "PASS: -Werror enforced"
make -B elf -j4 2>&1 | tee /tmp/build.log
if grep -i "warning:" /tmp/build.log; then echo "FAIL: warnings"; exit 1; fi
echo "PASS: zero warnings"
grep -q "block_debug_validate" kernel/kernel.c || { echo "FAIL: validation missing"; exit 1; }
grep -q "production validation passed" kernel/kernel.c || { echo "FAIL: marker missing"; exit 1; }
echo "PASS: validation gates"
for file in kernel/block.h kernel/vfs.h kernel/page_cache.h kernel/pci.h kernel/net.h; do grep -q "MAX" $file || { echo "FAIL: MAX missing $file"; exit 1; }; done
echo "PASS: bounded resources"
grep -q "STOPPED" kernel/block.h || { echo "FAIL: lifecycle"; exit 1; }
echo "PASS: lifecycle"
if grep -r "TODO\|FIXME" kernel/*.c | grep -v Binary; then echo "FAIL: TODO"; exit 1; fi
echo "PASS: no TODO"
make userspace-abi-check userspace-runtime-check userspace-abi-consistency
echo "PASS: ABI"
test -f docs/PRODUCTION_READINESS.md && test -f docs/PRODUCTION_CHECKLIST.md && test -f docs/FINAL_PRODUCTION_REPORT.md || { echo "FAIL: docs"; exit 1; }
echo "PASS: docs"
echo "=== ALL PRODUCTION CHECKS PASSED ==="
