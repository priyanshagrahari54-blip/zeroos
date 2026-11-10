#!/usr/bin/env python3
"""Guard ZJFS ordered-data durability: failed data writeback must abort before journaling."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"function not found: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"function body not found: {signature}")

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
            if ch == "\\\\":
                i += 1
            elif ch == '"':
                state = "code"
        elif state == "char":
            if ch == "\\\\":
                i += 1
            elif ch == "'":
                state = "code"
        elif state == "line":
            if ch == "\\n":
                state = "code"
        elif state == "block":
            if ch == "*" and nxt == "/":
                state = "code"
                i += 1
        i += 1
    raise AssertionError(f"unterminated function: {signature}")


zjfs = (ROOT / "kernel/storage/zjfs.c").read_text()
pagecache = (ROOT / "kernel/storage/pagecache.c").read_text()
test_fs = (ROOT / "kernel/storage/test_fs.c").read_text()
milestones = (ROOT / "tools/boot_milestones.txt").read_text()
workflow = (ROOT / ".github/workflows/build.yml").read_text()

commit = function_body(zjfs, "static int zj_commit_locked(struct zj_fs *fs) {")
writeback = commit.find("pc_writeback_mapping(")
abort = commit.find('zj_abort(fs,data_rc,"ordered data writeback")')
return_error = commit.find("return data_rc;", abort)
journal = commit.find("zj_sb_to_txn(fs)")
assert writeback >= 0, "ordered-data writeback call missing"
assert abort > writeback and return_error > abort, "writeback error is not aborted and returned"
assert journal > return_error, "journal metadata can be written before ordered-data error is handled"
assert "(void)pc_writeback_mapping" not in commit, "ordered-data writeback error is discarded"

done = function_body(pagecache, "static void pc_writeback_done(struct block_request *request) {")
assert "++m->error_seq;" in done, "page-cache writeback failure must advance mapping error sequence"
writeback_fn = function_body(pagecache, "int pc_writeback_mapping(struct pc_mapping *mapping, int wait, uint8_t priority) {")
assert "mapping->error_seq!=seq_before ? mapping->error : 0" in writeback_fn, (
    "pc_writeback_mapping must return an error from this writeback pass"
)

runtime = function_body(test_fs, "static int tf_ordered_writeback_failure(struct block_device *device,")
for required in (
    "pc_find(&file->inode->mapping,0)",
    "fault.lba_start=data_lba",
    "fault.lba_end=data_lba+file->inode->mapping.sectors_per_block",
    "fault.mode=BLOCK_FAULT_EIO",
    "zjfs_commit(sb)",
    "rc==-SE_IO",
    "after.aborts==before.aborts+1U",
    "after.commits==before.commits",
    'vfs_stat(&tf_root,"/e/ordered-failure",&st)==-SE_NOENT',
):
    assert required in runtime, f"runtime fault-injection regression missing: {required}"

marker = "ZEROOS: ZJFS ordered-data writeback failure abort self-test passed."
assert marker in milestones, "runtime regression is not a required QEMU boot milestone"
assert workflow.count("python3 tools/test_zjfs_ordered_writeback.py") == 1, (
    "CI must run this source regression exactly once"
)

print("ZJFS ordered-data writeback failure regression: PASS")
