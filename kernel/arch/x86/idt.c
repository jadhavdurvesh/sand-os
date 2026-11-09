#include "sand/arch/x86/idt.h"
#define IDT_ENTRIES 256u
#define KERNEL_CODE_SELECTOR 0x08u
#define IDT_INTERRUPT_GATE 0x8Eu
struct __attribute__((packed)) idt_entry { uint16_t offset_low; uint16_t selector; uint8_t zero; uint8_t attributes; uint16_t offset_high; };
struct __attribute__((packed)) idt_pointer { uint16_t limit; uint32_t base; };
static struct idt_entry idt[IDT_ENTRIES];
extern void (* const x86_exception_stub_table[32])(void);
static void idt_set_gate(uint8_t vector, void (*handler)(void)) {
    uint32_t address = (uint32_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = KERNEL_CODE_SELECTOR;
    idt[vector].zero = 0;
    idt[vector].attributes = IDT_INTERRUPT_GATE;
    idt[vector].offset_high = (uint16_t)(address >> 16);
}
void idt_set_irq0(void (*handler)(void)) { idt_set_gate(32u, handler); }
void idt_set_reschedule(void (*handler)(void)) { idt_set_gate(48u, handler); }
void idt_load(void) {
    struct idt_pointer pointer = { (uint16_t)(sizeof(idt) - 1u), (uint32_t)&idt[0] };
    __asm__ volatile ("lidt %0" : : "m"(pointer));
}
void idt_init(void) {
    for (uint32_t i = 0; i < IDT_ENTRIES; ++i) {
        idt[i].offset_low = 0; idt[i].selector = 0; idt[i].zero = 0;
        idt[i].attributes = 0; idt[i].offset_high = 0;
    }
    for (uint32_t i = 0; i < 32u; ++i) idt_set_gate((uint8_t)i, x86_exception_stub_table[i]);
    idt_load();
}
