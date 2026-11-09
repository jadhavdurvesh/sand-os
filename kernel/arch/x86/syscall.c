#include "sand/arch/x86/syscall.h"
#include "sand/arch/x86/idt.h"
static inline void debug_char(char c) { __asm__ volatile ("outb %0, $0xE9" : : "a"(c)); }
static void debug_text(const char *text) { while(*text) debug_char(*text++); }
extern void x86_syscall_entry(void);
void syscall_init(void) { idt_set_syscall(x86_syscall_entry); idt_load(); }
void syscall_dispatch(uint32_t *registers) {
    uint32_t number=registers[7];
    uint32_t argument=registers[4];
    if(number==1u && argument==0x53414E44u) {
        debug_text("SAND_TEST:RING3_SYSCALL_PASS\n");
        registers[7]=0u;
    } else {
        debug_text("SAND_TEST:RING3_SYSCALL_REJECTED\n");
        registers[7]=0xFFFFFFFFu;
    }
}
