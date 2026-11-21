#!/usr/bin/env python3
"""Guard block request pool against duplicate free-list insertion."""
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

free = function_body(block, "void block_request_free(")
pool_identity = free.find("&root->pool[request->pool_index]!=request")
free_state = free.find("request->state==BLOCK_REQ_FREE")
capacity = free.find("root->pool_free_count>=root->pool_size")
state_write = free.find("request->state=BLOCK_REQ_FREE")
link = free.find("request->next=root->pool_free")
increment = free.find("++root->pool_free_count")
assert min(pool_identity, free_state, capacity) >= 0
assert max(pool_identity, free_state, capacity) < state_write < link < increment, (
    "validate pool ownership, FREE state and capacity under lock before linking"
)

runtime = function_body(test_block, "static int tb_double_free(")
for required in (
    "block_request_free(request)",
    "duplicate return leaves pool count unchanged",
    "held[i]!=next",
    "count==disk->pool_size",
    "pool count restored exactly after unique drain",
    "ZEROOS: block request double-free rejected without pool corruption.",
):
    assert required in runtime, f"runtime double-free regression missing: {required}"

marker = "ZEROOS: block request double-free rejected without pool corruption."
assert milestones.count(marker) == 1, "double-free milestone must be required exactly once"
assert workflow.count("python3 tools/test_block_request_double_free.py") == 1, (
    "CI must run the double-free source regression exactly once"
)
assert workflow.index("python3 tools/test_block_request_double_free.py") < workflow.index(
    "- name: Install build and QEMU tools"
)

print("Block request double-free pool-integrity regression: PASS")
