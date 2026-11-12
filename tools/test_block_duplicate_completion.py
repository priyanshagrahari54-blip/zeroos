#!/usr/bin/env python3
"""Guard block completion against duplicate callbacks after in-flight unlink."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "kernel/storage/block.h").read_text()
block = (ROOT / "kernel/storage/block.c").read_text()
test_block = (ROOT / "kernel/storage/test_block.c").read_text()
milestones = (ROOT / "tools/boot_milestones.txt").read_text()
workflow = (ROOT / ".github/workflows/build.yml").read_text()

assert "BLOCK_REQ_COMPLETING=5" in header
complete = block.split("void block_complete(struct block_request *request, int status)", 1)[1].split(
    "void block_note_interrupt", 1
)[0]
membership = complete.find("if (!*cursor || device->inflight_count==0)")
claim = complete.find("request->state=BLOCK_REQ_COMPLETING")
decrement = complete.find("--device->inflight_count")
assert membership >= 0 and claim > membership and decrement > claim, (
    "completion must verify list membership and claim COMPLETING before decrement"
)

runtime = test_block.split("static int tb_duplicate_completion", 1)[1].split("static int tb_cancel", 1)[0]
for required in (
    "request->state=BLOCK_REQ_DISPATCHED",
    "block_complete(request,0)",
    "after.completed[BLOCK_OP_READ]==before.completed[BLOCK_OP_READ]",
    "inflight==0",
    "ZEROOS: block duplicate completion ignored.",
):
    assert required in runtime, f"runtime duplicate-completion probe missing: {required}"

marker = "ZEROOS: block duplicate completion ignored."
assert marker in milestones
assert workflow.count("python3 tools/test_block_duplicate_completion.py") == 1
assert workflow.index("python3 tools/test_block_duplicate_completion.py") < workflow.index(
    "- name: Install build and QEMU tools"
)
print("Block duplicate-completion regression: PASS")
