#include <stdint.h>
#include "sand/arch/x86/idt.h"
#include "sand/arch/x86/paging.h"
#include "sand/arch/x86/timer.h"
#include "sand/kernel/scheduler.h"
#include "sand/mm/frame.h"
#include "sand/mm/heap.h"
#include "sand/mm/memory_map.h"
void pic_init_masked(void);
static volatile uint16_t *const vga = (uint16_t *)0xB8000;
static volatile uint32_t worker_a_ticks;
static volatile uint32_t worker_b_ticks;
static uint16_t vga_entry(char c, uint8_t color) { return (uint16_t)(uint8_t)c | ((uint16_t)color << 8); }
static void clear_screen(uint8_t color) { for (uint32_t i=0;i<80u*25u;++i) vga[i]=vga_entry(' ',color); }
static void write_at(const char *text,uint32_t row,uint32_t col,uint8_t color) {
    for(uint32_t i=0;text[i]!='\0' && col+i<80u;++i) vga[row*80u+col+i]=vga_entry(text[i],color);
}
static void write_hex(uint32_t value,uint32_t row,uint32_t col,uint8_t color) {
    static const char digits[]="0123456789ABCDEF";
    for(uint32_t i=0;i<8u;++i) vga[row*80u+col+i]=vga_entry(digits[(value>>((7u-i)*4u))&0xFu],color);
}
static void worker_a(void *arg) { (void)arg; for(;;) ++worker_a_ticks; }
static void worker_b(void *arg) { (void)arg; for(;;) ++worker_b_ticks; }
void kernel_main(void) {
    __asm__ volatile ("cli");
    clear_screen(0x07);
    write_at("SAND OS",2,5,0x0B);
    idt_init(); pic_init_masked(); __asm__ volatile ("int $0x3");
    write_at("M1: IDT and exception test passed.",4,5,0x0A);
    struct sand_memory_map map;
    if(memory_map_get(&map)!=0 || frame_allocator_init()!=0 || paging_init()!=0 || !paging_is_enabled() || kheap_init()!=0) {
        write_at("M2 ERROR: memory subsystem initialization failed.",6,5,0x4F); for(;;) __asm__ volatile("hlt");
    }
    volatile uint32_t *sample=(volatile uint32_t *)kmalloc(128u);
    if(sample==0) { write_at("M2 ERROR: heap allocation failed.",6,5,0x4F); for(;;) __asm__ volatile("hlt"); }
    *sample=0x53414E44u;
    if(*sample!=0x53414E44u) { write_at("M2 ERROR: heap test failed.",6,5,0x4F); for(;;) __asm__ volatile("hlt"); }
    write_at("M2: physical memory, paging and heap passed.",6,5,0x0A);
    if(scheduler_init()!=0 || scheduler_create(worker_a,0)<0 || scheduler_create(worker_b,0)<0) {
        write_at("M3 ERROR: scheduler initialization failed.",8,5,0x4F); for(;;) __asm__ volatile("hlt");
    }
    timer_init(100u);
    __asm__ volatile ("sti");
    while(timer_ticks()<100u) __asm__ volatile("hlt");
    __asm__ volatile ("cli");
    write_at("M3: PIT preemptive round-robin scheduler active.",8,5,0x0A);
    write_at("Timer ticks:",10,5,0x0F); write_hex(timer_ticks(),10,18,0x0F);
    write_at("Context switches:",12,5,0x0F); write_hex(scheduler_switch_count(),12,24,0x0F);
    write_at("Worker A iterations:",14,5,0x0F); write_hex(worker_a_ticks,14,27,0x0F);
    write_at("Worker B iterations:",16,5,0x0F); write_hex(worker_b_ticks,16,27,0x0F);
    write_at("Next: thread blocking, synchronization and user mode.",18,5,0x07);
    for(;;) __asm__ volatile("sti; hlt");
}
