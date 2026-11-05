#!/usr/bin/env python3
"""Regression checks for x86-64 Ring-3 interrupt GS-base ownership."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPU_C = ROOT / "kernel" / "cpu.c"
CPU_H = ROOT / "kernel" / "cpu.h"
ISR = ROOT / "boot" / "isr.S"
USER_ENTRY = ROOT / "kernel" / "user_entry.S"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
BOOT_TEST = ROOT / "tools" / "boot_test.sh"
CONTEXT = ROOT / "kernel" / "context.S"


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


class InterruptGsOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpu = CPU_C.read_text(encoding="utf-8")
        cls.cpu_header = CPU_H.read_text(encoding="utf-8")
        cls.isr = ISR.read_text(encoding="utf-8")
        cls.user_entry = USER_ENTRY.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")
        cls.boot_test = BOOT_TEST.read_text(encoding="utf-8")
        cls.context = CONTEXT.read_text(encoding="utf-8")

    def test_cpu_local_validation_is_shared_by_lookup_and_entry(self):
        validator = function_body(self.cpu, "static int cpu_local_base_valid(")
        self.assertIn("&cpu_locals[0]", validator)
        self.assertIn("&cpu_locals[ZEROOS_MAX_CPUS]", validator)
        self.assertIn("sizeof(cpu_locals[0])", validator)

        lookup = function_body(self.cpu, "const struct cpu_local *cpu_local(")
        self.assertIn("cpu_local_base_valid(base)", lookup)

        entry = function_body(self.cpu, "int cpu_interrupt_gs_enter(")
        self.assertIn("cpu_local_base_valid(gs_base)", entry)
        self.assertIn("IA32_KERNEL_GS_BASE_MSR", entry)
        self.assertIn('volatile ("swapgs"', entry)
        self.assertIn("return 1", entry)

    def test_interrupt_entry_fails_closed_if_kernel_gs_is_not_published(self):
        entry = function_body(self.cpu, "int cpu_interrupt_gs_enter(")
        self.assertLess(
            entry.find("cpu_local_base_valid(cpu_read_msr(IA32_KERNEL_GS_BASE_MSR))"),
            entry.find('volatile ("swapgs"'),
        )
        self.assertIn("cpu_gs_transition_halt()", entry)

    def test_interrupt_return_restores_same_frame_or_target_privilege(self):
        restore = function_body(
            self.cpu, "void cpu_interrupt_gs_restore("
        )
        self.assertIn("target_frame==original_frame", restore)
        self.assertIn("if (entry_swapped)", restore)
        self.assertIn("cpu_interrupt_gs_select_target(target_frame)", restore)
        self.assertIn("cpu_local_base_valid(cpu_read_msr(IA32_GS_BASE_MSR))", restore)

    def test_isr_enters_kernel_gs_before_dispatch_and_preserves_frame_layout(self):
        body = self.isr.split("isr_common:", 1)[1].split(".section .rodata", 1)[0]
        enter = body.index("call cpu_interrupt_gs_enter")
        dispatch = body.index("call interrupt_dispatch")
        self.assertLess(enter, dispatch)
        self.assertIn("subq $16, %rsp", body[:enter])
        self.assertIn("movq %rax, (%rsp)", body[enter:dispatch])
        self.assertIn("leaq 16(%rsp), %rdi", body[enter:dispatch])

    def test_frame_return_restores_gs_after_handoff_and_before_iret(self):
        body = self.isr.split("isr_common:", 1)[1].split(".section .rodata", 1)[0]
        cooperative_return = body.index("retq")
        handoff = body.index("call task_handoff_complete")
        restore = body.index("call cpu_interrupt_gs_restore")
        iret = body.index("iretq")
        self.assertLess(cooperative_return, restore)
        self.assertLess(handoff, restore)
        self.assertLess(restore, iret)
        self.assertIn("leaq 16(%rsp), %r14", body)
        self.assertIn("movq 0(%rsp), %r13", body)
        self.assertIn("movq %r12, %rsi", body)
        self.assertIn("movq %r13, %rdx", body)


    def test_cooperative_frame_resume_selects_gs_before_iret(self):
        body = self.context.split(".Lresume_frame:", 1)[1].split(".section", 1)[0]
        handoff = body.index("call task_handoff_complete")
        prepare = body.index("call cpu_interrupt_gs_prepare_frame")
        iret = body.index("iretq")
        self.assertLess(handoff, prepare)
        self.assertLess(prepare, iret)
        self.assertIn("movq %rsp, %rdi", body[handoff:prepare])

    def test_cooperative_frame_gs_helper_fails_closed_and_uses_target_ring(self):
        select = function_body(
            self.cpu, "static void cpu_interrupt_gs_select_target("
        )
        self.assertIn(
            "cpu_local_base_valid(cpu_read_msr(IA32_GS_BASE_MSR))", select
        )
        self.assertIn("(target_frame->cs & 3ULL)==3ULL", select)
        self.assertIn("(target_frame->cs & 3ULL)!=0", select)
        self.assertIn("cpu_gs_transition_halt()", select)
        self.assertIn('volatile ("swapgs"', select)

        prepare = function_body(
            self.cpu, "void cpu_interrupt_gs_prepare_frame("
        )
        self.assertIn("cpu_interrupt_gs_select_target(target_frame)", prepare)

    def test_first_ring3_entry_switches_from_kernel_gs_before_iret(self):
        body = self.user_entry.split("zeroos_user_enter:", 1)[1]
        self.assertLess(body.index("swapgs"), body.index("iretq"))

    def test_bsp_and_ap_initialize_the_user_gs_slot(self):
        self.assertEqual(
            self.cpu.count("cpu_write_msr(IA32_KERNEL_GS_BASE_MSR,0);"),
            2,
            "BSP and AP bring-up must both start with a known user GS base",
        )
        self.assertIn(
            "void cpu_interrupt_gs_restore(const struct interrupt_frame *original_frame,",
            self.cpu_header,
        )

    def test_qemu_boot_timeout_force_kills_paused_emulator(self):
        self.assertIn('timeout --kill-after=5s "${timeout_s}s" qemu-system-x86_64', self.boot_test)
        self.assertIn('[ "$qemu_status" -ne 124 ] && [ "$qemu_status" -ne 137 ]', self.boot_test)

    def test_ci_runs_regression_before_installing_build_tools(self):
        invocation = "python3 tools/test_interrupt_gs_entry.py"
        self.assertIn(invocation, self.workflow)
        self.assertLess(
            self.workflow.index(invocation),
            self.workflow.index("- name: Install build and QEMU tools"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
