#include <stdint.h>
#include "sand/arch/x86/idt.h"
#include "sand/arch/x86/paging.h"
#include "sand/arch/x86/timer.h"
#include "sand/mm/frame.h"
#include "sand/mm/heap.h"
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
    idt_init();
    pic_init_masked();
    __asm__ volatile ("int $0x3");
    write_at("M1: IDT and controlled INT3 test passed.", 4, 5, 0x0A);

    struct sand_memory_map map;
    if (memory_map_get(&map) != 0 || frame_allocator_init() != 0) {
        write_at("M2 ERROR: memory discovery or frame allocator failed.", 6, 5, 0x4F);
        for (;;) __asm__ volatile ("hlt");
    }
    write_at("M2: BIOS E820 entries:", 6, 5, 0x0E);
    write_hex(map.count, 6, 30, 0x0E);
    uint32_t first = frame_alloc();
    if (first == 0u || frame_free(first) != 0) {
        write_at("M2 ERROR: frame allocate/free test failed.", 8, 5, 0x4F);
        for (;;) __asm__ volatile ("hlt");
    }
    write_at("M2: physical frame allocator test passed.", 8, 5, 0x0A);

    if (paging_init() != 0 || !paging_is_enabled() || kheap_init() != 0) {
        write_at("M2 ERROR: paging or heap initialization failed.", 10, 5, 0x4F);
        for (;;) __asm__ volatile ("hlt");
    }
    volatile uint32_t *sample = (volatile uint32_t *)kmalloc(128u);
    if (sample == 0) {
        write_at("M2 ERROR: kernel heap allocation failed.", 10, 5, 0x4F);
        for (;;) __asm__ volatile ("hlt");
    }
    *sample = 0x53414E44u;
    if (*sample != 0x53414E44u) {
        write_at("M2 ERROR: heap read/write test failed.", 10, 5, 0x4F);
        for (;;) __asm__ volatile ("hlt");
    }
    write_at("M2: paging and virtual heap test passed.", 10, 5, 0x0A);

    timer_init(100u);
    __asm__ volatile ("sti");
    while (timer_ticks() < 10u) __asm__ volatile ("hlt");
    __asm__ volatile ("cli");
    write_at("M3: PIT timer IRQ0 delivered successfully.", 12, 5, 0x0A);
    write_at("Timer ticks:", 14, 5, 0x0F);
    write_hex(timer_ticks(), 14, 18, 0x0F);
    write_at("Next: preemptive scheduler and thread contexts.", 16, 5, 0x07);
    for (;;) __asm__ volatile ("hlt");
}
