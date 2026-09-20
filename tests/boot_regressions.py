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


def descriptor_checks(elf):
    symbols = output("nm", "-S", str(elf))
    for name, size in (("tss", 104), ("double_fault_stack", 16384), ("nmi_stack", 16384)):
        symbol = re.search(rf"^[0-9a-f]+ ([0-9a-f]+) b {name}$", symbols, re.M)
        require(symbol and int(symbol[1], 16) == size, f"incorrect size: {name}")
    code = output("objdump", "-d", "--disassemble=gdt_init", str(elf))
    require("lgdt" in code and "ltr" in code and "lidt" not in code, "GDT instruction mixup")
    code = output("objdump", "-d", "--disassemble=isr_stub_2", str(elf))
    require(len(re.findall(r"\bpush\s", code)) == 2, "NMI needs a synthetic error word")
    print("PASS: GDT/TSS/emergency-stack binary layout", flush=True)


def late_fault_test(vector):
    directory = Path(f"build/late-fault-{vector}")
    subprocess.run(["make", f"BUILD={directory}",
                    f"EXTRA_CFLAGS=-DZEROOS_LATE_FAULT_TEST={vector}", "iso"], check=True)
    descriptor_checks(directory / "zeroos.elf")
    text = boot(directory / "zeroos.iso", directory)
    require(f"vector=0x{vector:016x} error=0x{0:016x}" in text,
            f"wrong late exception frame: {text!r}")
    require("kernel halted after fatal exception" in text, "fatal handler did not finish")
    if vector in (2, 8):
        name = "nmi_stack" if vector == 2 else "double_fault_stack"
        symbols = output("nm", "-S", str(directory / "zeroos.elf"))
        stack = re.search(rf"^([0-9a-f]+) ([0-9a-f]+) b {name}$", symbols, re.M)
        frame = re.search(r"frame=0x([0-9a-f]+)", text)
        require(stack and frame, "missing IST stack/frame evidence")
        base, size = int(stack[1], 16), int(stack[2], 16)
        require(base <= int(frame[1], 16) <= base + size - 176, "wrong IST stack")
    print(f"PASS: full IDT vector {vector}, correct frame and stack, no triple fault", flush=True)


def syscall_entry_checks(elf):
    code = output("objdump", "-d", "--disassemble=syscall_entry", str(elf))
    require(re.search(r"pop\s+%rsp\n[^\n]*sysretq", code) is not None,
            "user RSP must be restored only immediately before SYSRET")
    require(len(re.findall(r"\bpush\s", code)) == 10, "syscall frame must have ten words")
    code = output("objdump", "-d", "-j", ".text", str(elf))
    require(not re.search(r"%(?:mm|xmm|ymm|zmm)[0-9]", code),
            "kernel image contains SIMD registers without context ownership")
    print("PASS: syscall frame shape and general-register-only image", flush=True)


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


def wx_fault_test(case):
    directory = Path(f"build/wx-fault-{case}")
    subprocess.run(["make", f"BUILD={directory}",
                    f"EXTRA_CFLAGS=-DZEROOS_WX_FAULT_TEST={case}", "iso"], check=True)
    text = boot(directory / "zeroos.iso", directory)
    symbols = output("nm", "-n", str(directory / "zeroos.elf"))
    def address(name):
        m = re.search(rf"^([0-9a-f]+) [A-Za-z] {name}$", symbols, re.M)
        require(m is not None, f"missing {name}")
        return int(m[1],16)
    fault = re.search(r"EARLY FATAL: vector=([0-9a-f]+) err=([0-9a-f]+) rip=([0-9a-f]+) cr2=([0-9a-f]+)",text)
    require(fault is not None, f"missing W^X fault: {text}")
    vector, error, rip, cr2 = (int(x,16) for x in fault.groups())
    target = address("serial_write_public") if case==1 else address("wx_nx_target")
    if case==3:
        alias = re.search(r"sealed alias=([0-9a-f]+)",text)
        require(alias is not None, "missing sealed alias")
        target = int(alias[1],16)
    require(vector==14 and error==(17 if case==2 else 3) and cr2==target,
            f"incorrect W^X fault: {fault[0]}")
    require(rip==(target if case==2 else address("wx_fault_site")), "incorrect fault RIP")
    print(f"PASS: hardware W^X case {case}, exact RIP/CR2/error",flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--static", action="store_true")
    args = parser.parse_args()
    static_checks(Path("build/zeroos.elf"))
    descriptor_checks(Path("build/zeroos.elf"))
    syscall_entry_checks(Path("build/zeroos.elf"))
    if args.static:
        return
    for vector in (6, 14):
        fault_test(vector)
    for vector in (2, 6, 8):
        late_fault_test(vector)
    for case in (1,2,3):
        wx_fault_test(case)
    text = boot(Path("build/zeroos.iso"), Path("build/no-nx"), "qemu64,-nx")
    require("ZEROOS PANIC: virtual memory initialization failed (NX required)" in text,
            "CPU without NX did not fail closed")
    require("heap init starting" not in text, "unsupported CPU reached heap")
    print("PASS: CPU without NX rejected before NX page tables are installed", flush=True)


if __name__ == "__main__":
    main()
