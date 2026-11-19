#!/usr/bin/env python3
"""Guard VFS forced unmount against destroying a live file or mapping reference."""
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


vfs = (ROOT / "kernel/storage/vfs.c").read_text()
test_fs = (ROOT / "kernel/storage/test_fs.c").read_text()
milestones = (ROOT / "tools/boot_milestones.txt").read_text()
workflow = (ROOT / ".github/workflows/build.yml").read_text()

unmount = function_body(vfs, "int vfs_unmount(const char *path, int force) {")
busy_guard = unmount.find("if (busy) {")
dying = unmount.find("sb->flags|=VFS_SB_DYING;")
writeback = unmount.find("int rc=vfs_writeback_sb(sb);")
assert busy_guard >= 0, "live-reference rejection missing"
assert busy_guard < dying < writeback, (
    "unmount must reject live references before marking the mount DYING or writing back"
)
guard = unmount[busy_guard:dying]
assert "return -SE_BUSY;" in guard, "live references must return EBUSY"
assert "force" not in guard, "force must not bypass the live-reference guard"
assert "if (busy && !force)" not in unmount, "old force bypass must be removed"
assert "if (rc && !force)" in unmount, (
    "force must remain limited to the writeback-error policy"
)

runtime = function_body(
    test_fs, "static int tf_unmount_rules(struct block_device *device) {"
)
for required in (
    'vfs_unmount("/t",0)==-SE_BUSY',
    'vfs_unmount("/t",1)==-SE_BUSY',
    "vfs_pread(f,readback,sizeof(readback),0)",
    "memcmp(readback,payload,sizeof(payload))==0",
    "vfs_file_put(f)",
    'vfs_unlink(&tf_root,"/t/busy")==0',
):
    assert required in runtime, f"forced-unmount runtime regression missing: {required}"

marker = "ZEROOS: VFS forced unmount refuses live references."
assert marker in runtime, "runtime test marker missing"
assert marker in milestones, "forced-unmount regression is not a required QEMU milestone"
assert workflow.count("python3 tools/test_vfs_forced_unmount_refs.py") == 1, (
    "CI must run this source regression exactly once"
)
assert workflow.index("python3 tools/test_vfs_forced_unmount_refs.py") < workflow.index(
    "      - name: Install build and QEMU tools"
), "source regression must run before tool installation"

print("VFS forced-unmount live-reference regression: PASS")
