#include "sand/proc/diskapps.h"
#include "sand/fs/sandfs.h"
#include "sand/mm/heap.h"
#include "sand/kernel/perms.h"

#define SUFFIX ".sapp"
static uint8_t *staging;   /* one SAPP image; the spawn path is serialised (syscalls run with IRQs off) */

static int valid_app_name(const char *name) {
    uint32_t n=0u;
    if(name==0) return 0;
    for(;name[n];++n) {
        char c=name[n];
        if(n>=16u || !((c>='a' && c<='z') || (n>0u && c>='0' && c<='9'))) return 0;
    }
    return n>0u;
}
int diskapps_find(const char *name,const uint8_t **image,uint32_t *size) {
    char file[SANDFS_NAME_MAX]; uint32_t n=0u, len=0u;
    if(image==0 || size==0 || !valid_app_name(name) || !sandfs_is_mounted()) return -1;
    while(name[n]) { file[n]=name[n]; ++n; }
    for(uint32_t i=0;i<sizeof(SUFFIX);++i) file[n+i]=SUFFIX[i];
    if(staging==0) { staging=(uint8_t *)kmalloc(SANDFS_FILE_MAX); if(staging==0) return -1; }
    if(sandfs_read(file,staging,SANDFS_FILE_MAX,&len)!=0 || len==0u) return -1;
    *image=staging; *size=len; return 0;
}
int diskapps_name_at(uint32_t index,char *name_out,uint32_t capacity) {
    char file[SANDFS_NAME_MAX]; uint32_t length;
    for(uint32_t ordinal=0u;sandfs_list(ordinal,file,sizeof(file),&length)==0;++ordinal) {
        uint32_t n=0u; while(file[n]) ++n;
        if(n<=sizeof(SUFFIX)-1u) continue;
        uint32_t base=n-(uint32_t)(sizeof(SUFFIX)-1u), k=0u;
        while(k<sizeof(SUFFIX)-1u && file[base+k]==SUFFIX[k]) ++k;
        if(k!=sizeof(SUFFIX)-1u) continue;
        file[base]='\0';
        if(!valid_app_name(file)) continue;
        if(index--!=0u) continue;
        if(base+1u>capacity) return -1;
        for(uint32_t i=0u;i<=base;++i) name_out[i]=file[i];
        return 0;
    }
    return -1;
}

/* ---- manifest permissions from the SAPP metadata JSON (canonical, written by sapp.py) ---- */
static const struct { const char *name; uint32_t bits; } perm_names[]={
    {"console.write",SAND_PERM_CONSOLE_WRITE},{"console.read",SAND_PERM_CONSOLE_READ},
    {"console.rw",SAND_PERM_CONSOLE_WRITE|SAND_PERM_CONSOLE_READ},
    {"fs.read",SAND_PERM_FS_READ},{"fs.write",SAND_PERM_FS_WRITE},{"fs.rw",SAND_PERM_FS_READ|SAND_PERM_FS_WRITE},
    {"process.spawn",SAND_PERM_SPAWN},{"window.create",SAND_PERM_WINDOW},{"settings.write",SAND_PERM_SETTINGS},
};
/* Read the JSON string at p[*i]=='"' into buf (escapes rejected); advances past the closing quote. */
static int read_string(const char *p,uint32_t end,uint32_t *i,char *buf,uint32_t cap) {
    uint32_t n=0u; ++*i;
    while(*i<end && p[*i]!='"') {
        if(p[*i]=='\\' || (uint8_t)p[*i]<0x20u || n+1u>=cap) return -1;
        buf[n++]=p[(*i)++];
    }
    if(*i>=end) return -1;
    buf[n]='\0'; ++*i; return 0;
}
static void skip_ws(const char *p,uint32_t end,uint32_t *i) { while(*i<end && (p[*i]==' ' || p[*i]=='\n' || p[*i]=='\t' || p[*i]=='\r')) ++*i; }
static int str_eq(const char *a,const char *b) { uint32_t i=0u; while(a[i] && a[i]==b[i]) ++i; return a[i]==b[i]; }

uint32_t diskapps_manifest_permissions(const uint8_t *image,uint32_t size) {
    if(image==0 || size<96u) return 0u;
    uint32_t meta_off=(uint32_t)image[28]|((uint32_t)image[29]<<8)|((uint32_t)image[30]<<16)|((uint32_t)image[31]<<24);
    uint32_t meta_len=(uint32_t)image[32]|((uint32_t)image[33]<<8)|((uint32_t)image[34]<<16)|((uint32_t)image[35]<<24);
    if(meta_off>size || meta_len>size-meta_off || meta_len==0u || meta_len>4096u) return 0u;
    const char *p=(const char *)image+meta_off; uint32_t end=meta_len, i=0u, depth=0u;
    while(i<end && p[i]!='\0') {
        char c=p[i];
        if(c=='{' || c=='[') { ++depth; ++i; }
        else if(c=='}' || c==']') { if(depth==0u) return 0u; --depth; ++i; }
        else if(c=='"') {
            char key[24]; uint32_t save=i;
            if(read_string(p,end,&i,key,sizeof(key))!=0) { /* long or escaped string: skip it */ i=save+1u; while(i<end && p[i]!='"') { if(p[i]=='\\') ++i; ++i; } ++i; continue; }
            skip_ws(p,end,&i);
            if(depth==1u && i<end && p[i]==':' && str_eq(key,"permissions")) {
                ++i; skip_ws(p,end,&i);
                if(i>=end || p[i]!='[') return 0u;
                ++i; uint32_t mask=0u;
                for(;;) {
                    skip_ws(p,end,&i);
                    if(i<end && p[i]==']') return mask;
                    char name[20];
                    if(i>=end || p[i]!='"' || read_string(p,end,&i,name,sizeof(name))!=0) return 0u;
                    for(uint32_t k=0u;k<sizeof(perm_names)/sizeof(perm_names[0]);++k) if(str_eq(name,perm_names[k].name)) mask|=perm_names[k].bits;
                    skip_ws(p,end,&i);
                    if(i<end && p[i]==',') ++i;
                    else if(!(i<end && p[i]==']')) return 0u;
                }
            }
        } else ++i;
    }
    return 0u;
}
