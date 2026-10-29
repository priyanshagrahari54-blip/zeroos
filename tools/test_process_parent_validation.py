#!/usr/bin/env python3
"""Regression checks for invalid parent pointers passed to process_create()."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROCESS = ROOT / "kernel" / "process.c"
KERNEL = ROOT / "kernel" / "kernel.c"
MILESTONES = ROOT / "tools" / "boot_milestones.txt"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
MARKER = "ZEROOS: invalid process parent pointer rejected."


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


class ProcessParentValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.process = PROCESS.read_text(encoding="utf-8")
        cls.kernel = KERNEL.read_text(encoding="utf-8")
        cls.milestones = MILESTONES.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_parent_provenance_is_checked_before_dereference(self):
        body = function_body(self.process, "int process_create(")
        guard = body.find("!process_pointer_valid(parent)")
        self.assertGreaterEqual(guard, 0, "parent must belong to the process table")
        dereferences = [
            body.find("parent->state"),
            body.find("parent->pid"),
            body.find("parent->child_count"),
        ]
        dereferences = [index for index in dereferences if index >= 0]
        self.assertTrue(dereferences, "expected parent-field validation")
        self.assertLess(guard, min(dereferences),
                        "invalid parent must be rejected before any field read")

    def test_certification_guest_probes_a_noncanonical_caller_pointer(self):
        body = function_body(self.kernel, "static void process_thread_probe_monitor_step(")
        self.assertIn(
            "process_create((struct process *)(uintptr_t)1,",
            body,
            "boot certification must exercise a pointer outside the process table",
        )
        self.assertIn("rejected_parent_pid!=0xfeedULL", body)
        self.assertIn(MARKER, body)
        self.assertIn(MARKER, self.milestones)

    def test_ci_runs_regression_before_build_and_qemu(self):
        invocation = "python3 tools/test_process_parent_validation.py"
        self.assertEqual(self.workflow.count(invocation), 1)
        self.assertLess(
            self.workflow.index(invocation),
            self.workflow.index("- name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
