SAPP_SDK ?= ../..
CC ?= i686-elf-gcc
LD ?= i686-elf-ld
OBJCOPY ?= i686-elf-objcopy
PYTHON ?= python3
CFLAGS := -m32 -std=c11 -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-stack-protector -fno-asynchronous-unwind-tables -O2 \
          -Wall -Wextra -Werror -I$(SAPP_SDK)/include
BUILD := build

.PHONY: all clean inspect
all: $(BUILD)/hello.sapp

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hello.elf: $(BUILD)/main.o
	$(LD) -m elf_i386 -T $(SAPP_SDK)/linker/sapp-i386.ld -nostdlib $< -o $@

$(BUILD)/hello.bin: $(BUILD)/hello.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/hello.sapp: $(BUILD)/hello.bin manifest.json
	$(PYTHON) $(SAPP_SDK)/tools/sapp.py pack --input $< --manifest manifest.json \
	  --output $@ --base 0x40000000 --entry 0x40000000 --permissions rx

inspect: $(BUILD)/hello.sapp
	$(PYTHON) $(SAPP_SDK)/tools/sapp.py verify $<
	$(PYTHON) $(SAPP_SDK)/tools/sapp.py info $<

clean:
	rm -rf $(BUILD)
