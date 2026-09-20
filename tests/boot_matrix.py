#!/usr/bin/env python3
"""Bounded Stage-1 integration matrix; stop each guest only after ALL checks.

Runs the same fault-instrumented ISO on low/default/capped RAM and available
CPU features. Records the actual PCID mode instead of inferring it from -cpu.
"""
from pathlib import Path
import os
import subprocess
import time

EXPECTED = [
    "physical allocator self-test passed.", "heap self-test passed.",
    "virtual memory self-test passed.", "per-address-space VMM self-test passed.",
    "live-CR3 isolation, ownership and W^X passed.",
    "address-space reuse with displaced frames passed.",
    "process rollback and lifetime stress passed.",
    "bootstrap IRQ regression passed.", "process execution/reap stress passed.",
    "GDT extended (user segments) and TSS loaded.",
    "IDT installed and interrupts enabled.",
    "SYSCALL/SYSRET syscall entry initialized.", "foundation milestone reached.",
    "synchronization primitives self-test passed.",
    "wait queue block/wakeup self-test passed.", "wait queue integration verified.",
    "timed sleep wakeup self-test passed.", "timed sleep integration verified.",
    "timer tick 100.", "timer-only preemption stress passed.",
    "zombie reaping and slot-reuse stress passed.",
    "CPL3 and syscall register ABI verified.", "ring-3 hello process verified.",
    "per-process address-space isolation verified.",
    "ring-3 page-fault containment verified.",
    "ring-3 general-protection containment verified.",
    "negative syscall checks rejected as required.",
    "user pointer validation verified.",
    "integer-only state and bad-RSP containment passed.",
    "user W^X, kernel access and I/O denial passed.",
    "ring-3 integration self-test passed.",
]
FORBIDDEN = ["ZEROOS PANIC", "ZEROOS: FAIL", "ZEROOS EARLY FATAL",
             "kernel halted after fatal exception"]


def run(name, cpu, ram, accel="tcg"):
    directory = Path("build/matrix") / name
    directory.mkdir(parents=True, exist_ok=True)
    serial, trace = directory / "serial.log", directory / "qemu.log"
    serial.write_text("")
    with (directory / "startup.log").open("w") as log:
        proc = subprocess.Popen([
            "qemu-system-x86_64", "-accel", accel, "-cpu", cpu, "-m", str(ram), "-smp", "1",
            "-cdrom", "build/zeroos.iso", "-display", "none", "-monitor", "none",
            "-no-reboot", "-no-shutdown", "-serial", f"file:{serial}",
            "-d", "guest_errors,cpu_reset,int", "-D", str(trace),
        ], stdout=log, stderr=log)
        deadline = time.monotonic() + 45
        try:
            while True:
                text = serial.read_text(errors="replace")
                if any(marker in text for marker in FORBIDDEN):
                    raise RuntimeError(f"{name}: forbidden kernel failure; see {serial}")
                if all("ZEROOS: " + marker in text for marker in EXPECTED):
                    break
                if proc.poll() is not None:
                    raise RuntimeError(f"{name}: QEMU exited {proc.returncode}; see {directory}")
                if time.monotonic() >= deadline:
                    missing = [m for m in EXPECTED if "ZEROOS: " + m not in text]
                    raise RuntimeError(f"{name}: timed out; missing {missing}")
                time.sleep(0.1)
        except Exception:
            print(f"FAIL: {name} serial tail:\n{text[-6000:]}", flush=True)
            if trace.exists():
                print(trace.read_text(errors="replace")[-6000:], flush=True)
            raise
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)
    if "Triple fault" in trace.read_text(errors="replace"):
        raise RuntimeError(f"{name}: triple fault")
    mode = "enabled" if "PCID TLB isolation enabled." in text else "flush fallback"
    invpcid = "available" if "INVPCID retirement available." in text else "unavailable"
    print(f"PASS: {name}, accel={accel}, CPU={cpu}, RAM={ram} MiB, PCID={mode}, INVPCID={invpcid}, {len(EXPECTED)} checks", flush=True)
    return mode, invpcid


def main():
    cases = [("low-ram", "qemu64", 32), ("feature-cpu", "max", 128),
             ("no-pcid", "max,pcid=off,invpcid=off", 512),
             ("over-cap", "qemu64", 768), ("repeat", "qemu64", 128)]
    modes = [run(*case) for case in cases]
    if os.access("/dev/kvm", os.R_OK | os.W_OK):
        modes.append(run("kvm-host", "host", 128, "kvm"))
        modes.append(run("kvm-no-invpcid", "host,invpcid=off", 128, "kvm"))
    else:
        print("KVM unavailable: no accessible /dev/kvm on this runner", flush=True)
    print("PCID with INVPCID exercised: " + str(("enabled", "available") in modes), flush=True)
    print("PCID without INVPCID exercised: " + str(("enabled", "unavailable") in modes), flush=True)
    if os.environ.get("ZEROOS_REQUIRE_PCID") == "1" and not all(
        mode in modes for mode in [("enabled", "available"), ("enabled", "unavailable")]
    ):
        raise RuntimeError("Stage-1 CI requires PCID with and without INVPCID; runner hardware coverage is incomplete")
    print("PASS: bounded Stage-1 integration matrix", flush=True)


if __name__ == "__main__":
    main()
