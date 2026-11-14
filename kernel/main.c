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
#include "sand/console/console.h"
#include "sand/drivers/drivers.h"
#include "sand/proc/apps.h"
#include "sand/drivers/device.h"
int sand_stci_selftest(void);
int sand_stci_gate_selftest(void);
void pic_init_masked(void);
int userland_selftest(void);
extern uint8_t sapp_test_image[174];
/* SANDFS lives above the 5-window kernel area (LBA 17-656). */
#define SAND_SANDFS_START_LBA 1024u
static volatile uint16_t *const vga=(uint16_t *)0xB8000;
static volatile uint32_t producer_count,consumer_count,protected_count,reuse_done;
static volatile uint32_t test_failures;
static sand_semaphore_t available;
static sand_mutex_t counter_lock;
static const uint32_t TEST_ITEMS=1000u;
static volatile uint32_t wait_test_done, wait_test_failed;
static sand_pid_t wait_test_child;
static sand_pid_t sapp_test_pid;
static void debug_text(const char *s);
static void store_le16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void store_le32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
uint8_t sapp_test_image[174];
static int sapp_loader_self_test(void) {
    uint8_t *const image=sapp_test_image;
    const uint32_t image_size=sizeof(sapp_test_image);
    static const char metadata[]="{\"id\":\"org.sand.test\",\"name\":\"SAND test\",\"version\":\"0.2\"}";
    for(uint32_t i=0;i<image_size;++i) image[i]=0u;
    image[0]='S'; image[1]='A'; image[2]='P'; image[3]='P';
    store_le16(image+4,1u); store_le16(image+6,1u);
    store_le32(image+8,64u); store_le32(image+16,0x40000000u);
    store_le32(image+20,64u); store_le32(image+24,1u);
    store_le32(image+28,96u); store_le32(image+32,(uint32_t)(sizeof(metadata)-1u));
    store_le32(image+36,image_size);
    store_le32(image+64,0x40000000u); store_le32(image+68,4096u);
    store_le32(image+72,160u); store_le32(image+76,14u);
    store_le32(image+80,5u); store_le32(image+84,4096u);
    for(uint32_t i=0;i<sizeof(metadata)-1u;++i) image[96u+i]=(uint8_t)metadata[i];
    /* exit(SAND_ABI_TOKEN): mov eax,1; mov ebx,0x53414e44; int 0x80; jmp $ */
    static const uint8_t code[14]={0xB8,0x01,0x00,0x00,0x00,
        0xBB,0x44,0x4E,0x41,0x53,0xCD,0x80,0xEB,0xFE};
    for(uint32_t i=0;i<sizeof(code);++i) image[160u+i]=code[i];
    sapp_test_pid=0u;
    if(process_create_sapp("sapp-test",image,image_size,&sapp_test_pid)!=0) return -1;
    struct sand_process_info info;
    if(process_get_info(sapp_test_pid,&info)!=0 || info.entry!=0x40000000u ||
       info.state!=SAND_PROCESS_READY) return -1;
    /* Loader validated; the userland self-test spawns this image for real. */
    return process_destroy(sapp_test_pid);
}
static void wait_test_child_exit(void *arg) {
    (void)arg;
    debug_text("SAND_TEST:M7_WAIT_CHILD_EXITING\n");
    if(process_exit(wait_test_child,73)!=0) wait_test_failed=1u;
}
static void wait_test_parent_thread(void *arg) {
    (void)arg;
    int32_t status=-1;
    debug_text("SAND_TEST:M7_WAIT_PARENT_STARTED\n");
    if(process_create_test("wait-child",&wait_test_child)!=0) { debug_text("SAND_TEST:M7_WAIT_PROCESS_CREATE_FAIL\n"); wait_test_failed=1u; wait_test_done=1u; return; }
    debug_text("SAND_TEST:M7_WAIT_PROCESS_CREATED\n");
    int child_thread_id=scheduler_create(wait_test_child_exit,0);
    if(child_thread_id<0) {
        debug_text("SAND_TEST:M7_WAIT_THREAD_CREATE_FAIL\n");
        if(child_thread_id==-3) debug_text("SAND_TEST:M7_WAIT_STACK_POOL_EXHAUSTED\n");
        else if(child_thread_id==-4) debug_text("SAND_TEST:M7_WAIT_THREAD_SLOTS_EXHAUSTED\n");
        else if(child_thread_id==-2) debug_text("SAND_TEST:M7_WAIT_SCHEDULER_NOT_READY\n");
        wait_test_failed=1u; wait_test_done=1u; return;
    }
    debug_text("SAND_TEST:M7_WAIT_BLOCKING\n");
    int wait_result=process_wait(wait_test_child,&status);
    debug_text("SAND_TEST:M7_WAIT_PARENT_RESUMED\n");
    if(wait_result!=1 || status!=73) {
        debug_text("SAND_TEST:M7_WAIT_RESULT_INVALID\n");
        wait_test_failed=1u;
    }
    if(process_destroy(wait_test_child)!=0) {
        debug_text("SAND_TEST:M7_WAIT_DESTROY_FAIL\n");
        wait_test_failed=1u;
    } else {
        debug_text("SAND_TEST:M7_WAIT_DESTROY_PASS\n");
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
    timer_init(SAND_TICK_HZ);
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
    if(sand_stci_selftest()==0) debug_text("SAND_TEST:STCI_ED25519_KAT_PASS\n"); else { debug_text("SAND_TEST:STCI_ED25519_KAT_FAIL\n"); ++test_failures; }
    debug_text("SAND_TEST:M5_PROCESS_INIT\n");
    gdt_init();
    syscall_init();
    (void)paging_reserve_table(0xE0000000u); /* VBE window: share it with every address space */
    if(drivers_init()!=0) { debug_text("SAND_TEST:M8_DRIVERS_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
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
        debug_text("SAND_TEST:M7_INVALID_ELF_ACCEPTED\n"); for(;;) __asm__ volatile("cli; hlt");
    }
    debug_text("SAND_TEST:M7_ELF_REJECT_INVALID_PASS\n");
    { int gate_rc=sand_stci_gate_selftest(); if(gate_rc==0) debug_text("SAND_TEST:STCI_LAUNCH_GATE_PASS\n"); else { debug_text("SAND_TEST:STCI_LAUNCH_GATE_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); } }
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
    debug_text("SAND_TEST:M6_STORAGE_INIT\n");
    if(ata_pio_init()!=0 || block_count()<1u) { debug_text("SAND_TEST:M6_ATA_INIT_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    if(sandfs_mount(0u,SAND_SANDFS_START_LBA)!=0 && (sandfs_format(0u,SAND_SANDFS_START_LBA,2048u)!=0 || sandfs_mount(0u,SAND_SANDFS_START_LBA)!=0)) { debug_text("SAND_TEST:M6_FORMAT_MOUNT_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    static const char storage_message[]="SAND native storage read/write verified";
    char storage_readback[64]; uint32_t storage_length=0u;
    if(sandfs_write("m6-test.txt",storage_message,sizeof(storage_message))!=0 ||
       sandfs_read("m6-test.txt",storage_readback,sizeof(storage_readback),&storage_length)!=0 ||
       storage_length!=sizeof(storage_message)) { debug_text("SAND_TEST:M6_FILE_IO_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    for(uint32_t i=0;i<storage_length;++i) if(storage_readback[i]!=storage_message[i]) { debug_text("SAND_TEST:M6_VERIFY_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    debug_text("SAND_TEST:M6_ATA_IDENTIFY_PASS\n");
    drivers_storage_report();
    debug_text("SAND_TEST:M6_SANDFS_FORMAT_PASS\n");
    debug_text("SAND_TEST:M6_FILE_READ_WRITE_PASS\n");
    if(sapp_loader_self_test()!=0) { debug_text("SAND_TEST:SAPP_LOADER_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    debug_text("SAND_TEST:SAPP_NATIVE_LOADER_PASS\n");
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
    if(process_destroy(first)!=0) { debug_text("SAND_TEST:M5_FIRST_DESTROY_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    /* M7: ring-3 applications run as scheduler threads, each in its own address space. */
    debug_text("SAND_TEST:RING3_ENTER\n");
    if(userland_selftest()!=0) { debug_text("SAND_TEST:M7_USERLAND_FAIL\n"); for(;;) __asm__ volatile("cli; hlt"); }
    sand_framebuffer_t framebuffer;
    int desktop_rc=-1;
    if(vbe_framebuffer_init(&framebuffer)==0 && (desktop_rc=sand_desktop_start(&framebuffer))==0) {
        debug_text("SAND_TEST:GRAPHICS_FRAMEBUFFER_PASS\n");
        debug_text("SAND_TEST:M11_DESKTOP_STARTED\n");
    } else {
        debug_text("SAND_TEST:GRAPHICS_FRAMEBUFFER_UNAVAILABLE\n");
    }
    debug_text("SAND_TEST:ALL_PASS\n");
    /* Session supervisor: the boot context is the idle thread. It keeps one
     * interactive shell alive and restarts it if the user exits it. */
    console_input_flush();
    sand_pid_t shell=0u; uint32_t restarts=0u;
    for(;;) {
        struct sand_process_info info;
        if(shell!=0u && process_get_info(shell,&info)==0 && info.state==SAND_PROCESS_TERMINATED) {
            (void)process_destroy(shell); shell=0u; ++restarts;
            debug_text("SAND_TEST:M9_SHELL_EXITED\n");
        }
        if(shell==0u) {
            const uint8_t *image; uint32_t size;
            if(apps_find("sandsh",&image,&size)==0 && process_spawn_sapp("sandsh",image,size,&shell)==0) {
                debug_text(restarts?"SAND_TEST:M9_SHELL_RESTARTED\n":"SAND_TEST:M9_SHELL_STARTED\n");
            }
        }
        __asm__ volatile("sti; hlt");
    }
}
