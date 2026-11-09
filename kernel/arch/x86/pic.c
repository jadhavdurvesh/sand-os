#include <stdint.h>
/* Remap the legacy 8259 PIC; keep every IRQ masked until handlers exist. */
void pic_init_masked(void) {
    __asm__ volatile ("cli");
    __asm__ volatile ("outb %0, $0x20" : : "a"((uint8_t)0x11));
    __asm__ volatile ("outb %0, $0xA0" : : "a"((uint8_t)0x11));
    __asm__ volatile ("outb %0, $0x21" : : "a"((uint8_t)0x20));
    __asm__ volatile ("outb %0, $0xA1" : : "a"((uint8_t)0x28));
    __asm__ volatile ("outb %0, $0x21" : : "a"((uint8_t)0x04));
    __asm__ volatile ("outb %0, $0xA1" : : "a"((uint8_t)0x02));
    __asm__ volatile ("outb %0, $0x21" : : "a"((uint8_t)0x01));
    __asm__ volatile ("outb %0, $0xA1" : : "a"((uint8_t)0x01));
    __asm__ volatile ("outb %0, $0x21" : : "a"((uint8_t)0xFF));
    __asm__ volatile ("outb %0, $0xA1" : : "a"((uint8_t)0xFF));
}
