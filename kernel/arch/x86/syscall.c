#include "sand/arch/x86/syscall.h"
#include "sand/arch/x86/idt.h"
#include "sand/arch/x86/paging.h"
#include "sand/arch/x86/timer.h"
#include "sand/console/console.h"
#include "sand/drivers/device.h"
#include "sand/fs/sandfs.h"
#include "sand/graphics/desktop.h"
#include "sand/kernel/process.h"
#include "sand/kernel/scheduler.h"
#include "sand/mm/frame.h"
#include "sand/mm/heap.h"
#include "sand/proc/apps.h"
#include "sand/abi.h"

/* Layout of the frame built by x86_syscall_entry: PUSHAL then the CPU's
 * ring-3 interrupt frame. */
enum { R_EDI, R_ESI, R_EBP, R_ESP, R_EBX, R_EDX, R_ECX, R_EAX, R_EIP, R_CS, R_EFLAGS };
#define INT80_LENGTH 2u
#define WRITE_CHUNK 128u
#define WRITE_LIMIT 4096u
#define NAME_LIMIT 32u

static inline void debug_char(char c) { __asm__ volatile ("outb %0, $0xE9" : : "a"(c)); }
static void debug_text(const char *text) { while(*text) debug_char(*text++); }
extern void x86_syscall_entry(void);
void syscall_init(void) { idt_set_syscall(x86_syscall_entry); idt_load(); }

/* Syscalls run with interrupts disabled on a single CPU, so shared scratch
 * buffers are safe and keep the small per-process kernel stack shallow. */
static uint8_t scratch[512];
static char name_buffer[NAME_LIMIT];

static int user_ok(uint32_t address,uint32_t length,int writable) {
    return paging_user_range_ok(paging_current_directory(),address,length,writable);
}
static int copy_in(void *dst,uint32_t src,uint32_t length) {
    if(length==0u) return 0;
    if(!user_ok(src,length,0)) return -1;
    uint8_t *d=(uint8_t *)dst; const uint8_t *s=(const uint8_t *)src;
    for(uint32_t i=0;i<length;++i) d[i]=s[i];
    return 0;
}
static int copy_out(uint32_t dst,const void *src,uint32_t length) {
    if(length==0u) return 0;
    if(!user_ok(dst,length,1)) return -1;
    uint8_t *d=(uint8_t *)dst; const uint8_t *s=(const uint8_t *)src;
    for(uint32_t i=0;i<length;++i) d[i]=s[i];
    return 0;
}
/* Copy a NUL-terminated name page-safely; fails when too long or unmapped. */
static int copy_name(uint32_t src) {
    for(uint32_t i=0;i<NAME_LIMIT;++i) {
        if(!user_ok(src+i,1u,0)) return -1;
        char c=*(const char *)(src+i);
        name_buffer[i]=c;
        if(c=='\0') return i==0u?-1:0;
    }
    return -1;
}

static uint32_t retry_blocked(uint32_t *r,uint32_t frame,const void *object) {
    r[R_EIP]-=INT80_LENGTH;               /* re-execute int 0x80 once woken */
    return scheduler_block_current_irq(object,frame);
}
static uint32_t retry_yield(uint32_t *r,uint32_t frame) {
    r[R_EIP]-=INT80_LENGTH;
    return scheduler_reschedule(frame);
}

uint32_t syscall_dispatch(uint32_t frame) {
    uint32_t *r=(uint32_t *)frame;
    if((r[R_CS]&3u)!=3u || process_current()==0u) { r[R_EAX]=(uint32_t)SAND_E_FAIL; return frame; }
    uint32_t a=r[R_EBX], b=r[R_ECX], c=r[R_EDX];
    int32_t result=SAND_E_NOSYS;
    switch(r[R_EAX]) {
    case SAND_SYS_EXIT:
        if(a==SAND_ABI_TOKEN) {
            debug_text("SAND_TEST:M5_USER_PROCESS_SYSCALL_PASS\n");
            debug_text("SAND_TEST:RING3_SYSCALL_PASS\n");
        }
        return process_exit_current((int32_t)a,frame);
    case SAND_SYS_WRITE: {
        if(b>WRITE_LIMIT) { result=SAND_E_FAIL; break; }
        uint32_t done=0u; result=0;
        while(done<b) {
            uint32_t n=b-done>WRITE_CHUNK?WRITE_CHUNK:b-done;
            if(copy_in(scratch,a+done,n)!=0) { result=SAND_E_FAULT; break; }
            console_write((const char *)scratch,n); done+=n;
        }
        if(result==0) result=(int32_t)b;
        break; }
    case SAND_SYS_GET_TICKS: result=(int32_t)timer_ticks(); break;
    case SAND_SYS_GETPID: result=(int32_t)process_current(); break;
    case SAND_SYS_YIELD: r[R_EAX]=0u; return scheduler_reschedule(frame);
    case SAND_SYS_SLEEP_MS: {
        uint32_t *deadline=process_sleep_deadline();
        if(deadline==0) { result=SAND_E_FAIL; break; }
        uint32_t now=timer_ticks();
        if(*deadline==0u) {
            uint32_t ticks=(a*SAND_TICK_HZ+999u)/1000u;
            if(ticks==0u) { result=0; break; }
            *deadline=now+ticks; if(*deadline==0u) *deadline=1u;
        }
        if((int32_t)(now-*deadline)>=0) { *deadline=0u; result=0; break; }
        return retry_yield(r,frame); }
    case SAND_SYS_READ: {
        if(b==0u || b>sizeof(scratch)) { result=SAND_E_FAIL; break; }
        if(!user_ok(a,b,1)) { result=SAND_E_FAULT; break; }
        uint32_t n=console_input_read(scratch,b);
        if(n==0u) return retry_blocked(r,frame,console_input_wait_object());
        (void)copy_out(a,scratch,n); result=(int32_t)n; break; }
    case SAND_SYS_FS_READ: {
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        if(b==0u || c==0u || !user_ok(b,c,1)) { result=SAND_E_FAULT; break; }
        uint32_t length=0u;
        if(sandfs_read(name_buffer,scratch,c>sizeof(scratch)?(uint32_t)sizeof(scratch):c,&length)!=0) { result=SAND_E_NOENT; break; }
        (void)copy_out(b,scratch,length); result=(int32_t)length; break; }
    case SAND_SYS_FS_WRITE: {
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        if(c==0u || c>sizeof(scratch)) { result=SAND_E_FAIL; break; }
        if(copy_in(scratch,b,c)!=0) { result=SAND_E_FAULT; break; }
        result=sandfs_write(name_buffer,scratch,c)==0?0:SAND_E_FAIL; break; }
    case SAND_SYS_FS_LIST: {
        struct sand_dirent entry; uint32_t length=0u;
        if(!user_ok(b,sizeof(entry),1)) { result=SAND_E_FAULT; break; }
        if(sandfs_list(a,entry.name,sizeof(entry.name),&length)!=0) { result=SAND_E_NOENT; break; }
        entry.length=length; (void)copy_out(b,&entry,sizeof(entry)); result=0; break; }
    case SAND_SYS_FS_REMOVE:
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        result=sandfs_remove(name_buffer)==0?0:SAND_E_NOENT; break;
    case SAND_SYS_SPAWN: {
        const uint8_t *image; uint32_t size; sand_pid_t pid=0u;
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        if(apps_find(name_buffer,&image,&size)!=0) { result=SAND_E_NOENT; break; }
        result=process_spawn_sapp(name_buffer,image,size,&pid)==0?(int32_t)pid:SAND_E_FAIL; break; }
    case SAND_SYS_WAIT: {
        int32_t status=0;
        if(b!=0u && !user_ok(b,sizeof(int32_t),1)) { result=SAND_E_FAULT; break; }
        int polled=process_poll_child(a,&status);
        if(polled<0) { result=SAND_E_NOENT; break; }
        if(polled==0) return retry_blocked(r,frame,process_wait_object(a));
        if(b!=0u) (void)copy_out(b,&status,sizeof(status));
        (void)process_destroy(a); result=0; break; }
    case SAND_SYS_WIN_CREATE: {
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        result=sand_desktop_win_create(name_buffer,(int32_t)(b>>16),(int32_t)(b&0xFFFFu)); break; }
    case SAND_SYS_WIN_DRAW: {
        if(c==0u || c>SAND_WIN_MAX_DRAW) { result=SAND_E_FAIL; break; }
        if(!user_ok(b,c*(uint32_t)sizeof(struct sand_draw),0)) { result=SAND_E_FAULT; break; }
        result=sand_desktop_win_draw((int32_t)a,(const struct sand_draw *)b,c); break; }
    case SAND_SYS_WIN_EVENT: {
        struct sand_event ev;
        if(!user_ok(b,sizeof(ev),1)) { result=SAND_E_FAULT; break; }
        result=sand_desktop_win_event((int32_t)a,&ev);
        if(result==1) (void)copy_out(b,&ev,sizeof(ev));
        break; }
    case SAND_SYS_WIN_CLOSE: result=sand_desktop_win_close((int32_t)a); break;
    case SAND_SYS_SYSINFO: {
        struct sand_sysinfo info;
        info.abi_version=SAND_ABI_VERSION; info.ticks=timer_ticks(); info.tick_hz=SAND_TICK_HZ;
        info.free_frames=frame_free_count(); info.total_frames=SAND_FRAME_LIMIT;
        info.heap_used=kheap_used(); info.context_switches=scheduler_switch_count();
        info.process_count=process_live_count();
        result=copy_out(a,&info,sizeof(info))==0?0:SAND_E_FAULT; break; }
    case SAND_SYS_PROC_LIST: {
        struct sand_process_info pi; struct sand_procinfo out;
        if(!user_ok(b,sizeof(out),1)) { result=SAND_E_FAULT; break; }
        if(process_list_at(a,&pi)!=0) { result=SAND_E_NOENT; break; }
        out.pid=pi.pid; out.parent_pid=pi.parent_pid; out.state=(uint32_t)pi.state; out.exit_code=pi.exit_code;
        for(uint32_t i=0;i<sizeof(out.name);++i) out.name[i]=i<SAND_PROCESS_NAME_MAX?pi.name[i]:'\0';
        out.name[sizeof(out.name)-1u]='\0';
        (void)copy_out(b,&out,sizeof(out)); result=0; break; }
    case SAND_SYS_APP_LIST: {
        struct sand_dirent entry; const char *name;
        if(!user_ok(b,sizeof(entry),1)) { result=SAND_E_FAULT; break; }
        if(apps_name_at(a,&name)!=0) { result=SAND_E_NOENT; break; }
        uint32_t i=0u; while(name[i] && i<SAND_NAME_MAX-1u) { entry.name[i]=name[i]; ++i; }
        while(i<SAND_NAME_MAX) entry.name[i++]='\0';
        entry.length=0u; (void)copy_out(b,&entry,sizeof(entry)); result=0; break; }
    case SAND_SYS_DEVICE_LIST: {
        struct sand_devinfo out; const struct sand_device *d=device_get(a);
        if(!user_ok(b,sizeof(out),1)) { result=SAND_E_FAULT; break; }
        if(d==0) { result=SAND_E_NOENT; break; }
        for(uint32_t i=0;i<sizeof(out.name);++i) out.name[i]='\0';
        for(uint32_t i=0;i<sizeof(out.kind);++i) out.kind[i]='\0';
        for(uint32_t i=0;i<sizeof(out.driver);++i) out.driver[i]='\0';
        for(uint32_t i=0;d->name[i] && i<sizeof(out.name)-1u;++i) out.name[i]=d->name[i];
        for(uint32_t i=0;d->kind[i] && i<sizeof(out.kind)-1u;++i) out.kind[i]=d->kind[i];
        for(uint32_t i=0;d->driver[i] && i<sizeof(out.driver)-1u;++i) out.driver[i]=d->driver[i];
        out.status=(uint32_t)d->status; (void)copy_out(b,&out,sizeof(out)); result=0; break; }
    default: result=SAND_E_NOSYS; break;
    }
    r[R_EAX]=(uint32_t)result;
    return frame;
}
