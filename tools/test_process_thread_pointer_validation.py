#!/usr/bin/env python3
"""Regression checks for process APIs that receive thread-table pointers."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
THREAD = ROOT / "kernel" / "thread.c"
PROCESS = ROOT / "kernel" / "process.c"
KERNEL = ROOT / "kernel" / "kernel.c"
MILESTONES = ROOT / "tools" / "boot_milestones.txt"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
MARKER = "ZEROOS: invalid thread object pointers rejected."

THREAD_POINTER_APIS = (
    "int process_thread_attach(",
    "int process_thread_started(",
    "int process_thread_exited(",
    "int process_thread_detach(",
    "int process_thread_reap_begin(",
)


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
        elif state in ("string", "char"):
            if ch == chr(92):
                index += 1
            elif (state == "string" and ch == '"') or (
                state == "char" and ch == "'"
            ):
                state = "code"
        elif state == "line":
            if ch == chr(10):
                state = "code"
        elif state == "block" and ch == "*" and nxt == "/":
            state = "code"
            index += 1
        index += 1

    raise AssertionError(f"unterminated function body: {signature}")


class ProcessThreadPointerValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.thread = THREAD.read_text(encoding="utf-8")
        cls.process = PROCESS.read_text(encoding="utf-8")
        cls.kernel = KERNEL.read_text(encoding="utf-8")
        cls.milestones = MILESTONES.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_thread_pointer_validator_checks_static_table_range_and_alignment(self):
        body = function_body(self.thread, "int thread_pointer_valid(")
        compact = "".join(body.split())
        self.assertIn("address>=base", compact)
        self.assertIn("address<end", compact)
        self.assertIn("(address-base)%sizeof(threads[0])", compact)
        self.assertNotIn("thread->", body)

    def test_all_process_thread_apis_validate_before_dereferencing(self):
        for signature in THREAD_POINTER_APIS:
            with self.subTest(api=signature):
                body = function_body(self.process, signature)
                thread_guard = body.find("thread_pointer_valid(thread)")
                thread_deref = body.find("thread->")
                self.assertGreaterEqual(thread_guard, 0)
                self.assertGreaterEqual(thread_deref, 0)
                self.assertLess(thread_guard, thread_deref)

                process_guard = body.find("process_pointer_valid(process)")
                process_deref = body.find("process->")
                self.assertGreaterEqual(process_guard, 0)
                self.assertGreaterEqual(process_deref, 0)
                self.assertLess(process_guard, process_deref)

    def test_guest_probe_exercises_invalid_thread_with_valid_reserved_owner(self):
        body = function_body(
            self.kernel, "static void process_thread_probe_monitor_step("
        )
        reserve = body.find("process_thread_reserve(process_probe_parent)")
        attach = body.find(
            "process_thread_attach(process_probe_parent,invalid_thread)"
        )
        self.assertGreaterEqual(reserve, 0)
        self.assertGreaterEqual(attach, 0)
        self.assertLess(reserve, attach)

        for call in (
            "process_thread_started(invalid_thread)",
            "process_thread_exited(invalid_thread,0)",
            "process_thread_detach(invalid_thread)",
            "process_thread_reap_begin(invalid_thread,&unchanged_owner)",
        ):
            with self.subTest(call=call):
                self.assertIn(call, body)

        self.assertIn("unchanged_owner!=(struct process *)(uintptr_t)0xfeedULL", body)
        self.assertIn("process_thread_count(process_probe_parent)!=0", body)
        self.assertIn("process_live_thread_count(process_probe_parent)!=0", body)
        self.assertIn(MARKER, body)
        self.assertIn(MARKER, self.milestones)

    def test_ci_runs_regression_once_before_toolchain_install(self):
        invocation = "python3 tools/test_process_thread_pointer_validation.py"
        self.assertEqual(self.workflow.count(invocation), 1)
        self.assertLess(
            self.workflow.index(invocation),
            self.workflow.index("- name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
