#!/usr/bin/env python3
"""Regression checks for atomic process-owned user-memory copies."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROCESS = ROOT / "kernel" / "process.c"
PROCESS_HEADER = ROOT / "kernel" / "process.h"
SYSCALL = ROOT / "kernel" / "syscall.c"
EXEC = ROOT / "kernel" / "exec.c"
KERNEL = ROOT / "kernel" / "kernel.c"
MILESTONES = ROOT / "tools" / "boot_milestones.txt"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
MARKER = "ZEROOS: process user-copy mapping stability self-test passed."


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function signature: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing function body: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function body: {signature}")


def strip_c_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


class ProcessUserCopyAtomicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.process = PROCESS.read_text(encoding="utf-8")
        cls.process_header = PROCESS_HEADER.read_text(encoding="utf-8")
        cls.syscall = SYSCALL.read_text(encoding="utf-8")
        cls.exec = EXEC.read_text(encoding="utf-8")
        cls.kernel = KERNEL.read_text(encoding="utf-8")
        cls.milestones = MILESTONES.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_mapping_validation_translation_and_copy_share_process_lock(self):
        body = strip_c_comments(function_body(
            self.process, "static int process_address_space_copy("
        ))
        lock = body.find("spin_lock_irqsave(&process_lock)")
        validate = body.find("vmm_space_is_user_range")
        translate = body.find("vmm_space_translate")
        copy = body.find("destination[offset+i]")
        unlock = body.find("spin_unlock_irqrestore(&process_lock,flags)")
        self.assertGreaterEqual(lock, 0)
        self.assertGreater(validate, lock)
        self.assertGreater(translate, validate)
        self.assertGreater(copy, translate)
        self.assertGreater(unlock, copy)
        self.assertEqual(body.count("spin_unlock_irqrestore(&process_lock,flags)"), 1)
        self.assertIn("goto out;", body)
        self.assertIn("process->state==PROCESS_ZOMBIE", body)

    def test_process_header_exposes_both_copy_directions(self):
        self.assertIn("process_address_space_copy_from", self.process_header)
        self.assertIn("process_address_space_copy_to", self.process_header)

    def test_syscall_and_exec_paths_use_atomic_process_copy_api(self):
        for source, signature, api in (
            (self.syscall, "static int copy_from_user(", "process_address_space_copy_from"),
            (self.syscall, "static int copy_to_user(", "process_address_space_copy_to"),
            (self.exec, "static int exec_copy_from_user(", "process_address_space_copy_from"),
        ):
            with self.subTest(signature=signature):
                body = strip_c_comments(function_body(source, signature))
                self.assertIn(api, body)
                self.assertNotIn("vmm_space_translate", body)
                self.assertNotIn("process_address_space_is_user_range", body)

    def test_guest_probe_covers_cross_page_read_write_and_unmapped_tail(self):
        body = function_body(
            self.kernel, "static void process_thread_probe_monitor_step("
        )
        self.assertIn("process_address_space_copy_from", body)
        self.assertIn("process_address_space_copy_to", body)
        self.assertIn("process user-copy accepted an unmapped tail", body)
        self.assertIn(MARKER, body)
        self.assertIn(MARKER, self.milestones)

    def test_ci_runs_regression_before_build_and_qemu(self):
        invocation = "python3 tools/test_process_user_copy_atomic.py"
        self.assertEqual(self.workflow.count(invocation), 1)
        self.assertLess(
            self.workflow.index(invocation),
            self.workflow.index("- name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
