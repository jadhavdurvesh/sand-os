#include "sand/arch/x86/idt.h"
static volatile uint16_t *const vga=(uint16_t *)0xB8000;
static inline void debug_char(char c) { __asm__ volatile("outb %0, $0xE9"::"a"(c)); }
static void debug_text(const char *s) { while(*s) debug_char(*s++); }
static void debug_hex(uint32_t v) { static const char d[]="0123456789ABCDEF"; for(int i=7;i>=0;--i) debug_char(d[(v>>(i*4))&15u]); }
static void write_panic(const char *text) { for(uint32_t i=0;text[i]!='\0'&&i<70u;++i) vga[12u*80u+5u+i]=(uint16_t)(uint8_t)text[i]|((uint16_t)0x4Fu<<8); }
void x86_exception_dispatch(uint32_t vector,uint32_t error_code) {
    if(vector==3u) return;
    debug_text("SAND_EXCEPTION:VECTOR="); debug_hex(vector); debug_text(":ERROR="); debug_hex(error_code); debug_text("\n");
    write_panic("SAND KERNEL PANIC: CPU exception; system halted.");
    __asm__ volatile("cli"); for(;;) __asm__ volatile("hlt");
}
