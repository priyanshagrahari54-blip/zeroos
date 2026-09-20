#!/usr/bin/env python3
"""Bounded hardware-boundary regressions. Run from the repository root.

--static checks the built ELF without QEMU. The default additionally builds
isolated fault-test images and boots them; it never modifies the normal image.
"""
import argparse
from pathlib import Path
import re
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def static_checks(elf):
    symbols = output("nm", "-S", str(elf))
    gate = re.search(r"^[0-9a-f]+ ([0-9a-f]+) b early_gate$", symbols, re.M)
    require(gate and int(gate[1], 16) == 32 * 16, "early IDT must be 32 x 16 bytes")
    code = output("objdump", "-d", "--disassemble=early_common", str(elf))
    require("0x10(%rsp),%rdx" in code, "fatal RIP must come from frame offset 16")
    require("cld" in code and "and" in code, "fatal C entry must clear DF and align RSP")
    for vector in (8, 10, 11, 12, 13, 14, 17, 21, 29, 30):
        code = output("objdump", "-d", f"--disassemble=early_stub_{vector}", str(elf))
        require(len(re.findall(r"\bpush\s", code)) == 1,
                f"vector {vector}: hardware supplies the error code")
    print("PASS: early IDT binary layout and fatal entry ABI", flush=True)


def boot(image, directory, cpu="qemu64"):
    directory.mkdir(parents=True, exist_ok=True)
    serial = directory / "serial.log"
    trace = directory / "qemu.log"
    with (directory / "startup.log").open("w") as log:
        try:
            result = subprocess.run([
                "qemu-system-x86_64", "-cpu", cpu, "-cdrom", str(image),
                "-display", "none", "-monitor", "none", "-no-reboot", "-no-shutdown",
                "-serial", f"file:{serial}", "-d", "int,cpu_reset", "-D", str(trace),
            ], stdout=log, stderr=log, timeout=12)
            require(result.returncode == 0, f"QEMU startup failed; see {directory}")
        except subprocess.TimeoutExpired:
            pass  # Fatal kernels intentionally halt; the host enforces the bound.
    text = serial.read_text()
    require("Triple fault" not in trace.read_text(), f"triple fault in {directory}")
    return text


def fault_test(vector):
    directory = Path(f"build/early-fault-{vector}")
    subprocess.run(["make", f"BUILD={directory}",
                    f"EXTRA_CFLAGS=-DZEROOS_EARLY_FAULT_TEST={vector}", "iso"], check=True)
    static_checks(directory / "zeroos.elf")
    text = boot(directory / "zeroos.iso", directory)
    symbols = output("nm", "-n", str(directory / "zeroos.elf"))
    site = re.search(r"^([0-9a-f]+) T early_fault_test_site$", symbols, re.M)
    require(site is not None, "missing fault instruction symbol")
    cr2 = 0x4000000000 if vector == 14 else 0
    expected = (f"ZEROOS EARLY FATAL: vector={vector:016x} err={0:016x} "
                f"rip={int(site[1], 16):016x} cr2={cr2:016x}")
    require(expected in text, f"wrong fault frame; expected {expected!r}; got {text!r}")
    require("heap init starting" not in text, "fatal exception returned")
    print(f"PASS: early vector {vector}, exact RIP/error/CR2, no triple fault", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--static", action="store_true")
    args = parser.parse_args()
    static_checks(Path("build/zeroos.elf"))
    if args.static:
        return
    for vector in (6, 14):
        fault_test(vector)
    text = boot(Path("build/zeroos.iso"), Path("build/no-nx"), "qemu64,-nx")
    require("ZEROOS PANIC: virtual memory initialization failed (NX required)" in text,
            "CPU without NX did not fail closed")
    require("heap init starting" not in text, "unsupported CPU reached heap")
    print("PASS: CPU without NX rejected before NX page tables are installed", flush=True)


if __name__ == "__main__":
    main()
