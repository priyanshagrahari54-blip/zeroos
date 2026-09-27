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

# The boot-time validator in kernel/syscall.c (syscall_debug_validate)
# requires specific adjacency pairs plus a ZEROOS_SYS_MAX sentinel one
# past the last ID; a broken chain panics the kernel before userspace
# starts. Replicate it here so an added syscall fails on the host instead
# of only in guest CI.
ID_PAIRS = [
    ("ZEROOS_SYS_SHM_CLOSE", "ZEROOS_SYS_OPEN"),
    ("ZEROOS_SYS_CHOWN", "ZEROOS_SYS_DISPLAY_INFO"),
    ("ZEROOS_SYS_DISPLAY_INFO", "ZEROOS_SYS_DISPLAY_PRESENT"),
    ("ZEROOS_SYS_DISPLAY_PRESENT", "ZEROOS_SYS_INPUT_POLL"),
    ("ZEROOS_SYS_INPUT_POLL", "ZEROOS_SYS_INPUT_WAIT"),
    ("ZEROOS_SYS_INPUT_WAIT", "ZEROOS_SYS_SYSTEM_INFO"),
    ("ZEROOS_SYS_SYSTEM_INFO", "ZEROOS_SYS_MAX"),
]
for previous, current in ID_PAIRS:
    for name in (previous, current):
        if name not in public_ids:
            raise SystemExit(f"syscall ID chain member missing: {name}")
    if public_ids[previous] + 1 != public_ids[current]:
        raise SystemExit(
            f"syscall ID chain broken: {previous}={public_ids[previous]} "
            f"{current}={public_ids[current]}"
        )
ordered = sorted(
    (value, name)
    for name, value in public_ids.items()
    if name != "ZEROOS_SYS_MAX"
)
for (value, name), (next_value, next_name) in zip(ordered, ordered[1:]):
    if value + 1 != next_value:
        raise SystemExit(
            f"non-contiguous syscall IDs: {name}={value} then "
            f"{next_name}={next_value}"
        )
if ordered[-1][0] + 1 != public_ids["ZEROOS_SYS_MAX"]:
    raise SystemExit("ZEROOS_SYS_MAX is not one past the last syscall ID")

# Feature bits and the file/VFS ABI (additive to v1) must match exactly.
FILE_MACROS = sorted(set(re.findall(r"^#define\s+(ZEROOS_(?:ABI_FEATURE_|O_|SEEK_|S_IF|DT_|FSYNC_|MMAP_|FILE_|PATH_MAX|NAME_MAX|MAX_FDS)[A-Z0-9_]*)\s", PUBLIC, re.MULTILINE)))
if len(FILE_MACROS) < 23:
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


for name in ("zeroos_stat", "zeroos_statfs", "zeroos_dirent",
             "zeroos_display_info", "zeroos_system_info"):
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
