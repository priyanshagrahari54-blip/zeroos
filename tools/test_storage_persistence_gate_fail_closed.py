#!/usr/bin/env python3
"""Ensure the QEMU storage-persistence CI gate cannot swallow an ERR status."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / ".github/workflows/build.yml").read_text()
HELPER = ROOT / "tools/storage/persistence_gate_failure.sh"

assert "source tools/storage/persistence_gate_failure.sh" in WORKFLOW
assert """trap 'storage_persistence_gate_failure "$?"' ERR""" in WORKFLOW
assert WORKFLOW.count("python3 tools/test_storage_persistence_gate_fail_closed.py") == 1

helper_text = HELPER.read_text()
assert "trap - ERR" in helper_text, "diagnostic handler must prevent recursive ERR traps"
assert 'exit "$original_status"' in helper_text, "diagnostics must return the original failure"
storage_step = WORKFLOW.split("Storage persistence certification", 1)[1].split("Build production release image", 1)[0]
assert "if timeout 45s qemu-system-x86_64" in storage_step, "expected QEMU timeout must be handled in an if condition so ERR does not fire"
assert "set +e" in helper_text, "diagnostics must not replace the original status"

with tempfile.TemporaryDirectory() as temp:
    build = Path(temp) / "build"
    build.mkdir()
    (build / "serial-disk1.log").write_text("")
    (build / "serial-disk2.log").write_text("")

    trap_script = f'''
set -e
source "{HELPER}"
trap 'storage_persistence_gate_failure "$?"' ERR
false
'''
    failed = subprocess.run(
        ["bash", "-c", trap_script],
        cwd=temp,
        text=True,
        capture_output=True,
        check=False,
    )
    assert failed.returncode == 1, (
        f"ERR trap swallowed original failure: rc={failed.returncode}\n"
        f"stdout={failed.stdout}\nstderr={failed.stderr}"
    )
    assert "storage persistence gate failed (status=1)" in failed.stderr

    preserved = subprocess.run(
        ["bash", "-c", f'source "{HELPER}"; storage_persistence_gate_failure 23'],
        cwd=temp,
        text=True,
        capture_output=True,
        check=False,
    )
    assert preserved.returncode == 23, (
        f"ERR handler changed failure status: rc={preserved.returncode}"
    )

print("Storage persistence gate fail-closed regression: PASS")
