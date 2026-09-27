#!/usr/bin/env python3
"""Reject drift between the private implementation ABI and public headers."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PUBLIC = (ROOT / "userspace/include/zeroos/syscall.h").read_text()
KERNEL = (ROOT / "kernel/syscall.h").read_text()
IPC = (ROOT / "kernel/ipc.h").read_text()
INPUT_CORE = (ROOT / "kernel/input_core.h").read_text()
MOUSE_CORE = (ROOT / "kernel/mouse_core.h").read_text()


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
    "ZEROOS_INPUT_FLAG_DOWN",
    "ZEROOS_INPUT_FLAG_REPEAT",
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

# Feature bits and the file/VFS ABI (additive to v1) must match exactly.
FILE_MACROS = sorted(set(re.findall(r"^#define\s+(ZEROOS_(?:ABI_FEATURE_|O_|SEEK_|S_IF|FSYNC_|MMAP_|FILE_|PATH_MAX|NAME_MAX|MAX_FDS)[A-Z0-9_]*)\s", PUBLIC, re.MULTILINE)))
if len(FILE_MACROS) < 20:
    raise SystemExit("file ABI macros missing from public header")
for name in FILE_MACROS:
    if macro(PUBLIC, name) != macro(KERNEL, name):
        raise SystemExit(f"macro drift for {name}")


# ABI feature bits must stay in lockstep and must be distinct.

# Keycode and input-kind enums must match across headers.
public_keys = enum_values(PUBLIC, "zeroos_keycode")
kernel_keys = enum_values(KERNEL, "zeroos_keycode")
if public_keys != kernel_keys:
    raise SystemExit("keycode enum drift between public and kernel headers")

public_kinds = enum_values(PUBLIC, "zeroos_input_kind")
kernel_kinds = enum_values(KERNEL, "zeroos_input_kind")
if public_kinds != kernel_kinds:
    raise SystemExit("input-kind enum drift between public and kernel headers")

# enum input_kind in input_core.h uses kernel-local names; its VALUES are
# the ABI contract behind struct zeroos_input_event.kind.
def raw_enum_values(text: str, name: str) -> list[int]:
    block = re.search(
        rf"enum\s+{re.escape(name)}\s*\{{(?P<body>.*?)\}}\s*;",
        text,
        re.DOTALL,
    )
    if not block:
        raise SystemExit(f"missing enum {name}")
    values: list[int] = []
    next_value = 0
    for raw in block.group("body").split(","):
        item = raw.strip()
        if not item:
            continue
        match = re.fullmatch(r"([A-Z0-9_]+)(?:\s*=\s*(\d+))?", item)
        if not match:
            raise SystemExit(f"unparseable {name} item: {item!r}")
        if match.group(2) is not None:
            next_value = int(match.group(2))
        values.append(next_value)
        next_value += 1
    return values


if list(public_kinds.values()) != raw_enum_values(INPUT_CORE, "input_kind"):
    raise SystemExit("zeroos_input_kind values drift vs kernel/input_core.h")

# Pointer button codes (zeroos_pointer_code) must match across the ABI,
# the kernel input queue and the mouse decoder the driver uses.
public_pointers = enum_values(PUBLIC, "zeroos_pointer_code")
kernel_pointers = enum_values(KERNEL, "zeroos_pointer_code")
if public_pointers != kernel_pointers:
    raise SystemExit("pointer-code enum drift between public and kernel headers")
if list(public_pointers.values()) != raw_enum_values(MOUSE_CORE, "pointer_code"):
    raise SystemExit("zeroos_pointer_code values drift vs kernel/mouse_core.h")

def feature_bits(text: str, source: str) -> dict[str, str]:
    bits = dict(
        re.findall(
            r"^#define\s+(ZEROOS_ABI_FEATURE_[A-Z0-9_]+)\s+\(1ULL << (\d+)\)",
            text,
            re.MULTILINE,
        )
    )
    if not bits:
        raise SystemExit(f"missing ABI feature bits in {source}")
    if len(set(bits.values())) != len(bits):
        raise SystemExit(f"duplicate ABI feature bit in {source}: {bits}")
    return bits


if feature_bits(PUBLIC, "public") != feature_bits(KERNEL, "kernel"):
    raise SystemExit("ABI feature bit drift between public and kernel headers")

# Shared structures must match byte-for-byte between headers. Comma
# spacing is normalized so formatting differences cannot hide drift.
def struct_body(text: str, name: str) -> str:
    block = re.search(
        rf"struct\s+{re.escape(name)}\s*\{{(?P<body>.*?)\}}\s*;",
        text,
        re.DOTALL,
    )
    if not block:
        raise SystemExit(f"missing struct {name}")
    body = " ".join(block.group("body").split())
    return re.sub(r",\s*", ",", body)


for name in ("zeroos_stat", "zeroos_statfs", "zeroos_dirent", "zeroos_display_info"):
    if struct_body(PUBLIC, name) != struct_body(KERNEL, name):
        raise SystemExit(f"struct layout drift for {name}")

# Input event must match across the public ABI, the kernel syscall header
# and the kernel input queue implementation.
public_input_event = struct_body(PUBLIC, "zeroos_input_event")
if struct_body(KERNEL, "zeroos_input_event") != public_input_event:
    raise SystemExit("struct zeroos_input_event drift vs kernel/syscall.h")
if struct_body(INPUT_CORE, "input_event") != public_input_event:
    raise SystemExit("struct zeroos_input_event drift vs kernel/input_core.h")

print("ZEROOS public/kernel ABI consistency passed.")
