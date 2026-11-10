#include "sand/kernel/process.h"
#include "sand/arch/x86/paging.h"
#include "sand/arch/x86/gdt.h"
#include "sand/mm/frame.h"
#define USER_CODE_BASE 0x40000000u
#define USER_STACK_TOP 0xBFFFE000u
#define USER_CODE_FLAGS 0x005u
#define USER_STACK_FLAGS 0x007u
struct process_slot { struct sand_process_info info; uint8_t kernel_stack[8192] __attribute__((aligned(16))); };
static struct process_slot slots[SAND_PROCESS_LIMIT];
static sand_pid_t active_pid;
static sand_pid_t next_pid=1u;

extern uint8_t __user_text_start[];
extern uint8_t __user_text_end[];
extern void x86_enter_user(uint32_t entry,uint32_t stack_top);
int process_system_init(void) {
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i) { slots[i].info.pid=0u; slots[i].info.state=SAND_PROCESS_UNUSED; slots[i].info.page_directory=0u; }
    active_pid=0u; next_pid=1u; return 0;
}
static struct process_slot *find_slot(sand_pid_t pid) {
    if(pid==0u) return 0;
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i) if(slots[i].info.pid==pid && slots[i].info.state!=SAND_PROCESS_UNUSED) return &slots[i];
    return 0;
}
#define ELF32_MAX_PROGRAM_HEADERS 8u
#define ELF32_MAX_IMAGE_PAGES 64u
#define ELF32_PT_LOAD 1u
#define ELF32_ET_EXEC 2u
#define ELF32_EM_386 3u
#define ELF32_PF_X 1u
#define ELF32_PF_W 2u
#define USER_MIN_ADDRESS 0x40000000u
#define USER_MAX_ADDRESS 0xC0000000u
struct __attribute__((packed)) elf32_header {
    uint8_t ident[16]; uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct __attribute__((packed)) elf32_program {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
};
static struct process_slot *allocate_slot(void) {
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i)
        if(slots[i].info.state==SAND_PROCESS_UNUSED) return &slots[i];
    return 0;
}
static void set_process_name(struct process_slot *slot,const char *name) {
    uint32_t n=0u;
    while(name[n] && n<SAND_PROCESS_NAME_MAX-1u) { slot->info.name[n]=name[n]; ++n; }
    slot->info.name[n]='\0';
}
int process_create_elf(const char *name,const void *image,uint32_t image_size,sand_pid_t *pid_out) {
    if(name==0 || image==0 || pid_out==0 || image_size<sizeof(struct elf32_header)) return -1;
    const uint8_t *bytes=(const uint8_t *)image;
    const struct elf32_header *eh=(const struct elf32_header *)bytes;
    if(eh->ident[0]!=0x7Fu || eh->ident[1]!='E' || eh->ident[2]!='L' || eh->ident[3]!='F' ||
       eh->ident[4]!=1u || eh->ident[5]!=1u || eh->ident[6]!=1u ||
       eh->type!=ELF32_ET_EXEC || eh->machine!=ELF32_EM_386 || eh->version!=1u ||
       eh->ehsize!=sizeof(*eh) || eh->phentsize!=sizeof(struct elf32_program) ||
       eh->phnum==0u || eh->phnum>ELF32_MAX_PROGRAM_HEADERS || eh->phoff>image_size ||
       (uint32_t)eh->phnum>(image_size-eh->phoff)/eh->phentsize) return -1;
    struct process_slot *slot=allocate_slot();
    if(slot==0) return -1;
    uint32_t directory=0u, page_count=0u, entry_valid=0u;
    if(paging_space_create(&directory)!=0) return -1;
    const struct elf32_program *ph=(const struct elf32_program *)(bytes+eh->phoff);
    for(uint32_t i=0;i<eh->phnum;++i) {
        if(ph[i].type!=ELF32_PT_LOAD) continue;
        if(ph[i].memsz==0u || ph[i].filesz>ph[i].memsz ||
           ph[i].offset>image_size || ph[i].filesz>image_size-ph[i].offset ||
           ph[i].vaddr<USER_MIN_ADDRESS || ph[i].vaddr>=USER_MAX_ADDRESS ||
           ph[i].memsz>USER_MAX_ADDRESS-ph[i].vaddr ||
           ((ph[i].flags&(ELF32_PF_W|ELF32_PF_X))==(ELF32_PF_W|ELF32_PF_X)) ||
           (ph[i].align>1u && ((ph[i].align&(ph[i].align-1u))!=0u ||
             ((ph[i].vaddr-ph[i].offset)&(ph[i].align-1u))!=0u))) {
            paging_space_destroy(directory); return -1;
        }
        uint32_t segment_end=ph[i].vaddr+ph[i].memsz;
        if(ph[i].vaddr<USER_STACK_TOP && segment_end>USER_STACK_TOP-SAND_PAGE_SIZE) {
            paging_space_destroy(directory); return -1;
        }
        uint32_t first=ph[i].vaddr&0xFFFFF000u;
        uint32_t last=(segment_end-1u)&0xFFFFF000u;
        uint32_t pages=((last-first)/SAND_PAGE_SIZE)+1u;
        if(pages>ELF32_MAX_IMAGE_PAGES || page_count>ELF32_MAX_IMAGE_PAGES-pages) {
            paging_space_destroy(directory); return -1;
        }
        for(uint32_t p=0;p<pages;++p) {
            uint32_t va=first+p*SAND_PAGE_SIZE;
            uint32_t frame=frame_alloc();
            if(frame==0u) { paging_space_destroy(directory); return -1; }
            uint8_t *dst=(uint8_t *)frame;
            for(uint32_t b=0;b<SAND_PAGE_SIZE;++b) dst[b]=0u;
            uint32_t begin=ph[i].vaddr>va?ph[i].vaddr:va;
            uint32_t end=segment_end<va+SAND_PAGE_SIZE?segment_end:va+SAND_PAGE_SIZE;
            uint32_t file_end=ph[i].vaddr+ph[i].filesz;
            uint32_t copy_end=file_end<end?file_end:end;
            for(uint32_t address=begin;address<copy_end;++address)
                dst[address-va]=bytes[ph[i].offset+(address-ph[i].vaddr)];
            uint32_t flags=0x005u;
            if((ph[i].flags&ELF32_PF_W)!=0u) flags|=0x002u;
            if(paging_space_map_page(directory,va,frame,flags)!=0) {
                frame_free(frame); paging_space_destroy(directory); return -1;
            }
            ++page_count;
        }
        if((ph[i].flags&ELF32_PF_X)!=0u && eh->entry>=ph[i].vaddr && eh->entry<segment_end)
            entry_valid=1u;
    }
    if(page_count==0u || entry_valid==0u) { paging_space_destroy(directory); return -1; }
    uint32_t stack=frame_alloc();
    if(stack==0u) { paging_space_destroy(directory); return -1; }
    if(paging_space_map_page(directory,USER_STACK_TOP-SAND_PAGE_SIZE,stack,USER_STACK_FLAGS)!=0) {
        frame_free(stack); paging_space_destroy(directory); return -1;
    }
    sand_pid_t pid=next_pid++;
    if(next_pid==0u) next_pid=1u;
    slot->info.pid=pid; slot->info.state=SAND_PROCESS_READY; slot->info.page_directory=directory;
    slot->info.entry=eh->entry; slot->info.user_stack=USER_STACK_TOP;
    set_process_name(slot,name); *pid_out=pid; return 0;
}
int process_create_test(const char *name,sand_pid_t *pid_out) {
    uint8_t image[sizeof(struct elf32_header)+sizeof(struct elf32_program)+SAND_PAGE_SIZE];
    for(uint32_t i=0;i<sizeof(image);++i) image[i]=0u;
    struct elf32_header *eh=(struct elf32_header *)image;
    eh->ident[0]=0x7Fu; eh->ident[1]='E'; eh->ident[2]='L'; eh->ident[3]='F';
    eh->ident[4]=1u; eh->ident[5]=1u; eh->ident[6]=1u;
    eh->type=ELF32_ET_EXEC; eh->machine=ELF32_EM_386; eh->version=1u;
    eh->entry=USER_CODE_BASE; eh->phoff=sizeof(*eh); eh->ehsize=sizeof(*eh);
    eh->phentsize=sizeof(struct elf32_program); eh->phnum=1u;
    struct elf32_program *ph=(struct elf32_program *)(image+eh->phoff);
    ph->type=ELF32_PT_LOAD; ph->offset=sizeof(*eh)+sizeof(*ph); ph->vaddr=USER_CODE_BASE;
    ph->filesz=(uint32_t)(__user_text_end-__user_text_start); ph->memsz=ph->filesz;
    ph->flags=ELF32_PF_X; ph->align=1u;
    if(ph->filesz==0u || ph->filesz>SAND_PAGE_SIZE) return -1;
    for(uint32_t i=0;i<ph->filesz;++i) image[ph->offset+i]=__user_text_start[i];
    return process_create_elf(name,image,sizeof(image),pid_out);
}
int process_activate(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || slot->info.state==SAND_PROCESS_TERMINATED) return -1;
    if(active_pid!=0u) { struct process_slot *old=find_slot(active_pid); if(old && old->info.state==SAND_PROCESS_RUNNING) old->info.state=SAND_PROCESS_READY; }
    if(paging_activate(slot->info.page_directory)!=0) return -1;
    slot->info.state=SAND_PROCESS_RUNNING; active_pid=pid; return 0;
}
int process_terminate(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || slot->info.state==SAND_PROCESS_TERMINATED) return -1;
    if(active_pid==pid) { if(paging_activate(paging_kernel_directory())!=0) return -1; active_pid=0u; }
    slot->info.state=SAND_PROCESS_TERMINATED; return 0;
}
int process_destroy(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || slot->info.state==SAND_PROCESS_RUNNING || active_pid==pid) return -1;
    if(slot->info.state!=SAND_PROCESS_TERMINATED) slot->info.state=SAND_PROCESS_TERMINATED;
    if(paging_space_destroy(slot->info.page_directory)!=0) return -1;
    /* paging_space_destroy reclaimed the user-mapped code and stack frames. */
    slot->info.page_directory=0u;
    slot->info.state=SAND_PROCESS_UNUSED; slot->info.pid=0u; return 0;
}
int process_get_info(sand_pid_t pid,struct sand_process_info *out) { struct process_slot *slot=find_slot(pid); if(slot==0 || out==0) return -1; *out=slot->info; return 0; }
sand_pid_t process_current(void) { return active_pid; }
void process_launch(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || process_activate(pid)!=0) for(;;) __asm__ volatile("cli; hlt");
    tss_set_kernel_stack((uint32_t)(slot->kernel_stack+sizeof(slot->kernel_stack))); x86_enter_user(slot->info.entry,slot->info.user_stack);
    for(;;) __asm__ volatile("cli; hlt");
}
