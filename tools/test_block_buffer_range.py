#!/usr/bin/env python3
"""Guard physical DMA segment range arithmetic against uint64 wraparound."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    assert start >= 0, f"missing function: {signature}"
    opening = source.find("{", start)
    depth = 0
    state = "code"
    i = opening
    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            elif ch == "/" and nxt == "/":
                state = "line"
                i += 1
            elif ch == "/" and nxt == "*":
                state = "block"
                i += 1
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1:i]
        elif state == "string":
            if ch == "\\":
                i += 1
            elif ch == '"':
                state = "code"
        elif state == "char":
            if ch == "\\":
                i += 1
            elif ch == "'":
                state = "code"
        elif state == "line" and ch == "\n":
            state = "code"
        elif state == "block" and ch == "*" and nxt == "/":
            state = "code"
            i += 1
        i += 1
    raise AssertionError(f"unterminated function: {signature}")


block = (ROOT / "kernel/storage/block.c").read_text()
tests = (ROOT / "kernel/storage/test_block.c").read_text()
milestones = (ROOT / "tools/boot_milestones.txt").read_text()
workflow = (ROOT / ".github/workflows/build.yml").read_text()

fn = body(block, "int block_request_add_buffer(")
null_check = fn.find("!request")
range_check = fn.find("physical>max_address-(uint64_t)length")
last_range_check = fn.find("last->physical>max_address-(uint64_t)last->length")
adjacency = fn.find("last->physical+last->length==physical")
segment_write = fn.find("request->segments[request->segment_count].physical=physical")
assert min(null_check, range_check, last_range_check, adjacency, segment_write) >= 0
assert null_check < range_check < last_range_check < adjacency < segment_write, (
    "validate null, new range and prior range before coalescing or writing"
)

runtime = body(tests, "static int tb_buffer_range(")
for required in (
    "block_request_add_buffer(0,0,4)==-SE_INVAL",
    "0xfffffffffffffffCULL,4)==-SE_INVAL",
    "request->segment_count==0",
    "overflowing prior segment rejected before coalescing",
    "prior segment unchanged",
    "ordinary aligned buffer accepted",
    "ZEROOS: block DMA buffer range overflow rejected.",
):
    assert required in runtime, f"runtime overflow regression missing: {required}"

marker = "ZEROOS: block DMA buffer range overflow rejected."
assert milestones.count(marker) == 1, "boot milestone must occur exactly once"
assert workflow.count("python3 tools/test_block_buffer_range.py") == 1, (
    "CI must run the source regression exactly once"
)
assert workflow.index("python3 tools/test_block_buffer_range.py") < workflow.index(
    "- name: Install build and QEMU tools"
), "source regression must run before installing build tools"

print("Block DMA buffer range-overflow regression: PASS")
