#include "sand/arch/x86/paging.h"
#include "sand/mm/frame.h"
#define PAGE_PRESENT 0x001u
#define PAGE_WRITABLE 0x002u
#define PAGE_USER 0x004u
#define PAGE_FLAGS_MASK 0xFFFu
#define PAGE_DIRECTORY_ENTRIES 1024u
#define PAGE_TABLE_ENTRIES 1024u
#define IDENTITY_MAPPED_BYTES 0x01000000u
#define IDENTITY_TABLE_COUNT (IDENTITY_MAPPED_BYTES / (SAND_PAGE_SIZE * PAGE_TABLE_ENTRIES))
#define USER_DIRECTORY_FIRST 256u
#define USER_DIRECTORY_LAST 768u
static uint32_t kernel_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(4096)));
static uint32_t identity_tables[IDENTITY_TABLE_COUNT][PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint32_t active_directory;
static int paging_active;
static void zero_words(uint32_t *memory,uint32_t count) { for(uint32_t i=0;i<count;++i) memory[i]=0u; }
static int map_into(uint32_t *directory,uint32_t virtual_address,uint32_t physical_address,uint32_t flags) {
    if((virtual_address&(SAND_PAGE_SIZE-1u))!=0u || (physical_address&(SAND_PAGE_SIZE-1u))!=0u) return -1;
    uint32_t di=virtual_address>>22, ti=(virtual_address>>12)&0x3FFu;
    uint32_t *table;
    if((directory[di]&PAGE_PRESENT)==0u) {
        uint32_t table_phys=frame_alloc(); if(table_phys==0u) return -1;
        table=(uint32_t *)table_phys; zero_words(table,PAGE_TABLE_ENTRIES);
        directory[di]=table_phys|PAGE_PRESENT|PAGE_WRITABLE|(flags&PAGE_USER);
    } else {
        if((flags&PAGE_USER)!=0u) directory[di]|=PAGE_USER;
        table=(uint32_t *)(directory[di]&0xFFFFF000u);
    }
    if((table[ti]&PAGE_PRESENT)!=0u) return -1;
    table[ti]=(physical_address&0xFFFFF000u)|(flags&PAGE_FLAGS_MASK)|PAGE_PRESENT;
    if(paging_active && directory==(uint32_t *)active_directory)
        __asm__ volatile("invlpg (%0)"::"r"(virtual_address):"memory");
    return 0;
}
int paging_init(void) {
    zero_words(kernel_directory,PAGE_DIRECTORY_ENTRIES);
    for(uint32_t t=0;t<IDENTITY_TABLE_COUNT;++t) {
        for(uint32_t p=0;p<PAGE_TABLE_ENTRIES;++p) {
            uint32_t address=(t*PAGE_TABLE_ENTRIES+p)*SAND_PAGE_SIZE;
            identity_tables[t][p]=address|PAGE_PRESENT|PAGE_WRITABLE;
        }
        kernel_directory[t]=(uint32_t)&identity_tables[t][0]|PAGE_PRESENT|PAGE_WRITABLE;
    }
    active_directory=(uint32_t)&kernel_directory[0];
    __asm__ volatile("mov %0, %%cr3"::"r"(active_directory):"memory");
    uint32_t cr0; __asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    cr0|=0x80000000u; __asm__ volatile("mov %0,%%cr0"::"r"(cr0):"memory");
    paging_active=1; return 0;
}
int paging_map_page(uint32_t va,uint32_t pa,uint32_t flags) { return map_into(kernel_directory,va,pa,flags); }
int paging_space_create(uint32_t *directory_physical) {
    if(directory_physical==0) return -1;
    uint32_t physical=frame_alloc(); if(physical==0u) return -1;
    uint32_t *directory=(uint32_t *)physical; zero_words(directory,PAGE_DIRECTORY_ENTRIES);
    for(uint32_t i=0;i<USER_DIRECTORY_FIRST;++i) directory[i]=kernel_directory[i];
    for(uint32_t i=USER_DIRECTORY_LAST;i<PAGE_DIRECTORY_ENTRIES;++i) directory[i]=kernel_directory[i];
    *directory_physical=physical; return 0;
}
int paging_space_map_page(uint32_t directory_physical,uint32_t va,uint32_t pa,uint32_t flags) {
    if(directory_physical==0u || directory_physical==active_directory) return -1;
    uint32_t di=va>>22;
    if(di<USER_DIRECTORY_FIRST || di>=USER_DIRECTORY_LAST || (flags&PAGE_USER)==0u) return -1;
    return map_into((uint32_t *)directory_physical,va,pa,flags);
}
int paging_space_destroy(uint32_t directory_physical) {
    if(directory_physical==0u || directory_physical==active_directory || directory_physical==(uint32_t)&kernel_directory[0]) return -1;
    uint32_t *directory=(uint32_t *)directory_physical;
    for(uint32_t di=USER_DIRECTORY_FIRST;di<USER_DIRECTORY_LAST;++di) {
        if((directory[di]&PAGE_PRESENT)==0u) continue;
        uint32_t *table=(uint32_t *)(directory[di]&0xFFFFF000u);
        for(uint32_t ti=0;ti<PAGE_TABLE_ENTRIES;++ti) if((table[ti]&PAGE_PRESENT)!=0u) frame_free(table[ti]&0xFFFFF000u);
        frame_free((uint32_t)table); directory[di]=0u;
    }
    return frame_free(directory_physical);
}
int paging_activate(uint32_t directory_physical) {
    if(directory_physical==0u || (directory_physical&0xFFFu)!=0u) return -1;
    active_directory=directory_physical;
    __asm__ volatile("mov %0,%%cr3"::"r"(active_directory):"memory"); return 0;
}
uint32_t paging_kernel_directory(void) { return (uint32_t)&kernel_directory[0]; }
uint32_t paging_current_directory(void) { return active_directory; }
int paging_is_enabled(void) { return paging_active; }
