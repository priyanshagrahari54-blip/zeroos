#!/usr/bin/env python3
"""Source-contract regressions for bounded process child-list traversal."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROCESS = (ROOT / "kernel/process.c").read_text()
HEADER = (ROOT / "kernel/process.h").read_text()
KERNEL = (ROOT / "kernel/kernel.c").read_text()
MILESTONES = (ROOT / "tools/boot_milestones.txt").read_text()
WORKFLOW = (ROOT / ".github/workflows/build.yml").read_text()


def function_body(source, signature, next_signature):
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


class ProcessChildListIntegrityTests(unittest.TestCase):
    def test_child_walk_is_bounded_and_validates_before_dereference(self):
        body = function_body(
            PROCESS,
            "static int process_child_link_locked(",
            "/* Preflight parent unlink",
        )
        self.assertIn("visited<ZEROOS_MAX_PROCESSES", body)
        self.assertIn("if (!process_pointer_valid(child))", body)
        self.assertLess(body.index("process_pointer_valid(child)"),
                        body.index("process_lookup_locked(child->pid)"))
        self.assertIn("child->parent!=parent", body)
        self.assertIn("visited!=parent->child_count", body)
        self.assertIn("if (*cursor || visited!=parent->child_count)", body)

    def test_find_child_uses_validated_chain_not_unbounded_walk(self):
        body = function_body(
            PROCESS,
            "struct process *process_find_child(",
            "int process_child_wait_prepare(",
        )
        self.assertIn("process_child_link_locked(parent,pid,pid==0,0,&match,0)", body)
        self.assertNotIn("for (child=parent->first_child", body)
        self.assertNotIn("while (child)", body)

    def test_wait_prepare_uses_validated_chain(self):
        body = function_body(
            PROCESS,
            "int process_child_wait_prepare(",
            "int process_thread_reserve(",
        )
        self.assertIn("process_child_link_locked(parent,pid,0,0,&child,0)", body)
        self.assertNotIn("while (child)", body)

    def test_create_validates_existing_parent_chain_before_insert(self):
        body = function_body(
            PROCESS,
            "int process_create(",
            "struct process *process_lookup(",
        )
        self.assertIn("process_child_link_locked(parent,0,0,0,0,0)<0", body)

    def test_reap_and_abort_preflight_before_irreversible_teardown(self):
        for signature, next_signature in (
            ("int process_reap(", "int process_abort_new("),
            ("int process_abort_new(", "int process_set_limits("),
        ):
            body = function_body(PROCESS, signature, next_signature)
            preflight = body.index("process_child_unlink_preflight_locked")
            revoke = body.index("ipc_process_revoke")
            destroy = body.index("vmm_space_destroy")
            self.assertLess(preflight, revoke)
            self.assertLess(preflight, destroy)
            self.assertIn("*parent_link=process->next_sibling", body)

    def test_debug_selftest_covers_invalid_pointer_cycle_and_count_mismatch(self):
        body = function_body(
            PROCESS,
            "int process_debug_child_chain_selftest(",
            "int process_debug_validate(",
        )
        self.assertIn("(struct process *)(uint64_t)1", body)
        self.assertIn("child->next_sibling=child", body)
        self.assertIn("saved_count+1ULL", body)
        self.assertIn("parent->first_child=saved_first", body)
        self.assertIn("child->next_sibling=saved_next", body)
        self.assertIn("parent->child_count=saved_count", body)

    def test_guest_probe_requires_the_corruption_guard_milestone(self):
        self.assertIn(
            "process_debug_child_chain_selftest(process_probe_parent,",
            KERNEL,
        )
        self.assertIn(
            "ZEROOS: process child-list corruption guards passed.",
            KERNEL,
        )
        self.assertIn(
            "ZEROOS: process child-list corruption guards passed.",
            MILESTONES,
        )
        self.assertIn(
            "process_debug_child_chain_selftest(struct process *parent,",
            HEADER,
        )

    def test_ci_runs_regression_before_build_tools(self):
        step = "      - name: Verify process child-list integrity\n        run: python3 tools/test_process_child_list_integrity.py"
        self.assertEqual(WORKFLOW.count(step), 1)
        self.assertLess(
            WORKFLOW.index(step),
            WORKFLOW.index("      - name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
