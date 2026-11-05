#!/usr/bin/env python3
"""Regression checks for all-or-nothing process address-space range unmapping."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROCESS = ROOT / "kernel" / "process.c"
KERNEL = ROOT / "kernel" / "kernel.c"
MILESTONES = ROOT / "tools" / "boot_milestones.txt"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
MARKER = "ZEROOS: process address-space range unmap preflight self-test passed."


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


class ProcessAddressSpaceUnmapAtomicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.process = strip_c_comments(PROCESS.read_text(encoding="utf-8"))
        cls.kernel = KERNEL.read_text(encoding="utf-8")
        cls.milestones = MILESTONES.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_page_limit_is_checked_under_process_lock(self):
        body = function_body(
            self.process, "int process_address_space_unmap_range("
        )
        lock = body.find("spin_lock_irqsave(&process_lock)")
        lookup = body.find("process_lookup_locked(process->pid)")
        limit = body.find("page_count>process->max_address_space_pages")
        self.assertGreaterEqual(lock, 0)
        self.assertGreater(lookup, lock)
        self.assertGreater(limit, lookup)

    def test_all_expected_frames_are_validated_before_any_unmap(self):
        body = function_body(
            self.process, "int process_address_space_unmap_range("
        )
        lock = body.find("spin_lock_irqsave(&process_lock)")
        zero_frame = body.find("physical_pages[i]==0")
        alignment = body.find("physical_pages[i]&(VMM_PAGE_SIZE-1ULL)")
        translate = body.find("vmm_space_translate")
        unmap = body.find("vmm_space_unmap_page")
        self.assertGreaterEqual(lock, 0)
        self.assertGreater(zero_frame, lock)
        self.assertGreater(alignment, zero_frame)
        self.assertGreater(translate, alignment)
        self.assertGreater(unmap, translate)
        self.assertEqual(body.count("for (uint64_t i=0; i<page_count; ++i)"), 2)

    def test_guest_probe_rejects_unmapped_tail_without_partial_unmap(self):
        body = function_body(
            self.kernel, "static void process_thread_probe_monitor_step("
        )
        remove_second = body.find(
            "process_address_space_unmap_page(process_probe_parent,\n"
            "                    VMM_SPACE_TEST_VA+VMM_PAGE_SIZE)"
        )
        range_unmap = body.find(
            "process_address_space_unmap_range(process_probe_parent,"
        )
        preserved_mapping = body.find(
            "process range-unmap preflight partially unmapped a valid page"
        )
        cleanup = body.find("process range-unmap regression cleanup failed")
        self.assertGreaterEqual(remove_second, 0)
        self.assertGreater(range_unmap, remove_second)
        self.assertGreater(preserved_mapping, range_unmap)
        self.assertGreater(cleanup, preserved_mapping)
        self.assertIn("uint64_t expected_range_pages[2]", body)
        self.assertIn("vmm_space_translate(&process_probe_parent->address_space", body)
        self.assertIn("process_address_space_mapped_pages(process_probe_parent)!=2", body)
        self.assertIn(MARKER, body)
        self.assertIn(MARKER, self.milestones)

    def test_ci_runs_regression_before_build_and_qemu(self):
        invocation = "python3 tools/test_process_address_space_unmap_atomic.py"
        self.assertEqual(self.workflow.count(invocation), 1)
        self.assertLess(
            self.workflow.index(invocation),
            self.workflow.index("- name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
