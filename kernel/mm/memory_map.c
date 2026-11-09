#include "sand/mm/memory_map.h"
int memory_map_get(struct sand_memory_map *out) {
    if (out == 0) return -1;
    volatile const uint16_t *count_ptr = (volatile const uint16_t *)SAND_E820_MAP_ADDRESS;
    uint16_t count = *count_ptr;
    if (count == 0 || count > SAND_E820_MAX_ENTRIES) return -1;
    out->count = count;
    out->entries = (const struct sand_e820_entry *)(SAND_E820_MAP_ADDRESS + 4u);
    return 0;
}
