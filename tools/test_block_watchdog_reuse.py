#!/usr/bin/env python3
"""Guard block watchdog timeout snapshots against request-pool ABA reuse."""
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
header = (ROOT / "kernel/storage/block.h").read_text()
test_block = (ROOT / "kernel/storage/test_block.c").read_text()
milestones = (ROOT / "tools/boot_milestones.txt").read_text()
workflow = (ROOT / ".github/workflows/build.yml").read_text()

assert "int block_timeout_stalled_request(struct block_request *request," in header

complete = function_body(block, "static int block_complete_internal(")
sequence_guard = complete.find("request->sequence!=expected_sequence")
stall_guard = complete.find("request->injected!=BLOCK_FAULT_STALL")
state_guard = complete.find("request->state!=BLOCK_REQ_DISPATCHED")
membership_guard = complete.find("if (!*cursor || device->inflight_count==0)")
timeout_count = complete.find("++device->stats.timeouts")
claim = complete.find("request->state=BLOCK_REQ_COMPLETING")
assert min(sequence_guard, stall_guard, state_guard, membership_guard) >= 0
assert sequence_guard < state_guard < membership_guard < timeout_count < claim, (
    "watchdog must validate submission identity and in-flight ownership before "
    "counting timeout or claiming completion"
)
assert "request->injected=0" in complete

watchdog = function_body(block, "static void block_watchdog_scan(")
lock = watchdog.find("spin_lock_irqsave(&device->lock)")
capture = watchdog.find("expired[expired_count].request=r")
capture_sequence = watchdog.find("expired[expired_count].sequence=r->sequence")
unlock = watchdog.find("spin_unlock_irqrestore(&device->lock,flags)")
claim_call = watchdog.find(
    "block_timeout_stalled_request(expired[i].request,\n                                            expired[i].sequence)"
)
assert lock >= 0 and lock < capture < capture_sequence < unlock < claim_call, (
    "watchdog must snapshot request identity while locked and claim it through "
    "the sequence-checked helper after unlock"
)
assert "expired[i]->injected=0" not in watchdog
assert "block_complete(expired[i]" not in watchdog

runtime = function_body(test_block, "static int tb_watchdog_reuse_aba(")
for required in (
    "stale_sequence=first->sequence",
    "block_request_free(first)",
    "second==first",
    "second->sequence!=stale_sequence",
    "block_timeout_stalled_request(second,stale_sequence)==0",
    "inflight==1 && second->state==BLOCK_REQ_DISPATCHED",
    "after.timeouts==before.timeouts",
    "ZEROOS: block watchdog stale timeout snapshot ignored after request reuse.",
):
    assert required in runtime, f"runtime watchdog ABA regression missing: {required}"

marker = "ZEROOS: block watchdog stale timeout snapshot ignored after request reuse."
assert marker in milestones, "watchdog request-reuse regression must be a required boot milestone"
assert workflow.count("python3 tools/test_block_watchdog_reuse.py") == 1, (
    "CI must run the watchdog request-reuse source regression exactly once"
)
assert workflow.index("python3 tools/test_block_watchdog_reuse.py") < workflow.index(
    "- name: Install build and QEMU tools"
)

print("Block watchdog request-reuse ABA regression: PASS")
