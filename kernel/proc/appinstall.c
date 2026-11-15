#include "sand/proc/appinstall.h"
#include "sand/proc/apps.h"
#include "sand/fs/sandfs.h"
#include "sand/mm/heap.h"
#include "sand/kernel/process.h"
#include "sand/security/launch_gate.h"
#include "sand/abi.h"

#define SAPP_MIN_SIZE 96u
static uint8_t *stage;            /* separate from the spawn-time staging buffer: an install spans many syscalls */
static uint32_t stage_len;
static uint32_t owner_pid;

static int owner_alive(void) {
    struct sand_process_info info;
    return owner_pid!=0u && process_get_info(owner_pid,&info)==0 && info.state!=SAND_PROCESS_TERMINATED;
}
static int valid_name(const char *name) {
    uint32_t n=0u;
    for(;name[n];++n) { char c=name[n]; if(n>=16u || !((c>='a' && c<='z') || (n>0u && c>='0' && c<='9'))) return 0; }
    return n>0u;
}
int32_t appinstall_put(uint32_t pid,uint32_t offset,const void *data,uint32_t length) {
    if(stage==0) { stage=(uint8_t *)kmalloc(SANDFS_FILE_MAX); if(stage==0) return SAND_E_FAIL; }
    if(offset==0u) {
        if(stage_len!=0u && owner_pid!=pid && owner_alive()) return SAND_E_FAIL;   /* another installer is mid-transfer */
        owner_pid=pid; stage_len=0u;
    } else if(owner_pid!=pid || offset!=stage_len) return SAND_E_FAIL;               /* append-only, by the owner */
    if(length>SANDFS_FILE_MAX-stage_len) { stage_len=0u; owner_pid=0u; return SAND_E_FAIL; }
    const uint8_t *src=(const uint8_t *)data;
    for(uint32_t i=0u;i<length;++i) stage[stage_len+i]=src[i];
    stage_len+=length;
    return (int32_t)stage_len;
}
int32_t appinstall_commit(uint32_t pid,const char *name) {
    const uint8_t *image; uint32_t size;
    int32_t result=SAND_E_FAIL;
    if(stage==0 || owner_pid!=pid || stage_len<SAPP_MIN_SIZE || !valid_name(name)) goto done;
    if(apps_find(name,&image,&size)==0) goto done;                                    /* never shadow a built-in app */
    if(stage[0]!='S' || stage[1]!='A' || stage[2]!='P' || stage[3]!='P') goto done;
    if(sand_launch_gate(name,stage,stage_len)!=0) { sand_audit("INSTALL_DENIED_BY_GATE"); goto done; }
    {
        char file[SANDFS_NAME_MAX]; uint32_t n=0u;
        while(name[n]) { file[n]=name[n]; ++n; }
        file[n++]='.'; file[n++]='s'; file[n++]='a'; file[n++]='p'; file[n++]='p'; file[n]='\0';
        if(sandfs_write(file,stage,stage_len)==0) { sand_audit("APP_INSTALLED"); result=0; }
    }
done:
    if(owner_pid==pid) { stage_len=0u; owner_pid=0u; }
    return result;
}
