# Third-party notices

## External development and test tools

GCC, GNU Binutils, GNU Make, QEMU, GitHub Actions, and its checkout/upload actions may be used on the developer or CI host. They are not included in the SAND OS disk image or runtime and retain their own licenses. QEMU is only an external machine emulator for testing.

## Platform firmware interface

The initial bootloader targets legacy BIOS. SAND's boot-sector and stage-two source are implemented in this repository, but the boot process calls firmware-provided BIOS interrupt services (INT 13h disk I/O and INT 10h text output) before entering protected mode. Firmware is part of the target machine platform, not code shipped by this repository.

## Runtime implementation

No GRUB bootloader, Multiboot implementation, Linux kernel, or third-party OS runtime is intended to be part of the SAND OS boot image. Record future third-party code or runtime components here before introducing them, including version, origin, license, and affected files.
