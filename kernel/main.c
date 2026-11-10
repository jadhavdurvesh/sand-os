#include <stdint.h>
#include "sand/arch/x86/idt.h"
#include "sand/arch/x86/gdt.h"
#include "sand/arch/x86/syscall.h"
#include "sand/kernel/process.h"
#include "sand/arch/x86/paging.h"
#include "sand/arch/x86/timer.h"
#include "sand/kernel/scheduler.h"
#include "sand/kernel/sync.h"
#include "sand/mm/frame.h"
#include "sand/mm/heap.h"
#include "sand/mm/memory_map.h"
#include "sand/arch/x86/ata_pio.h"
#include "sand/fs/sandfs.h"
#include "sand/arch/x86/vbe.h"
#include "sand/graphics/desktop.h"
#include "sand/storage/block.h"
void pic_init_masked(void);
static volatile uint16_t *const vga=(uint16_t *)0xB8000;
static volatile uint32_t producer_count,consumer_count,protected_count,reuse_done;
static volatile uint32_t test_failures;
static sand_semaphore_t available;
static sand_mutex_t counter_lock;
static const uint32_t TEST_ITEMS=1000u;
static volatile uint32_t wait_test_done, wait_test_failed;
static sand_pid_t wait_test_child;
static void debug_text(const char *s);
static void wait_test_child_exit(void *arg) {
    (void)arg;
    debug_text("SAND_TEST:M7_WAIT_CHILD_EXITING\\n");
    if(process_exit(wait_test_child,73)!=0) wait_test_failed=1u;
}
static void wait_test_parent_thread(void *arg) {
    (void)arg;
    int32_t status=-1;
    debug_text("SAND_TEST:M7_WAIT_PARENT_STARTED\\n");
    if(process_create_test("wait-child",&wait_test_child)!=0) { debug_text("SAND_TEST:M7_WAIT_PROCESS_CREATE_FAIL\\n"); wait_test_failed=1u; wait_test_done=1u; return; }
    debug_text("SAND_TEST:M7_WAIT_PROCESS_CREATED\\n");
    int child_thread_id=scheduler_create(wait_test_child_exit,0);
    if(child_thread_id<0) {
        debug_text("SAND_TEST:M7_WAIT_THREAD_CREATE_FAIL\\n");
        if(child_thread_id==-3) debug_text("SAND_TEST:M7_WAIT_STACK_POOL_EXHAUSTED\\n");
        else if(child_thread_id==-4) debug_text("SAND_TEST:M7_WAIT_THREAD_SLOTS_EXHAUSTED\\n");
        else if(child_thread_id==-2) debug_text("SAND_TEST:M7_WAIT_SCHEDULER_NOT_READY\\n");
        wait_test_failed=1u; wait_test_done=1u; return;
    }
    debug_text("SAND_TEST:M7_WAIT_BLOCKING\\n");
    int wait_result=process_wait(wait_test_child,&status);
    debug_text("SAND_TEST:M7_WAIT_PARENT_RESUMED\\n");
    if(wait_result!=1 || status!=73) {
        debug_text("SAND_TEST:M7_WAIT_RESULT_INVALID\\n");
        wait_test_failed=1u;
    }
    if(process_destroy(wait_test_child)!=0) {
        debug_text("SAND_TEST:M7_WAIT_DESTROY_FAIL\\n");
        wait_test_failed=1u;
    } else {
        debug_text("SAND_TEST:M7_WAIT_DESTROY_PASS\\n");
    }
    wait_test_done=1u;
}

static inline void debug_char(char c) { __asm__ volatile ("outb %0, $0xE9" : : "a"(c)); }
static void debug_text(const char *s) { while(*s) debug_char(*s++); }
static uint16_t ve(char c,uint8_t color) { return (uint16_t)(uint8_t)c|((uint16_t)color<<8); }
static void clear_screen(uint8_t color) { for(uint32_t i=0;i<2000u;++i) vga[i]=ve(' ',color); }
static void write_at(const char *s,uint32_t row,uint32_t col,uint8_t color) { for(uint32_t i=0;s[i]!='\0'&&col+i<80u;++i) vga[row*80u+col+i]=ve(s[i],color); }
static void write_hex(uint32_t v,uint32_t row,uint32_t col,uint8_t color) { static const char d[]="0123456789ABCDEF"; for(uint32_t i=0;i<8u;++i) vga[row*80u+col+i]=ve(d[(v>>((7u-i)*4u))&15u],color); }
static void producer(void *arg) {
    (void)arg;
    for(uint32_t i=0;i<TEST_ITEMS;++i) {
        mutex_lock(&counter_lock);
        ++protected_count;
        ++producer_count;
        mutex_unlock(&counter_lock);
        semaphore_post(&available);
    }
}
static void consumer(void *arg) {
    (void)arg;
    for(uint32_t i=0;i<TEST_ITEMS;++i) {
        semaphore_wait(&available);
        mutex_lock(&counter_lock);
        ++protected_count;
        ++consumer_count;
        mutex_unlock(&counter_lock);
    }
}
static void reuse_probe(void *arg) { (void)arg; reuse_done=1u; debug_text("SAND_TEST:REUSE_PASS\n"); }
void kernel_main(void) {
    __asm__ volatile ("cli");
    clear_screen(0x07); write_at("SAND OS",2,5,0x0B);
    debug_text("SAND_TEST:BOOT\n");
    idt_init(); pic_init_masked(); __asm__ volatile ("int $0x3");
    write_at("M1: IDT and exception test passed.",4,5,0x0A);
    struct sand_memory_map map;
    if(memory_map_get(&map)!=0||frame_allocator_init()!=0||paging_init()!=0||!paging_is_enabled()||kheap_init()!=0) {
        debug_text("SAND_TEST:MEMORY_FAIL\n"); write_at("M2 ERROR: memory subsystem failed.",6,5,0x4F); for(;;)__asm__ volatile("hlt");
    }
    volatile uint32_t *sample=(volatile uint32_t *)kmalloc(128u);
    if(sample==0) { debug_text("SAND_TEST:HEAP_FAIL\n"); for(;;)__asm__ volatile("hlt"); }
    *sample=0x53414E44u;
    if(*sample!=0x53414E44u) { debug_text("SAND_TEST:HEAP_FAIL\n"); for(;;)__asm__ volatile("hlt"); }
    write_at("M2: physical memory, paging and heap passed.",6,5,0x0A);
    semaphore_init(&available,0u); mutex_init(&counter_lock);
    if(scheduler_init()!=0) { debug_text("SAND_TEST:SCHED_INIT_FAIL\n"); for(;;)__asm__ volatile("hlt"); }
    /* Start consumer first so it must block before the producer posts. */
    if(scheduler_create(consumer,0)<0||scheduler_create(producer,0)<0) {
        debug_text("SAND_TEST:THREAD_CREATE_FAIL\n"); for(;;)__asm__ volatile("hlt");
    }
    timer_init(100u);
    __asm__ volatile("sti");
    while(consumer_count<TEST_ITEMS) __asm__ volatile("hlt");
    __asm__ volatile("cli");
    if(producer_count!=TEST_ITEMS||consumer_count!=TEST_ITEMS||protected_count!=(TEST_ITEMS*2u)) {
        ++test_failures; debug_text("SAND_TEST:SYNC_FAIL\n");
    } else {
        debug_text("SAND_TEST:BLOCK_WAKE_PASS\n");
        debug_text("SAND_TEST:MUTEX_COUNTER_PASS\n");
    }
    /* Wait until both worker functions have returned and been reaped logically. */
    while(scheduler_thread_count()!=0u) __asm__ volatile("sti; hlt");
    /* Reuse a dead worker's thread slot and its reclaimed stack slot. */
    if(scheduler_create(reuse_probe,0)<0) {
        ++test_failures; debug_text("SAND_TEST:REUSE_FAIL\n");
    } else {
        while(reuse_done==0u) __asm__ volatile("sti; hlt");
        __asm__ volatile("cli");
    }
    if(test_failures==0u) { write_at("M3: scheduler synchronization tests passed.",8,5,0x0A); } else { debug_text("SAND_TEST:FAILED\n"); write_at("M3 ERROR: scheduler tests failed.",8,5,0x4F); }
    write_at("Timer ticks:",10,5,0x0F); write_hex(timer_ticks(),10,18,0x0F);
    write_at("Context switches:",12,5,0x0F); write_hex(scheduler_switch_count(),12,24,0x0F);
    write_at("Producer events:",14,5,0x0F); write_hex(producer_count,14,24,0x0F);
    write_at("Consumer events:",16,5,0x0F); write_hex(consumer_count,16,24,0x0F);
    write_at("Boot context now provides scheduler idle fallback.",18,5,0x07);
    debug_text("SAND_TEST:M5_PROCESS_INIT\n");
    gdt_init();
    syscall_init();
    if(process_system_init()!=0) { debug_text("SAND_TEST:M5_INIT_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    sand_pid_t first=0u,second=0u;
    if(process_create_test("init",&first)!=0 || process_activate(first)!=0 ||
       process_create_test("worker",&second)!=0 || first==second) {
        debug_text("SAND_TEST:M5_CREATE_FAIL\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    struct sand_process_info first_info,second_info;
    if(process_get_info(first,&first_info)!=0 || process_get_info(second,&second_info)!=0 ||
       first_info.page_directory==second_info.page_directory || second_info.parent_pid!=first) {
        debug_text("SAND_TEST:M5_ISOLATION_FAIL\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    debug_text("SAND_TEST:M7_PARENT_CHILD_LINK_PASS\n");
    debug_text("SAND_TEST:M5_TWO_ADDRESS_SPACES_PASS\n");
    uint8_t malformed_elf[16]={0}; sand_pid_t rejected_pid=0u;
    if(process_create_elf("bad-elf",malformed_elf,sizeof(malformed_elf),&rejected_pid)==0) {
        debug_text("SAND_TEST:M7_INVALID_ELF_ACCEPTED\\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    debug_text("SAND_TEST:M7_ELF_REJECT_INVALID_PASS\\n");
    if(process_activate(second)!=0 || process_current()!=second) {
        debug_text("SAND_TEST:M5_ACTIVATE_FAIL\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    debug_text("SAND_TEST:M5_CR3_SWITCH_PASS\n");
    if(process_exit(second,37)!=0) {
        debug_text("SAND_TEST:M7_EXIT_FAIL\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    if(process_activate(first)!=0) { debug_text("SAND_TEST:M7_PARENT_REACTIVATE_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    int32_t child_status=-1;
    if(process_wait(second,&child_status)!=1 || child_status!=37) {
        debug_text("SAND_TEST:M7_WAIT_STATUS_FAIL\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    debug_text("SAND_TEST:M7_EXIT_WAIT_STATUS_PASS\n");
    if(process_destroy(second)!=0) {
        debug_text("SAND_TEST:M5_DESTROY_FAIL\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    debug_text("SAND_TEST:M5_TERMINATE_DESTROY_PASS\n");
    if(process_activate(first)!=0) { debug_text("SAND_TEST:M5_REACTIVATE_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    debug_text("SAND_TEST:M6_STORAGE_INIT\\n");
    if(ata_pio_init()!=0 || block_count()!=1u) { debug_text("SAND_TEST:M6_ATA_INIT_FAIL\\n"); for(;;) __asm__ volatile("cli; hlt"); }
    if(sandfs_format(0u,512u,2048u)!=0 || sandfs_mount(0u,512u)!=0) { debug_text("SAND_TEST:M6_FORMAT_MOUNT_FAIL\\n"); for(;;) __asm__ volatile("cli; hlt"); }
    static const char storage_message[]="SAND native storage read/write verified";
    char storage_readback[64]; uint32_t storage_length=0u;
    if(sandfs_write("m6-test.txt",storage_message,sizeof(storage_message))!=0 ||
       sandfs_read("m6-test.txt",storage_readback,sizeof(storage_readback),&storage_length)!=0 ||
       storage_length!=sizeof(storage_message)) { debug_text("SAND_TEST:M6_FILE_IO_FAIL\\n"); for(;;) __asm__ volatile("cli; hlt"); }
    for(uint32_t i=0;i<storage_length;++i) if(storage_readback[i]!=storage_message[i]) { debug_text("SAND_TEST:M6_VERIFY_FAIL\\n"); for(;;) __asm__ volatile("cli; hlt"); }
    debug_text("SAND_TEST:M6_ATA_IDENTIFY_PASS\\n");
    debug_text("SAND_TEST:M6_SANDFS_FORMAT_PASS\\n");
    debug_text("SAND_TEST:M6_FILE_READ_WRITE_PASS\\n");
    if(scheduler_create(wait_test_parent_thread,0)<0) {
        debug_text("SAND_TEST:M7_BLOCKING_WAIT_CREATE_FAIL\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    __asm__ volatile("sti");
    while(wait_test_done==0u) __asm__ volatile("hlt");
    __asm__ volatile("cli");
    if(wait_test_failed!=0u) {
        debug_text("SAND_TEST:M7_BLOCKING_WAIT_FAIL\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    debug_text("SAND_TEST:M7_BLOCKING_WAIT_WAKE_PASS\n");
    /* VBE is mapped in the kernel page directory, not the process CR3. */
    if(paging_activate(paging_kernel_directory())!=0) {
        debug_text("SAND_TEST:KERNEL_CR3_RESTORE_FAIL\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    sand_framebuffer_t framebuffer;
    if(vbe_framebuffer_init(&framebuffer)==0 && sand_desktop_render(&framebuffer)==0) {
        debug_text("SAND_TEST:GRAPHICS_FRAMEBUFFER_PASS\\n");
    } else {
        debug_text("SAND_TEST:GRAPHICS_FRAMEBUFFER_UNAVAILABLE\\n");
    }
    debug_text("SAND_TEST:RING3_ENTER\n");
    process_launch(first);
    for(;;) __asm__ volatile("cli; hlt");
}
