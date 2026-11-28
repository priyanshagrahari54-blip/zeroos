#!/usr/bin/env python3
"""Source-contract regression for single-submit block request ownership."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def body(source, signature):
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
            if ch == '"': state = "string"
            elif ch == "'": state = "char"
            elif ch == "/" and nxt == "/": state = "line"; i += 1
            elif ch == "/" and nxt == "*": state = "block"; i += 1
            elif ch == "{": depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0: return source[opening + 1:i]
        elif state == "string":
            if ch == "\\": i += 1
            elif ch == '"': state = "code"
        elif state == "char":
            if ch == "\\": i += 1
            elif ch == "'": state = "code"
        elif state == "line" and ch == "\n": state = "code"
        elif state == "block" and ch == "*" and nxt == "/": state = "code"; i += 1
        i += 1
    raise AssertionError("unterminated function: " + signature)

block = (ROOT / "kernel/storage/block.c").read_text()
tests = (ROOT / "kernel/storage/test_block.c").read_text()
milestones = (ROOT / "tools/boot_milestones.txt").read_text()
workflow = (ROOT / ".github/workflows/build.yml").read_text()

add = body(block, "int block_request_add_buffer(")
lock = add.find("spin_lock_irqsave(&root->lock)")
state = add.find("request->state!=BLOCK_REQ_ALLOCATED")
write = add.find("request->segments[request->segment_count].physical")
assert min(lock, state, write) >= 0 and lock < state < write
assert "request->pool_index>=root->pool_size" in add
assert "&root->pool[request->pool_index]!=request" in add
assert "spin_unlock_irqrestore(&root->lock,flags)" in add

free = body(block, "void block_request_free(")
allocated_only = free.find("request->state!=BLOCK_REQ_ALLOCATED")
complete_only = free.find("request->state!=BLOCK_REQ_COMPLETE")
free_link = free.find("request->state=BLOCK_REQ_FREE")
assert min(allocated_only, complete_only, free_link) >= 0
assert allocated_only < free_link and complete_only < free_link

submit = body(block, "void block_submit(")
lock = submit.find("spin_lock_irqsave(&root->lock)")
state = submit.find("request->state!=BLOCK_REQ_ALLOCATED")
mutate = submit.find("request->submit_tick=timer_ticks()")
seq = submit.find("request->sequence=++root->next_sequence")
assert min(lock, state, mutate, seq) >= 0 and lock < state < mutate < seq
assert "&root->pool[request->pool_index]!=request" in submit
bound = submit.find("request->segment_count>BLOCK_MAX_SEGMENTS")
loop = submit.find("for (uint32_t i=0; i<request->segment_count")
assert bound >= 0 and loop >= 0 and bound < loop, "validate segment_count before reading the fixed array"

test = body(tests, "static int tb_request_lifecycle(")
for phrase in ("state_after_premature_free==state_first", "late_add==-SE_INVAL", "sequence_second==sequence_first",
               "post_complete_add==-SE_INVAL",
               "corrupt->segment_count=(uint16_t)(BLOCK_MAX_SEGMENTS+1U)",
               "corrupt_status==-SE_INVAL",
               "ZEROOS: block request mutation and duplicate submit rejected."):
    assert phrase in test, "missing runtime lifecycle assertion: " + phrase
assert milestones.count("ZEROOS: block request mutation and duplicate submit rejected.") == 1
double_free = body(tests, "static int tb_double_free(")
assert "uint32_t free_before=disk->pool_free_count;" in double_free
assert "while (count<free_before)" in double_free
assert "count==free_before" in double_free
assert "count==disk->pool_size" not in double_free
assert "pool_free_count==free_before" in double_free
assert "double-free preserves available pool slots" in double_free
assert workflow.count("python3 tools/test_block_request_lifecycle.py") == 1
assert workflow.index("python3 tools/test_block_request_lifecycle.py") < workflow.index(
    "- name: Install build and QEMU tools")
print("Block request single-submit lifecycle regression: PASS")
