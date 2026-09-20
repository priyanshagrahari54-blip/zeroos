BUILD := build
KERNEL := $(BUILD)/zeroos.elf
ISO := $(BUILD)/zeroos.iso

CC := gcc
LD := ld
AS := gcc

CFLAGS := -m64 -mno-red-zone -mgeneral-regs-only -mcmodel=small -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdinc -Wall -Wextra -Werror -O2
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -nostdlib
LDFLAGS := -m elf_x86_64 -T kernel/linker.ld -nostdlib

C_OBJECTS := \
	$(BUILD)/kernel.o \
	$(BUILD)/interrupts.o \
	$(BUILD)/pic.o \
	$(BUILD)/timer.o \
	$(BUILD)/sync.o \
	$(BUILD)/memory.o \
	$(BUILD)/heap.o \
	$(BUILD)/vmm.o \
	$(BUILD)/gdt.o \
	$(BUILD)/task.o \
	$(BUILD)/process.o \
	$(BUILD)/syscall.o \
	$(BUILD)/elf.o \
	$(BUILD)/wait.o \
	$(BUILD)/scheduler.o

A_OBJECTS := \
	$(BUILD)/boot.o \
	$(BUILD)/isr.o \
	$(BUILD)/early_isr.o \
	$(BUILD)/context.o \
	$(BUILD)/syscall_entry.o \
	$(BUILD)/user_init.o

OBJECTS := $(C_OBJECTS) $(A_OBJECTS)
HEADERS := $(wildcard kernel/*.h)

vpath %.c kernel
vpath %.S boot kernel user

.PHONY: all clean iso run

all: iso

$(BUILD):
	mkdir -p $(BUILD)

$(C_OBJECTS): $(HEADERS) Makefile

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -Ikernel -c $< -o $@

$(A_OBJECTS): $(HEADERS)

$(BUILD)/%.o: %.S | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

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

.PHONY: host-test
host-test: | $(BUILD)
	$(CC) -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DZEROOS_HOST_TEST -Ikernel -Wl,--gc-sections tests/user_range.c kernel/vmm.c -o $(BUILD)/test-user-range
	timeout 5 $(BUILD)/test-user-range
	$(CC) -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DZEROOS_HOST_TEST -Ikernel -Wl,--gc-sections tests/syscall_dispatch.c kernel/syscall.c kernel/vmm.c -o $(BUILD)/test-syscall
	timeout 5 $(BUILD)/test-syscall
	$(CC) -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DZEROOS_HOST_TEST -Ikernel -Wl,--gc-sections tests/page_ownership.c kernel/vmm.c kernel/memory.c -o $(BUILD)/test-page-ownership
	timeout 5 $(BUILD)/test-page-ownership
	$(CC) -O2 -Wall -Wextra -Werror -DZEROOS_HOST_TEST -DZEROOS_TEST_FAULTS -Ikernel tests/pmm.c kernel/memory.c -o $(BUILD)/test-pmm
	timeout 5 $(BUILD)/test-pmm
	$(CC) -O2 -Wall -Wextra -Werror -DZEROOS_TEST_FAULTS -Ikernel tests/heap.c kernel/heap.c -o $(BUILD)/test-heap
	timeout 5 $(BUILD)/test-heap
	$(CC) -O2 -Wall -Wextra -Werror -DZEROOS_HOST_TEST -Ikernel tests/elf.c kernel/elf.c -o $(BUILD)/test-elf
	timeout 5 $(BUILD)/test-elf
