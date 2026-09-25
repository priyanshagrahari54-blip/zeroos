BUILD := build
KERNEL := $(BUILD)/zeroos.elf
ISO := $(BUILD)/zeroos.iso
HARDWARE_CORE_NAMES := net_core net_route net_transport net_conntrack net_l2 net_ipv6 netif net_stack net_socket dns_core dhcp_core input_core usb_core audio_core display_core driver_core dma resource_core scancode_core mouse_core crypto
HARDWARE_CORE_OBJS := $(addprefix $(BUILD)/hardware-,$(addsuffix .o,$(HARDWARE_CORE_NAMES)))

CC := gcc
LD := ld
AS := gcc

CFLAGS := -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdinc -Wall -Wextra -O2
CFLAGS += $(EXTRA_CFLAGS)
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -nostdlib
LDFLAGS := -m elf_x86_64 -T kernel/linker.ld -nostdlib

.PHONY: all clean elf iso run userspace-abi-check userspace-runtime-check userspace-abi-consistency hardware-core-test desktop-check compat-check

all: iso

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
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_stack_test.c kernel/net_stack.c kernel/netif.c kernel/net_l2.c kernel/net_core.c kernel/net_ipv6.c kernel/net_transport.c kernel/net_socket.c -o $(BUILD)/net-stack-test
	$(BUILD)/net-stack-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/net_socket_test.c kernel/net_socket.c -o $(BUILD)/net-socket-test
	$(BUILD)/net-socket-test
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel tests/crypto_test.c kernel/crypto.c -o $(BUILD)/crypto-core-test
	$(BUILD)/crypto-core-test

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
EXTRA_OBJS := $(STORAGE_OBJS) $(INFRA_OBJS) $(PROBE_OBJ) $(SESSION_OBJ)

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
	@set -e; for src in $(SESSION_DESKTOP_SRCS); do \
		$(CC) $(SESSION_CFLAGS) -c $$src -o $(BUILD)/session_desktop_$$(basename $$src .c).o; \
	done
	$(LD) -m elf_x86_64 -T userspace/session/session.ld -nostdlib -o $@ \
		$(BUILD)/session_start.o $(BUILD)/session_probe_user.o $(SESSION_DESKTOP_OBJS)

$(BUILD)/session_probe_image.o: kernel/session_probe_image.S $(BUILD)/session_probe.elf | $(BUILD)
	$(AS) $(ASFLAGS) -DPROBE_PATH='"$(BUILD)/session_probe.elf"' -c $< -o $@

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
