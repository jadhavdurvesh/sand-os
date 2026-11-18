#include "sand/ipc/tideway.h"
#include "sand/abi.h"
static struct { char name[TIDEWAY_NAME_MAX]; uint8_t used; uint8_t head, count; struct tideway_msg q[TIDEWAY_QUEUE]; } chan[TIDEWAY_CHANNELS];
static int name_ok(const char *n) {
    uint32_t i=0u; if(!n) return 0;
    for(; n[i]; ++i) { char c=n[i]; if(i>=TIDEWAY_NAME_MAX-1u) return 0; if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_')) return 0; }
    return i>0u;
}
static int same(const char *a,const char *b) { uint32_t i=0u; while(a[i] && a[i]==b[i]) ++i; return a[i]==b[i]; }
int32_t tideway_open(const char *name) {
    int32_t freeslot=-1;
    if(!name_ok(name)) return SAND_E_FAIL;
    for(uint32_t i=0u;i<TIDEWAY_CHANNELS;++i) {
        if(chan[i].used && same(chan[i].name,name)) return (int32_t)i+1;
        if(!chan[i].used && freeslot<0) freeslot=(int32_t)i;
    }
    if(freeslot<0) return SAND_E_FULL;
    chan[freeslot].used=1u; chan[freeslot].head=0u; chan[freeslot].count=0u;
    for(uint32_t i=0u;i<TIDEWAY_NAME_MAX;++i) chan[freeslot].name[i]=0;
    for(uint32_t i=0u;name[i];++i) chan[freeslot].name[i]=name[i];
    return freeslot+1;
}
static int valid(int32_t h) { return h>=1 && (uint32_t)h<=TIDEWAY_CHANNELS && chan[h-1].used; }
int32_t tideway_send(int32_t h,uint32_t from,const void *data,uint32_t length) {
    if(!valid(h) || length==0u || length>TIDEWAY_MSG_MAX || !data) return SAND_E_FAIL;
    uint32_t c=(uint32_t)h-1u; if(chan[c].count>=TIDEWAY_QUEUE) return SAND_E_FULL;
    struct tideway_msg *m=&chan[c].q[(chan[c].head+chan[c].count)%TIDEWAY_QUEUE];
    m->from=from; m->length=length; for(uint32_t i=0u;i<length;++i) m->data[i]=((const uint8_t *)data)[i];
    ++chan[c].count; return 0;
}
int32_t tideway_recv(int32_t h,struct tideway_msg *out) {
    if(!valid(h) || !out) return SAND_E_FAIL;
    uint32_t c=(uint32_t)h-1u; if(chan[c].count==0u) return 0;
    *out=chan[c].q[chan[c].head]; chan[c].head=(uint8_t)((chan[c].head+1u)%TIDEWAY_QUEUE); --chan[c].count; return 1;
}
uint32_t tideway_pending(int32_t h) { return valid(h)?chan[h-1].count:0u; }
