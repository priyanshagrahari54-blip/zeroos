#!/usr/bin/env python3
"""Guard ZJFS ordered-data durability and torn-journal recovery."""
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
data_flush = commit.find("int data_flush_rc=zj_flush(fs);")
flush_abort = commit.find('zj_abort(fs,data_flush_rc,"ordered data flush")')
flush_return = commit.find("return data_flush_rc;", flush_abort)
assert writeback >= 0, "ordered-data writeback call missing"
assert abort > writeback and return_error > abort, "writeback error is not aborted and returned"
assert data_flush > return_error, "ordered data is not flushed after successful writeback"
assert flush_abort > data_flush and flush_return > flush_abort, (
    "ordered-data flush failure is not aborted and returned"
)
assert journal > flush_return, "journal metadata can be written before the ordered-data flush"
assert "(void)pc_writeback_mapping" not in commit, "ordered-data writeback error is discarded"
assert "zj_flush(fs);" in commit[writeback:journal], (
    "the ordered-data durability barrier must precede the metadata journal"
)

replay = function_body(zjfs, "static int zj_replay(struct zj_fs *fs, int write_allowed) {")
assert "int jsb_valid=" in replay, "journal superblock validity must be tracked separately"
assert "checking descriptor/commit" in replay, (
    "a torn journal superblock must fall back to validating the transaction records"
)
assert "if (jsb_valid && desc->sequence<jsb.sequence)" in replay, (
    "sequence filtering must only use a validated journal superblock"
)
assert "if (!jsb_valid)\n            fs->seq=seq+1U;" in replay, (
    "fallback recovery must advance sequence when discarding an uncommitted slot"
)

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

flush_runtime = function_body(test_fs, "static int tf_ordered_flush_failure(struct block_device *device,")
for required in (
    "fault.op_mask=1U<<BLOCK_OP_FLUSH",
    "fault.remaining=1",
    "zjfs_commit(sb)",
    "rc==-SE_IO",
    "after.aborts==before.aborts+1U",
    "after.commits==before.commits",
    'vfs_stat(&tf_root,"/f/ordered-flush-failure",&st)==-SE_NOENT',
):
    assert required in flush_runtime, f"runtime flush-failure regression missing: {required}"

flush_marker = "ZEROOS: ZJFS ordered-data flush barrier failure abort self-test passed."
assert flush_marker in milestones, "flush-barrier runtime regression is not a required QEMU boot milestone"

marker = "ZEROOS: ZJFS ordered-data writeback failure abort self-test passed."
assert marker in milestones, "runtime regression is not a required QEMU boot milestone"
assert workflow.count("python3 tools/test_zjfs_ordered_writeback.py") == 1, (
    "CI must run this source regression exactly once"
)

print("ZJFS ordered-data writeback/flush durability regression: PASS")
