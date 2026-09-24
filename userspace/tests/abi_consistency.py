#!/usr/bin/env python3
"""Reject drift between the private implementation ABI and public headers."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PUBLIC = (ROOT / "userspace/include/zeroos/syscall.h").read_text()
KERNEL = (ROOT / "kernel/syscall.h").read_text()
IPC = (ROOT / "kernel/ipc.h").read_text()


def enum_values(text: str, name: str) -> dict[str, int]:
    block = re.search(
        rf"enum\s+{re.escape(name)}\s*\{{(?P<body>.*?)\}}\s*;",
        text,
        re.DOTALL,
    )
    if not block:
        raise SystemExit(f"missing enum {name}")
    values: dict[str, int] = {}
    next_value = 0
    for raw in block.group("body").split(","):
        item = raw.strip()
        if not item:
            continue
        match = re.fullmatch(r"(ZEROOS_[A-Z0-9_]+)(?:\s*=\s*(\d+))?", item)
        if not match:
            # The public header has no implementation-only comments or
            # expressions in these enums; fail closed if that changes.
            raise SystemExit(f"unparseable {name} item: {item!r}")
        key, explicit = match.groups()
        if explicit is not None:
            next_value = int(explicit)
        values[key] = next_value
        next_value += 1
    return values


def macro(text: str, name: str) -> str:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(.+)$", text, re.MULTILINE)
    if not match:
        raise SystemExit(f"missing macro {name}")
    return " ".join(match.group(1).split())


public_ids = enum_values(PUBLIC, "zeroos_syscall_id")
kernel_ids = enum_values(KERNEL, "zeroos_syscall_id")
if public_ids != kernel_ids:
    raise SystemExit(f"syscall ID drift: public={public_ids!r} kernel={kernel_ids!r}")

for name in (
    "ZEROOS_SYSCALL_VECTOR",
    "ZEROOS_SYSCALL_ABI_VERSION",
    "ZEROOS_SYSCALL_MAX_TRANSFER",
):
    if macro(PUBLIC, name) != macro(KERNEL, name):
        raise SystemExit(f"macro drift for {name}")

for name in ("ZEROOS_IPC_MAX_MESSAGE", "ZEROOS_IPC_PIPE_CAPACITY"):
    if macro(PUBLIC, name) != macro(IPC, name):
        raise SystemExit(f"macro drift for {name}")

public_errors = enum_values(PUBLIC, "zeroos_error")
kernel_errors = enum_values(KERNEL, "zeroos_syscall_error")
if public_errors != kernel_errors:
    raise SystemExit("syscall error drift between public and kernel headers")

print("ZEROOS public/kernel ABI consistency passed.")
