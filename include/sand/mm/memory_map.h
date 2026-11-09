#ifndef SAND_MM_MEMORY_MAP_H
#define SAND_MM_MEMORY_MAP_H
#include <stdint.h>
#define SAND_E820_MAP_ADDRESS 0x5000u
#define SAND_E820_MAX_ENTRIES 32u
struct __attribute__((packed)) sand_e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
};
struct sand_memory_map {
    uint16_t count;
    const struct sand_e820_entry *entries;
};
int memory_map_get(struct sand_memory_map *out);
#endif
