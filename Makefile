BUILD := build
KERNEL := $(BUILD)/zeroos.elf
ISO := $(BUILD)/zeroos.iso
HARDWARE_CORE_NAMES := net_core net_route net_transport net_conntrack net_l2 net_arp net_ipv6 netif net_stack net_socket firewall dns_core dhcp_core input_core usb_core audio_core display_core driver_core dma resource_core scancode_core mouse_core crypto
HARDWARE_CORE_OBJS := $(addprefix $(BUILD)/hardware-,$(addsuffix .o,$(HARDWARE_CORE_NAMES)))

CC := gcc
LD := ld
AS := gcc

# -mgeneral-regs-only is mandatory: interrupt entry, the IRQ reschedule path
# and context_switch_ex save only general-purpose registers, never x87/MMX/
# SSE/AVX state. Without it GCC -O2 keeps constants and copies in XMM
# registers, and any interrupt or task switch that also touches XMM silently
# replaces them (observed: init code immediates 16->0 and 12->0xffffffff,
# random IPC/storage self-test failures on SMP). kernel-simd-check enforces
# the invariant on the linked image.
CFLAGS := -m64 -mno-red-zone -mgeneral-regs-only -mcmodel=small -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdinc -Wall -Wextra -Werror -O2
CFLAGS += $(EXTRA_CFLAGS)
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -nostdlib
LDFLAGS := -m elf_x86_64 -T kernel/linker.ld -nostdlib

.PHONY: all clean elf iso iso-test verify-iso verify-multiboot2 repro-check boot-test completion-matrix run run-test check storage-tools-check stage1-scheduler-cert stage2-userspace-cert stage3-storage-cert stage4-hardware-cert stage5-desktop-cert stage6-security-cert stage7-gpu-media-cert stage8-compat-cert stage9-ai-perf-cert stage10-certification-release kernel-simd-check userspace-abi-check userspace-runtime-check userspace-abi-consistency hardware-core-test desktop-check compat-check

all: iso

# Reproducible local release gate: compile, Stage 1 scheduler certification,
# Stage 2 userspace certification, Stage 3 storage certification, Stage 4
# hardware certification, Stage 5 desktop certification, Stage 6 security/recovery,
# Stage 7 GPU/media/browser, Stage 8 Windows/Android compatibility,
# Stage 9 AI/ecosystem performance, Stage 10 release certification, ABI, core,
# desktop, compatibility, storage-image recovery, and SIMD-safety checks
# before guest boot.
# The certification build intentionally runs the embedded session in finite
# probe mode. A normal `make` produces the persistent interactive session.
check: CFLAGS += -DZEROOS_BOOT_CERTIFICATION
check: SESSION_CFLAGS += -DZEROOS_BOOT_CERTIFICATION
# NOTE: verify-iso is deliberately NOT a prerequisite here. It is invoked at
# the end of the iso recipe, which keeps it ordered after the image is written;
# listing it as a sibling prerequisite lets `make -j check` run it before the
# image exists.
check: iso completion-matrix userspace-abi-check userspace-runtime-check userspace-abi-consistency hardware-core-test desktop-check compat-check storage-tools-check stage1-scheduler-cert stage2-userspace-cert stage3-storage-cert stage4-hardware-cert stage5-desktop-cert stage6-security-cert stage7-gpu-media-cert stage8-compat-cert stage9-ai-perf-cert stage10-certification-release

storage-tools-check:
	bash tools/storage/host_selftest.sh

# Stage certification gates. Every gate receives $(BUILD) and declares the
# targets that produce the artifacts it consumes.
#
# (a) Without the prerequisites `make -j check` starts a certification before
#     its test binaries or the linked kernel exist and the whole gate fails
#     (reproduced deterministically as "stage10-cert: zeroos.elf missing").
# (b) Without $(BUILD) the gates certified a stale ./build even when the caller
#     selected another tree with BUILD=, e.g. make BUILD=build-fault check.
# Stages 1-5 inspect the linked image and run host suites, so they declare the
# targets that produce those artifacts as well.
stage1-scheduler-cert: elf
	bash tools/stage1_scheduler_cert.sh $(BUILD)

stage2-userspace-cert: elf
	bash tools/stage2_userspace_cert.sh $(BUILD)

stage3-storage-cert: elf
	bash tools/stage3_storage_cert.sh $(BUILD)

stage4-hardware-cert: elf hardware-core-test
	bash tools/stage4_hardware_cert.sh $(BUILD)

stage5-desktop-cert: elf desktop-check
	bash tools/stage5_desktop_cert.sh $(BUILD)

stage6-security-cert: hardware-core-test
	bash tools/stage6_security_update_recovery_cert.sh $(BUILD)

stage7-gpu-media-cert: hardware-core-test
	bash tools/stage7_gpu_media_browser_apps_cert.sh $(BUILD)

stage8-compat-cert: compat-check
	bash tools/stage8_windows_android_gaming_cert.sh $(BUILD)

stage9-ai-perf-cert: hardware-core-test desktop-check
	bash tools/stage9_zero_ai_ecosystem_performance_cert.sh $(BUILD)

stage10-certification-release: elf
	bash tools/stage10_certification_release.sh $(BUILD)

elf: $(KERNEL)

userspace-abi-check:
	$(CC) -std=c11 -Wall -Wextra -Werror -m64 -Iuserspace/include -fsyntax-only userspace/tests/abi_compile.c

userspace-runtime-check: | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin -m64 \
		-Iuserspace/include -c userspace/runtime.c -o $(BUILD)/userspace-runtime.o

userspace-abi-consistency:
	python3 userspace/tests/abi_consistency.py

# Stage 5 desktop/platform core: hosted unit + integration suite. Sources
# must also compile freestanding (no libc) for the on-target build.
DESKTOP_DIR := userspace/desktop
DESKTOP_SRC := $(wildcard $(DESKTOP_DIR)/src/*.c)
DESKTOP_TEST_SRC := $(wildcard $(DESKTOP_DIR)/tests/*.c)
DESKTOP_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 \
	-Iuserspace/include -I$(DESKTOP_DIR)/include -Ikernel

desktop-check: | $(BUILD)
	@set -e; for src in $(DESKTOP_SRC); do \
		$(CC) $(DESKTOP_CFLAGS) -c $$src -o $(BUILD)/desktop-$$(basename $$src .c).o; \
		$(CC) $(DESKTOP_CFLAGS) -ffreestanding -fno-builtin -m64 -c $$src \
			-o $(BUILD)/fs_desktop_$$(basename $$src .c).o; \
	done
	$(CC) $(DESKTOP_CFLAGS) -o $(BUILD)/desktop-tests $(DESKTOP_SRC) $(DESKTOP_TEST_SRC) kernel/crypto.c
	$(BUILD)/desktop-tests
	@echo "desktop-check: PASS"

COMPAT_DIR := userspace/compat
COMPAT_SRC := $(wildcard $(COMPAT_DIR)/src/*.c)
COMPAT_TEST_SRC := $(wildcard $(COMPAT_DIR)/tests/*.c)
COMPAT_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -I$(COMPAT_DIR)/include

compat-check: | $(BUILD)
	@set -e; for src in $(COMPAT_SRC); do \
		$(CC) $(COMPAT_CFLAGS) -c $$src -o $(BUILD)/compat-$$(basename $$src .c).o; \
		$(CC) $(COMPAT_CFLAGS) -ffreestanding -fno-builtin -m64 -c $$src \
			-o $(BUILD)/fs_compat_$$(basename $$src .c).o; \
	done
	$(CC) $(COMPAT_CFLAGS) -o $(BUILD)/compat-tests $(COMPAT_SRC) $(COMPAT_TEST_SRC)
	$(BUILD)/compat-tests
	@echo "compat-check: PASS"

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: boot/boot.S | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/isr.o: boot/isr.S | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/context.o: kernel/context.S | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/ap_trampoline.o: boot/ap_trampoline.S | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/user_entry.o: kernel/user_entry.S | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/hardware-%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -Ikernel -c $< -o $@

hardware-core-test: | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_core_test.c kernel/net_core.c -o $(BUILD)/net-core-test
	$(BUILD)/net-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/input_core_test.c kernel/input_core.c -o $(BUILD)/input-core-test
	$(BUILD)/input-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/usb_core_test.c kernel/usb_core.c -o $(BUILD)/usb-core-test
	$(BUILD)/usb-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/audio_core_test.c kernel/audio_core.c -o $(BUILD)/audio-core-test
	$(BUILD)/audio-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/display_core_test.c kernel/display_core.c -o $(BUILD)/display-core-test
	$(BUILD)/display-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/driver_core_test.c kernel/driver_core.c -o $(BUILD)/driver-core-test
	$(BUILD)/driver-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_route_test.c kernel/net_route.c -o $(BUILD)/net-route-test
	$(BUILD)/net-route-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_transport_test.c kernel/net_transport.c -o $(BUILD)/net-transport-test
	$(BUILD)/net-transport-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/dhcp_core_test.c kernel/dhcp_core.c -o $(BUILD)/dhcp-core-test
	$(BUILD)/dhcp-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/dhcp_client_test.c kernel/dhcp_core.c -o $(BUILD)/dhcp-client-test
	$(BUILD)/dhcp-client-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/dma_test.c kernel/dma.c -o $(BUILD)/dma-test
	$(BUILD)/dma-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/dns_core_test.c kernel/dns_core.c -o $(BUILD)/dns-core-test
	$(BUILD)/dns-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_l2_test.c kernel/net_l2.c -o $(BUILD)/net-l2-test
	$(BUILD)/net-l2-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_arp_test.c kernel/net_arp.c kernel/net_l2.c -o $(BUILD)/net-arp-test
	$(BUILD)/net-arp-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_ipv6_test.c kernel/net_ipv6.c -o $(BUILD)/net-ipv6-test
	$(BUILD)/net-ipv6-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_conntrack_test.c kernel/net_conntrack.c -o $(BUILD)/net-conntrack-test
	$(BUILD)/net-conntrack-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/resource_core_test.c kernel/resource_core.c -o $(BUILD)/resource-core-test
	$(BUILD)/resource-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/scancode_core_test.c kernel/scancode_core.c -o $(BUILD)/scancode-core-test
	$(BUILD)/scancode-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/mouse_core_test.c kernel/mouse_core.c -o $(BUILD)/mouse-core-test
	$(BUILD)/mouse-core-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/netif_test.c kernel/netif.c kernel/net_l2.c -o $(BUILD)/netif-test
	$(BUILD)/netif-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_stack_test.c kernel/net_stack.c kernel/netif.c kernel/net_l2.c kernel/net_core.c kernel/firewall.c kernel/net_ipv6.c kernel/net_transport.c kernel/net_socket.c kernel/sandbox.c -o $(BUILD)/net-stack-test
	$(BUILD)/net-stack-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_stack_tx_test.c kernel/net_stack.c kernel/netif.c kernel/net_l2.c kernel/net_core.c kernel/firewall.c kernel/net_ipv6.c kernel/net_transport.c kernel/sandbox.c -o $(BUILD)/net-stack-tx-test
	$(BUILD)/net-stack-tx-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_socket_test.c kernel/net_socket.c -o $(BUILD)/net-socket-test
	$(BUILD)/net-socket-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/crypto_test.c kernel/crypto.c -o $(BUILD)/crypto-core-test
	$(BUILD)/crypto-core-test

# Stage 3 storage stack and shared kernel infrastructure. These objects use
# compiler-generated dependency files so header changes rebuild dependants.
STORAGE_SRCS := $(wildcard kernel/storage/*.c)
STORAGE_OBJS := $(patsubst kernel/storage/%.c,$(BUILD)/storage/%.o,$(STORAGE_SRCS))
INFRA_OBJS := $(BUILD)/ksync.o $(BUILD)/crc.o $(BUILD)/kstring.o $(BUILD)/pci.o $(BUILD)/sandbox.o
PROBE_OBJ := $(BUILD)/storage_probe_image.o
SESSION_OBJ := $(BUILD)/session_probe_image.o
EXTRA_OBJS := $(STORAGE_OBJS) $(INFRA_OBJS) $(PROBE_OBJ) $(SESSION_OBJ)

$(BUILD)/storage:
	mkdir -p $(BUILD)/storage

$(BUILD)/storage/%.o: kernel/storage/%.c | $(BUILD)/storage
	$(CC) $(CFLAGS) -MMD -MP -Ikernel -c $< -o $@

# Compiler-generated dependency files. Including them is what makes a new
# #include rebuild its dependants; without this every header set is maintained
# by hand and a missed entry silently links stale objects.
-include $(wildcard $(BUILD)/*.d) $(wildcard $(BUILD)/storage/*.d)

# Stage 5 Ring-3 session/shell process: desktop display/compositor/input
# modules linked freestanding and embedded into the kernel image.
SESSION_CFLAGS := -std=c11 -m64 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -nostdlib -mno-red-zone -mgeneral-regs-only -mcmodel=large \
	-Wall -Wextra -Werror -O2 -Iuserspace/include -Iuserspace/desktop/include
SESSION_DESKTOP_SRCS := userspace/desktop/src/common.c \
	userspace/desktop/src/window.c userspace/desktop/src/compositor.c \
	userspace/desktop/src/input.c userspace/desktop/src/display.c \
	userspace/desktop/src/metrics.c
SESSION_DESKTOP_OBJS := $(patsubst userspace/desktop/src/%.c,$(BUILD)/session_desktop_%.o,$(SESSION_DESKTOP_SRCS))
SESSION_DEPS := userspace/session/session.c userspace/session/session_start.S \
	userspace/session/session.ld userspace/include/zeroos/syscall.h \
	$(wildcard userspace/desktop/include/zeroos/desktop/*.h) \
	$(SESSION_DESKTOP_SRCS)
$(BUILD)/session_probe.elf: $(SESSION_DEPS) | $(BUILD)
	$(CC) $(SESSION_CFLAGS) -c userspace/session/session_start.S -o $(BUILD)/session_start.o
	$(CC) $(SESSION_CFLAGS) -c userspace/session/session.c -o $(BUILD)/session_probe_user.o
	$(CC) $(SESSION_CFLAGS) -c userspace/desktop/src/common.c -o $(BUILD)/session_desktop_common.o
	$(CC) $(SESSION_CFLAGS) -c userspace/desktop/src/window.c -o $(BUILD)/session_desktop_window.o
	$(CC) $(SESSION_CFLAGS) -c userspace/desktop/src/compositor.c -o $(BUILD)/session_desktop_compositor.o
	$(CC) $(SESSION_CFLAGS) -c userspace/desktop/src/input.c -o $(BUILD)/session_desktop_input.o
	$(CC) $(SESSION_CFLAGS) -c userspace/desktop/src/display.c -o $(BUILD)/session_desktop_display.o
	$(CC) $(SESSION_CFLAGS) -c userspace/desktop/src/metrics.c -o $(BUILD)/session_desktop_metrics.o
	$(LD) -m elf_x86_64 -T userspace/session/session.ld -nostdlib -o $@ \
		$(BUILD)/session_start.o $(BUILD)/session_probe_user.o $(SESSION_DESKTOP_OBJS)

$(BUILD)/session_probe_image.o: kernel/session_probe_image.S $(BUILD)/session_probe.elf | $(BUILD)
	$(AS) $(ASFLAGS) -DPROBE_PATH='"$(BUILD)/session_probe.elf"' -c $< -o $@

# Freestanding Ring-3 storage probe (C), embedded into the kernel image.
PROBE_CFLAGS := -std=c11 -m64 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -nostdlib -mno-red-zone -mgeneral-regs-only -mcmodel=large \
	-Wall -Wextra -Werror -O2 -Iuserspace/include -Iuserspace/storage
$(BUILD)/storage_probe.elf: userspace/storage/probe.c userspace/storage/probe_start.S userspace/storage/probe.ld userspace/include/zeroos/syscall.h userspace/include/zeroos/storage.h | $(BUILD)
	$(CC) $(PROBE_CFLAGS) -c userspace/storage/probe_start.S -o $(BUILD)/storage_probe_start.o
	$(CC) $(PROBE_CFLAGS) -c userspace/storage/probe.c -o $(BUILD)/storage_probe.o
	$(LD) -m elf_x86_64 -T userspace/storage/probe.ld -nostdlib -o $@ \
		$(BUILD)/storage_probe_start.o $(BUILD)/storage_probe.o

$(BUILD)/storage_probe_image.o: kernel/storage_probe_image.S $(BUILD)/storage_probe.elf | $(BUILD)
	$(AS) $(ASFLAGS) -DPROBE_PATH='"$(BUILD)/storage_probe.elf"' -c $< -o $@

# One rule builds every kernel C object, with compiler-generated dependency
# files. It replaces 28 hand-written rules that each listed their headers by
# hand - a new #include did not rebuild the object, so a header change could
# silently link a stale object into the kernel.
# session_launch.o is built from kernel/session.c: the target name differs from
# the source name, so the pattern rule below cannot produce it.
$(BUILD)/session_launch.o: kernel/session.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -Ikernel -c kernel/session.c -o $@

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -Ikernel -c $< -o $@

# Build provenance, recorded inside the kernel image. A released binary that
# does not name the revision it was built from cannot be tied back to source.
BUILD_REVISION := $(shell git -C . rev-parse --short HEAD 2>/dev/null || echo unknown)
BUILD_DIRTY := $(shell test -n "$$(git -C . status --porcelain 2>/dev/null)" && echo -dirty || echo "")
TOOLCHAIN_ID := $(shell $(CC) -dumpmachine 2>/dev/null || echo unknown)

$(BUILD)/build_info.c: | $(BUILD)
	@{ \
	  echo '/* Generated by the Makefile. Do not edit. */'; \
	  echo 'const char zeroos_build_revision[] = "$(BUILD_REVISION)$(BUILD_DIRTY)";'; \
	  echo 'const char zeroos_build_toolchain[] = "$(CC) $(TOOLCHAIN_ID) $(LD)";'; \
	  echo 'const char zeroos_build_cflags[] = "$(CFLAGS)";'; \
	} > $@.tmp
	@if cmp -s $@.tmp $@ 2>/dev/null; then rm -f $@.tmp; else mv $@.tmp $@; fi

BUILD_INFO_OBJ := $(BUILD)/build_info.o

KERNEL_OBJS := $(BUILD_INFO_OBJ) $(BUILD)/session_launch.o $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/context.o $(BUILD)/ap_trampoline.o $(BUILD)/user_entry.o $(BUILD)/kernel.o $(BUILD)/cpu.o $(BUILD)/apic.o $(BUILD)/smp.o $(BUILD)/acpi.o $(BUILD)/interrupts.o $(BUILD)/syscall.o $(BUILD)/ipc.o $(BUILD)/shmem.o $(BUILD)/fb.o $(BUILD)/input.o $(BUILD)/elf.o $(BUILD)/exec.o $(BUILD)/user.o $(BUILD)/pic.o $(BUILD)/timer.o $(BUILD)/sync.o $(BUILD)/memory.o $(BUILD)/gdt.o $(BUILD)/vmm.o $(BUILD)/tlb.o $(BUILD)/task.o $(BUILD)/wait.o $(BUILD)/scheduler.o $(BUILD)/thread.o $(BUILD)/process.o $(BUILD)/scheduler_stress.o $(EXTRA_OBJS) $(HARDWARE_CORE_OBJS)
$(KERNEL): $(KERNEL_OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)
	@$(MAKE) --no-print-directory kernel-simd-check verify-multiboot2

# Fails the build if the linked kernel contains any x87/MMX/SSE/AVX
# instruction (see the CFLAGS note: that state is never saved).
kernel-simd-check:
	@test -f $(KERNEL) || { echo "kernel-simd-check: $(KERNEL) missing"; exit 1; }
	@objdump -d --no-show-raw-insn $(KERNEL) | \
		grep -E '%[xyz]mm[0-9]|%mm[0-9]|%st(\(|[^a-z]|$$)|\b(f(ld|st|add|mul|sub|div|init|nstenv|xsave|xrstor|ninit)|ldmxcsr|stmxcsr|emms)[a-z0-9]*\b' \
		> $(BUILD)/kernel-simd.txt; \
	if [ -s $(BUILD)/kernel-simd.txt ]; then \
		echo "kernel-simd-check: FP/SIMD instructions in kernel image:"; head -20 $(BUILD)/kernel-simd.txt; exit 1; \
	fi; echo "kernel-simd-check: no FP/SIMD instructions in kernel image."

# ---------------------------------------------------------------------------
# Compile-flag fingerprint.
#
# `make check` adds -DZEROOS_BOOT_CERTIFICATION through a target-specific
# variable. Make keys rebuilds on timestamps only, so without this the
# certification objects stay in $(BUILD) and the next plain `make iso` links
# them straight into an image that is labelled production but exits after the
# finite session probes instead of staying interactive. Verified before the
# fix: build/session_launch.o still contained the certification-only string
# "session did not finish" after a certification-free `make iso`.
#
# The recipe rewrites the fingerprint only when the flags actually change, so
# a normal rebuild is untouched; switching between flavours rebuilds once.
FLAGS_FILE := $(BUILD)/.compile-flags

# Two targets on purpose. A plain file target whose only prerequisite is
# order-only is treated as up to date, so its comparison recipe never runs and
# a flag change is missed. The phony probe always runs the comparison but only
# rewrites the real file when the flags actually differ, so dependants are
# rebuilt exactly when the flavour changes and never otherwise.
.PHONY: flags-probe
flags-probe: | $(BUILD)
	@printf 'CFLAGS=%s\nSESSION_CFLAGS=%s\nPROBE_CFLAGS=%s\n' \
		'$(CFLAGS)' '$(SESSION_CFLAGS)' '$(PROBE_CFLAGS)' > $(FLAGS_FILE).tmp
	@if cmp -s $(FLAGS_FILE).tmp $(FLAGS_FILE) 2>/dev/null; then \
		rm -f $(FLAGS_FILE).tmp; \
	else \
		mv $(FLAGS_FILE).tmp $(FLAGS_FILE); \
		echo "compile flags changed - rebuilding objects"; \
	fi

$(FLAGS_FILE): flags-probe ; @:

# Everything compiled with $(CFLAGS), $(SESSION_CFLAGS) or $(PROBE_CFLAGS)
# depends on the fingerprint. The two probe ELFs are listed rather than their
# objects: those .o files are produced inside the probe recipes and have no
# rules of their own, so declaring them as prerequisites does nothing. Listing
# the ELF targets makes the whole recipe re-run, which is what recompiles the
# embedded Ring-3 session under the current SESSION_CFLAGS.

$(KERNEL_OBJS) $(BUILD)/session_probe.elf $(BUILD)/storage_probe.elf: $(FLAGS_FILE)

# ISO images.
#
# A release ISO is only useful if firmware can boot it, so the writer refuses
# to emit an image with no boot path. grub-mkrescue is the supported route: it
# supplies the GRUB El Torito boot image that a multiboot2 kernel needs. The
# previous xorriso branch (`xorriso -as mkisofs -R -J`) and the old
# tools/build_iso.py both produced images that tooling accepted and firmware
# could not boot - no El Torito boot record at all, and in the script's case a
# flattened directory tree, so even a loaded GRUB could not find
# /boot/grub/grub.cfg. Both are gone.
#
# On a host without GRUB, set ZEROOS_ISO_ALLOW_UNBOOTABLE=1 to force a
# data-only image (for ISO-layout testing only); verify-iso then reports it as
# not bootable rather than letting it pass silently.
ISO_UNBOOTABLE_FLAG := $(if $(ZEROOS_ISO_ALLOW_UNBOOTABLE),--no-boot,)
ISO_VERIFY_FLAG := $(if $(ZEROOS_ISO_ALLOW_UNBOOTABLE),--allow-unbootable,)
# Hosts that have grub-mkimage but not grub-mkrescue can point this at the El
# Torito boot image they generated: make ZEROOS_ELTORITO=/path/to/eltorito.img iso
ISO_ELTORITO_FLAG := $(if $(ZEROOS_ELTORITO),--eltorito $(ZEROOS_ELTORITO),)

# Validates the Multiboot2 header of the linked image against the
# specification: magic/architecture/checksum arithmetic, 8-byte alignment,
# containment in a PT_LOAD segment inside the 32 KiB search window, tag list
# termination, and agreement between the framebuffer tag and grub.cfg. A
# header defect is invisible to every host test but fatal at boot.
# Release hygiene: the image must be freestanding (no host libc), must name the
# revision it came from, and must be reproducible from the same inputs.
repro-check: $(KERNEL)
	bash tools/check_release_hygiene.sh $(BUILD)

# Section 58 of the G560 master prompt: one authoritative completion matrix,
# derived from the built tree rather than written by hand. --strict turns the
# current state into a floor, so a regression fails here instead of showing up
# as a silently downgraded document.
completion-matrix: elf hardware-core-test desktop-check
	python3 tools/completion_matrix.py --build-dir $(BUILD) --write \
		--strict Boot=QEMU_CERTIFIED \
		--strict Memory=QEMU_CERTIFIED \
		--strict Process=QEMU_CERTIFIED \
		--strict ELF=QEMU_CERTIFIED \
		--strict Input=QEMU_CERTIFIED \
		--strict Display=QEMU_CERTIFIED \
		--strict IPC=QEMU_CERTIFIED \
		--strict Scheduler=QEMU_CERTIFIED

verify-multiboot2:
	@test -f $(KERNEL) || { echo "verify-multiboot2: $(KERNEL) missing"; exit 1; }
	python3 tools/verify_multiboot2.py $(KERNEL) --expect-wxhxb 1024x768x32

iso: $(KERNEL)
	rm -rf $(BUILD)/iso
	mkdir -p $(BUILD)/iso/boot/grub
	cp $(KERNEL) $(BUILD)/iso/boot/zeroos.elf
	cp grub/grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	@if command -v grub-mkrescue >/dev/null 2>&1; then \
		grub-mkrescue -o $(ISO) $(BUILD)/iso; \
	else \
		python3 tools/build_iso.py $(ISO) $(BUILD)/iso $(ISO_ELTORITO_FLAG) $(ISO_UNBOOTABLE_FLAG); \
	fi
	@$(MAKE) --no-print-directory verify-iso

# Independent check that the produced image is a structurally bootable ISO 9660
# / El Torito image. Runs the project's own parser and, when pycdlib is
# installed, cross-checks the tree with a third-party implementation.
verify-iso:
	@test -f $(ISO) || { echo "verify-iso: $(ISO) missing - build it first"; exit 1; }
	python3 tools/verify_iso.py $(ISO) $(ISO_VERIFY_FLAG)

# Testing-mode image: the same kernel with ZEROOS_BOOT_CERTIFICATION, so the
# embedded Ring-3 session runs its finite display/compositor/input probes and
# exits instead of blocking on the interactive input wait. Built in its own
# tree so it never overwrites the production image. Boot this one for
# automated/repeatable testing; boot zeroos.iso for the persistent desktop.
TEST_BUILD := $(BUILD)-test
iso-test:
	@$(MAKE) --no-print-directory ZEROOS_CERTIFICATION=1 BUILD=$(TEST_BUILD) iso
	@echo "iso-test: testing-mode image at $(TEST_BUILD)/zeroos.iso"

# Real boot test: boots the image in QEMU and asserts the guest serial log
# against every milestone in tools/boot_milestones.txt - the same list CI uses.
# This is the only gate that executes kernel code, so it is the only one that
# can catch a kernel which compiles and links but hangs during boot. Requires
# qemu-system-x86_64; pass ZEROOS_SERIAL_LOG=<file> to assert a captured log
# without an emulator.
boot-test:
	@if [ -n "$(ZEROOS_SERIAL_LOG)" ]; then \
		bash tools/boot_test.sh --serial-log $(ZEROOS_SERIAL_LOG); \
	else \
		bash tools/boot_test.sh --build-dir $(BUILD) --iterations 3; \
	fi

run: iso
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -display none

run-test:
	qemu-system-x86_64 -cdrom $(TEST_BUILD)/zeroos.iso -serial stdio -display none

clean:
	rm -rf $(BUILD) $(TEST_BUILD)

# ZEROOS_CERTIFICATION=1 builds the finite-probe certification flavour of both
# the kernel and the embedded session. It must be a command-line variable so
# the recursive `make iso-test` sees it while this file is parsed.
ifeq ($(ZEROOS_CERTIFICATION),1)
CFLAGS += -DZEROOS_BOOT_CERTIFICATION
SESSION_CFLAGS += -DZEROOS_BOOT_CERTIFICATION
endif
