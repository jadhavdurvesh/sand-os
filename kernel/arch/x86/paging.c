#include "sand/arch/x86/paging.h"
#include "sand/mm/frame.h"

#define PAGE_PRESENT 0x001u
#define PAGE_WRITABLE 0x002u
#define PAGE_FLAGS_MASK 0xFFFu
#define PAGE_DIRECTORY_ENTRIES 1024u
#define PAGE_TABLE_ENTRIES 1024u
#define IDENTITY_MAPPED_BYTES 0x01000000u
#define IDENTITY_TABLE_COUNT (IDENTITY_MAPPED_BYTES / (SAND_PAGE_SIZE * PAGE_TABLE_ENTRIES))

static uint32_t page_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(4096)));
static uint32_t identity_tables[IDENTITY_TABLE_COUNT][PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static int paging_active;

static void zero_words(uint32_t *memory, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) memory[i] = 0;
}

int paging_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags) {
    if ((virtual_address & (SAND_PAGE_SIZE - 1u)) != 0u ||
        (physical_address & (SAND_PAGE_SIZE - 1u)) != 0u) return -1;

    uint32_t directory_index = virtual_address >> 22;
    uint32_t table_index = (virtual_address >> 12) & 0x3FFu;
    uint32_t *table;

    if ((page_directory[directory_index] & PAGE_PRESENT) == 0u) {
        uint32_t table_physical = frame_alloc();
        if (table_physical == 0u) return -1;
        table = (uint32_t *)table_physical;
        zero_words(table, PAGE_TABLE_ENTRIES);
        page_directory[directory_index] = table_physical | PAGE_PRESENT | PAGE_WRITABLE;
    } else {
        table = (uint32_t *)(page_directory[directory_index] & 0xFFFFF000u);
    }

    if ((table[table_index] & PAGE_PRESENT) != 0u) return -1;
    table[table_index] = (physical_address & 0xFFFFF000u) |
                         (flags & PAGE_FLAGS_MASK) | PAGE_PRESENT;
    if (paging_active) {
        __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    }
    return 0;
}

int paging_init(void) {
    zero_words(page_directory, PAGE_DIRECTORY_ENTRIES);
    for (uint32_t table = 0; table < IDENTITY_TABLE_COUNT; ++table) {
        for (uint32_t page = 0; page < PAGE_TABLE_ENTRIES; ++page) {
            uint32_t address = (table * PAGE_TABLE_ENTRIES + page) * SAND_PAGE_SIZE;
            identity_tables[table][page] = address | PAGE_PRESENT | PAGE_WRITABLE;
        }
        uint32_t table_address = (uint32_t)&identity_tables[table][0];
        page_directory[table] = table_address | PAGE_PRESENT | PAGE_WRITABLE;
    }

    uint32_t directory_address = (uint32_t)&page_directory[0];
    __asm__ volatile ("mov %0, %%cr3" : : "r"(directory_address) : "memory");
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    paging_active = 1;
    return 0;
}

int paging_is_enabled(void) { return paging_active; }
