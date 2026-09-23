BUILD := build
KERNEL := $(BUILD)/zeroos.elf
ISO := $(BUILD)/zeroos.iso

CC := gcc
LD := ld
AS := gcc

CFLAGS := -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdinc -Wall -Wextra -O2
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -nostdlib
LDFLAGS := -m elf_x86_64 -T kernel/linker.ld -nostdlib

.PHONY: all clean elf iso run

all: iso

elf: $(KERNEL)

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

$(BUILD)/kernel.o: kernel/kernel.c kernel/types.h kernel/cpu.h kernel/apic.h kernel/acpi.h kernel/memory.h kernel/timer.h kernel/vmm.h kernel/gdt.h kernel/sync.h kernel/tlb.h kernel/task.h kernel/thread.h kernel/process.h kernel/scheduler.h kernel/smp.h kernel/wait.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/interrupts.o: kernel/interrupts.c kernel/interrupts.h kernel/types.h kernel/cpu.h kernel/apic.h kernel/pic.h kernel/timer.h kernel/gdt.h kernel/task.h kernel/thread.h kernel/tlb.h | $(BUILD)
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

$(BUILD)/apic.o: kernel/apic.c kernel/apic.h kernel/acpi.h kernel/cpu.h kernel/pic.h kernel/vmm.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/smp.o: kernel/smp.c kernel/smp.h kernel/acpi.h kernel/apic.h kernel/cpu.h kernel/gdt.h kernel/interrupts.h kernel/memory.h kernel/tlb.h kernel/vmm.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/acpi.o: kernel/acpi.c kernel/acpi.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/gdt.o: kernel/gdt.c kernel/gdt.h kernel/memory.h kernel/cpu.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/vmm.o: kernel/vmm.c kernel/vmm.h kernel/memory.h kernel/tlb.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/tlb.o: kernel/tlb.c kernel/tlb.h kernel/sync.h kernel/cpu.h kernel/types.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/task.o: kernel/task.c kernel/task.h kernel/types.h kernel/memory.h kernel/gdt.h kernel/sync.h kernel/timer.h kernel/thread.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/wait.o: kernel/wait.c kernel/wait.h kernel/task.h kernel/types.h kernel/sync.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -Ikernel -c $< -o $@

$(BUILD)/thread.o: kernel/thread.c kernel/thread.h kernel/task.h kernel/process.h kernel/types.h kernel/sync.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/process.o: kernel/process.c kernel/process.h kernel/thread.h kernel/vmm.h kernel/types.h kernel/sync.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(BUILD)/scheduler.o: kernel/scheduler.c kernel/scheduler.h kernel/task.h kernel/timer.h kernel/sync.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel -c $< -o $@

$(KERNEL): $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/context.o $(BUILD)/ap_trampoline.o $(BUILD)/kernel.o $(BUILD)/cpu.o $(BUILD)/apic.o $(BUILD)/smp.o $(BUILD)/acpi.o $(BUILD)/interrupts.o $(BUILD)/pic.o $(BUILD)/timer.o $(BUILD)/sync.o $(BUILD)/memory.o $(BUILD)/gdt.o $(BUILD)/vmm.o $(BUILD)/tlb.o $(BUILD)/task.o $(BUILD)/wait.o $(BUILD)/scheduler.o $(BUILD)/thread.o $(BUILD)/process.o kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/context.o $(BUILD)/ap_trampoline.o $(BUILD)/kernel.o $(BUILD)/cpu.o $(BUILD)/apic.o $(BUILD)/smp.o $(BUILD)/acpi.o $(BUILD)/interrupts.o $(BUILD)/pic.o $(BUILD)/timer.o $(BUILD)/sync.o $(BUILD)/memory.o $(BUILD)/gdt.o $(BUILD)/vmm.o $(BUILD)/tlb.o $(BUILD)/task.o $(BUILD)/wait.o $(BUILD)/scheduler.o $(BUILD)/thread.o $(BUILD)/process.o

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
