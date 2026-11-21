#include "sand/arch/x86/syscall.h"
#include "sand/arch/x86/idt.h"
#include "sand/arch/x86/paging.h"
#include "sand/arch/x86/timer.h"
#include "sand/console/console.h"
#include "sand/drivers/device.h"
#include "sand/fs/mimir.h"
#include "sand/ipc/tideway.h"
#include "sand/net/driftnet.h"
#include "sand/veyra/subsystems.h"
#include "sand/graphics/desktop.h"
#include "sand/kernel/process.h"
#include "sand/kernel/scheduler.h"
#include "sand/mm/frame.h"
#include "sand/mm/heap.h"
#include "sand/proc/apps.h"
#include "sand/proc/diskapps.h"
#include "sand/proc/appinstall.h"
#include "sand/arch/x86/rtc.h"
#include "sand/abi.h"
#include "sand/kernel/perms.h"
#include "sand/security/launch_gate.h"

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

/* Syscalls run with interrupts disabled on a single CPU, so shared scratch
 * buffers are safe and keep the small per-process kernel stack shallow. */
static uint8_t scratch[512];
/* Whole-file FS transfers: the 512-byte scratch serves small ones, a lazily allocated 64 KiB buffer the rest. */
static uint8_t *fs_big;
static uint8_t *fs_buffer(uint32_t need) {
    if(need<=sizeof(scratch)) return scratch;
    if(fs_big==0) fs_big=(uint8_t *)kmalloc(MIMIR_FILE_MAX);
    return fs_big;
}
void syscall_init(void) { idt_set_syscall(x86_syscall_entry); idt_load(); (void)fs_buffer(sizeof(scratch)+1u); /* allocate the 64 KiB FS buffer up front: frame accounting stays stable */ }
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

/* Capability needed for a syscall (0 = none). Reads of public state (ticks, pid, sysinfo, lists, events,
 * setting_get, close) stay open; creating windows, writing settings, files, spawning and the console are gated. */
static uint32_t required_permission(uint32_t number,uint32_t a) {
    (void)a;
    switch(number) {
    case SAND_SYS_WRITE: return SAND_PERM_CONSOLE_WRITE;
    case SAND_SYS_READ: return SAND_PERM_CONSOLE_READ;
    case SAND_SYS_FS_READ: case SAND_SYS_FS_LIST: case SAND_SYS_FS_STAT: case SAND_SYS_FS_HISTORY: case SAND_SYS_FS_SEAL: case SAND_SYS_FS_SCRUB: return SAND_PERM_FS_READ;
    case SAND_SYS_FS_WRITE: case SAND_SYS_FS_REMOVE: case SAND_SYS_FS_ROLLBACK: case SAND_SYS_FS_EBB: return SAND_PERM_FS_WRITE;
    case SAND_SYS_FS_LABEL: return SAND_PERM_INSTALL;
    case SAND_SYS_SPAWN: return SAND_PERM_SPAWN;
    case SAND_SYS_WIN_CREATE: return SAND_PERM_WINDOW;
    case SAND_SYS_SETTING_SET: return SAND_PERM_SETTINGS;
    case SAND_SYS_INSTALL_PUT: case SAND_SYS_INSTALL_COMMIT: return SAND_PERM_INSTALL;
    case SAND_SYS_CHAN_OPEN: case SAND_SYS_CHAN_SEND: case SAND_SYS_CHAN_RECV: return SAND_PERM_IPC;
    case SAND_SYS_NET_INFO: case SAND_SYS_NET_PING: case SAND_SYS_NET_UDP_SEND: case SAND_SYS_NET_UDP_RECV: case SAND_SYS_TCP_CONNECT: case SAND_SYS_TCP_SEND: case SAND_SYS_TCP_RECV: case SAND_SYS_TCP_CLOSE: case SAND_SYS_NAME_RESOLVE: return SAND_PERM_NET;
    default: return 0u;
    }
}
/* MIMIR labels: every file version carries read and write masks of SAND_PERM_* bits; the caller must hold them all. */
static int label_allows(const char *name, int write) {
    struct mimir_info in;
    if(mimir_stat(name,&in)!=0) return 1;                 /* no such file yet: nothing to protect */
    uint32_t need=write?in.write_perm:in.read_perm;
    return (process_permissions(process_current())&need)==need;
}
static int32_t fs_error(int rc,const char *name) {
    if(rc==MIMIR_E_CORRUPT) { char line[64]="MIMIR_CORRUPT "; uint32_t n=0u,i; while(line[n]) ++n; for(i=0u;name[i] && n<62u;++i) line[n++]=name[i]; line[n]='\0'; sand_audit(line); return SAND_E_CORRUPT; }
    return SAND_E_NOENT;
}
static void audit_label(const char *kind,const char *name) {
    char line[72]; uint32_t n=0u,i; for(i=0u;kind[i];++i) line[n++]=kind[i]; line[n++]=' ';
    for(i=0u;name[i] && n<70u;++i) line[n++]=name[i];
    line[n]='\0'; sand_audit(line);
}
static void audit_denied(uint32_t number) {
    char line[48]="PERMISSION_DENIED syscall="; uint32_t n=0u; while(line[n]) ++n;
    char digits[11]; uint32_t d=0u; do { digits[d++]=(char)('0'+number%10u); number/=10u; } while(number && d<10u);
    while(d) line[n++]=digits[--d];
    line[n]='\0'; sand_audit(line);
}

/* Egress audit: every datagram or echo request an app sends is logged with its destination (the Veyra security trail). */
static void audit_net(const char *kind, uint32_t ip, uint32_t port, uint32_t length) {
    char line[72]; uint32_t n=0u, i;
    for(i=0u; kind[i]; ++i) line[n++]=kind[i];
    line[n++]=' ';
    for(i=0u; i<4u; ++i) {
        uint32_t v=(ip>>(8u*i))&255u; char dg[3]; uint32_t d=0u; do { dg[d++]=(char)('0'+v%10u); v/=10u; } while(v);
        while(d) line[n++]=dg[--d];
        if(i<3u) line[n++]='.';
    }
    if(port) { line[n++]=':'; char dg[6]; uint32_t d=0u; do { dg[d++]=(char)('0'+port%10u); port/=10u; } while(port); while(d) line[n++]=dg[--d]; }
    if(length) { line[n++]=' '; line[n++]='l'; line[n++]='e'; line[n++]='n'; line[n++]='='; char dg[6]; uint32_t d=0u; do { dg[d++]=(char)('0'+length%10u); length/=10u; } while(length); while(d) line[n++]=dg[--d]; }
    line[n]='\0'; sand_audit(line);
}
/* HUGINN audit: which name an app looked up and what it got back. */
static void audit_name(const char *name, uint32_t ip) {
    char line[112]; uint32_t n=0u, i;
    const char *k="NET_RESOLVE "; for(i=0u;k[i];++i) line[n++]=k[i];
    for(i=0u;name[i] && i<63u;++i) line[n++]=name[i];
    line[n++]=' ';
    for(i=0u;i<4u;++i) { uint32_t v=(ip>>(8u*i))&255u; char dg[3]; uint32_t d=0u; do { dg[d++]=(char)('0'+v%10u); v/=10u; } while(v); while(d) line[n++]=dg[--d]; if(i<3u) line[n++]='.'; }
    line[n]='\0'; sand_audit(line);
}
static struct driftnet_udp net_udp;      /* syscalls run with IRQs off on one CPU, so one kernel-side buffer is safe */

uint32_t syscall_dispatch(uint32_t frame) {
    uint32_t *r=(uint32_t *)frame;
    if((r[R_CS]&3u)!=3u || process_current()==0u) { r[R_EAX]=(uint32_t)SAND_E_FAIL; return frame; }
    uint32_t a=r[R_EBX], b=r[R_ECX], c=r[R_EDX];
    int32_t result=SAND_E_NOSYS;
    { uint32_t need=required_permission(r[R_EAX],a);
      if(need!=0u && (process_permissions(process_current())&need)!=need) {
          result=SAND_E_PERM; audit_denied(r[R_EAX]); r[R_EAX]=(uint32_t)result; return frame; } }
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
        if(b==0u || c==0u) { result=SAND_E_FAULT; break; }
        uint32_t cap=c>MIMIR_FILE_MAX?MIMIR_FILE_MAX:c;      /* BEDROCK: whole files up to 64 KiB, not just 512 bytes */
        if(!user_ok(b,cap,1)) { result=SAND_E_FAULT; break; }
        uint8_t *buf=fs_buffer(cap); if(buf==0) { result=SAND_E_FAIL; break; }
        uint32_t length=0u;
        if(!label_allows(name_buffer,0)) { audit_label("MIMIR_LABEL_DENIED read",name_buffer); result=SAND_E_PERM; break; }
        { int rc=mimir_read(name_buffer,buf,cap,&length); if(rc!=0) { result=fs_error(rc,name_buffer); break; } }
        (void)copy_out(b,buf,length); result=(int32_t)length; break; }
    case SAND_SYS_FS_WRITE: {
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        if(c==0u || c>MIMIR_FILE_MAX) { result=SAND_E_FAIL; break; }
        uint8_t *buf=fs_buffer(c); if(buf==0) { result=SAND_E_FAIL; break; }
        if(copy_in(buf,b,c)!=0) { result=SAND_E_FAULT; break; }
        if(!label_allows(name_buffer,1)) { audit_label("MIMIR_LABEL_DENIED write",name_buffer); result=SAND_E_PERM; break; }
        result=mimir_write(name_buffer,buf,c)==0?0:SAND_E_FAIL; break; }
    case SAND_SYS_TIME_GET: {
        char iso[20]; struct sand_time t; uint32_t v[6]; static const uint8_t at[6]={0,5,8,11,14,17}, wd[6]={4,2,2,2,2,2};
        if(!user_ok(a,sizeof(t),1)) { result=SAND_E_FAULT; break; }
        if(rtc_read_iso(iso)!=0) { result=SAND_E_FAIL; break; }
        for(uint32_t f=0;f<6u;++f) { v[f]=0u; for(uint32_t d=0;d<wd[f];++d) v[f]=v[f]*10u+(uint32_t)(iso[at[f]+d]-'0'); }
        t.year=v[0]; t.month=v[1]; t.day=v[2]; t.hour=v[3]; t.minute=v[4]; t.second=v[5];
        (void)copy_out(a,&t,sizeof(t)); result=0; break; }
    case SAND_SYS_CHAN_OPEN:
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        result=tideway_open(name_buffer); break;
    case SAND_SYS_CHAN_SEND: {
        if(c==0u || c>TIDEWAY_MSG_MAX) { result=SAND_E_FAIL; break; }
        uint8_t msg[TIDEWAY_MSG_MAX];
        if(copy_in(msg,b,c)!=0) { result=SAND_E_FAULT; break; }
        result=tideway_send((int32_t)a,process_current(),msg,c); break; }
    case SAND_SYS_CHAN_RECV: {
        struct tideway_msg m;
        if(!user_ok(b,sizeof(m),1)) { result=SAND_E_FAULT; break; }
        result=tideway_recv((int32_t)a,&m);
        if(result==1) (void)copy_out(b,&m,sizeof(m));
        break; }
    case SAND_SYS_NAME_RESOLVE: {
        static struct driftnet_resolve rv;
        if(copy_in(&rv,a,sizeof(rv))!=0) { result=SAND_E_FAULT; break; }
        sand_veyra_status_t st=driftnet_resolve_step(process_current(),&rv,4000u);
        if(st==DRIFTNET_RESOLVE_PENDING) return retry_yield(r,frame);
        if(st==SAND_VEYRA_OK) { audit_name(rv.name,rv.ip); (void)copy_out(a,&rv,sizeof(rv)); }
        result=sand_veyra_to_syscall(st); break; }
    case SAND_SYS_TCP_CONNECT: {
        sand_veyra_status_t st=driftnet_tcp_connect_step(process_current(),a,b,c>10000u?10000u:c);
        if(st==DRIFTNET_TCP_PENDING) return retry_yield(r,frame);
        if(st==SAND_VEYRA_OK) audit_net("NET_TCP_CONNECT",a,b,0u);
        result=st==SAND_VEYRA_E_IO?SAND_E_CLOSED:sand_veyra_to_syscall(st); break; }
    case SAND_SYS_TCP_SEND: {
        static uint8_t tcp_buf[DRIFTNET_TCP_SEG_MAX];
        if(b==0u || b>DRIFTNET_TCP_SEG_MAX) { result=SAND_E_FAIL; break; }
        if(copy_in(tcp_buf,a,b)!=0) { result=SAND_E_FAULT; break; }
        int32_t st=driftnet_tcp_send_step(process_current(),tcp_buf,b);
        if(st==DRIFTNET_TCP_PENDING) return retry_yield(r,frame);
        if(st>0) audit_net("NET_TCP_SEND",(uint32_t)0,0u,(uint32_t)st);
        result=st>=0?st:st==DRIFTNET_TCP_CLOSED?SAND_E_CLOSED:sand_veyra_to_syscall(st); break; }
    case SAND_SYS_TCP_RECV: {
        static uint8_t tcp_in[DRIFTNET_TCP_SEG_MAX];
        uint32_t cap=b>DRIFTNET_TCP_SEG_MAX?DRIFTNET_TCP_SEG_MAX:b;
        if(cap==0u || !user_ok(a,cap,1)) { result=SAND_E_FAULT; break; }
        int32_t n=driftnet_tcp_recv(process_current(),tcp_in,cap);
        if(n>0) (void)copy_out(a,tcp_in,(uint32_t)n);
        result=n>=0?n:SAND_E_CLOSED; break; }
    case SAND_SYS_TCP_CLOSE: driftnet_tcp_close(process_current()); result=0; break;
    case SAND_SYS_VEYRA_QUERY: {
        struct sand_veyra_subsystem_info info;
        if(!user_ok(b,sizeof(info),1)) { result=SAND_E_FAULT; break; }
        result=sand_veyra_to_syscall(veyra_query(a,&info));
        if(result==0) (void)copy_out(b,&info,sizeof(info));
        break; }
    case SAND_SYS_NET_INFO: {
        struct driftnet_info ni;
        if(!user_ok(a,sizeof(ni),1)) { result=SAND_E_FAULT; break; }
        result=sand_veyra_to_syscall(driftnet_info(&ni));
        if(result==0) (void)copy_out(a,&ni,sizeof(ni));
        break; }
    case SAND_SYS_NET_PING: {
        sand_veyra_status_t st=driftnet_ping_step(process_current(),a,b>10000u?10000u:b);
        if(st==DRIFTNET_PING_PENDING) return retry_yield(r,frame);
        if(st>=0) audit_net("NET_PING",a,0u,0u);
        result=st>=0?st:sand_veyra_to_syscall(st); break; }
    case SAND_SYS_NET_UDP_SEND: {
        if(!user_ok(a,sizeof(net_udp),0)) { result=SAND_E_FAULT; break; }
        if(copy_in(&net_udp,a,sizeof(net_udp))!=0) { result=SAND_E_FAULT; break; }
        audit_net("NET_UDP_SEND",net_udp.ip,net_udp.dst_port,net_udp.length);
        result=sand_veyra_to_syscall(driftnet_udp_send(&net_udp)); break; }
    case SAND_SYS_NET_UDP_RECV: {
        if(!user_ok(b,sizeof(net_udp),1)) { result=SAND_E_FAULT; break; }
        int32_t got=driftnet_udp_recv((uint16_t)a,&net_udp);
        if(got==1) (void)copy_out(b,&net_udp,sizeof(net_udp));
        result=got>=0?got:sand_veyra_to_syscall(got); break; }
    case SAND_SYS_INSTALL_PUT: {
        if(c>sizeof(scratch)) { result=SAND_E_FAIL; break; }
        if(c!=0u && copy_in(scratch,b,c)!=0) { result=SAND_E_FAULT; break; }
        result=appinstall_put(process_current(),a,scratch,c); break; }
    case SAND_SYS_INSTALL_COMMIT:
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        result=appinstall_commit(process_current(),name_buffer); break;
    case SAND_SYS_FS_LIST: {
        struct sand_dirent entry; uint32_t length=0u;
        if(!user_ok(b,sizeof(entry),1)) { result=SAND_E_FAULT; break; }
        if(mimir_list(a,entry.name,sizeof(entry.name),&length)!=0) { result=SAND_E_NOENT; break; }
        entry.length=length; (void)copy_out(b,&entry,sizeof(entry)); result=0; break; }
    case SAND_SYS_FS_REMOVE:
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        if(!label_allows(name_buffer,1)) { audit_label("MIMIR_LABEL_DENIED remove",name_buffer); result=SAND_E_PERM; break; }
        result=mimir_remove(name_buffer)==0?0:SAND_E_NOENT; break;
    case SAND_SYS_FS_STAT: {
        struct mimir_info in;
        if(copy_name(a)!=0 || !user_ok(b,sizeof(in),1)) { result=SAND_E_FAULT; break; }
        if(mimir_stat(name_buffer,&in)!=0) { result=SAND_E_NOENT; break; }
        (void)copy_out(b,&in,sizeof(in)); result=0; break; }
    case SAND_SYS_FS_HISTORY: {
        struct mimir_ver v;
        if(copy_name(a)!=0 || !user_ok(c,sizeof(v),1)) { result=SAND_E_FAULT; break; }
        if(mimir_history(name_buffer,b,&v)!=0) { result=SAND_E_NOENT; break; }
        (void)copy_out(c,&v,sizeof(v)); result=0; break; }
    case SAND_SYS_FS_ROLLBACK:
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        if(!label_allows(name_buffer,1)) { audit_label("MIMIR_LABEL_DENIED rollback",name_buffer); result=SAND_E_PERM; break; }
        if(mimir_rollback(name_buffer,b)==0) { audit_label("MIMIR_ROLLBACK",name_buffer); result=0; } else result=SAND_E_NOENT;
        break;
    case SAND_SYS_FS_SEAL: {
        struct mimir_seal s;
        if(!user_ok(a,sizeof(s),1)) { result=SAND_E_FAULT; break; }
        if(mimir_seal_info(&s)!=0) { result=SAND_E_NODEV; break; }
        (void)copy_out(a,&s,sizeof(s)); result=0; break; }
    case SAND_SYS_FS_SCRUB: {
        struct mimir_scrub s;
        if(!user_ok(a,sizeof(s),1)) { result=SAND_E_FAULT; break; }
        if(mimir_scrub(&s)!=0) { result=SAND_E_NODEV; break; }
        if(s.bad!=0u) sand_audit("MIMIR_SCRUB_DAMAGE");
        (void)copy_out(a,&s,sizeof(s)); result=0; break; }
    case SAND_SYS_FS_LABEL:
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        if(mimir_set_labels(name_buffer,b,c)==0) { audit_label("MIMIR_LABEL_SET",name_buffer); result=0; } else result=SAND_E_NOENT;
        break;
    case SAND_SYS_FS_EBB:
        if(mimir_ebb()==0) { sand_audit("MIMIR_EBB"); result=0; } else result=SAND_E_FAIL;
        break;
    case SAND_SYS_SPAWN: {
        const uint8_t *image; uint32_t size; sand_pid_t pid=0u;
        if(copy_name(a)!=0) { result=SAND_E_FAULT; break; }
        if(apps_find(name_buffer,&image,&size)==0) {
            result=process_spawn_sapp(name_buffer,image,size,&pid)==0?(int32_t)pid:SAND_E_FAIL; break; }
        /* Not built in: try MIMIR. Its permissions come from the image's own metadata; the spawn runs with
         * IRQs off, so the child cannot execute a syscall before they are applied. */
        if(diskapps_find(name_buffer,&image,&size)!=0) { result=SAND_E_NOENT; break; }
        if(process_spawn_sapp(name_buffer,image,size,&pid)!=0) { result=SAND_E_FAIL; break; }
        (void)process_set_permissions(pid,diskapps_manifest_permissions(image,size));
        result=(int32_t)pid; break; }
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
    case SAND_SYS_WIN_BLIT: {
        struct sand_blit bl;
        if(copy_in(&bl,b,sizeof(bl))!=0) { result=SAND_E_FAULT; break; }
        if(bl.scale<0 || bl.scale>4) { result=SAND_E_FAIL; break; }
        if(bl.w<=0 || bl.h<=0 || bl.w>SAND_WIN_MAX_W || bl.h>SAND_WIN_MAX_H) { result=SAND_E_FAIL; break; }
        if(!user_ok((uint32_t)bl.pixels,(uint32_t)(bl.w*bl.h)*4u,0)) { result=SAND_E_FAULT; break; }
        result=sand_desktop_win_blit((int32_t)a,&bl,bl.pixels); break; }
    case SAND_SYS_WIN_RESIZABLE: result=sand_desktop_win_resizable((int32_t)a,(int32_t)(b>>16),(int32_t)(b&0xFFFFu)); break;
    case SAND_SYS_SETTING_GET: result=sand_desktop_setting_get(a); break;
    case SAND_SYS_SETTING_SET: result=sand_desktop_setting_set(a,b); break;
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
        char disk_name[SAND_NAME_MAX]; uint32_t built_in=apps_count();
        if(a<built_in) { if(apps_name_at(a,&name)!=0) { result=SAND_E_NOENT; break; } }
        else if(diskapps_name_at(a-built_in,disk_name,sizeof(disk_name))==0) name=disk_name;
        else { result=SAND_E_NOENT; break; }
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
