BUILD := build
BOOT_SECTOR := $(BUILD)/sand-boot.bin
STAGE2 := $(BUILD)/sand-stage2.bin
KERNEL_ELF := $(BUILD)/sand-kernel.elf
KERNEL_BIN := $(BUILD)/sand-kernel.bin
DISK := $(BUILD)/sand-os.img
CC ?= gcc
AS ?= as
LD ?= ld
OBJCOPY ?= objcopy
CFLAGS := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-pic -fno-pie -Iinclude
LDFLAGS := -m elf_i386 -nostdlib -z max-page-size=0x1000 -T linker/kernel.ld
KERNEL_OBJECTS := $(BUILD)/kernel-entry.o $(BUILD)/kernel.o $(BUILD)/idt.o $(BUILD)/exceptions.o $(BUILD)/irq.o $(BUILD)/thread-bootstrap.o $(BUILD)/exception.o $(BUILD)/pic.o $(BUILD)/memory_map.o $(BUILD)/frame.o $(BUILD)/paging.o $(BUILD)/heap.o $(BUILD)/timer.o $(BUILD)/scheduler.o $(BUILD)/sync.o $(BUILD)/gdt.o $(BUILD)/gdt-asm.o $(BUILD)/syscall.o $(BUILD)/syscall-asm.o $(BUILD)/user-asm.o $(BUILD)/user-space.o
.PHONY: all clean kernel disk run check
all: disk
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/boot-sector.o: boot/boot.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BOOT_SECTOR): $(BUILD)/boot-sector.o
	$(LD) -m elf_i386 -Ttext 0x7C00 --oformat binary -o $@ $<
	test "$$(od -An -tx1 -j510 -N2 $@ | tr -d ' \n')" = "55aa"
	test $$(stat -c%s $@) -eq 512
$(BUILD)/stage2.o: boot/stage2.S | $(BUILD)
	$(AS) --32 $< -o $@
$(STAGE2): $(BUILD)/stage2.o
	$(LD) -m elf_i386 -Ttext 0x8000 --oformat binary -o $@ $<
	test $$(stat -c%s $@) -le 8192
	truncate -s 8192 $@
$(BUILD)/kernel-entry.o: boot/entry.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/kernel.o: kernel/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/idt.o: kernel/arch/x86/idt.c include/sand/arch/x86/idt.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/exceptions.o: kernel/arch/x86/exceptions.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/irq.o: kernel/arch/x86/irq.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/thread-bootstrap.o: kernel/arch/x86/thread_bootstrap.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/exception.o: kernel/arch/x86/exception.c include/sand/arch/x86/idt.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/pic.o: kernel/arch/x86/pic.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/memory_map.o: kernel/mm/memory_map.c include/sand/mm/memory_map.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/frame.o: kernel/mm/frame.c include/sand/mm/frame.h include/sand/mm/memory_map.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/paging.o: kernel/arch/x86/paging.c include/sand/arch/x86/paging.h include/sand/mm/frame.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/heap.o: kernel/mm/heap.c include/sand/mm/heap.h include/sand/arch/x86/paging.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/timer.o: kernel/arch/x86/timer.c include/sand/arch/x86/timer.h include/sand/arch/x86/idt.h include/sand/kernel/scheduler.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/scheduler.o: kernel/sched/scheduler.c include/sand/kernel/scheduler.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sync.o: kernel/sched/sync.c include/sand/kernel/sync.h include/sand/kernel/scheduler.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gdt.o: kernel/arch/x86/gdt.c include/sand/arch/x86/gdt.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gdt-asm.o: kernel/arch/x86/gdt.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/syscall.o: kernel/arch/x86/syscall.c include/sand/arch/x86/syscall.h include/sand/arch/x86/idt.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/syscall-asm.o: kernel/arch/x86/syscall.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/user-asm.o: kernel/arch/x86/user.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/user-space.o: kernel/user/user_space.c include/sand/kernel/user.h include/sand/arch/x86/gdt.h include/sand/arch/x86/paging.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(KERNEL_ELF): $(KERNEL_OBJECTS) linker/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJECTS)
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	test $$(stat -c%s $@) -le 32768
kernel: $(KERNEL_BIN)
disk: $(BOOT_SECTOR) $(STAGE2) $(KERNEL_BIN)
	dd if=/dev/zero of=$(DISK) bs=512 count=81 status=none
	dd if=$(BOOT_SECTOR) of=$(DISK) conv=notrunc status=none
	dd if=$(STAGE2) of=$(DISK) bs=512 seek=1 conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$(DISK) bs=512 seek=17 conv=notrunc status=none
	test $$(stat -c%s $(DISK)) -eq 41472
run: disk
	qemu-system-i386 -machine pc -drive format=raw,file=$(DISK) -m 128M
check: disk
	@echo "SAND boot, memory, paging, heap, PIT, preemptive scheduler and synchronization built."
clean:
	rm -rf $(BUILD)
