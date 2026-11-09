#include "sand/mm/frame.h"
#include "sand/mm/memory_map.h"

#define FRAME_COUNT SAND_FRAME_LIMIT
#define BITMAP_BYTES (FRAME_COUNT / 8u)
#define MANAGED_LIMIT ((uint64_t)FRAME_COUNT * SAND_PAGE_SIZE)

static uint8_t frame_bitmap[BITMAP_BYTES];
static uint32_t free_frames;

static void frame_mark_used(uint32_t frame) {
    uint8_t mask = (uint8_t)(1u << (frame & 7u));
    if ((frame_bitmap[frame >> 3] & mask) == 0u) {
        frame_bitmap[frame >> 3] |= mask;
        if (free_frames != 0u) --free_frames;
    }
}
static void frame_mark_free(uint32_t frame) {
    uint8_t mask = (uint8_t)(1u << (frame & 7u));
    if ((frame_bitmap[frame >> 3] & mask) != 0u) {
        frame_bitmap[frame >> 3] &= (uint8_t)~mask;
        ++free_frames;
    }
}
int frame_allocator_init(void) {
    struct sand_memory_map map;
    for (uint32_t i = 0; i < BITMAP_BYTES; ++i) frame_bitmap[i] = 0xFFu;
    free_frames = 0;
    if (memory_map_get(&map) != 0) return -1;

    /* Only expose complete, usable pages below our 16 MiB initial ceiling. */
    for (uint32_t i = 0; i < map.count; ++i) {
        const struct sand_e820_entry *e = &map.entries[i];
        if (e->type != 1u || e->length == 0u) continue;
        uint64_t start = (e->base + SAND_PAGE_SIZE - 1u) & ~((uint64_t)SAND_PAGE_SIZE - 1u);
        uint64_t end = (e->base + e->length) & ~((uint64_t)SAND_PAGE_SIZE - 1u);
        if (start >= MANAGED_LIMIT) continue;
        if (end > MANAGED_LIMIT) end = MANAGED_LIMIT;
        for (uint64_t address = start; address < end; address += SAND_PAGE_SIZE) {
            uint32_t frame = (uint32_t)(address / SAND_PAGE_SIZE);
            frame_mark_free(frame);
        }
    }

    /* Reserve low memory, firmware data, loader, kernel and boot stack. */
    for (uint32_t frame = 0; frame < 256u; ++frame) frame_mark_used(frame);
    return 0;
}
uint32_t frame_alloc(void) {
    for (uint32_t frame = 256u; frame < FRAME_COUNT; ++frame) {
        if ((frame_bitmap[frame >> 3] & (uint8_t)(1u << (frame & 7u))) == 0u) {
            frame_mark_used(frame);
            return frame * SAND_PAGE_SIZE;
        }
    }
    return 0u;
}
int frame_free(uint32_t physical_address) {
    if ((physical_address & (SAND_PAGE_SIZE - 1u)) != 0u) return -1;
    uint32_t frame = physical_address / SAND_PAGE_SIZE;
    if (frame < 256u || frame >= FRAME_COUNT) return -1;
    if ((frame_bitmap[frame >> 3] & (uint8_t)(1u << (frame & 7u))) == 0u) return -1;
    frame_mark_free(frame);
    return 0;
}
uint32_t frame_free_count(void) { return free_frames; }
