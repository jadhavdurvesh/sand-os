#include "sand/arch/x86/idt.h"
static volatile uint16_t *const vga = (uint16_t *)0xB8000;
static void write_panic(const char *text) {
    for (uint32_t i = 0; text[i] != '\0' && i < 70u; ++i)
        vga[12u * 80u + 5u + i] = (uint16_t)(uint8_t)text[i] | ((uint16_t)0x4Fu << 8);
}
void x86_exception_dispatch(uint32_t vector, uint32_t error_code) {
    if (vector == 3u) return;
    (void)error_code;
    write_panic("SAND KERNEL PANIC: CPU exception; system halted.");
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}
