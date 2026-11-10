#!/usr/bin/env python3
"""Guard block-layer FLUSH ordering while the barrier request is in flight."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"function not found: {signature}")
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
test_block = (ROOT / "kernel/storage/test_block.c").read_text()
milestones = (ROOT / "tools/boot_milestones.txt").read_text()
workflow = (ROOT / ".github/workflows/build.yml").read_text()

barrier = function_body(
    block, "static uint64_t block_barrier_sequence(struct block_device *device) {"
)
for required in (
    "device->flush_waiting",
    "device->inflight",
    "request->op==BLOCK_OP_FLUSH",
    "request->sequence<barrier",
):
    assert required in barrier, f"in-flight barrier tracking missing: {required}"

pick = function_body(
    block, "static struct block_request *block_pick_locked(struct block_device *device) {"
)
assert "uint64_t barrier=block_barrier_sequence(device);" in pick, (
    "scheduler must use the combined pending/in-flight barrier sequence"
)

runtime = function_body(
    test_block, "static int tb_barrier(struct block_device *disk, uint8_t *buffer) {"
)
for required in (
    "fault.mode=BLOCK_FAULT_STALL",
    "fault.op_mask=1U<<BLOCK_OP_FLUSH",
    "r[1]->state==BLOCK_REQ_DISPATCHED",
    "r[2]->state==BLOCK_REQ_QUEUED",
    "block_complete(r[1],0)",
    "block_idle(disk)",
):
    assert required in runtime, f"runtime in-flight barrier regression missing: {required}"

marker = "ZEROOS: storage block in-flight flush barrier passed."
assert marker in milestones, "in-flight flush barrier is not a required QEMU milestone"
assert workflow.count("python3 tools/test_block_flush_barrier.py") == 1, (
    "CI must run the block flush barrier source regression exactly once"
)

print("Block-layer in-flight FLUSH barrier regression: PASS")
