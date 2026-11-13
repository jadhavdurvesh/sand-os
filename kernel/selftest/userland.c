#include <stdint.h>
#include "sand/abi.h"
#include "sand/arch/x86/timer.h"
#include "sand/console/console.h"
#include "sand/kernel/process.h"
#include "sand/kernel/scheduler.h"
#include "sand/mm/frame.h"
#include "sand/proc/apps.h"

/* M7 evidence: real ring-3 applications run concurrently under the
 * preemptive scheduler, use the syscall ABI, exit, and are reaped; a faulting
 * application dies alone; no frames leak. */
extern uint8_t sapp_test_image[174];
static inline void debug_char(char c) { __asm__ volatile ("outb %0, $0xE9" : : "a"(c)); }
static void debug_text(const char *s) { while(*s) debug_char(*s++); }

#define WAIT_TICKS (15u*SAND_TICK_HZ)
static char capture[2048];

static int spawn_app(const char *name,sand_pid_t *pid) {
    const uint8_t *image; uint32_t size;
    if(apps_find(name,&image,&size)!=0) return -1;
    return process_spawn_sapp(name,image,size,pid);
}
/* The boot context is the idle thread: it sleeps in hlt while apps run. */
static int wait_exit(sand_pid_t pid,int32_t *status) {
    uint32_t start=timer_ticks();
    struct sand_process_info info;
    for(;;) {
        if(process_get_info(pid,&info)!=0) return -1;
        if(info.state==SAND_PROCESS_TERMINATED) { *status=info.exit_code; return process_destroy(pid); }
        if(timer_ticks()-start>WAIT_TICKS) return -1;
        __asm__ volatile("sti; hlt");
    }
}
static int contains(const char *needle) { return console_capture_contains(needle); }

int userland_selftest(void) {
    int failed=0;
    __asm__ volatile("sti");
    uint32_t frames_before=frame_free_count();
    uint32_t switches_before=scheduler_switch_count();
    console_capture_start(capture,sizeof(capture));

    /* Wave 1: ABI conformance + two CPU-bound apps competing for the CPU. */
    sand_pid_t abi=0u,spin_a=0u,spin_b=0u;
    if(spawn_app("abitest",&abi)!=0 || spawn_app("spin",&spin_a)!=0 || spawn_app("spin",&spin_b)!=0) {
        debug_text("SAND_TEST:M7_SPAWN_FAIL\n"); return -1;
    }
    debug_text("SAND_TEST:M7_APPS_SPAWNED\n");
    int32_t abi_status=-1,a_status=-1,b_status=-1;
    if(wait_exit(abi,&abi_status)!=0 || wait_exit(spin_a,&a_status)!=0 || wait_exit(spin_b,&b_status)!=0) {
        debug_text("SAND_TEST:M7_WAIT_TIMEOUT\n"); return -1;
    }
    if(abi_status==0 && contains("ABITEST:PASS") && contains("Hello from a native SAND application"))
        debug_text("SAND_TEST:M7_SYSCALL_ABI_PASS\n");
    else { debug_text("SAND_TEST:M7_SYSCALL_ABI_FAIL\n"); failed=1; }
    if(a_status==5 && b_status==5 && scheduler_switch_count()-switches_before>=4u)
        debug_text("SAND_TEST:M7_PREEMPTIVE_MULTIPROCESS_PASS\n");
    else { debug_text("SAND_TEST:M7_PREEMPTIVE_MULTIPROCESS_FAIL\n"); failed=1; }

    /* Wave 2: fault isolation and the original ring-3 token proof. Reuses the slots freed above. */
    sand_pid_t crash=0u,token=0u;
    if(spawn_app("crash",&crash)!=0 || process_spawn_sapp("sapp-test",sapp_test_image,sizeof(sapp_test_image),&token)!=0) {
        debug_text("SAND_TEST:M7_SPAWN2_FAIL\n"); return -1;
    }
    int32_t crash_status=0,token_status=0;
    if(wait_exit(crash,&crash_status)!=0 || wait_exit(token,&token_status)!=0) {
        debug_text("SAND_TEST:M7_WAIT2_TIMEOUT\n"); return -1;
    }
    if(crash_status==SAND_EXIT_FAULT_BASE-14 && !contains("SURVIVED"))
        debug_text("SAND_TEST:M7_FAULT_ISOLATION_PASS\n");
    else { debug_text("SAND_TEST:M7_FAULT_ISOLATION_FAIL\n"); failed=1; }
    if(token_status!=(int32_t)SAND_ABI_TOKEN) { debug_text("SAND_TEST:M7_TOKEN_FAIL\n"); failed=1; }

    console_capture_stop();
    __asm__ volatile("cli");
    if(frame_free_count()==frames_before && process_live_count()==0u)
        debug_text("SAND_TEST:M7_PROCESS_RECLAIM_PASS\n");
    else { debug_text("SAND_TEST:M7_PROCESS_RECLAIM_FAIL\n"); failed=1; }
    if(failed) debug_text("SAND_TEST:M7_USERLAND_FAILED\n");
    return failed?-1:0;
}
