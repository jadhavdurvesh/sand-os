#include "sand/kernel/process.h"
#include "sand/arch/x86/paging.h"
#include "sand/arch/x86/gdt.h"
#include "sand/mm/frame.h"
#include "sand/kernel/scheduler.h"
#include "sand/abi.h"
#define USER_CODE_BASE 0x40000000u
#define USER_STACK_TOP 0xBFFFE000u
#define USER_CODE_FLAGS 0x005u
#define USER_STACK_FLAGS 0x007u
#define NO_THREAD 0xFFFFFFFFu
struct process_slot { struct sand_process_info info; uint32_t thread_id; uint32_t sleep_deadline; uint8_t kernel_stack[8192] __attribute__((aligned(16))); };
static struct process_slot slots[SAND_PROCESS_LIMIT];
static sand_pid_t active_pid;
static sand_pid_t next_pid=1u;

extern uint8_t __user_text_start[];
extern uint8_t __user_text_end[];
extern void x86_enter_user(uint32_t entry,uint32_t stack_top);
int process_system_init(void) {
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i) { slots[i].info.pid=0u; slots[i].info.state=SAND_PROCESS_UNUSED; slots[i].info.page_directory=0u; slots[i].thread_id=NO_THREAD; slots[i].sleep_deadline=0u; }
    active_pid=0u; next_pid=1u;
    scheduler_set_switch_hook(process_scheduler_hook);
    return 0;
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
#define SAPP_HEADER_SIZE 64u
#define SAPP_SEGMENT_SIZE 32u
#define SAPP_RELOC_SIZE 12u
#define SAPP_ARCH_X86_32 1u
#define SAPP_RELOC_ABS32 1u
struct __attribute__((packed)) sapp_header {
    uint8_t magic[4]; uint16_t version, architecture;
    uint32_t header_size, flags, entry, segment_offset, segment_count;
    uint32_t metadata_offset, metadata_size, file_size;
    uint32_t signature_offset, signature_size;
    uint32_t relocation_offset, relocation_count;
    uint8_t reserved[8];
};
struct __attribute__((packed)) sapp_segment {
    uint32_t vaddr, memsz, file_offset, file_size, permissions, alignment;
    uint8_t reserved[8];
};
struct __attribute__((packed)) sapp_relocation {
    uint32_t target, type, addend;
};
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
    slot->info.pid=pid; slot->info.parent_pid=active_pid; slot->info.parent_thread_id=scheduler_current_id(); slot->info.state=SAND_PROCESS_READY; slot->info.page_directory=directory; slot->thread_id=NO_THREAD; slot->sleep_deadline=0u;
    slot->info.entry=eh->entry; slot->info.user_stack=USER_STACK_TOP;
    set_process_name(slot,name); *pid_out=pid; return 0;
}
static int sapp_range_ok(uint32_t offset,uint32_t length,uint32_t total) {
    return offset<=total && length<=total-offset;
}
static int sapp_ranges_overlap(uint32_t a,uint32_t alen,uint32_t b,uint32_t blen) {
    return a<b+blen && b<a+alen;
}
int process_create_sapp(const char *name,const void *image,uint32_t image_size,sand_pid_t *pid_out) {
    if(name==0 || image==0 || pid_out==0 || image_size<SAPP_HEADER_SIZE) return -1;
    const uint8_t *bytes=(const uint8_t *)image;
    const struct sapp_header *h=(const struct sapp_header *)bytes;
    if(h->magic[0]!='S' || h->magic[1]!='A' || h->magic[2]!='P' || h->magic[3]!='P' ||
       h->version!=1u || h->architecture!=SAPP_ARCH_X86_32 ||
       h->header_size!=SAPP_HEADER_SIZE || h->flags!=0u || h->file_size!=image_size ||
       h->reserved[0] || h->reserved[1] || h->reserved[2] || h->reserved[3] ||
       h->reserved[4] || h->reserved[5] || h->reserved[6] || h->reserved[7] ||
       h->segment_count==0u || h->segment_count>8u ||
       h->signature_offset!=0u || h->signature_size!=0u ||
       !sapp_range_ok(h->segment_offset,h->segment_count*SAPP_SEGMENT_SIZE,image_size) ||
       !sapp_range_ok(h->metadata_offset,h->metadata_size,image_size) ||
       h->segment_offset<SAPP_HEADER_SIZE ||
       h->metadata_offset<h->segment_offset+h->segment_count*SAPP_SEGMENT_SIZE ||
       h->metadata_size==0u || h->metadata_size>4096u ||
       h->relocation_count>256u ||
       (h->relocation_count && (!sapp_range_ok(h->relocation_offset,h->relocation_count*SAPP_RELOC_SIZE,image_size) ||
          h->relocation_offset<h->metadata_offset+h->metadata_size)) ||
       (!h->relocation_count && h->relocation_offset!=0u)) return -1;
    const struct sapp_segment *segments=(const struct sapp_segment *)(bytes+h->segment_offset);
    const struct sapp_relocation *relocs=(const struct sapp_relocation *)(bytes+h->relocation_offset);
    uint32_t entry_valid=0u,total_pages=0u;
    for(uint32_t i=0;i<h->segment_count;++i) {
        const struct sapp_segment *s=&segments[i];
        if(s->memsz==0u || s->file_size>s->memsz || s->vaddr<USER_MIN_ADDRESS ||
           s->vaddr>=USER_MAX_ADDRESS || s->memsz>USER_MAX_ADDRESS-s->vaddr ||
           !sapp_range_ok(s->file_offset,s->file_size,image_size) ||
           s->file_offset<(h->relocation_count ? h->relocation_offset+h->relocation_count*SAPP_RELOC_SIZE : h->metadata_offset+h->metadata_size) ||
           (s->permissions&~7u)!=0u || (s->permissions&1u)==0u ||
           (s->permissions&6u)==6u || s->alignment<SAND_PAGE_SIZE ||
           (s->alignment&(s->alignment-1u))!=0u || (s->vaddr&(s->alignment-1u))!=0u ||
           s->reserved[0] || s->reserved[1] || s->reserved[2] || s->reserved[3] ||
           s->reserved[4] || s->reserved[5] || s->reserved[6] || s->reserved[7]) return -1;
        if(s->vaddr<USER_STACK_TOP && s->vaddr+s->memsz>USER_STACK_TOP-SAND_PAGE_SIZE) return -1;
        uint32_t pages=(((s->vaddr+s->memsz-1u)&0xFFFFF000u)-(s->vaddr&0xFFFFF000u))/SAND_PAGE_SIZE+1u;
        if(pages>ELF32_MAX_IMAGE_PAGES || total_pages>ELF32_MAX_IMAGE_PAGES-pages) return -1;
        total_pages+=pages;
        if((s->permissions&4u) && h->entry>=s->vaddr && h->entry<s->vaddr+s->memsz) entry_valid=1u;
        for(uint32_t j=0;j<i;++j) {
            if(sapp_ranges_overlap(s->vaddr,s->memsz,segments[j].vaddr,segments[j].memsz) ||
               sapp_ranges_overlap(s->file_offset,s->file_size,segments[j].file_offset,segments[j].file_size)) return -1;
        }
    }
    if(!entry_valid || total_pages==0u) return -1;
    for(uint32_t r=0;r<h->relocation_count;++r) {
        const struct sapp_relocation *rel=&relocs[r];
        uint32_t found=0u;
        if(rel->type!=SAPP_RELOC_ABS32 || !sapp_range_ok(h->relocation_offset+r*SAPP_RELOC_SIZE,SAPP_RELOC_SIZE,image_size)) return -1;
        for(uint32_t i=0;i<h->segment_count;++i)
            if((segments[i].permissions&2u) && rel->target>=segments[i].vaddr &&
               rel->target<=segments[i].vaddr+segments[i].memsz-4u) found=1u;
        if(!found || (rel->target&0xFFFu)>0xFFCu) return -1;
    }
    struct process_slot *slot=allocate_slot();
    if(slot==0) return -1;
    uint32_t directory=0u;
    if(paging_space_create(&directory)!=0) return -1;
    for(uint32_t i=0;i<h->segment_count;++i) {
        const struct sapp_segment *s=&segments[i];
        uint32_t first=s->vaddr&0xFFFFF000u;
        uint32_t last=(s->vaddr+s->memsz-1u)&0xFFFFF000u;
        for(uint32_t va=first;va<=last;va+=SAND_PAGE_SIZE) {
            uint32_t frame=frame_alloc();
            if(frame==0u) { paging_space_destroy(directory); return -1; }
            uint8_t *dst=(uint8_t *)frame;
            for(uint32_t b=0;b<SAND_PAGE_SIZE;++b) dst[b]=0u;
            uint32_t begin=s->vaddr>va?s->vaddr:va;
            uint32_t end=(s->vaddr+s->memsz)<(va+SAND_PAGE_SIZE)?(s->vaddr+s->memsz):(va+SAND_PAGE_SIZE);
            uint32_t file_end=s->vaddr+s->file_size;
            uint32_t copy_end=file_end<end?file_end:end;
            for(uint32_t a=begin;a<copy_end;++a) dst[a-va]=bytes[s->file_offset+(a-s->vaddr)];
            for(uint32_t r=0;r<h->relocation_count;++r) {
                const struct sapp_relocation *rel=&relocs[r];
                if(rel->target>=va && rel->target+4u<=va+SAND_PAGE_SIZE) {
                    uint32_t value=rel->addend;
                    dst[rel->target-va]=(uint8_t)value;
                    dst[rel->target-va+1u]=(uint8_t)(value>>8);
                    dst[rel->target-va+2u]=(uint8_t)(value>>16);
                    dst[rel->target-va+3u]=(uint8_t)(value>>24);
                }
            }
            uint32_t map_flags=0x005u;
            if(s->permissions&2u) map_flags|=0x002u;
            if(paging_space_map_page(directory,va,frame,map_flags)!=0) {
                frame_free(frame); paging_space_destroy(directory); return -1;
            }
        }
    }
    uint32_t stack=frame_alloc();
    if(stack==0u) { paging_space_destroy(directory); return -1; }
    if(paging_space_map_page(directory,USER_STACK_TOP-SAND_PAGE_SIZE,stack,USER_STACK_FLAGS)!=0) {
        frame_free(stack); paging_space_destroy(directory); return -1;
    }
    sand_pid_t pid=next_pid++; if(next_pid==0u) next_pid=1u;
    slot->info.pid=pid; slot->info.parent_pid=active_pid;
    slot->thread_id=NO_THREAD; slot->sleep_deadline=0u;
    slot->info.parent_thread_id=scheduler_current_id(); slot->info.state=SAND_PROCESS_READY;
    slot->info.page_directory=directory; slot->info.entry=h->entry;
    slot->info.user_stack=USER_STACK_TOP; set_process_name(slot,name); *pid_out=pid;
    return 0;
}
int process_create_test(const char *name,sand_pid_t *pid_out) {
    /* The generated test ELF is larger than THREAD_STACK_SIZE. Keep it in BSS,
       not on a scheduler worker stack, or process tests corrupt kernel state. */
    static uint8_t image[sizeof(struct elf32_header)+sizeof(struct elf32_program)+SAND_PAGE_SIZE];
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
    tss_set_kernel_stack((uint32_t)(slot->kernel_stack+sizeof(slot->kernel_stack)));
    slot->info.state=SAND_PROCESS_RUNNING; active_pid=pid; return 0;
}
int process_exit(sand_pid_t pid,int32_t exit_code) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || slot->info.state==SAND_PROCESS_TERMINATED) return -1;
    if(active_pid==pid) {
        if(paging_activate(paging_kernel_directory())!=0) return -1;
        tss_set_kernel_stack(0x90000u);
        active_pid=0u;
    }
    slot->info.exit_code=exit_code; slot->info.state=SAND_PROCESS_TERMINATED; (void)scheduler_wake_one(slot); return 0;
}
int process_terminate(sand_pid_t pid) { return process_exit(pid,0); }
int process_wait(sand_pid_t pid,int32_t *exit_code) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || exit_code==0) return -1;
    if(slot->info.parent_pid!=active_pid ||
       slot->info.parent_thread_id!=scheduler_current_id()) return -1;
    for(;;) {
        uint32_t flags;
        __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
        if(slot->info.state==SAND_PROCESS_TERMINATED) {
            *exit_code=slot->info.exit_code;
            if((flags&0x200u)!=0u) __asm__ volatile("sti" : : : "memory");
            return 1;
        }
        /* The boot/idle context is not a schedulable parent thread. */
        if(scheduler_current_id()==0u) {
            if((flags&0x200u)!=0u) __asm__ volatile("sti" : : : "memory");
            return 0;
        }
        scheduler_block_current(slot);
        if((flags&0x200u)!=0u) __asm__ volatile("sti" : : : "memory");
    }
}
int process_destroy(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || slot->info.state==SAND_PROCESS_RUNNING || active_pid==pid) return -1;
    if(slot->info.state!=SAND_PROCESS_TERMINATED) slot->info.state=SAND_PROCESS_TERMINATED;
    if(paging_space_destroy(slot->info.page_directory)!=0) return -1;
    /* paging_space_destroy reclaimed the user-mapped code and stack frames. */
    slot->info.page_directory=0u;
    slot->info.state=SAND_PROCESS_UNUSED; slot->info.pid=0u; slot->info.parent_pid=0u; slot->info.parent_thread_id=0u; slot->thread_id=NO_THREAD; slot->sleep_deadline=0u; return 0;
}
int process_get_info(sand_pid_t pid,struct sand_process_info *out) { struct process_slot *slot=find_slot(pid); if(slot==0 || out==0) return -1; *out=slot->info; return 0; }
sand_pid_t process_current(void) { return active_pid; }

/* ---- M7: scheduler-controlled user processes ------------------------- */
static uint32_t proc_irq_save(void) { uint32_t f; __asm__ volatile("pushfl; popl %0; cli":"=r"(f)::"memory"); return f; }
static void proc_irq_restore(uint32_t f) { if((f&0x200u)!=0u) __asm__ volatile("sti":::"memory"); }
static struct process_slot *slot_for_thread(uint32_t tid) {
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i)
        if(slots[i].info.pid!=0u && slots[i].thread_id==tid &&
           (slots[i].info.state==SAND_PROCESS_READY || slots[i].info.state==SAND_PROCESS_RUNNING)) return &slots[i];
    return 0;
}
/* Runs with interrupts off from inside scheduler_reschedule. */
void process_scheduler_hook(uint32_t thread_id) {
    struct process_slot *next=slot_for_thread(thread_id);
    if(active_pid!=0u) {
        struct process_slot *old=find_slot(active_pid);
        if(old && old->info.state==SAND_PROCESS_RUNNING) old->info.state=SAND_PROCESS_READY;
    }
    if(next) {
        (void)paging_activate(next->info.page_directory);
        tss_set_kernel_stack((uint32_t)(next->kernel_stack+sizeof(next->kernel_stack)));
        next->info.state=SAND_PROCESS_RUNNING; active_pid=next->info.pid;
    } else {
        (void)paging_activate(paging_kernel_directory());
        tss_set_kernel_stack(0x90000u); active_pid=0u;
    }
}
static void process_thread_entry(void *argument) {
    struct process_slot *slot=find_slot((sand_pid_t)(uint32_t)argument);
    if(slot==0) return;
    x86_enter_user(slot->info.entry,slot->info.user_stack);
}
int process_spawn_sapp(const char *name,const void *image,uint32_t image_size,sand_pid_t *pid_out) {
    sand_pid_t pid=0u;
    if(process_create_sapp(name,image,image_size,&pid)!=0) return -1;
    struct process_slot *slot=find_slot(pid);
    uint32_t flags=proc_irq_save();
    int tid=slot?scheduler_create(process_thread_entry,(void *)pid):-1;
    if(tid<0) { proc_irq_restore(flags); (void)process_destroy(pid); return -1; }
    slot->thread_id=(uint32_t)tid;
    proc_irq_restore(flags);
    *pid_out=pid; return 0;
}
/* Reap terminated children of a dying process; orphan the live ones. */
static void release_children(sand_pid_t parent) {
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i) {
        struct process_slot *c=&slots[i];
        if(c->info.pid==0u || c->info.parent_pid!=parent) continue;
        if(c->info.state==SAND_PROCESS_TERMINATED) (void)process_destroy(c->info.pid);
        else c->info.parent_pid=SAND_PID_ORPHAN;
    }
}
uint32_t process_exit_current(int32_t exit_code,uint32_t saved_stack) {
    struct process_slot *slot=find_slot(active_pid);
    if(slot==0) return saved_stack;
    slot->info.exit_code=exit_code; slot->info.state=SAND_PROCESS_TERMINATED;
    sand_pid_t pid=slot->info.pid;
    release_children(pid);
    (void)scheduler_wake_all(slot);
    uint32_t next=scheduler_exit_current(saved_stack);
    /* Orphans have no parent to reap them; free them once off their stack. */
    if(slot->info.parent_pid==SAND_PID_ORPHAN) (void)process_destroy(pid);
    return next;
}
uint32_t process_fault_current(uint32_t vector,uint32_t saved_stack) {
    return process_exit_current(SAND_EXIT_FAULT_BASE-(int32_t)vector,saved_stack);
}
int process_poll_child(sand_pid_t pid,int32_t *exit_code) {
    struct process_slot *slot=find_slot(pid);
    if(slot==0 || slot->info.parent_pid!=active_pid || active_pid==0u) return -1;
    if(slot->info.state!=SAND_PROCESS_TERMINATED) return 0;
    if(exit_code) *exit_code=slot->info.exit_code;
    return 1;
}
const void *process_wait_object(sand_pid_t pid) { return find_slot(pid); }
uint32_t *process_sleep_deadline(void) {
    struct process_slot *slot=find_slot(active_pid);
    return slot?&slot->sleep_deadline:0;
}
int process_list_at(uint32_t ordinal,struct sand_process_info *out) {
    if(out==0) return -1;
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i) {
        if(slots[i].info.pid==0u || slots[i].info.state==SAND_PROCESS_UNUSED) continue;
        if(ordinal--==0u) { *out=slots[i].info; return 0; }
    }
    return -1;
}
uint32_t process_live_count(void) {
    uint32_t n=0u;
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i) if(slots[i].info.pid!=0u && slots[i].info.state!=SAND_PROCESS_UNUSED) ++n;
    return n;
}
void process_launch(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || process_activate(pid)!=0) for(;;) __asm__ volatile("cli; hlt");
    tss_set_kernel_stack((uint32_t)(slot->kernel_stack+sizeof(slot->kernel_stack))); x86_enter_user(slot->info.entry,slot->info.user_stack);
    for(;;) __asm__ volatile("cli; hlt");
}

/* Spawn on behalf of the desktop: the child has no parent process, so it is reaped
 * automatically when it exits (same path as an orphan). */
int process_spawn_detached(const char *name, const void *image, uint32_t image_size, sand_pid_t *pid_out) {
    sand_pid_t pid = 0u;
    if(process_spawn_sapp(name,image,image_size,&pid)!=0) return -1;
    struct process_slot *slot=find_slot(pid);
    if(slot!=0) slot->info.parent_pid=SAND_PID_ORPHAN;
    if(pid_out!=0) *pid_out=pid;
    return 0;
}
