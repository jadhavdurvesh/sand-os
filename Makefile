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
CFLAGS := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-tree-loop-distribute-patterns -fno-pic -fno-pie -Iinclude -Isdk/sapp/include
LDFLAGS := -m elf_i386 -z noexecstack -nostdlib -z max-page-size=0x1000 -T linker/kernel.ld
ABI_H := sdk/sapp/include/sand/abi.h
PYTHON ?= python3
SAPP := sdk/sapp
APPS := sandsh hello abitest spin crash locus locusc quanta chronos lumen cairn
APP_BUILD := $(BUILD)/apps
APP_CFLAGS := -m32 -std=gnu11 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns -O2 -Wall -Wextra -Werror -I$(SAPP)/include
APP_OBJECTS := $(foreach a,$(APPS),$(APP_BUILD)/$(a)-image.o)
KERNEL_OBJECTS := $(BUILD)/kernel-entry.o $(BUILD)/kernel.o $(BUILD)/idt.o $(BUILD)/exceptions.o $(BUILD)/irq.o $(BUILD)/thread-bootstrap.o $(BUILD)/exception.o $(BUILD)/pic.o $(BUILD)/memory_map.o $(BUILD)/frame.o $(BUILD)/paging.o $(BUILD)/heap.o $(BUILD)/timer.o $(BUILD)/scheduler.o $(BUILD)/sync.o $(BUILD)/gdt.o $(BUILD)/gdt-asm.o $(BUILD)/syscall.o $(BUILD)/syscall-asm.o $(BUILD)/user-asm.o $(BUILD)/user-space.o $(BUILD)/process.o $(BUILD)/block.o $(BUILD)/ata-pio.o $(BUILD)/sandfs.o $(BUILD)/graphics-framebuffer.o $(BUILD)/vbe.o $(BUILD)/desktop.o $(BUILD)/ui.o $(BUILD)/ui-wall.o $(BUILD)/gfx.o $(BUILD)/font.o $(BUILD)/font-data.o $(BUILD)/rtc.o $(BUILD)/string.o $(BUILD)/ps2.o $(BUILD)/console.o $(BUILD)/device.o $(BUILD)/serial.o $(BUILD)/drivers.o $(BUILD)/apps.o $(BUILD)/userland-test.o $(BUILD)/sha512.o $(BUILD)/ed25519.o $(BUILD)/stci.o $(BUILD)/stci-kat.o $(APP_OBJECTS)
.PHONY: all clean kernel disk run check apps test ui-preview
apps: $(foreach a,$(APPS),$(APP_BUILD)/$(a).sapp)
$(APP_BUILD):
	mkdir -p $(APP_BUILD)
$(APP_BUILD)/libsand.o: $(SAPP)/lib/libsand.c $(SAPP)/include/sand/libsand.h $(SAPP)/include/sand/app.h $(SAPP)/include/sand/abi.h | $(APP_BUILD)
	$(CC) $(APP_CFLAGS) -c $< -o $@
$(APP_BUILD)/%.o: apps/%/main.c apps/sandsh/shell_core.h $(SAPP)/include/sand/libsand.h $(SAPP)/include/sand/app.h $(SAPP)/include/sand/abi.h | $(APP_BUILD)
	$(CC) $(APP_CFLAGS) -c $< -o $@
$(APP_BUILD)/%.elf: $(APP_BUILD)/%.o $(APP_BUILD)/libsand.o $(SAPP)/linker/sapp-i386.ld
	$(LD) -m elf_i386 -z max-page-size=0x1000 -T $(SAPP)/linker/sapp-i386.ld -nostdlib $< $(APP_BUILD)/libsand.o -o $@
$(APP_BUILD)/%.sapp: $(APP_BUILD)/%.elf apps/%/manifest.json $(SAPP)/tools/sapp.py
	$(PYTHON) $(SAPP)/tools/sapp.py pack --input $< --manifest apps/$*/manifest.json --output $@
	$(PYTHON) $(SAPP)/tools/sapp.py verify $@ >/dev/null
$(APP_BUILD)/%-image.o: $(APP_BUILD)/%.sapp
	cd $(APP_BUILD) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 $*.sapp $*-image.o
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
$(BUILD)/syscall.o: kernel/arch/x86/syscall.c include/sand/arch/x86/syscall.h include/sand/arch/x86/idt.h $(ABI_H) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/syscall-asm.o: kernel/arch/x86/syscall.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/user-asm.o: kernel/arch/x86/user.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/user-space.o: kernel/user/user_space.c include/sand/kernel/user.h include/sand/arch/x86/gdt.h include/sand/arch/x86/paging.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/process.o: kernel/proc/process.c include/sand/kernel/process.h include/sand/arch/x86/paging.h include/sand/arch/x86/gdt.h $(ABI_H) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/block.o: kernel/storage/block.c include/sand/storage/block.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ata-pio.o: kernel/arch/x86/ata_pio.c include/sand/arch/x86/ata_pio.h include/sand/storage/block.h include/sand/drivers/device.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sandfs.o: kernel/fs/sandfs.c include/sand/fs/sandfs.h include/sand/storage/block.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/graphics-framebuffer.o: kernel/graphics/framebuffer.c include/sand/graphics/framebuffer.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/vbe.o: kernel/arch/x86/vbe.c include/sand/arch/x86/vbe.h include/sand/graphics/framebuffer.h include/sand/arch/x86/paging.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/desktop.o: kernel/graphics/desktop.c include/sand/graphics/desktop.h include/sand/graphics/ui.h include/sand/graphics/framebuffer.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
GFX_HDRS := include/sand/graphics/gfx.h include/sand/graphics/font.h
$(BUILD)/ui.o: kernel/graphics/ui.c include/sand/graphics/ui.h $(GFX_HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ui-wall.o: kernel/graphics/ui_wall.c include/sand/graphics/ui.h $(GFX_HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gfx.o: kernel/graphics/gfx.c $(GFX_HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/font.o: kernel/graphics/font.c $(GFX_HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/font-data.o: kernel/graphics/font_data.c $(GFX_HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/rtc.o: kernel/arch/x86/rtc.c include/sand/arch/x86/rtc.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/string.o: kernel/lib/string.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ps2.o: kernel/arch/x86/ps2.c include/sand/arch/x86/input.h include/sand/arch/x86/irq.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/console.o: kernel/console/console.c include/sand/console/console.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/device.o: kernel/drivers/device.c include/sand/drivers/device.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/serial.o: kernel/drivers/serial.c include/sand/drivers/serial.h include/sand/drivers/device.h include/sand/arch/x86/irq.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/drivers.o: kernel/drivers/drivers.c include/sand/drivers/drivers.h include/sand/drivers/device.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/apps.o: kernel/proc/apps.c include/sand/proc/apps.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sha512.o: kernel/crypto/sha512.c kernel/crypto/crypto_consts.h include/sand/crypto/sha512.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel/crypto -c $< -o $@
$(BUILD)/ed25519.o: kernel/crypto/ed25519.c kernel/crypto/crypto_consts.h include/sand/crypto/ed25519.h include/sand/crypto/sha512.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel/crypto -c $< -o $@
$(BUILD)/stci.o: kernel/security/stci.c include/sand/security/stci.h include/sand/crypto/ed25519.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/stci-kat.o: kernel/selftest/stci_kat.c include/sand/security/stci.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/userland-test.o: kernel/selftest/userland.c $(ABI_H) include/sand/kernel/process.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(KERNEL_ELF): $(KERNEL_OBJECTS) linker/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJECTS)
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	test $$(stat -c%s $@) -le 393216
kernel: $(KERNEL_BIN)
disk: $(BOOT_SECTOR) $(STAGE2) $(KERNEL_BIN)
	dd if=/dev/zero of=$(DISK) bs=512 count=4096 status=none
	dd if=$(BOOT_SECTOR) of=$(DISK) conv=notrunc status=none
	dd if=$(STAGE2) of=$(DISK) bs=512 seek=1 conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$(DISK) bs=512 seek=17 conv=notrunc status=none
	test $$(stat -c%s $(DISK)) -eq 2097152
run: disk
	qemu-system-i386 -machine pc -drive format=raw,file=$(DISK) -m 128M
check: disk
	@echo "SAND boot, memory, paging, heap, PIT, preemptive scheduler and synchronization built."
host-test:
	mkdir -p $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -Iinclude -Ikernel/crypto -o $(BUILD)/ed25519-kat tests/veyra/ed25519_kat_test.c kernel/crypto/sha512.c kernel/crypto/ed25519.c kernel/security/stci.c
	./$(BUILD)/ed25519-kat
	$(PYTHON) tests/veyra/ed25519_crosscheck.py
test: disk host-test
	$(PYTHON) tests/qemu/test_shell.py
	$(PYTHON) tests/qemu/test_desktop.py
ui-preview:
	$(CC) -O2 -Wall -Iinclude -Isdk/sapp/include -o $(BUILD)/ui_preview tools/ui_preview.c kernel/graphics/ui.c kernel/graphics/ui_wall.c kernel/graphics/gfx.c kernel/graphics/font.c kernel/graphics/font_data.c
	./$(BUILD)/ui_preview $(BUILD)/ui.ppm
clean:
	rm -rf $(BUILD)
