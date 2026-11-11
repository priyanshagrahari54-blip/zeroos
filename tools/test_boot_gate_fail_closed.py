#!/usr/bin/env python3
"""Regression tests for fail-closed QEMU boot evidence."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BOOT_TEST = ROOT / "tools" / "boot_test.sh"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
BOOT_FAILURE_HELPER = ROOT / "tools" / "ci" / "boot_gate_failure.sh"


class BootGateFailClosedTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="zeroos-boot-gate-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.fakebin = self.root / "bin"
        self.fakebin.mkdir()
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "zeroos.iso").write_bytes(b"test-only placeholder; never booted")
        self._executable(
            self.fakebin / "timeout",
            """#!/usr/bin/env python3
import os
import subprocess
import sys
import time
args = sys.argv[1:]
if args and args[0].startswith("--verbose"):
    args = args[1:]
if args and args[0].startswith("--kill-after="):
    args = args[1:]
if len(args) < 2:
    sys.exit(125)
child = subprocess.run(args[1:], env=os.environ.copy())
forced = os.environ.get("FAKE_TIMEOUT_STATUS")
elapsed = float(os.environ.get("FAKE_TIMEOUT_ELAPSED", "0"))
if elapsed > 0:
    time.sleep(elapsed)
duration = float(args[0][:-1]) if args[0].endswith("s") else None
if forced in ("124", "137") and duration is not None and elapsed >= duration:
    print("timeout: sending signal TERM to command 'qemu-system-x86_64'", file=sys.stderr)
sys.exit(int(forced) if forced is not None else child.returncode)
""",
        )
        self._executable(
            self.fakebin / "qemu-system-x86_64",
            """#!/usr/bin/env python3
import os
import sys
from pathlib import Path
args = sys.argv[1:]
def after(option):
    try:
        return args[args.index(option) + 1]
    except (ValueError, IndexError):
        return None
def output_path(option):
    value = after(option)
    if value and value.startswith("file:"):
        return Path(value[5:])
    return None
mode = os.environ.get("FAKE_QEMU_MODE", "none")
serial = output_path("-serial")
debug = output_path("-debugcon")
qemu = after("-D")
if mode == "partial":
    if serial:
        serial.write_text("synthetic partial log\\n", encoding="utf-8")
    if debug:
        debug.write_text("BL\\n", encoding="utf-8")
    if qemu:
        Path(qemu).write_text("synthetic qemu log\\n", encoding="utf-8")
sys.exit(0)
""",
        )

    @staticmethod
    def _executable(path, content):
        path.write_text(content, encoding="utf-8")
        path.chmod(0o755)

    def run_gate(self, timeout_status, mode="none", elapsed=None):
        env = os.environ.copy()
        env["PATH"] = str(self.fakebin) + os.pathsep + env.get("PATH", "")
        env["FAKE_TIMEOUT_STATUS"] = str(timeout_status)
        env["FAKE_QEMU_MODE"] = mode
        if elapsed is None:
            elapsed = 1.1 if timeout_status in (124, 137) else 0.0
        env["FAKE_TIMEOUT_ELAPSED"] = str(elapsed)
        return subprocess.run(
            [
                "bash", str(BOOT_TEST),
                "--build-dir", str(self.build),
                "--iterations", "1",
                "--smp", "2",
                "--timeout", "1",
            ],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

    def test_unexpected_qemu_exit_is_failure_even_if_old_logs_exist(self):
        for name in ("serial.log", "debug.log", "qemu.log", "timeout.log"):
            (self.build / name).write_text("stale success evidence\\n", encoding="utf-8")
        result = self.run_gate(timeout_status=1)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(
            "QEMU exited unexpectedly (status=1; expected timeout 124 or forced-kill status 137)",
            result.stdout,
        )
        self.assertNotIn("boot_test: PASS", result.stdout)

    def test_stale_serial_log_is_removed_before_each_boot(self):
        (self.build / "serial.log").write_text("stale success evidence\\n", encoding="utf-8")
        result = self.run_gate(timeout_status=124)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("serial log not produced", result.stdout)
        self.assertNotIn("stale success evidence", result.stdout)

    def test_expected_timeout_is_accepted_but_milestones_are_still_required(self):
        result = self.run_gate(timeout_status=124, mode="partial")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(": FAIL -", result.stdout)
        self.assertNotIn("QEMU exited unexpectedly", result.stdout)


    def test_forced_timeout_kill_is_accepted_but_milestones_are_still_required(self):
        result = self.run_gate(timeout_status=137, mode="partial")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(": FAIL -", result.stdout)
        self.assertNotIn("QEMU exited unexpectedly", result.stdout)

    def test_timeout_like_status_requires_the_supervisor_deadline_signal(self):
        for status in (124, 137):
            with self.subTest(status=status):
                result = self.run_gate(
                    timeout_status=status,
                    mode="partial",
                    elapsed=0.05,
                )
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(
                    "without the timeout supervisor sending TERM at the requested deadline",
                    result.stdout,
                )
                self.assertNotIn(": FAIL -", result.stdout)
                self.assertNotIn("boot_test: PASS", result.stdout)

    def test_boot_gate_checks_timeout_supervisor_before_accepting_milestones(self):
        script = BOOT_TEST.read_text(encoding="utf-8")
        self.assertIn("timeout --verbose --kill-after=5s", script)
        self.assertIn('2>"$build_root/timeout.log"', script)
        deadline_guard = script.index(
            "without the timeout supervisor sending TERM at the requested deadline"
        )
        milestone_check = script.index('assert_log "$build_root/serial.log"')
        self.assertLess(deadline_guard, milestone_check)

    def test_workflow_diagnostic_trap_preserves_original_failure(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("source tools/ci/boot_gate_failure.sh", workflow)
        self.assertIn("""trap 'zeroos_boot_gate_failure "$?"' ERR""", workflow)
        self.assertIn("""trap 'zeroos_boot_gate_failure "$?" build-fault/serial.log' ERR""", workflow)

        with tempfile.TemporaryDirectory(prefix="zeroos-boot-trap-") as temp:
            build = Path(temp) / "build"
            build.mkdir()
            (build / "serial.log").write_text("synthetic serial tail\\n", encoding="utf-8")
            script = f'''
set -e
source "{BOOT_FAILURE_HELPER}"
trap 'zeroos_boot_gate_failure "$?"' ERR
false
'''
            failed = subprocess.run(
                ["bash", "-c", script],
                cwd=temp,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(failed.returncode, 1, failed.stdout + failed.stderr)
            self.assertIn("boot milestone failed", failed.stdout + failed.stderr)

            preserved = subprocess.run(
                ["bash", "-c", f'source "{BOOT_FAILURE_HELPER}"; zeroos_boot_gate_failure 23'],
                cwd=temp,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(preserved.returncode, 23)

            fault_log = build / "fault-serial.log"
            fault_log.write_text("synthetic failed-IPI serial tail\\n", encoding="utf-8")
            fault_script = f'''
set -e
source "{BOOT_FAILURE_HELPER}"
trap 'zeroos_boot_gate_failure "$?" "{fault_log}"' ERR
false
'''
            fault = subprocess.run(
                ["bash", "-c", fault_script],
                cwd=temp,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(fault.returncode, 1, fault.stdout + fault.stderr)
            self.assertIn("synthetic failed-IPI serial tail", fault.stderr)


    def test_ci_covers_the_shared_gate_and_does_not_ignore_qemu_exit(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("python3 tools/test_boot_gate_fail_closed.py", workflow)
        self.assertIn("build/timeout.log", workflow)
        self.assertGreaterEqual(workflow.count("qemu_status=$?"), 5)
        for line in workflow.splitlines():
            if "|| true" in line and (
                "-D build/" in line
                or "-no-shutdown || true" in line
                or ("serial-disk$boot.log" in line and "-serial file:" in line)
            ):
                self.fail(f"QEMU exit status is still swallowed: {line}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
