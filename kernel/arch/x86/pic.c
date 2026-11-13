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

#include "sand/arch/x86/irq.h"
#include "sand/arch/x86/idt.h"
static sand_irq_handler irq_handlers[16];
extern void (* const x86_irq_stub_table[16])(void);
static inline void pic_outb(uint16_t port,uint8_t value) { __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port)); }
static inline uint8_t pic_inb(uint16_t port) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }
void irq_unmask(uint8_t line) {
    if(line>=16u) return;
    uint16_t port=line<8u?0x21u:0xA1u; uint8_t bit=(uint8_t)(1u<<(line&7u));
    pic_outb(port,(uint8_t)(pic_inb(port)&(uint8_t)~bit));
    if(line>=8u) pic_outb(0x21u,(uint8_t)(pic_inb(0x21u)&(uint8_t)~0x04u)); /* cascade line */
}
void irq_mask(uint8_t line) {
    if(line>=16u) return;
    uint16_t port=line<8u?0x21u:0xA1u; uint8_t bit=(uint8_t)(1u<<(line&7u));
    pic_outb(port,(uint8_t)(pic_inb(port)|bit));
}
int irq_install(uint8_t line,sand_irq_handler handler) {
    if(line==0u || line>=16u || handler==0) return -1;
    irq_handlers[line]=handler;
    idt_set_irq(line,x86_irq_stub_table[line]); idt_load();
    irq_unmask(line); return 0;
}
void irq_dispatch(uint32_t line) {
    if(line<16u && irq_handlers[line]!=0) irq_handlers[line]();
    if(line>=8u) pic_outb(0xA0u,0x20u);
    pic_outb(0x20u,0x20u);
}
