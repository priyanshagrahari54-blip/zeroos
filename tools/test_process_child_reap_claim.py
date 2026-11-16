#!/usr/bin/env python3
"""Source-contract regressions for exclusive process child reaping."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROCESS = (ROOT / "kernel/process.c").read_text()
HEADER = (ROOT / "kernel/process.h").read_text()
SYSCALL = (ROOT / "kernel/syscall.c").read_text()
KERNEL = (ROOT / "kernel/kernel.c").read_text()
MILESTONES = (ROOT / "tools/boot_milestones.txt").read_text()
WORKFLOW = (ROOT / ".github/workflows/build.yml").read_text()


def function_body(source, signature, next_signature):
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


class ProcessChildReapClaimTests(unittest.TestCase):
    def test_child_claim_validates_parent_chain_and_claims_under_lock(self):
        body = function_body(
            PROCESS,
            "int process_child_reap_claim(",
            "int process_reap_cancel(",
        )
        self.assertIn("spin_lock_irqsave(&process_lock)", body)
        self.assertIn("process_child_link_locked(parent", body)
        self.assertIn("process_reap_claim_locked(child)", body)
        self.assertIn("*child_out=child", body)
        self.assertLess(body.index("process_reap_claim_locked(child)"),
                        body.rindex("spin_unlock_irqrestore(&process_lock,flags)"))

    def test_zombie_parent_can_claim_children_for_kernel_cleanup(self):
        body = function_body(
            PROCESS,
            "int process_child_reap_claim(",
            "int process_reap_cancel(",
        )
        self.assertIn("parent->state==PROCESS_UNUSED", body)
        self.assertNotIn("parent->state==PROCESS_ZOMBIE", body)

    def test_pid_zero_selects_any_unclaimed_zombie(self):
        self.assertIn(
            "process_child_link_locked(parent,0,1,1,0,&child,0)",
            PROCESS,
        )
        self.assertIn("!child->reap_claimed", PROCESS)
        self.assertIn("child->lifetime_refs==0", PROCESS)

    def test_wait_prepare_queues_behind_an_existing_reaper(self):
        body = function_body(
            PROCESS,
            "int process_child_wait_prepare(",
            "int process_thread_reserve(",
        )
        self.assertIn("!child->reap_claimed && child->lifetime_refs==0", body)
        self.assertIn("wait_queue_prepare(&parent->child_waiters", body)
        self.assertNotIn("if (child->state==PROCESS_ZOMBIE) {\n        spin_unlock", body)

    def test_process_reap_requires_claim_and_wakes_parent_after_unlink(self):
        body = function_body(
            PROCESS,
            "int process_reap(",
            "int process_abort_new(",
        )
        self.assertIn("!process->reap_claimed", body)
        self.assertIn("parent_to_wake=parent", body)
        self.assertIn("wait_queue_wake_all(&parent_to_wake->child_waiters)", body)

    def test_cancel_releases_claim_and_wakes_waiters(self):
        body = function_body(
            PROCESS,
            "int process_reap_cancel(",
            "int process_child_wait_prepare(",
        )
        self.assertIn("process->reap_claimed=0", body)
        self.assertIn("wait_queue_wake_all(&parent->child_waiters)", body)

    def test_zombie_thread_reap_requires_exclusive_process_claim(self):
        body = function_body(
            PROCESS,
            "int process_thread_reap_begin(",
            "int process_thread_reap_finish(",
        )
        self.assertIn(
            "(process->state==PROCESS_ZOMBIE && !process->reap_claimed)",
            body,
        )

    def test_wait_syscall_never_uses_borrowed_child_pointer(self):
        body = function_body(
            SYSCALL,
            "case ZEROOS_SYS_WAIT:",
            "case ZEROOS_SYS_DISPLAY_INFO:",
        )
        self.assertIn("process_child_reap_claim", body)
        self.assertIn("process_reap_cancel(child)", body)
        self.assertNotIn("process_find_child", body)

    def test_claimed_reaper_uses_only_the_exclusive_pointer(self):
        body = function_body(
            SYSCALL,
            "static int syscall_reap_claimed_child(",
            "static void syscall_create_channel(",
        )
        self.assertIn("child->reap_claimed", body)
        self.assertNotIn("process_find_child", body)

    def test_guest_probe_checks_duplicate_claim_and_unclaimed_reap(self):
        self.assertIn("child reap claim was not exclusive", KERNEL)
        self.assertIn("unclaimed process reap was accepted", KERNEL)
        self.assertIn(
            "ZEROOS: child process reap claim is exclusive and cancellable.",
            KERNEL,
        )
        self.assertIn(
            "ZEROOS: child process reap claim is exclusive and cancellable.",
            MILESTONES,
        )
        self.assertIn("uint32_t reap_claimed;", HEADER)

    def test_pid_reuse_probe_claims_before_reaping_zombie_thread(self):
        self.assertIn(
            "process_reap_claim(process_probe_reuse)!=0 ||\n            thread_reap(thread_probe_reuse",
            KERNEL,
        )

    def test_claimed_reaper_bounds_and_validates_thread_walk(self):
        pending = function_body(
            SYSCALL,
            "static int syscall_child_final_thread_transition_pending(",
            "static int syscall_reap_claimed_child(",
        )
        self.assertIn("visited<ZEROOS_PROCESS_DEFAULT_MAX_THREADS", pending)
        self.assertIn("thread_pointer_valid(thread)", pending)
        reaper = function_body(
            SYSCALL,
            "static int syscall_reap_claimed_child(",
            "static void syscall_create_channel(",
        )
        self.assertIn("visited<ZEROOS_PROCESS_DEFAULT_MAX_THREADS", reaper)
        self.assertIn("thread_pointer_valid(thread)", reaper)

    def test_ci_runs_regression_before_build_tools(self):
        step = (
            "      - name: Verify process child reap ownership\n"
            "        run: python3 tools/test_process_child_reap_claim.py"
        )
        self.assertEqual(WORKFLOW.count(step), 1)
        self.assertLess(
            WORKFLOW.index(step),
            WORKFLOW.index("      - name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
