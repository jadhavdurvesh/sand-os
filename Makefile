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
# Built into the kernel image (launcher + boot-time needs) ...
EMBEDDED_APPS := sandsh hello abitest spin crash locus locusc quanta chronos cairn keystone citadel herald netecho netget
# ... and apps stored only in SANDFS inside the disk image (loaded by diskapps.c, permissions from their manifest).
DISK_APPS := jail lumen wardprobe runetest
APPS := $(EMBEDDED_APPS) $(DISK_APPS)
MIMIR_LBA := 2112
MIMIR_SECTORS := 8192
APP_BUILD := $(BUILD)/apps
APP_CFLAGS := -m32 -std=gnu11 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns -O2 -Wall -Wextra -Werror -I$(SAPP)/include
APP_OBJECTS := $(foreach a,$(EMBEDDED_APPS),$(APP_BUILD)/$(a)-image.o)
KERNEL_OBJECTS := $(BUILD)/kernel-entry.o $(BUILD)/kernel.o $(BUILD)/idt.o $(BUILD)/exceptions.o $(BUILD)/irq.o $(BUILD)/thread-bootstrap.o $(BUILD)/exception.o $(BUILD)/pic.o $(BUILD)/memory_map.o $(BUILD)/frame.o $(BUILD)/paging.o $(BUILD)/heap.o $(BUILD)/timer.o $(BUILD)/scheduler.o $(BUILD)/sync.o $(BUILD)/gdt.o $(BUILD)/gdt-asm.o $(BUILD)/syscall.o $(BUILD)/syscall-asm.o $(BUILD)/user-asm.o $(BUILD)/user-space.o $(BUILD)/process.o $(BUILD)/diskapps.o $(BUILD)/appinstall.o $(BUILD)/block.o $(BUILD)/ata-pio.o $(BUILD)/mimir.o $(BUILD)/sigil-key.o $(BUILD)/sigil-pub.o $(BUILD)/tideway.o $(BUILD)/driftnet.o $(BUILD)/veyra-registry.o $(BUILD)/graphics-framebuffer.o $(BUILD)/vbe.o $(BUILD)/desktop.o $(BUILD)/ui.o $(BUILD)/ui-wall.o $(BUILD)/gfx.o $(BUILD)/font.o $(BUILD)/font-data.o $(BUILD)/rtc.o $(BUILD)/string.o $(BUILD)/ps2.o $(BUILD)/console.o $(BUILD)/device.o $(BUILD)/serial.o $(BUILD)/drivers.o $(APP_BUILD)/apps_table.o $(BUILD)/userland-test.o $(BUILD)/sha512.o $(BUILD)/ed25519.o $(BUILD)/stci.o $(BUILD)/stci-kat.o $(BUILD)/sha256.o $(BUILD)/launch-gate.o $(BUILD)/oath.o $(BUILD)/stci-gate.o $(BUILD)/stci-fixture-signed.o $(BUILD)/stci-fixture-bin.o $(APP_OBJECTS)
.PHONY: run-serial all clean kernel disk run check apps test ui-preview
apps: $(foreach a,$(APPS),$(APP_BUILD)/$(a).sapp)
$(APP_BUILD):
	mkdir -p $(APP_BUILD)
$(APP_BUILD)/libsand.o: $(SAPP)/lib/libsand.c $(SAPP)/include/sand/libsand.h $(SAPP)/include/sand/app.h $(SAPP)/include/sand/abi.h | $(APP_BUILD)
	$(CC) $(APP_CFLAGS) -c $< -o $@
$(APP_BUILD)/%.o: apps/%/main.c apps/sandsh/shell_core.h $(SAPP)/include/sand/libsand.h $(SAPP)/include/sand/app.h $(SAPP)/include/sand/abi.h | $(APP_BUILD)
	$(CC) $(APP_CFLAGS) -c $< -o $@
$(APP_BUILD)/%.elf: $(APP_BUILD)/%.o $(APP_BUILD)/libsand.o $(SAPP)/linker/sapp-i386.ld
	$(LD) -m elf_i386 -z max-page-size=0x1000 -T $(SAPP)/linker/sapp-i386.ld -nostdlib $< $(APP_BUILD)/libsand.o -o $@
# Citadel: the app plus the vendored Citadel core/adapter (third_party/citadel) and a tiny C-library shim.
CITADEL_FLAGS := $(APP_CFLAGS) -Iapps/citadel/shim -Ithird_party/citadel/include
$(APP_BUILD)/citadel.o: apps/citadel/main.c $(SAPP)/include/sand/libsand.h $(SAPP)/include/sand/app.h $(SAPP)/include/sand/abi.h third_party/citadel/include/citadel/citadel.h third_party/citadel/include/citadel/sandos.h | $(APP_BUILD)
	$(CC) $(CITADEL_FLAGS) -c $< -o $@
$(APP_BUILD)/citadel-shim.o: apps/citadel/shim.c apps/citadel/shim/string.h | $(APP_BUILD)
	$(CC) $(CITADEL_FLAGS) -c $< -o $@
$(APP_BUILD)/citadel-parser.o: third_party/citadel/src/core/parser.c | $(APP_BUILD)
	$(CC) $(CITADEL_FLAGS) -c $< -o $@
$(APP_BUILD)/citadel-registry.o: third_party/citadel/src/core/registry.c | $(APP_BUILD)
	$(CC) $(CITADEL_FLAGS) -c $< -o $@
$(APP_BUILD)/citadel-dispatcher.o: third_party/citadel/src/core/dispatcher.c | $(APP_BUILD)
	$(CC) $(CITADEL_FLAGS) -c $< -o $@
$(APP_BUILD)/citadel-sandos.o: third_party/citadel/src/adapters/sandos.c | $(APP_BUILD)
	$(CC) $(CITADEL_FLAGS) -c $< -o $@
CITADEL_LINK := $(APP_BUILD)/citadel.o $(APP_BUILD)/citadel-shim.o $(APP_BUILD)/citadel-parser.o $(APP_BUILD)/citadel-registry.o $(APP_BUILD)/citadel-dispatcher.o $(APP_BUILD)/citadel-sandos.o
$(APP_BUILD)/citadel.elf: $(CITADEL_LINK) $(APP_BUILD)/libsand.o $(SAPP)/linker/sapp-i386.ld
	$(LD) -m elf_i386 -z max-page-size=0x1000 -T $(SAPP)/linker/sapp-i386.ld -nostdlib $(CITADEL_LINK) $(APP_BUILD)/libsand.o -o $@
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
$(BUILD)/process.o: kernel/proc/process.c include/sand/kernel/process.h include/sand/kernel/perms.h include/sand/arch/x86/paging.h include/sand/arch/x86/gdt.h $(ABI_H) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/block.o: kernel/storage/block.c include/sand/storage/block.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ata-pio.o: kernel/arch/x86/ata_pio.c include/sand/arch/x86/ata_pio.h include/sand/storage/block.h include/sand/drivers/device.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/appinstall.o: kernel/proc/appinstall.c include/sand/proc/appinstall.h include/sand/proc/apps.h include/sand/fs/mimir.h include/sand/security/launch_gate.h $(ABI_H) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/diskapps.o: kernel/proc/diskapps.c include/sand/proc/diskapps.h include/sand/kernel/perms.h include/sand/fs/mimir.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/mimir.o: kernel/fs/mimir.c include/sand/fs/mimir.h include/sand/storage/block.h include/sand/crypto/sha256.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tideway.o: kernel/ipc/tideway.c include/sand/ipc/tideway.h $(ABI_H) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/driftnet.o: kernel/net/driftnet.c include/sand/net/driftnet.h include/sand/veyra/status.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/veyra-registry.o: kernel/veyra/registry.c include/sand/veyra/subsystems.h include/sand/net/driftnet.h include/sand/ipc/tideway.h $(ABI_H) | $(BUILD)
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
# Built-in app table + launcher catalogue are generated from apps/<name>/manifest.json.
$(APP_BUILD)/apps_table.c: tools/gen_app_table.py $(foreach a,$(EMBEDDED_APPS),apps/$(a)/manifest.json) Makefile | $(APP_BUILD)
	$(PYTHON) tools/gen_app_table.py $@ apps $(EMBEDDED_APPS)
$(APP_BUILD)/apps_table.o: $(APP_BUILD)/apps_table.c include/sand/proc/apps.h
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sha512.o: kernel/crypto/sha512.c kernel/crypto/crypto_consts.h include/sand/crypto/sha512.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel/crypto -c $< -o $@
$(BUILD)/ed25519.o: kernel/crypto/ed25519.c kernel/crypto/crypto_consts.h include/sand/crypto/ed25519.h include/sand/crypto/sha512.h | $(BUILD)
	$(CC) $(CFLAGS) -Ikernel/crypto -c $< -o $@
$(BUILD)/stci.o: kernel/security/stci.c include/sand/security/stci.h include/sand/crypto/ed25519.h include/sand/crypto/sha256.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/stci-kat.o: kernel/selftest/stci_kat.c include/sand/security/stci.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sha256.o: kernel/crypto/sha256.c include/sand/crypto/sha256.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/launch-gate.o: kernel/security/launch_gate.c include/sand/security/launch_gate.h include/sand/security/stci.h include/sand/arch/x86/rtc.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/oath.o: kernel/security/oath.c include/sand/security/oath.h include/sand/security/launch_gate.h include/sand/fs/mimir.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/stci-gate.o: kernel/selftest/stci_gate.c include/sand/security/launch_gate.h include/sand/security/stci.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
# Throw-away test root + publisher, generated per build (never committed, never in the default trust store).
STCI_FIX := $(BUILD)/stci
$(STCI_FIX)/signed.sapp $(STCI_FIX)/stci-fixture.bin: $(APP_BUILD)/hello.sapp tools/stci_fixtures.py
	$(PYTHON) tools/stci_fixtures.py kernel $(STCI_FIX) $(APP_BUILD)/hello.sapp
$(BUILD)/stci-fixture-signed.o: $(STCI_FIX)/signed.sapp
	cd $(STCI_FIX) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 signed.sapp ../stci-fixture-signed.o
$(BUILD)/stci-fixture-bin.o: $(STCI_FIX)/stci-fixture.bin
	cd $(STCI_FIX) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 stci-fixture.bin ../stci-fixture-bin.o
$(BUILD)/userland-test.o: kernel/selftest/userland.c $(ABI_H) include/sand/kernel/process.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(KERNEL_ELF): $(KERNEL_OBJECTS) linker/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJECTS)
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	test $$(stat -c%s $@) -le 1048576
kernel: $(KERNEL_BIN)
# SIGIL: throw-away developer key pair for the factory manifest; the public half is linked into the kernel, the private half signs the image.
SIGIL := $(BUILD)/sigil
$(SIGIL)/sigil.key: | $(BUILD)
	mkdir -p $(SIGIL) && openssl genpkey -algorithm ED25519 -out $@
$(SIGIL)/sigil-pub.bin: $(SIGIL)/sigil.key
	openssl pkey -in $< -pubout -outform DER | tail -c 32 > $@
$(BUILD)/sigil-pub.o: $(SIGIL)/sigil-pub.bin
	cd $(SIGIL) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 sigil-pub.bin ../sigil-pub.o
$(BUILD)/sigil-key.o: kernel/fs/sigil_key.c include/sand/fs/mimir.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
disk: $(BOOT_SECTOR) $(STAGE2) $(KERNEL_BIN) $(foreach a,$(DISK_APPS),$(APP_BUILD)/$(a).sapp) tools/mkmimir.py $(SIGIL)/sigil.key
	dd if=/dev/zero of=$(DISK) bs=512 count=10304 status=none
	dd if=$(BOOT_SECTOR) of=$(DISK) conv=notrunc status=none
	dd if=$(STAGE2) of=$(DISK) bs=512 seek=1 conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$(DISK) bs=512 seek=17 conv=notrunc status=none
	$(PYTHON) tools/mkmimir.py --sigil-key $(SIGIL)/sigil.key $(DISK) $(MIMIR_LBA) $(MIMIR_SECTORS) $(foreach a,$(DISK_APPS),$(a).sapp=$(APP_BUILD)/$(a).sapp)
	test $$(stat -c%s $(DISK)) -eq 5275648
# OATH test images: the same disk, but with a signed boot profile (require-signed, one trust root) and v2-signed disk apps.
OATH_DIR := $(BUILD)/oath
SIGNED_IMG := $(BUILD)/sand-os-signed.img
BADOATH_IMG := $(BUILD)/sand-os-badoath.img
signed-images: disk tools/sapp_sign.py tools/mkmimir.py
	$(PYTHON) tools/sapp_sign.py init $(OATH_DIR)
	mkdir -p $(OATH_DIR)/apps
	for a in $(DISK_APPS); do $(PYTHON) tools/sapp_sign.py sign $(OATH_DIR) $(APP_BUILD)/$$a.sapp $(OATH_DIR)/apps/$$a.sapp; done
	cp $(DISK) $(SIGNED_IMG) && cp $(DISK) $(BADOATH_IMG)
	$(PYTHON) tools/mkmimir.py --sigil-key $(SIGIL)/sigil.key --oath require-signed:$(OATH_DIR)/root.pem $(SIGNED_IMG) $(MIMIR_LBA) $(MIMIR_SECTORS) $(foreach a,$(DISK_APPS),$(a).sapp=$(OATH_DIR)/apps/$(a).sapp)
	$(PYTHON) tools/mkmimir.py --sigil-key $(SIGIL)/sigil.key --oath require-signed:$(OATH_DIR)/root.pem --oath-bad-signature $(BADOATH_IMG) $(MIMIR_LBA) $(MIMIR_SECTORS) $(foreach a,$(DISK_APPS),$(a).sapp=$(OATH_DIR)/apps/$(a).sapp)
run-serial: disk
	qemu-system-i386 -machine pc -drive format=raw,file=$(DISK) -m 128M -serial tcp:127.0.0.1:4555,server=on,wait=off

run: disk
	qemu-system-i386 -machine pc -drive format=raw,file=$(DISK) -m 128M
check: disk
	@echo "SAND boot, memory, paging, heap, PIT, preemptive scheduler and synchronization built."
host-test: apps disk
	mkdir -p $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -Iinclude -Ikernel/crypto -o $(BUILD)/ed25519-kat tests/veyra/ed25519_kat_test.c kernel/crypto/sha512.c kernel/crypto/ed25519.c kernel/crypto/sha256.c kernel/security/stci.c
	./$(BUILD)/ed25519-kat
	cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Isdk/sapp/include -o $(BUILD)/veyra-subsystems tests/veyra/subsystems_test.c kernel/veyra/registry.c kernel/ipc/tideway.c && $(BUILD)/veyra-subsystems
	$(PYTHON) tests/veyra/ed25519_crosscheck.py
	$(PYTHON) tests/veyra/stci_chain_test.py
	cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -o $(BUILD)/diskapps-perm tests/veyra/diskapps_perm_test.c kernel/proc/diskapps.c
	./$(BUILD)/diskapps-perm $(APP_BUILD)/jail.sapp 0x01 $(APP_BUILD)/lumen.sapp 0x20 $(APP_BUILD)/locus.sapp 0x2C $(APP_BUILD)/keystone.sapp 0x60
	$(PYTHON) tests/tools/test_gen_app_table.py
	cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -o $(BUILD)/mimir-test tests/fs/mimir_test.c kernel/fs/mimir.c kernel/crypto/sha256.c kernel/crypto/sha512.c kernel/crypto/ed25519.c kernel/storage/block.c
	./$(BUILD)/mimir-test
	./$(BUILD)/mimir-test $(DISK) $(MIMIR_LBA) $(MIMIR_SECTORS) jail.sapp=$(APP_BUILD)/jail.sapp lumen.sapp=$(APP_BUILD)/lumen.sapp wardprobe.sapp=$(APP_BUILD)/wardprobe.sapp runetest.sapp=$(APP_BUILD)/runetest.sapp $(SIGIL)/sigil-pub.bin
test: disk host-test signed-images
	$(PYTHON) tests/qemu/test_shell.py
	$(PYTHON) tests/qemu/test_desktop.py
	$(PYTHON) tests/qemu/test_install_tool.py
	$(PYTHON) tests/qemu/test_net.py
	$(PYTHON) tests/qemu/test_mimir.py
	$(PYTHON) tests/qemu/test_oath.py
ui-preview:
	$(CC) -O2 -Wall -Iinclude -Isdk/sapp/include -o $(BUILD)/ui_preview tools/ui_preview.c kernel/graphics/ui.c kernel/graphics/ui_wall.c kernel/graphics/gfx.c kernel/graphics/font.c kernel/graphics/font_data.c
	./$(BUILD)/ui_preview $(BUILD)/ui.ppm
clean:
	rm -rf $(BUILD)
