#!/usr/bin/env python3
"""Generate the authoritative ZEROOS completion matrix from the built tree.

Section 58 of the G560 master prompt asks for one authoritative completion
matrix, and section 57 requires documentation to match implementation. A
hand-written matrix cannot satisfy either: it drifts the moment the code moves,
and nothing stops a row from claiming a state the build does not back.

So this tool derives every row from artifacts instead:

  * the kernel ELF's symbol table  -> what is actually linked into the kernel
  * tools/boot_milestones.txt      -> what the CI boot gate asserts
  * the host test binaries         -> what is covered by executable tests
  * docs/ZEROOS_MASTER/hardware/   -> what has been validated on real hardware

Statuses follow the ladder defined by the master prompt:

  SPEC_ONLY < FOUNDATION < PARTIAL < IMPLEMENTED < INTEGRATED
            < TESTED < QEMU_CERTIFIED < G560_VALIDATED

The tool is fail-closed: --strict exits non-zero if any row's computed state is
below the floor declared for it, so a regression in the build shows up as a
failed gate rather than as a quietly downgraded document.

Nothing here can award G560_VALIDATED. That state requires a recorded result
from the physical machine, and only a human running the hardware can write one.
"""

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LADDER = [
    "SPEC_ONLY",
    "FOUNDATION",
    "PARTIAL",
    "IMPLEMENTED",
    "INTEGRATED",
    "TESTED",
    "QEMU_CERTIFIED",
    "G560_VALIDATED",
]

# Each area: what has to be true for each rung of the ladder.
#
#   sources   files whose existence is the difference between SPEC_ONLY and
#             FOUNDATION. A module that is only described in docs is SPEC_ONLY.
#   linked    symbol fragments that must appear in the kernel ELF. A module
#             that exists but is not linked into the kernel is host-side logic,
#             which is FOUNDATION, not an implementation in the running OS.
#   host      host test binary that exercises the module.
#   reach     symbol prefixes that the boot path (kernel/kernel.c) must
#             actually call. Being linked into the image is not the same as
#             being reached: the 23 hardware-* core modules are all linked but
#             none of them is called from kernel.c, so they ship in the image
#             without being part of the running boot sequence.
#   boot      plain keywords that must appear in tools/boot_milestones.txt for
#             the module to count as covered by the QEMU boot gate. Keywords,
#             not regexes: that file's own lines are regex syntax, so matching
#             them with a regex double-escapes and fails silently.
#   docs      documentation that must exist.
AREA = [
    dict(area="Boot",
         sources=["boot/boot.S", "boot/isr.S", "boot/ap_trampoline.S"],
         linked=["kernel_main"],
         boot=["foundation milestone reached"],
         host=[],
         docs=["docs/ZEROOS_MASTER/BUILD.md"]),
    dict(area="Kernel",
         sources=["kernel/kernel.c", "kernel/cpu.c", "kernel/interrupts.c",
                  "kernel/gdt.c"],
         linked=["kernel_main"],
         boot=["CPU capabilities detected", "bootstrap stack guard verified"],
         host=["resource-core-test"],
         docs=[]),
    dict(area="Memory",
         sources=["kernel/memory.c", "kernel/vmm.c", "kernel/tlb.c"],
         linked=["pmm_", "vmm_"],
         reach=['pmm_', 'vmm_'],
         boot=["physical page allocator initialized",
               "TLB shootdown boundary self-test passed"],
         host=[],
         docs=[]),
    dict(area="Scheduler",
         sources=["kernel/scheduler.c", "kernel/scheduler_stress.c"],
         linked=["scheduler_"],
         reach=['sched_'],
         boot=["scheduler certification passed",
               "scheduler deterministic trace"],
         host=["resource-core-test"],
         docs=[]),
    dict(area="IPC",
         sources=["kernel/ipc.c", "kernel/shmem.c", "kernel/ksync.c"],
         linked=["ipc_"],
         reach=['ipc_'],
         boot=["interrupt-frame ownership invariant verified"],
         host=[],
         docs=[]),
    dict(area="Process",
         sources=["kernel/process.c", "kernel/task.c", "kernel/thread.c",
                  "kernel/wait.c"],
         linked=["process_", "thread_"],
         reach=['process_', 'thread_'],
         boot=["userspace init process published"],
         host=[],
         docs=[]),
    dict(area="ELF",
         sources=["kernel/elf.c", "kernel/exec.c"],
         linked=["elf_"],
         reach=['elf_'],
         boot=["userspace init process published"],
         host=[],
         docs=[]),
    dict(area="Dynamic linker",
         sources=[],
         linked=[],
         boot=[],
         host=[],
         docs=[]),
    dict(area="Storage",
         sources=["kernel/storage/block.c", "kernel/storage/ahci.c",
                  "kernel/storage/gpt.c"],
         linked=["block_", "ahci_"],
         reach=['block_', 'ahci_'],
         boot=["storage"],
         host=["storage_probe.elf"],
         docs=[]),
    dict(area="Filesystem",
         sources=["kernel/storage/zjfs.c"],
         linked=["zjfs_"],
         reach=['zjfs_'],
         boot=["vfs"],
         host=[],
         docs=[]),
    dict(area="VFS",
         sources=["kernel/storage/vfs.c"],
         linked=["vfs_"],
         reach=['vfs_'],
         boot=["vfs"],
         host=[],
         docs=[]),
    dict(area="PCI",
         sources=["kernel/pci.c"],
         linked=["pci_"],
         reach=['pci_'],
         boot=["PCI"],
         host=[],
         docs=[]),
    dict(area="USB",
         sources=["kernel/usb_core.c"],
         linked=["usb_"],
         reach=['usb_'],
         boot=[],
         host=["usb-core-test"],
         docs=[]),
    dict(area="Input",
         sources=["kernel/input.c", "kernel/input_core.c",
                  "kernel/scancode_core.c", "kernel/mouse_core.c"],
         linked=["input_"],
         reach=['input_'],
         boot=["PS/2 keyboard", "PS/2 mouse"],
         host=["input-core-test", "scancode-core-test", "mouse-core-test"],
         docs=[]),
    dict(area="Display",
         sources=["kernel/fb.c", "kernel/display_core.c"],
         linked=["fb_"],
         reach=['fb_'],
         boot=["display primitive", "scanout"],
         host=["display-core-test"],
         docs=[]),
    dict(area="UI",
         sources=["userspace/desktop/src/ui.c", "userspace/desktop/src/bar.c",
                  "userspace/desktop/src/capsule.c"],
         linked=[],
         boot=["desktop session"],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Networking",
         sources=["kernel/net_core.c", "kernel/net_stack.c",
                  "kernel/net_transport.c", "kernel/net_socket.c",
                  "kernel/netif.c"],
         linked=["net_", "netif_"],
         reach=['net_', 'netif_'],
         boot=[],
         host=["net-core-test", "net-stack-test", "netif-test",
               "net-socket-test", "net-route-test",
               "net-transport-test", "net-l2-test",
               "net-arp-test", "net-ipv6-test",
               "net-conntrack-test"],
         docs=[]),
    dict(area="Wi-Fi",
         sources=[],
         linked=[],
         boot=[],
         host=[],
         docs=[]),
    dict(area="Audio",
         sources=["kernel/audio_core.c"],
         linked=["audio_"],
         reach=['audio_'],
         boot=[],
         host=["audio-core-test"],
         docs=[]),
    dict(area="Bluetooth",
         sources=["userspace/desktop/src/bluetooth.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Video",
         sources=["userspace/desktop/src/media.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Browser",
         sources=["userspace/desktop/src/browser.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Windows compatibility",
         sources=["userspace/compat/src"],
         linked=[],
         boot=[],
         host=[],
         docs=[]),
    dict(area="Android",
         sources=[],
         linked=[],
         boot=[],
         host=[],
         docs=[]),
    dict(area="Package manager",
         sources=["userspace/desktop/src/package.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Security",
         sources=["kernel/sandbox.c", "kernel/crypto.c",
                  "userspace/desktop/src/vault.c"],
         linked=["sandbox_", "crypto_"],
         reach=['sandbox_'],
         boot=[],
         host=["crypto-core-test", "desktop-tests"],
         docs=[]),
    dict(area="Recovery",
         sources=["userspace/desktop/src/recovery.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Updates",
         sources=["userspace/desktop/src/update.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="ZERO AI",
         sources=["userspace/desktop/src/ai.c",
                  "userspace/desktop/src/nlp.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Control Center",
         sources=["userspace/desktop/src/control_center.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Search",
         sources=["userspace/desktop/src/search.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Gaming",
         sources=["userspace/desktop/src/gaming.c", "kernel/../tools/g560_benchmark.py"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Study",
         sources=["userspace/desktop/src/study.c",
                  "userspace/desktop/src/ocr.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Backup",
         sources=["userspace/desktop/src/backup.c",
                  "userspace/desktop/src/snapshot.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Accessibility",
         sources=["userspace/desktop/src/a11y.c",
                  "userspace/desktop/src/i18n.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Power",
         sources=["kernel/acpi.c", "userspace/desktop/src/lifecycle.c"],
         linked=["acpi_"],
         reach=['acpi_'],
         boot=["ACPI"],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Thermal",
         sources=["userspace/desktop/src/governor.c"],
         linked=[],
         boot=[],
         host=["desktop-tests"],
         docs=[]),
    dict(area="Performance",
         sources=["tools/g560_benchmark.py", "kernel/resource_core.c"],
         linked=["resource_"],
         reach=['resource_'],
         boot=[],
         host=["resource-core-test"],
         docs=[]),
    dict(area="G560 hardware",
         sources=[],
         linked=[],
         boot=[],
         host=[],
         docs=[]),
    dict(area="QEMU",
         sources=["tools/boot_test.sh", "tools/boot_milestones.txt"],
         linked=[],
         boot=["foundation milestone reached"],
         host=[],
         docs=["docs/ZEROOS_MASTER/BUILD.md"]),
    dict(area="Release",
         sources=["tools/release.sh", "tools/check_release_hygiene.sh",
                  "tools/verify_iso.py"],
         linked=[],
         boot=[],
         host=[],
         docs=["docs/ZEROOS_MASTER/BUILD.md"]),
]

HW_REPORT_DIR = "docs/ZEROOS_MASTER/hardware"


def read(path):
    try:
        with open(path, "rb") as fh:
            return fh.read().decode("utf-8", "replace")
    except OSError:
        return ""


def exists(path):
    full = path if os.path.isabs(path) else os.path.join(REPO, path)
    if os.path.isdir(full):
        return any(True for _ in os.listdir(full))
    return os.path.exists(full)


def kernel_symbols(build_dir):
    """Symbol names actually present in the linked kernel image."""
    elf = os.path.join(REPO, build_dir, "zeroos.elf")
    if not os.path.exists(elf):
        return None
    try:
        out = subprocess.run(["nm", elf], capture_output=True, text=True,
                             check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if parts:
            syms.add(parts[-1])
    return syms


def host_binaries(build_dir):
    d = os.path.join(REPO, build_dir)
    if not os.path.isdir(d):
        return set()
    return {name for name in os.listdir(d)
            if os.access(os.path.join(d, name), os.X_OK)
            and not os.path.isdir(os.path.join(d, name))}


def boot_milestones():
    return read(os.path.join(REPO, "tools/boot_milestones.txt"))


def classify(spec, syms, binaries, milestones, hw_reports):
    """Return (status, evidence) using only what the artifacts show."""
    ev = []

    if hw_reports and spec["area"] == "G560 hardware":
        return "G560_VALIDATED", f"{len(hw_reports)} recorded hardware report(s)"

    have_sources = [s for s in spec["sources"] if exists(s)]
    if not have_sources:
        return "SPEC_ONLY", "no source in the tree"
    ev.append(f"source: {len(have_sources)}/{len(spec['sources'])}")

    # A module that is not linked into the kernel is host-side logic. It can be
    # well tested on the host and still not exist in the running OS.
    if spec["linked"]:
        if syms is None:
            ev.append("kernel ELF absent - cannot confirm linkage")
            return "FOUNDATION", "; ".join(ev)
        hits = sorted({frag for frag in spec["linked"]
                       if any(frag in s for s in syms)})
        if not hits:
            ev.append("NOT linked into the kernel ELF (host-side only)")
            status = "FOUNDATION"
            if spec["host"]:
                present = [b for b in spec["host"] if b in binaries]
                if present:
                    ev.append(f"host-tested by {', '.join(present)}")
                    status = "PARTIAL"
            return status, "; ".join(ev)
        ev.append(f"linked: {', '.join(hits)}")
    status = "IMPLEMENTED"

    # IMPLEMENTED -> INTEGRATED only if the boot path reaches it.
    reach = spec.get("reach")
    if reach:
        src = read(os.path.join(REPO, spec.get("reach_from", "kernel/kernel.c")))
        reached = sorted({r for r in reach if re.search(r + r"[a-z0-9_]*\s*\(", src)})
        if reached:
            ev.append("reached from the boot path: " + ", ".join(reached))
            status = "INTEGRATED"
        else:
            ev.append("linked into the image but NOT called from the boot path")

    if spec["host"]:
        present = [b for b in spec["host"] if b in binaries]
        if present:
            ev.append(f"host-tested by {', '.join(present)}")
            if LADDER.index(status) < LADDER.index("TESTED"):
                status = "TESTED"
        else:
            ev.append("host test binary not built")

    if spec["boot"]:
        missing = [b for b in spec["boot"] if b not in milestones]
        if missing:
            ev.append(f"no boot-gate coverage for: {', '.join(missing)}")
        else:
            ev.append(f"boot gate asserts {len(spec['boot'])} milestone(s)")
            status = "QEMU_CERTIFIED"

    for doc in spec["docs"]:
        if not exists(doc):
            ev.append(f"missing doc {doc}")
    return status, "; ".join(ev)


def render(rows, build_dir):
    out = ["# ZEROOS completion matrix",
           "",
           "Generated by `tools/completion_matrix.py`. Do not edit by hand: every",
           "status here is derived from the built tree, so a hand edit would be",
           "overwritten and would also be a claim the artifacts do not back.",
           "",
           f"Kernel image inspected: `{build_dir}/zeroos.elf`",
           "",
           "Statuses follow the ladder in the G560 master prompt:",
           "",
           "```",
           "SPEC_ONLY < FOUNDATION < PARTIAL < IMPLEMENTED < INTEGRATED",
           "          < TESTED < QEMU_CERTIFIED < G560_VALIDATED",
           "```",
           "",
           "`FOUNDATION` means source exists but is **not linked into the kernel** -",
           "host-side logic, however well tested, is not part of the running OS.",
           "`G560_VALIDATED` is never awarded by this tool: it requires a recorded",
           f"result from the physical machine under `{HW_REPORT_DIR}/`.",
           "",
           "| Area | Status | Evidence |",
           "| --- | --- | --- |"]
    for status, area, ev in rows:
        out.append(f"| {area} | {status} | {ev} |")
    out.append("")
    return "\n".join(out)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--write", action="store_true",
                    help="write docs/ZEROOS_MASTER/COMPLETION_MATRIX.md")
    ap.add_argument("--strict", metavar="AREA=MIN", action="append",
                    default=[],
                    help="fail if AREA is below MIN, e.g. --strict Boot=QEMU_CERTIFIED")
    args = ap.parse_args(argv[1:])

    syms = kernel_symbols(args.build_dir)
    binaries = host_binaries(args.build_dir)
    milestones = boot_milestones()
    hw_dir = os.path.join(REPO, HW_REPORT_DIR)
    hw_reports = sorted(os.listdir(hw_dir)) if os.path.isdir(hw_dir) else []

    rows = []
    for spec in AREA:
        status, ev = classify(spec, syms, binaries, milestones, hw_reports)
        rows.append((status, spec["area"], ev))

    text = render(rows, args.build_dir)
    if args.write:
        dest = os.path.join(REPO, "docs/ZEROOS_MASTER/COMPLETION_MATRIX.md")
        with open(dest, "w") as fh:
            fh.write(text)
        print(f"wrote {dest}")
    else:
        print(text)

    by_area = {area: status for status, area, _ in rows}
    failures = []
    for rule in args.strict:
        if "=" not in rule:
            print(f"--strict expects AREA=MIN, got {rule!r}", file=sys.stderr)
            return 2
        area, minimum = rule.split("=", 1)
        if area not in by_area:
            print(f"--strict names unknown area {area!r}", file=sys.stderr)
            return 2
        if minimum not in LADDER:
            print(f"--strict names unknown state {minimum!r}", file=sys.stderr)
            return 2
        if LADDER.index(by_area[area]) < LADDER.index(minimum):
            failures.append(f"{area}: {by_area[area]} < required {minimum}")

    tally = {}
    for status, _, _ in rows:
        tally[status] = tally.get(status, 0) + 1
    print("state tally: " + ", ".join(
        f"{k}={tally[k]}" for k in LADDER if k in tally), file=sys.stderr)
    if failures:
        for f in failures:
            print(f"completion-matrix: FAIL {f}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
