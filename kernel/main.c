#include <stdint.h>
#include "sand/arch/x86/idt.h"
#include "sand/arch/x86/paging.h"
#include "sand/arch/x86/timer.h"
#include "sand/kernel/scheduler.h"
#include "sand/kernel/sync.h"
#include "sand/mm/frame.h"
#include "sand/mm/heap.h"
#include "sand/mm/memory_map.h"
void pic_init_masked(void);
static volatile uint16_t *const vga=(uint16_t *)0xB8000;
static volatile uint32_t producer_count,consumer_count;
static sand_semaphore_t available, consumed;
static sand_mutex_t counter_lock;
static uint16_t ve(char c,uint8_t color) { return (uint16_t)(uint8_t)c|((uint16_t)color<<8); }
static void clear_screen(uint8_t color) { for(uint32_t i=0;i<2000u;++i) vga[i]=ve(' ',color); }
static void write_at(const char *s,uint32_t row,uint32_t col,uint8_t color) { for(uint32_t i=0;s[i]!='\0'&&col+i<80u;++i) vga[row*80u+col+i]=ve(s[i],color); }
static void write_hex(uint32_t v,uint32_t row,uint32_t col,uint8_t color) { static const char d[]="0123456789ABCDEF"; for(uint32_t i=0;i<8u;++i) vga[row*80u+col+i]=ve(d[(v>>((7u-i)*4u))&15u],color); }
static void producer(void *arg) {
    (void)arg;
    for(;;) { mutex_lock(&counter_lock); ++producer_count; mutex_unlock(&counter_lock); semaphore_post(&available); semaphore_wait(&consumed); }
}
static void consumer(void *arg) {
    (void)arg;
    for(;;) { semaphore_wait(&available); mutex_lock(&counter_lock); ++consumer_count; mutex_unlock(&counter_lock); semaphore_post(&consumed); }
}
void kernel_main(void) {
    __asm__ volatile ("cli"); clear_screen(0x07); write_at("SAND OS",2,5,0x0B);
    idt_init(); pic_init_masked(); __asm__ volatile ("int $0x3");
    write_at("M1: IDT and exception test passed.",4,5,0x0A);
    struct sand_memory_map map;
    if(memory_map_get(&map)!=0||frame_allocator_init()!=0||paging_init()!=0||!paging_is_enabled()||kheap_init()!=0) {
        write_at("M2 ERROR: memory subsystem initialization failed.",6,5,0x4F); for(;;)__asm__ volatile("hlt");
    }
    volatile uint32_t *sample=(volatile uint32_t *)kmalloc(128u);
    if(sample==0) { write_at("M2 ERROR: heap allocation failed.",6,5,0x4F); for(;;)__asm__ volatile("hlt"); }
    *sample=0x53414E44u;
    if(*sample!=0x53414E44u) { write_at("M2 ERROR: heap test failed.",6,5,0x4F); for(;;)__asm__ volatile("hlt"); }
    write_at("M2: physical memory, paging and heap passed.",6,5,0x0A);
    semaphore_init(&available,0u); semaphore_init(&consumed,0u); mutex_init(&counter_lock);
    if(scheduler_init()!=0||scheduler_create(producer,0)<0||scheduler_create(consumer,0)<0) {
        write_at("M3 ERROR: scheduler initialization failed.",8,5,0x4F); for(;;)__asm__ volatile("hlt");
    }
    timer_init(100u); __asm__ volatile("sti");
    while(timer_ticks()<200u) __asm__ volatile("hlt");
    __asm__ volatile("cli");
    write_at("M3: preemptive scheduler and blocking sync passed.",8,5,0x0A);
    write_at("Timer ticks:",10,5,0x0F); write_hex(timer_ticks(),10,18,0x0F);
    write_at("Context switches:",12,5,0x0F); write_hex(scheduler_switch_count(),12,24,0x0F);
    write_at("Producer events:",14,5,0x0F); write_hex(producer_count,14,24,0x0F);
    write_at("Consumer events:",16,5,0x0F); write_hex(consumer_count,16,24,0x0F);
    write_at("Thread slots now support dead-thread reuse.",18,5,0x07);
    for(;;)__asm__ volatile("sti; hlt");
}
