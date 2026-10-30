#!/usr/bin/env python3
"""Regression checks for process-table pointer provenance before dereference."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROCESS = ROOT / "kernel" / "process.c"
KERNEL = ROOT / "kernel" / "kernel.c"
MILESTONES = ROOT / "tools" / "boot_milestones.txt"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
MARKER = "ZEROOS: invalid process API pointers rejected."

# Public APIs that receive a process-table pointer directly. The pointer must
# be proven to identify an aligned static table entry before any field read.
POINTER_APIS = {
    "int process_release_live(": "process",
    "struct process *process_find_child(": "parent",
    "int process_child_wait_prepare(": "parent",
    "int process_thread_reserve(": "process",
    "int process_thread_unreserve(": "process",
    "int process_thread_attach(": "process",
    "int process_thread_reap_finish(": "process",
    "int process_reap(": "process",
    "int process_abort_new(": "process",
    "int process_set_limits(": "process",
    "int process_get_limits(": "process",
    "int process_address_space_map_page(": "process",
    "int process_address_space_unmap_page(": "process",
    "int process_address_space_unmap_range(": "process",
    "uint64_t process_address_space_mapped_pages(": "process",
    "int process_address_space_is_user_range(": "process",
    "uint64_t process_child_count(": "process",
    "uint64_t process_thread_count(": "process",
    "uint64_t process_live_thread_count(": "process",
    "int process_address_space_is_executable(": "process",
}

PROBE_APIS = (
    "process_release_live",
    "process_find_child",
    "process_child_wait_prepare",
    "process_thread_reserve",
    "process_thread_unreserve",
    "process_thread_attach",
    "process_thread_reap_finish",
    "process_reap",
    "process_abort_new",
    "process_set_limits",
    "process_get_limits",
    "process_address_space_map_page",
    "process_address_space_unmap_page",
    "process_address_space_unmap_range",
    "process_address_space_mapped_pages",
    "process_address_space_is_user_range",
    "process_child_count",
    "process_thread_count",
    "process_live_thread_count",
    "process_address_space_is_executable",
)


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
    import re
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


class ProcessApiPointerValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.process = PROCESS.read_text(encoding="utf-8")
        cls.kernel = KERNEL.read_text(encoding="utf-8")
        cls.milestones = MILESTONES.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_every_process_pointer_api_checks_provenance_before_field_read(self):
        source = strip_c_comments(self.process)
        for signature, parameter in POINTER_APIS.items():
            with self.subTest(api=signature):
                body = strip_c_comments(function_body(source, signature))
                guard = body.find(f"process_pointer_valid({parameter})")
                dereference = body.find(f"{parameter}->")
                self.assertGreaterEqual(
                    guard, 0, f"{signature} must validate {parameter} provenance"
                )
                self.assertGreaterEqual(
                    dereference, 0, f"{signature} must inspect process state"
                )
                self.assertLess(
                    guard, dereference,
                    f"{signature} dereferences {parameter} before provenance check",
                )

    def test_certification_guest_exercises_invalid_process_pointer_apis(self):
        body = function_body(
            self.kernel, "static void process_thread_probe_monitor_step("
        )
        self.assertIn(
            "struct process *invalid_process=(struct process *)(uintptr_t)1;",
            body,
        )
        for api in PROBE_APIS:
            with self.subTest(api=api):
                self.assertIn(
                    f"{api}(invalid_process", body,
                    f"boot certification must exercise {api}",
                )
        self.assertIn(MARKER, body)
        self.assertIn(MARKER, self.milestones)

    def test_ci_runs_pointer_regression_before_build_and_qemu(self):
        invocation = "python3 tools/test_process_api_pointer_validation.py"
        self.assertEqual(self.workflow.count(invocation), 1)
        self.assertLess(
            self.workflow.index(invocation),
            self.workflow.index("- name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
