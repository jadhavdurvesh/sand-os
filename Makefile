BUILD := build
ISO_ROOT := $(BUILD)/iso
KERNEL := $(BUILD)/sand-kernel.elf
ISO := $(BUILD)/sand-os.iso

CC ?= gcc
AS ?= as
LD ?= ld
CFLAGS := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-pic
LDFLAGS := -m elf_i386 -nostdlib -z max-page-size=0x1000 -T linker/kernel.ld

.PHONY: all clean kernel iso run check
all: iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: boot/entry.S | $(BUILD)
	$(AS) --32 $< -o $@

$(BUILD)/kernel.o: kernel/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(BUILD)/boot.o $(BUILD)/kernel.o linker/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/boot.o $(BUILD)/kernel.o

kernel: $(KERNEL)

iso: $(KERNEL)
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $(KERNEL) $(ISO_ROOT)/boot/sand-kernel.elf
	cp boot/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_ROOT)

run: iso
	qemu-system-i386 -cdrom $(ISO) -m 256M

check: kernel
	grub-file --is-x86-multiboot2 $(KERNEL)

clean:
	rm -rf $(BUILD)
