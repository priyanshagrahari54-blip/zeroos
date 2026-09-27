BUILD := build
KERNEL := $(BUILD)/zeroos.elf
ISO := $(BUILD)/zeroos.iso
HARDWARE_CORE_NAMES := net_core net_route net_transport net_conntrack net_l2 net_arp net_ipv6 netif net_stack net_socket dns_core dhcp_core input_core usb_core audio_core display_core driver_core dma resource_core scancode_core mouse_core crypto
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

.PHONY: all clean elf iso run check storage-tools-check kernel-simd-check userspace-abi-check userspace-runtime-check userspace-abi-consistency hardware-core-test desktop-check compat-check sanitizer-check

all: iso

# Reproducible local release gate: compile, ABI, core, desktop,
# compatibility, storage-image recovery, and SIMD-safety checks before guest boot.
check: elf userspace-abi-check userspace-runtime-check userspace-abi-consistency hardware-core-test desktop-check compat-check storage-tools-check sanitizer-check

storage-tools-check:
	bash tools/storage/host_selftest.sh

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

$(BUILD)/kernel.o: kernel/kernel.c kernel/storage/storage.h kernel/types.h kernel/cpu.h kernel/apic.h kernel/acpi.h kernel/memory.h kernel/timer.h kernel/vmm.h kernel/gdt.h kernel/sync.h kernel/tlb.h kernel/task.h kernel/thread.h kernel/process.h kernel/scheduler.h kernel/smp.h kernel/wait.h kernel/user.h kernel/ipc.h kernel/shmem.h kernel/fb.h kernel/input.h kernel/session.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/interrupts.o: kernel/interrupts.c kernel/interrupts.h kernel/sync.h kernel/types.h kernel/cpu.h kernel/apic.h kernel/pic.h kernel/timer.h kernel/gdt.h kernel/task.h kernel/thread.h kernel/tlb.h kernel/scheduler.h kernel/syscall.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/syscall.o: kernel/syscall.c kernel/syscall.h kernel/interrupts.h kernel/process.h kernel/thread.h kernel/task.h kernel/timer.h kernel/vmm.h kernel/ipc.h kernel/shmem.h kernel/exec.h kernel/fb.h kernel/display_core.h kernel/input.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/fb.o: kernel/fb.c kernel/fb.h kernel/memory.h kernel/vmm.h kernel/sync.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/input.o: kernel/input.c kernel/input.h kernel/input_core.h kernel/scancode_core.h kernel/mouse_core.h kernel/interrupts.h kernel/apic.h kernel/pic.h kernel/wait.h kernel/sync.h kernel/timer.h kernel/task.h kernel/syscall.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/ipc.o: kernel/ipc.c kernel/ipc.h kernel/process.h kernel/sync.h kernel/wait.h kernel/task.h kernel/timer.h kernel/syscall.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/shmem.o: kernel/shmem.c kernel/shmem.h kernel/memory.h kernel/process.h kernel/sync.h kernel/user.h kernel/vmm.h kernel/syscall.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/elf.o: kernel/elf.c kernel/elf.h kernel/memory.h kernel/process.h kernel/user.h kernel/vmm.h kernel/syscall.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/exec.o: kernel/exec.c kernel/exec.h kernel/elf.h kernel/memory.h kernel/process.h kernel/thread.h kernel/sync.h kernel/syscall.h kernel/user.h kernel/vmm.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/user.o: kernel/user.c kernel/user.h kernel/exec.h kernel/memory.h kernel/process.h kernel/thread.h kernel/vmm.h kernel/syscall.h kernel/ipc.h kernel/elf.h kernel/fb.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/pic.o: kernel/pic.c kernel/pic.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/timer.o: kernel/timer.c kernel/timer.h kernel/types.h kernel/cpu.h kernel/sync.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/sync.o: kernel/sync.c kernel/sync.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/memory.o: kernel/memory.c kernel/memory.h kernel/types.h kernel/sync.h kernel/linker.ld | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/cpu.o: kernel/cpu.c kernel/cpu.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/apic.o: kernel/apic.c kernel/apic.h kernel/acpi.h kernel/cpu.h kernel/pic.h kernel/timer.h kernel/vmm.h kernel/task.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/smp.o: kernel/smp.c kernel/smp.h kernel/acpi.h kernel/apic.h kernel/cpu.h kernel/gdt.h kernel/interrupts.h kernel/memory.h kernel/timer.h kernel/tlb.h kernel/vmm.h kernel/task.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/acpi.o: kernel/acpi.c kernel/acpi.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/hardware-%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

# Host core suites: <binary>:<comma,separated,sources>. One list feeds both
# the plain gate and the sanitizer gate below, so a new suite cannot be added
# to one and forgotten by the other.
HARDWARE_CORE_SUITES := \
	net-core-test:tests/net_core_test.c,kernel/net_core.c \
	input-core-test:tests/input_core_test.c,kernel/input_core.c \
	usb-core-test:tests/usb_core_test.c,kernel/usb_core.c \
	audio-core-test:tests/audio_core_test.c,kernel/audio_core.c \
	display-core-test:tests/display_core_test.c,kernel/display_core.c \
	driver-core-test:tests/driver_core_test.c,kernel/driver_core.c \
	net-route-test:tests/net_route_test.c,kernel/net_route.c \
	net-transport-test:tests/net_transport_test.c,kernel/net_transport.c \
	dhcp-core-test:tests/dhcp_core_test.c,kernel/dhcp_core.c \
	dhcp-client-test:tests/dhcp_client_test.c,kernel/dhcp_core.c \
	dma-test:tests/dma_test.c,kernel/dma.c \
	dns-core-test:tests/dns_core_test.c,kernel/dns_core.c \
	net-l2-test:tests/net_l2_test.c,kernel/net_l2.c \
	net-arp-test:tests/net_arp_test.c,kernel/net_arp.c,kernel/net_l2.c \
	net-ipv6-test:tests/net_ipv6_test.c,kernel/net_ipv6.c \
	net-conntrack-test:tests/net_conntrack_test.c,kernel/net_conntrack.c \
	resource-core-test:tests/resource_core_test.c,kernel/resource_core.c \
	scancode-core-test:tests/scancode_core_test.c,kernel/scancode_core.c \
	mouse-core-test:tests/mouse_core_test.c,kernel/mouse_core.c \
	netif-test:tests/netif_test.c,kernel/netif.c,kernel/net_l2.c \
	net-stack-test:tests/net_stack_test.c,kernel/net_stack.c,kernel/netif.c,kernel/net_l2.c,kernel/net_core.c,kernel/net_ipv6.c,kernel/net_transport.c,kernel/net_socket.c \
	net-stack-tx-test:tests/net_stack_tx_test.c,kernel/net_stack.c,kernel/netif.c,kernel/net_l2.c,kernel/net_core.c,kernel/net_ipv6.c,kernel/net_transport.c \
	net-socket-test:tests/net_socket_test.c,kernel/net_socket.c \
	crypto-core-test:tests/crypto_test.c,kernel/crypto.c

hardware-core-test: | $(BUILD)
	@set -e; for entry in $(HARDWARE_CORE_SUITES); do \
		name=$${entry%%:*}; \
		srcs=$$(echo $${entry#*:} | tr ',' ' '); \
		$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel $$srcs -o $(BUILD)/$$name; \
		$(BUILD)/$$name; \
	done

# Memory-safety / undefined-behaviour gate. Every host suite is rebuilt under
# AddressSanitizer + UndefinedBehaviorSanitizer with
# -fno-sanitize-recover=all so that a single out-of-bounds index or UB
# operation fails the run instead of printing a warning nobody reads. This
# gate is what caught the PS/2 extended-key down-state overrun in
# kernel/scancode_core.c (a 128-entry table indexed with a full-byte index).
SAN_CFLAGS := -std=c11 -g -O1 -Wall -Wextra -Werror -Ikernel \
	-fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer
SAN_ENV := ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1

sanitizer-check: | $(BUILD)
	@mkdir -p $(BUILD)/san
	@set -e; for entry in $(HARDWARE_CORE_SUITES); do \
		name=$${entry%%:*}; \
		srcs=$$(echo $${entry#*:} | tr ',' ' '); \
		$(CC) $(SAN_CFLAGS) $$srcs -o $(BUILD)/san/$$name; \
		if ! $(SAN_ENV) $(BUILD)/san/$$name >$(BUILD)/san/$$name.log 2>&1; then \
			echo "sanitizer-check: $$name reported memory/UB errors:"; \
			tail -30 $(BUILD)/san/$$name.log; exit 1; \
		fi; \
	done
	$(CC) $(SAN_CFLAGS) -Iuserspace/include -I$(DESKTOP_DIR)/include \
		-o $(BUILD)/san/desktop-tests $(DESKTOP_SRC) $(DESKTOP_TEST_SRC) kernel/crypto.c
	$(CC) $(SAN_CFLAGS) -I$(COMPAT_DIR)/include \
		-o $(BUILD)/san/compat-tests $(COMPAT_SRC) $(COMPAT_TEST_SRC)
	@set -e; for suite in desktop-tests compat-tests; do \
		if ! $(SAN_ENV) $(BUILD)/san/$$suite >$(BUILD)/san/$$suite.log 2>&1; then \
			echo "sanitizer-check: $$suite reported memory/UB errors:"; \
			tail -40 $(BUILD)/san/$$suite.log; exit 1; \
		fi; \
		tail -2 $(BUILD)/san/$$suite.log; \
	done
	@echo "sanitizer-check: PASS"

$(BUILD)/gdt.o: kernel/gdt.c kernel/gdt.h kernel/memory.h kernel/cpu.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/vmm.o: kernel/vmm.c kernel/vmm.h kernel/memory.h kernel/tlb.h kernel/cpu.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/tlb.o: kernel/tlb.c kernel/tlb.h kernel/sync.h kernel/cpu.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/task.o: kernel/task.c kernel/task.h kernel/types.h kernel/memory.h kernel/gdt.h kernel/sync.h kernel/timer.h kernel/thread.h kernel/cpu.h kernel/smp.h kernel/tlb.h kernel/apic.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/wait.o: kernel/wait.c kernel/wait.h kernel/task.h kernel/types.h kernel/sync.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -Ikernel -c $< -o $@

$(BUILD)/thread.o: kernel/thread.c kernel/thread.h kernel/task.h kernel/process.h kernel/types.h kernel/sync.h kernel/vmm.h kernel/user.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/process.o: kernel/process.c kernel/process.h kernel/thread.h kernel/vmm.h kernel/types.h kernel/sync.h kernel/ipc.h kernel/shmem.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/scheduler.o: kernel/scheduler.c kernel/scheduler.h kernel/task.h kernel/timer.h kernel/sync.h kernel/cpu.h kernel/apic.h kernel/smp.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

# Stage 3 storage stack and shared kernel infrastructure. These objects use
# compiler-generated dependency files so header changes rebuild dependants.
STORAGE_SRCS := $(wildcard kernel/storage/*.c)
STORAGE_OBJS := $(patsubst kernel/storage/%.c,$(BUILD)/storage/%.o,$(STORAGE_SRCS))
INFRA_OBJS := $(BUILD)/ksync.o $(BUILD)/crc.o $(BUILD)/kstring.o $(BUILD)/pci.o
PROBE_OBJ := $(BUILD)/storage_probe_image.o
SESSION_OBJ := $(BUILD)/session_probe_image.o
CHILD_OBJ := $(BUILD)/shell_child_image.o
EXTRA_OBJS := $(STORAGE_OBJS) $(INFRA_OBJS) $(PROBE_OBJ) $(SESSION_OBJ) \
	$(CHILD_OBJ)

$(BUILD)/storage:
	mkdir -p $(BUILD)/storage

$(BUILD)/storage/%.o: kernel/storage/%.c | $(BUILD)/storage
	$(CC) $(CFLAGS) -MMD -MP -Ikernel -c $< -o $@

$(BUILD)/ksync.o $(BUILD)/crc.o $(BUILD)/kstring.o $(BUILD)/pci.o: $(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -Ikernel -c $< -o $@

-include $(STORAGE_OBJS:.o=.d) $(INFRA_OBJS:.o=.d)

# Stage 5 Ring-3 session/shell process: desktop display/compositor/input
# modules linked freestanding and embedded into the kernel image.
SESSION_CFLAGS := -std=c11 -m64 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -nostdlib -mno-red-zone -mgeneral-regs-only -mcmodel=large \
	-Wall -Wextra -Werror -O2 -Iuserspace/include -Iuserspace/desktop/include \
	-Ikernel
SESSION_DESKTOP_SRCS := userspace/desktop/src/common.c \
	userspace/desktop/src/window.c userspace/desktop/src/compositor.c \
	userspace/desktop/src/input.c userspace/desktop/src/display.c \
	userspace/desktop/src/metrics.c \
	userspace/desktop/src/filemgr.c userspace/desktop/src/search.c \
	userspace/desktop/src/settings.c userspace/desktop/src/providers.c \
	userspace/desktop/src/ui.c userspace/desktop/src/i18n.c \
	userspace/desktop/src/governor.c userspace/desktop/src/lifecycle.c \
	userspace/desktop/src/watchdog.c \
	userspace/desktop/src/automation.c userspace/desktop/src/term.c \
	userspace/desktop/src/sandbox.c userspace/desktop/src/privacy.c \
	userspace/desktop/src/update.c \
	userspace/desktop/src/clipboard.c \
	userspace/desktop/src/downloads.c \
	userspace/desktop/src/notify.c
SESSION_DESKTOP_OBJS := $(patsubst userspace/desktop/src/%.c,$(BUILD)/session_desktop_%.o,$(SESSION_DESKTOP_SRCS))
SESSION_DEPS := userspace/session/session.c userspace/session/session_start.S \
	userspace/session/session.ld userspace/include/zeroos/syscall.h \
	$(wildcard userspace/desktop/include/zeroos/desktop/*.h) \
	$(SESSION_DESKTOP_SRCS)
$(BUILD)/session_probe.elf: $(SESSION_DEPS) $(BUILD)/session_crypto.o | $(BUILD)
	$(CC) $(SESSION_CFLAGS) -c userspace/session/session_start.S -o $(BUILD)/session_start.o
	$(CC) $(SESSION_CFLAGS) -c userspace/session/session.c -o $(BUILD)/session_probe_user.o
	@set -e; for src in $(SESSION_DESKTOP_SRCS); do \
		$(CC) $(SESSION_CFLAGS) -c $$src -o $(BUILD)/session_desktop_$$(basename $$src .c).o; \
	done
	$(LD) -m elf_x86_64 -T userspace/session/session.ld -nostdlib -o $@ \
		$(BUILD)/session_start.o $(BUILD)/session_probe_user.o \
		$(SESSION_DESKTOP_OBJS) $(BUILD)/session_crypto.o

$(BUILD)/session_crypto.o: kernel/crypto.c kernel/crypto.h | $(BUILD)
	$(CC) $(SESSION_CFLAGS) -c kernel/crypto.c -o $@

$(BUILD)/session_probe_image.o: kernel/session_probe_image.S $(BUILD)/session_probe.elf | $(BUILD)
	$(AS) $(ASFLAGS) -DPROBE_PATH='"$(BUILD)/session_probe.elf"' -c $< -o $@

# Minimal Ring-3 child the shell spawns to certify the SPAWN/WAIT lifecycle.
CHILD_DEPS := userspace/session/child.c userspace/session/child_start.S \
	userspace/session/child.ld userspace/include/zeroos/syscall.h
$(BUILD)/shell_child.elf: $(CHILD_DEPS) | $(BUILD)
	$(CC) $(SESSION_CFLAGS) -c userspace/session/child_start.S -o $(BUILD)/child_start.o
	$(CC) $(SESSION_CFLAGS) -c userspace/session/child.c -o $(BUILD)/child.o
	$(LD) -m elf_x86_64 -T userspace/session/child.ld -nostdlib -o $@ \
		$(BUILD)/child_start.o $(BUILD)/child.o
	@python3 tools/check_child_elf.py $@

$(BUILD)/shell_child_image.o: kernel/shell_child_image.S $(BUILD)/shell_child.elf | $(BUILD)
	$(AS) $(ASFLAGS) -DPROBE_PATH='"$(BUILD)/shell_child.elf"' -c $< -o $@

$(BUILD)/session_launch.o: kernel/session.c kernel/session.h kernel/types.h kernel/kstring.h kernel/memory.h kernel/vmm.h kernel/timer.h kernel/task.h kernel/process.h kernel/thread.h kernel/elf.h kernel/user.h kernel/syscall.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c kernel/session.c -o $@

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

$(KERNEL): $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/context.o $(BUILD)/ap_trampoline.o $(BUILD)/user_entry.o $(BUILD)/kernel.o $(BUILD)/cpu.o $(BUILD)/apic.o $(BUILD)/smp.o $(BUILD)/acpi.o $(BUILD)/interrupts.o $(BUILD)/syscall.o $(BUILD)/ipc.o $(BUILD)/shmem.o $(BUILD)/fb.o $(BUILD)/input.o $(BUILD)/elf.o $(BUILD)/exec.o $(BUILD)/user.o $(BUILD)/pic.o $(BUILD)/timer.o $(BUILD)/sync.o $(BUILD)/memory.o $(BUILD)/gdt.o $(BUILD)/vmm.o $(BUILD)/tlb.o $(BUILD)/task.o $(BUILD)/wait.o $(BUILD)/scheduler.o $(BUILD)/thread.o $(BUILD)/process.o $(BUILD)/session_launch.o $(EXTRA_OBJS) $(HARDWARE_CORE_OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/session_launch.o $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/context.o $(BUILD)/ap_trampoline.o $(BUILD)/user_entry.o $(BUILD)/kernel.o $(BUILD)/cpu.o $(BUILD)/apic.o $(BUILD)/smp.o $(BUILD)/acpi.o $(BUILD)/interrupts.o $(BUILD)/syscall.o $(BUILD)/ipc.o $(BUILD)/shmem.o $(BUILD)/fb.o $(BUILD)/input.o $(BUILD)/elf.o $(BUILD)/exec.o $(BUILD)/user.o $(BUILD)/pic.o $(BUILD)/timer.o $(BUILD)/sync.o $(BUILD)/memory.o $(BUILD)/gdt.o $(BUILD)/vmm.o $(BUILD)/tlb.o $(BUILD)/task.o $(BUILD)/wait.o $(BUILD)/scheduler.o $(BUILD)/thread.o $(BUILD)/process.o $(EXTRA_OBJS) $(HARDWARE_CORE_OBJS)
	@$(MAKE) --no-print-directory kernel-simd-check

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

iso: $(KERNEL)
	rm -rf $(BUILD)/iso
	mkdir -p $(BUILD)/iso/boot/grub
	cp $(KERNEL) $(BUILD)/iso/boot/zeroos.elf
	cp grub/grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(BUILD)/iso

run: iso
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -display none

clean:
	rm -rf $(BUILD)
