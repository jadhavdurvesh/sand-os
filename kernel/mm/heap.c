#include "sand/mm/heap.h"
#include "sand/arch/x86/paging.h"
#include "sand/mm/frame.h"

#define KHEAP_BASE 0x01000000u
#define KHEAP_LIMIT 0x01C00000u

static uint32_t heap_current = KHEAP_BASE;
static uint32_t heap_mapped_end = KHEAP_BASE;

int kheap_init(void) {
    if (!paging_is_enabled()) return -1;
    heap_current = KHEAP_BASE;
    heap_mapped_end = KHEAP_BASE;
    /* Pre-create the heap page table so every later address space shares it. */
    for (uint32_t va = KHEAP_BASE; va < KHEAP_LIMIT; va += 0x400000u)
        if (paging_reserve_table(va) != 0) return -1;
    return 0;
}

void *kmalloc(uint32_t size) {
    if (size == 0u) return 0;
    if (size > 0xFFFFFFF7u) return 0;
    uint32_t aligned_size = (size + 7u) & ~7u;
    if (aligned_size > KHEAP_LIMIT - heap_current) return 0;

    uint32_t required_end = heap_current + aligned_size;
    while (heap_mapped_end < required_end) {
        uint32_t physical = frame_alloc();
        if (physical == 0u) return 0;
        if (paging_map_page(heap_mapped_end, physical, 0x003u) != 0) {
            (void)frame_free(physical);
            return 0;
        }
        heap_mapped_end += SAND_PAGE_SIZE;
    }

    uint32_t result = heap_current;
    heap_current = required_end;
    return (void *)result;
}

uint32_t kheap_used(void) { return heap_current - KHEAP_BASE; }
