#include <stdint.h>

static volatile uint16_t *const vga = (uint16_t *)0xB8000;

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

static void clear_screen(uint8_t color) {
    for (uint32_t i = 0; i < 80u * 25u; ++i) {
        vga[i] = vga_entry(' ', color);
    }
}

static void write_at(const char *text, uint32_t row, uint32_t col,
                     uint8_t color) {
    uint32_t i = 0;
    while (text[i] != '\0' && col + i < 80u) {
        vga[row * 80u + col + i] = vga_entry(text[i], color);
        ++i;
    }
}

void kernel_main(void) {
    clear_screen(0x07);
    write_at("SAND OS", 3, 5, 0x0B);
    write_at("Independent operating system research project", 5, 5, 0x0F);
    write_at("Milestone 0: kernel entry reached successfully.", 7, 5, 0x0A);
    write_at("No Linux kernel is running here.", 9, 5, 0x0E);
    write_at("Next: long mode, GDT, IDT, memory management.", 11, 5, 0x07);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
