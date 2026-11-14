#!/usr/bin/env python3
"""Regression checks for process/thread ownership during list attachment."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
THREAD = (ROOT / "kernel" / "thread.c").read_text(encoding="utf-8")
PROCESS = (ROOT / "kernel" / "process.c").read_text(encoding="utf-8")
KERNEL = (ROOT / "kernel" / "kernel.c").read_text(encoding="utf-8")
MILESTONES = (ROOT / "tools" / "boot_milestones.txt").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
MARKER = "ZEROOS: cross-process and duplicate thread attachment rejected."


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing body: {signature}")
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
            elif (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = "code"
        elif state == "line":
            if ch == chr(10):
                state = "code"
        elif state == "block" and ch == "*" and nxt == "/":
            state = "code"
            index += 1
        index += 1
    raise AssertionError(f"unterminated body: {signature}")


class ProcessThreadAttachmentTests(unittest.TestCase):
    def test_attach_validator_checks_slot_generation_state_owner_and_staged_task(self):
        body = function_body(THREAD, "int thread_attachable_to_process(")
        compact = "".join(body.split())
        for token in (
            "thread_pointer_valid(thread)",
            "thread_lookup_locked(thread->tid)==thread",
            "thread->state==THREAD_NEW",
            "thread->process==process",
            "thread->scheduler_task_id!=0",
            "spin_lock_irqsave(&thread_lock)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, compact)

    def test_process_thread_walk_is_bounded_and_validates_each_link(self):
        body = function_body(PROCESS, "static int process_thread_link_locked(")
        compact = "".join(body.split())
        self.assertIn("visited<ZEROOS_PROCESS_THREAD_WALK_LIMIT", compact)
        self.assertIn("thread_pointer_valid(current)", compact)
        self.assertIn("current->process!=process", compact)
        self.assertIn("return*cursor?-1:0", compact)

    def test_attach_rejects_foreign_and_duplicate_objects_before_accounting(self):
        body = function_body(PROCESS, "int process_thread_attach(")
        self.assertIn("process_thread_link_locked(process,thread,0)!=0", body)
        self.assertIn("thread_attachable_to_process(thread,process)", body)
        self.assertLess(
            body.index("thread_attachable_to_process(thread,process)"),
            body.index("process_thread_unreserve_locked(process)"),
        )
        self.assertLess(
            body.index("process_thread_link_locked(process,thread,0)"),
            body.index("process_thread_unreserve_locked(process)"),
        )

    def test_started_and_exited_require_current_process_list_membership(self):
        for signature in (
            "int process_thread_started(",
            "int process_thread_exited(",
        ):
            body = function_body(PROCESS, signature)
            with self.subTest(function=signature):
                self.assertIn("process_thread_link_locked(process,thread,0)!=1", body)

    def test_detach_and_reap_use_bounded_validated_list_links(self):
        for signature in (
            "int process_thread_detach(",
            "int process_thread_reap_begin(",
        ):
            body = function_body(PROCESS, signature)
            with self.subTest(function=signature):
                self.assertIn("process_thread_link_locked(process,thread,&cursor)!=1", body)
                self.assertNotIn("while (*cursor && *cursor!=thread)", body)

    def test_guest_probe_rejects_foreign_and_duplicate_attach_without_count_changes(self):
        body = function_body(KERNEL, "static void process_thread_probe_monitor_step(")
        self.assertIn("process_thread_attach(process_probe_child,thread_probe_parent)", body)
        self.assertIn("process_thread_attach(process_probe_parent,thread_probe_parent)", body)
        self.assertIn("process_thread_unreserve(process_probe_child)", body)
        self.assertIn("process_thread_unreserve(process_probe_parent)", body)
        self.assertIn("process_thread_count(process_probe_parent)!=parent_threads", body)
        self.assertIn("process_live_thread_count(process_probe_child)!=child_live", body)
        self.assertIn(MARKER, body)
        self.assertIn(MARKER, MILESTONES)

    def test_ci_invokes_regression_exactly_once_before_toolchain_install(self):
        invocation = "python3 tools/test_process_thread_attachment.py"
        self.assertEqual(WORKFLOW.count(invocation), 1)
        self.assertLess(
            WORKFLOW.index(invocation),
            WORKFLOW.index("- name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
