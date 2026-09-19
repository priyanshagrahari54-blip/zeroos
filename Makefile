BUILD := build
KERNEL := $(BUILD)/zeroos.elf
ISO := $(BUILD)/zeroos.iso

CC := gcc
LD := ld
AS := gcc

CFLAGS := -m64 -mno-red-zone -mcmodel=kernel -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdinc -Wall -Wextra -O2
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -nostdlib
LDFLAGS := -m elf_x86_64 -T kernel/linker.ld -nostdlib

.PHONY: all clean iso run

all: iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: boot/boot.S | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/isr.o: boot/isr.S | $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel.o: kernel/kernel.c kernel/interrupts.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/interrupts.o: kernel/interrupts.c kernel/interrupts.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(KERNEL): $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/kernel.o $(BUILD)/interrupts.o kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/kernel.o $(BUILD)/interrupts.o

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
