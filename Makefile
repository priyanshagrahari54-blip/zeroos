BUILD := build
KERNEL := $(BUILD)/zeroos.elf
ISO := $(BUILD)/zeroos.iso

CC := gcc
LD := ld
AS := gcc

CFLAGS := -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdinc -Wall -Wextra -Werror -O2
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

$(C_OBJECTS): $(HEADERS)

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
