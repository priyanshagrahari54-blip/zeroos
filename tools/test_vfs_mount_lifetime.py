#!/usr/bin/env python3
"""Regression checks for VFS mount/inode lifetime during lookup and unmount."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    vfs = (ROOT / "kernel/storage/vfs.c").read_text()
    header = (ROOT / "kernel/storage/vfs.h").read_text()
    guest = (ROOT / "kernel/storage/test_fs.c").read_text()
    stage3 = (ROOT / "tools/stage3_storage_cert.sh").read_text()
    milestones = (ROOT / "tools/boot_milestones.txt").read_text()
    workflow = (ROOT / ".github/workflows/build.yml").read_text()

    put = function_body(vfs, "static void vfs_mount_ref_put(")
    require("--sb->refcount" in put and "spin_lock_irqsave(&vfs_lock)" in put,
            "temporary mount reference must be released under vfs_lock")

    find = function_body(vfs, "static int vfs_find_mount(")
    named_pin = find.index("++(*sb)->refcount;")
    named_unlock = find.index("spin_unlock_irqrestore(&vfs_lock,flags);", named_pin)
    root_pin = find.index("++root->refcount;")
    root_unlock = find.index("spin_unlock_irqrestore(&vfs_lock,flags);", root_pin)
    require(named_pin < named_unlock and root_pin < root_unlock,
            "named and root mounts must be pinned before dropping vfs_lock")

    resolve = function_body(vfs, "static int vfs_resolve(")
    inode_get = resolve.index("vfs_inode_get(current)")
    pin_put = resolve.index("vfs_mount_ref_put(sb)", inode_get)
    require("if (!current)" in resolve and inode_get < pin_put,
            "root inode reference must be acquired before temporary mount pin release")

    cached = function_body(vfs, "struct vfs_inode *vfs_inode_cached(")
    require("VFS_SB_DYING" in cached,
            "cached inode lookups must reject a DYING superblock")

    unmount = function_body(vfs, "int vfs_unmount(")
    require(unmount.index("sb->flags|=VFS_SB_DYING") <
            unmount.index("int busy=sb->refcount!=0"),
            "unmount must close the lookup gate before checking references")
    require("inode->refcount!=expected_refs" in unmount and
            "inode->mmap_count" in unmount and
            "VFS_I_BUSY|VFS_I_LOADING" in unmount,
            "unmount must reject live inode, mmap, loading, and busy references")
    require("if (busy && !force)" not in unmount and
            "if (!busy || force)" not in unmount,
            "force must not bypass live-reference protection")
    require("if (rc && !force)" in unmount,
            "force may still bypass a writeback error after lifetime preflight")

    require("never overrides live" in header,
            "public header must document force semantics")
    require('vfs_unmount("/t",1)==-SE_BUSY' in guest and
            "vfs_pwrite(f,&value,1,0)==1" in guest and
            "vfs_pread(f,&readback,1,0)==1" in guest,
            "guest test must prove a forced refusal leaves an open file usable")
    require('vfs_stat(&tf_root,"/t/mount-race",&st)' in guest and
            'vfs_unmount("/t",1)' in guest and
            'vfs_mount(device,"/t",0)' in guest,
            "guest test must race path lookups with repeated forced unmount/remount")

    marker = "ZEROOS: storage VFS mount/unmount lifetime stress passed."
    require(marker in stage3 and marker in milestones,
            "Stage 3 and boot milestone gates must require the guest lifetime test")
    require(workflow.count("python3 tools/test_vfs_mount_lifetime.py") == 1,
            "CI must run the VFS lifetime regression exactly once")

    print("vfs mount/unmount lifetime regression: PASS")


if __name__ == "__main__":
    main()
