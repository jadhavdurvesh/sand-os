#include <stdint.h>
#include "sand/arch/x86/vbe.h"
#include "sand/arch/x86/paging.h"

#define VBE_HANDOFF ((volatile uint32_t *)0x6100u)
#define VBE_VIRTUAL_BASE 0xE0000000u
#define PAGE_SIZE 4096u
#define PAGE_PRESENT_WRITE 0x003u

int vbe_framebuffer_init(sand_framebuffer_t *framebuffer) {
    if (framebuffer == 0) return -1;
    uint32_t physical = VBE_HANDOFF[0];
    uint32_t pitch = VBE_HANDOFF[1];
    uint32_t width = VBE_HANDOFF[2];
    uint32_t height = VBE_HANDOFF[3];
    uint32_t bpp = VBE_HANDOFF[4];
    if (physical == 0u || pitch == 0u || width == 0u || height == 0u || bpp != 32u)
        return -1;
    if (pitch < width * 4u || height > 4096u || pitch > 16384u) return -1;
    uint32_t bytes = pitch * height;
    if (bytes / height != pitch || bytes > 0x01000000u) return -1;
    uint32_t aligned_physical = physical & ~(PAGE_SIZE - 1u);
    uint32_t offset = physical - aligned_physical;
    uint32_t mapped_bytes = bytes + offset;
    uint32_t pages = (mapped_bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    if (pages > 4096u || VBE_VIRTUAL_BASE + pages * PAGE_SIZE < VBE_VIRTUAL_BASE) return -1;
    for (uint32_t i = 0; i < pages; ++i) {
        if (paging_map_page(VBE_VIRTUAL_BASE + i * PAGE_SIZE,
                            aligned_physical + i * PAGE_SIZE,
                            PAGE_PRESENT_WRITE) != 0) return -1;
    }
    framebuffer->pixels = (volatile uint32_t *)(VBE_VIRTUAL_BASE + offset);
    framebuffer->width = width;
    framebuffer->height = height;
    framebuffer->pitch_bytes = pitch;
    framebuffer->bits_per_pixel = (uint8_t)bpp;
    return 0;
}
