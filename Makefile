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
CFLAGS := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-pic -fno-pie
LDFLAGS := -m elf_i386 -nostdlib -z max-page-size=0x1000 -T linker/kernel.ld

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

$(KERNEL_ELF): $(BUILD)/kernel-entry.o $(BUILD)/kernel.o linker/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/kernel-entry.o $(BUILD)/kernel.o

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
	@echo "SAND boot sector signature, stage-2 bounds, kernel size and disk layout validated."

clean:
	rm -rf $(BUILD)
