#!/usr/bin/env python3
"""Guard thread-creation rejection against dereferencing an invalid owner."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
THREAD = ROOT / "kernel" / "thread.c"
KERNEL = ROOT / "kernel" / "kernel.c"
MILESTONES = ROOT / "tools" / "boot_milestones.txt"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
MARKER = "ZEROOS: invalid thread owner pointer rejected."


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function signature: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing function body: {signature}")
    depth = 0
    state = "code"
    index = opening
    while index < len(source):
        ch = source[index]
        nxt = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            elif ch == "/" and nxt == "/":
                state = "line"
                index += 1
            elif ch == "/" and nxt == "*":
                state = "block"
                index += 1
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1:index]
        elif state == "string":
            if ch == "\\\\":
                index += 1
            elif ch == '"':
                state = "code"
        elif state == "char":
            if ch == "\\\\":
                index += 1
            elif ch == "'":
                state = "code"
        elif state == "line":
            if ch == "\n":
                state = "code"
        elif state == "block":
            if ch == "*" and nxt == "/":
                state = "code"
                index += 1
        index += 1
    raise AssertionError(f"unterminated function body: {signature}")


class ThreadInvalidOwnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.thread = THREAD.read_text(encoding="utf-8")
        cls.kernel = KERNEL.read_text(encoding="utf-8")
        cls.milestones = MILESTONES.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_failure_diagnostic_never_dereferences_owner_pointer(self):
        body = function_body(
            self.thread, "static int thread_create_failure("
        )
        self.assertNotIn("process->", body)
        self.assertIn("process_ptr=", body)
        self.assertIn("(uint64_t)(uintptr_t)process", body)

    def test_kernel_and_user_creation_reject_invalid_owner_without_publishing_tid(self):
        common = function_body(
            self.thread, "static int thread_create_common("
        )
        self.assertLess(
            common.find("process_thread_reserve(process)"),
            common.find('thread_create_failure("process reservation",process)'),
        )
        user = function_body(self.thread, "int thread_create_user(")
        self.assertIn("user_entry==0", user)
        self.assertIn(
            'thread_create_failure("user entry/stack validation",process)',
            user,
        )

        probe = function_body(
            self.kernel, "static void process_thread_probe_monitor_step("
        )
        self.assertIn("thread_create_kernel(invalid_owner", probe)
        self.assertIn("thread_create_user(invalid_owner", probe)
        self.assertIn("rejected_kernel_tid=0xfeedULL", probe)
        self.assertIn("rejected_user_tid=0xbeefULL", probe)
        self.assertIn(MARKER, probe)
        self.assertIn(MARKER, self.milestones)

    def test_ci_runs_this_regression_once_before_toolchain_install(self):
        invocation = "python3 tools/test_thread_create_invalid_owner.py"
        self.assertEqual(self.workflow.count(invocation), 1)
        self.assertLess(
            self.workflow.index(invocation),
            self.workflow.index("- name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
