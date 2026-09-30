#!/usr/bin/env python3
"""Reject implicit kernel FP/SIMD while allowing audited FXSAVE + probe asm."""
import re
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: check_kernel_simd.py KERNEL.ELF")

text = subprocess.check_output(
    ["objdump", "-d", "--no-show-raw-insn", sys.argv[1]], text=True
)
current = ""
seen = {"fpu_system_init": set(), "fpu_context_switch": set(),
        "scheduler_fpu_probe_worker": set()}
errors = []
fp_operand = re.compile(r"%(?:[xyz]mm\d+|mm\d+|st(?:\(|\b))")
allowed = {
    "fpu_system_init": {"fninit", "fldz", "pxor", "ldmxcsr", "fxsave64"},
    "fpu_context_switch": {"fxsave64", "fxrstor64"},
    "scheduler_fpu_probe_worker": {"movdqu"},
}

for line in text.splitlines():
    header = re.search(r"<([^>]+)>:", line)
    if header:
        current = header.group(1).split("+", 1)[0]
        continue
    m = re.match(r"\s*[0-9a-f]+:\s+([a-z][a-z0-9.]*)\s*(.*)$", line)
    if not m:
        continue
    mnemonic, operands = m.groups()
    suspicious = (mnemonic.startswith("f") or mnemonic.startswith("v") or
                  mnemonic in {"emms", "ldmxcsr", "stmxcsr"} or
                  bool(fp_operand.search(operands)))
    if not suspicious:
        continue
    permitted = allowed.get(current, set())
    if mnemonic not in permitted:
        errors.append(f"{current}: {mnemonic} {operands}".rstrip())
    else:
        seen.setdefault(current, set()).add(mnemonic)

required = {
    "fpu_system_init": {"fninit", "fldz", "pxor", "ldmxcsr", "fxsave64"},
    "fpu_context_switch": {"fxsave64", "fxrstor64"},
    "scheduler_fpu_probe_worker": {"movdqu"},
}
for symbol, instructions in required.items():
    missing = instructions - seen.get(symbol, set())
    if missing:
        errors.append(f"{symbol}: missing expected instruction(s): {', '.join(sorted(missing))}")

if errors:
    print("kernel-simd-check: unexpected or missing FP/SIMD instruction(s):")
    print("\n".join(errors[:40]))
    raise SystemExit(1)
print("kernel-simd-check: no compiler-generated FP/SIMD outside audited FPU routines and the runtime probe.")
