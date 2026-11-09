#include "sand/arch/x86/gdt.h"
#define GDT_ENTRIES 6u
#define KERNEL_DATA_SELECTOR 0x10u
#define TSS_SELECTOR 0x28u
struct __attribute__((packed)) gdt_entry { uint16_t limit_low; uint16_t base_low; uint8_t base_middle; uint8_t access; uint8_t granularity; uint8_t base_high; };
struct __attribute__((packed)) gdt_pointer { uint16_t limit; uint32_t base; };
struct __attribute__((packed)) tss32 {
    uint32_t previous, esp0; uint16_t ss0, pad0;
    uint32_t esp1; uint16_t ss1, pad1;
    uint32_t esp2; uint16_t ss2, pad2;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint16_t es, pad3, cs, pad4, ss, pad5, ds, pad6, fs, pad7, gs, pad8, ldt, pad9;
    uint16_t trap, iomap_base;
};
static struct gdt_entry gdt[GDT_ENTRIES];
static struct tss32 tss;
extern void x86_gdt_flush(const struct gdt_pointer *pointer);
extern void x86_tss_load(uint16_t selector);
static void set_entry(uint32_t index,uint32_t base,uint32_t limit,uint8_t access,uint8_t gran) {
    gdt[index].base_low=(uint16_t)base;
    gdt[index].base_middle=(uint8_t)(base>>16);
    gdt[index].base_high=(uint8_t)(base>>24);
    gdt[index].limit_low=(uint16_t)limit;
    gdt[index].granularity=(uint8_t)((limit>>16)&0x0Fu);
    gdt[index].granularity|=(uint8_t)(gran&0xF0u);
    gdt[index].access=access;
}
void gdt_init(void) {
    for(uint32_t i=0;i<GDT_ENTRIES;++i) set_entry(i,0u,0u,0u,0u);
    set_entry(1u,0u,0xFFFFFu,0x9Au,0xCFu);
    set_entry(2u,0u,0xFFFFFu,0x92u,0xCFu);
    set_entry(3u,0u,0xFFFFFu,0xFAu,0xCFu);
    set_entry(4u,0u,0xFFFFFu,0xF2u,0xCFu);
    tss.esp0=0x90000u; tss.ss0=KERNEL_DATA_SELECTOR;
    tss.iomap_base=(uint16_t)sizeof(tss);
    set_entry(5u,(uint32_t)&tss,(uint32_t)sizeof(tss)-1u,0x89u,0x00u);
    struct gdt_pointer pointer={(uint16_t)(sizeof(gdt)-1u),(uint32_t)&gdt[0]};
    x86_gdt_flush(&pointer);
    x86_tss_load(TSS_SELECTOR);
}
void tss_set_kernel_stack(uint32_t stack_top) { tss.esp0=stack_top; }
