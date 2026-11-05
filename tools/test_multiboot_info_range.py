#!/usr/bin/env python3
"""Regression checks for Multiboot2 information-block address/size validation."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
MEMORY = ROOT / "kernel" / "memory.c"
BOOT = ROOT / "boot" / "boot.S"
MILESTONES = ROOT / "tools" / "boot_milestones.txt"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
MARKER = "ZEROOS: Multiboot2 info range policy self-test passed."


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


class MultibootInfoRangeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.memory = strip_c_comments(MEMORY.read_text(encoding="utf-8"))
        cls.boot = BOOT.read_text(encoding="utf-8")
        cls.milestones = MILESTONES.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_start_address_uses_identity_map_and_multiboot_alignment(self):
        body = function_body(
            self.memory, "static int multiboot_info_address_valid("
        )
        self.assertIn("address!=0", body)
        self.assertIn("(address&7ULL)==0", body)
        self.assertIn("address<ZEROOS_BOOT_IDENTITY_MAP_LIMIT", body)
        self.assertNotIn("address<ZEROOS_MAX_PHYS_MEM", body)
        self.assertNotIn("ZEROOS_MAX_MULTIBOOT_INFO_SIZE", body)

    def test_size_cap_is_applied_to_block_length_not_start_address(self):
        body = function_body(
            self.memory, "static int multiboot_info_size_valid("
        )
        self.assertIn("multiboot_info_address_valid(address)", body)
        self.assertIn("size>=16ULL", body)
        self.assertIn("(size&7ULL)==0", body)
        self.assertIn("size<=ZEROOS_MAX_MULTIBOOT_INFO_SIZE", body)
        self.assertIn("size<=ZEROOS_BOOT_IDENTITY_MAP_LIMIT-address", body)
        self.assertNotIn("multiboot_info>ZEROOS_MAX_MULTIBOOT_INFO_SIZE", body)

    def test_runtime_boundary_self_test_covers_above_16m_and_window_end(self):
        body = function_body(
            self.memory, "static int multiboot_info_range_self_test("
        )
        self.assertIn("32ULL*1024ULL*1024ULL", body)
        self.assertIn("ZEROOS_MAX_PHYS_MEM+256ULL*1024ULL*1024ULL", body)
        self.assertIn("ZEROOS_BOOT_IDENTITY_MAP_LIMIT-ZEROOS_PAGE_SIZE", body)
        self.assertIn("ZEROOS_BOOT_IDENTITY_MAP_LIMIT", body)
        self.assertIn("ZEROOS_MAX_MULTIBOOT_INFO_SIZE+1ULL", body)
        compact = re.sub(r"\s+", "", body)
        self.assertIn("multiboot_info_size_valid(middle,17ULL)", compact)
        self.assertIn("ZEROOS_PAGE_SIZE+1ULL", body)
        self.assertIn("multiboot_info_size_valid(last_page,ZEROOS_PAGE_SIZE)", body)

    def test_memory_init_validates_start_before_read_and_size_after_read(self):
        body = function_body(self.memory, "void memory_init(")
        self_test = body.find("multiboot_info_range_self_test()")
        address_check = body.find("multiboot_info_address_valid(multiboot_info)")
        dereference = body.find("*(uint32_t *)(uint64_t)multiboot_info")
        size_check = body.find(
            "multiboot_info_size_valid(multiboot_info,(uint64_t)total_size)"
        )
        self.assertGreaterEqual(self_test, 0)
        self.assertGreater(address_check, self_test)
        self.assertGreater(dereference, address_check)
        self.assertGreater(size_check, dereference)
        self.assertNotIn("multiboot_info>ZEROOS_MAX_MULTIBOOT_INFO_SIZE", body)
        self.assertIn(MARKER, body)

    def test_boot_identity_map_covers_the_multiboot_window(self):
        self.assertIn("Identity-map the first 1 GiB", self.boot)
        self.assertIn(".rept 512", self.boot)
        self.assertIn("(pd_index << 21) | 0x83", self.boot)

    def test_multiboot_block_is_reserved_after_successful_parse(self):
        body = function_body(self.memory, "void memory_init(")
        parse_end = body.find("if (!end_tag_found)")
        reserve = body.find(
            "reserve_range(multiboot_info,multiboot_info+(uint64_t)total_size)"
        )
        self.assertGreaterEqual(parse_end, 0)
        self.assertGreater(reserve, parse_end)
        self.assertNotIn("multiboot_info<=ZEROOS_MAX_PHYS_MEM", body[parse_end:reserve])

    def test_ci_runs_this_regression_once_before_toolchain_install(self):
        invocation = "python3 tools/test_multiboot_info_range.py"
        self.assertEqual(self.workflow.count(invocation), 1)
        self.assertLess(
            self.workflow.index(invocation),
            self.workflow.index("- name: Install build and QEMU tools"),
        )
        self.assertIn(MARKER, self.milestones)


if __name__ == "__main__":
    unittest.main(verbosity=2)
