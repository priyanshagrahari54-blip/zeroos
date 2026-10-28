#!/usr/bin/env python3
"""Regression checks for affinity updates racing AP hot-offline."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
TASK = ROOT / "kernel" / "task.c"
MILESTONES = ROOT / "tools" / "boot_milestones.txt"
STAGE1 = ROOT / "tools" / "stage1_scheduler_cert.sh"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
MARKER = "ZEROOS: scheduler affinity/offline race self-test passed."


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


class SchedulerAffinityOfflineRaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.task = TASK.read_text(encoding="utf-8")
        cls.milestones = MILESTONES.read_text(encoding="utf-8")
        cls.stage1 = STAGE1.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_running_and_runnable_affinity_is_revalidated_under_task_lock(self):
        body = function_body(self.task, "int task_set_affinity(")
        lock = body.find("spin_lock_irqsave(&task_lock)")
        gate = body.find("task_affinity_has_online_target_locked(affinity)")
        mutation = body.find("task->cpu_affinity=affinity;")
        self.assertGreaterEqual(lock, 0, "setter must hold task_lock")
        self.assertGreater(gate, lock, "target must be revalidated after locking")
        self.assertGreater(mutation, gate, "reject unavailable target before mutation")
        rejection = body[gate:mutation]
        self.assertIn("spin_unlock_irqrestore(&task_lock,flags)", rejection)
        self.assertIn("return -1", rejection)

    def test_offline_request_excludes_still_online_cpu(self):
        helper = function_body(
            self.task, "static int task_affinity_mask_has_online_target("
        )
        self.assertIn("affinity & online_mask & ~offline_mask", helper)
        policy = function_body(
            self.task, "static int task_scheduler_policy_self_test("
        )
        self.assertIn(
            "task_affinity_mask_has_online_target(0x2ULL,0x3ULL,0x2ULL)",
            policy,
        )
        self.assertIn(
            "task_affinity_mask_has_online_target(0x3ULL,0x3ULL,0x2ULL)",
            policy,
        )
        self.assertIn(
            "task_affinity_mask_has_online_target(0x1ULL,0x3ULL,0x2ULL)",
            policy,
        )

    def test_runtime_marker_is_required_by_boot_and_stage1_certification(self):
        self.assertIn(MARKER, self.task)
        self.assertIn(MARKER, self.milestones)
        self.assertIn(MARKER, self.stage1)

    def test_ci_runs_this_regression_before_build_and_boot_gates(self):
        invocation = "python3 tools/test_task_affinity_offline_race.py"
        self.assertIn(invocation, self.workflow)
        self.assertLess(
            self.workflow.index(invocation),
            self.workflow.index("- name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
