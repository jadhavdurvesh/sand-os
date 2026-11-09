#include <stdint.h>
#include "sand/arch/x86/idt.h"
#include "sand/mm/frame.h"
#include "sand/mm/memory_map.h"
void pic_init_masked(void);
static volatile uint16_t *const vga = (uint16_t *)0xB8000;
static uint16_t vga_entry(char c, uint8_t color) { return (uint16_t)(uint8_t)c | ((uint16_t)color << 8); }
static void clear_screen(uint8_t color) {
    for (uint32_t i = 0; i < 80u * 25u; ++i) vga[i] = vga_entry(' ', color);
}
static void write_at(const char *text, uint32_t row, uint32_t col, uint8_t color) {
    uint32_t i = 0;
    while (text[i] != '\0' && col + i < 80u) { vga[row * 80u + col + i] = vga_entry(text[i], color); ++i; }
}
static void write_hex(uint32_t value, uint32_t row, uint32_t col, uint8_t color) {
    static const char digits[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < 8u; ++i) vga[row * 80u + col + i] = vga_entry(digits[(value >> ((7u-i)*4u)) & 0xFu], color);
}
void kernel_main(void) {
    __asm__ volatile ("cli");
    clear_screen(0x07);
    write_at("SAND OS", 2, 5, 0x0B);
    write_at("Custom BIOS boot chain reached kernel.", 4, 5, 0x0A);
    idt_init();
    pic_init_masked();
    __asm__ volatile ("int $0x3");
    write_at("M1: IDT and controlled INT3 test passed.", 6, 5, 0x0A);

    struct sand_memory_map map;
    if (memory_map_get(&map) != 0) {
        write_at("M2 ERROR: BIOS E820 memory map unavailable.", 8, 5, 0x4F);
        for (;;) __asm__ volatile ("hlt");
    }
    write_at("M2: BIOS E820 entries:", 8, 5, 0x0E);
    write_hex(map.count, 8, 30, 0x0E);

    if (frame_allocator_init() != 0) {
        write_at("M2 ERROR: physical frame allocator init failed.", 10, 5, 0x4F);
        for (;;) __asm__ volatile ("hlt");
    }
    uint32_t first = frame_alloc();
    write_at("Physical frame allocator initialized. First frame:", 10, 5, 0x0A);
    write_hex(first, 10, 57, 0x0A);
    write_at("Free managed frames:", 12, 5, 0x0F);
    write_hex(frame_free_count(), 12, 27, 0x0F);
    if (first != 0u) (void)frame_free(first);
    write_at("Allocator allocate/free smoke test passed.", 14, 5, 0x0A);
    write_at("Next: paging and kernel virtual memory.", 16, 5, 0x07);
    for (;;) __asm__ volatile ("hlt");
}
