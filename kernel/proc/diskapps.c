#include "sand/proc/diskapps.h"
#include "sand/fs/sandfs.h"
#include "sand/mm/heap.h"
#include "sand/kernel/perms.h"
#include "sand/proc/apps.h"

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
    {"process.spawn",SAND_PERM_SPAWN},{"system.install",SAND_PERM_INSTALL},{"window.create",SAND_PERM_WINDOW},{"settings.write",SAND_PERM_SETTINGS},
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

/* Locate the value of a top-level key in the metadata JSON; returns the index just after the ':' (skipping
 * strings so a key spelled inside an escaped string never matches), or -1. */
static int find_top_key(const char *p,uint32_t end,const char *wanted) {
    uint32_t i=0u, depth=0u;
    while(i<end && p[i]!='\0') {
        char c=p[i];
        if(c=='{' || c=='[') { ++depth; ++i; }
        else if(c=='}' || c==']') { if(depth==0u) return -1; --depth; ++i; }
        else if(c=='"') {
            char key[24]; uint32_t save=i;
            if(read_string(p,end,&i,key,sizeof(key))!=0) { i=save+1u; while(i<end && p[i]!='"') { if(p[i]=='\\') ++i; ++i; } ++i; continue; }
            skip_ws(p,end,&i);
            if(depth==1u && i<end && p[i]==':' && str_eq(key,wanted)) { ++i; skip_ws(p,end,&i); return (int)i; }
        } else ++i;
    }
    return -1;
}
static int metadata_window(const uint8_t *image,uint32_t size,const char **p,uint32_t *len) {
    if(image==0 || size<96u) return -1;
    uint32_t off=(uint32_t)image[28]|((uint32_t)image[29]<<8)|((uint32_t)image[30]<<16)|((uint32_t)image[31]<<24);
    uint32_t n=(uint32_t)image[32]|((uint32_t)image[33]<<8)|((uint32_t)image[34]<<16)|((uint32_t)image[35]<<24);
    if(off>size || n>size-off || n==0u || n>4096u) return -1;
    *p=(const char *)image+off; *len=n; return 0;
}
uint32_t diskapps_manifest_permissions(const uint8_t *image,uint32_t size) {
    const char *p; uint32_t end, i; int at;
    if(metadata_window(image,size,&p,&end)!=0) return 0u;
    at=find_top_key(p,end,"permissions");
    if(at<0) return 0u;
    i=(uint32_t)at;
    if(i>=end || p[i]!='[') return 0u;
    ++i;
    uint32_t mask=0u;
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

/* ---- launcher entries for disk apps (manifest "launcher" object) ---- */
static int copy_text(char *dst,uint32_t cap,const char *src) {   /* printable ASCII only, must fit */
    uint32_t n=0u;
    for(;src[n];++n) { if(n+1u>=cap || (uint8_t)src[n]<0x20u || (uint8_t)src[n]>0x7Eu) return -1; dst[n]=src[n]; }
    if(n==0u) return -1;
    dst[n]='\0'; return 0;
}
static int parse_color(const char *t,uint32_t *out) {
    if(t[0]!='#') return -1;
    uint32_t v=0u;
    for(uint32_t i=1u;i<7u;++i) {
        char c=t[i]; uint32_t d;
        if(c>='0' && c<='9') d=(uint32_t)(c-'0'); else if(c>='a' && c<='f') d=(uint32_t)(c-'a'+10); else if(c>='A' && c<='F') d=(uint32_t)(c-'A'+10); else return -1;
        v=(v<<4)|d;
    }
    if(t[7]!='\0') return -1;
    *out=v; return 0;
}
int diskapps_manifest_launcher(const uint8_t *image,uint32_t size,const char *app,struct sand_app_launcher_info *out,uint32_t *order_out) {
    const char *p; uint32_t end, i; int at, have=0;
    char text[32], key[16]; uint32_t order=0u;
    if(out==0 || order_out==0 || app==0 || metadata_window(image,size,&p,&end)!=0) return -1;
    at=find_top_key(p,end,"launcher");
    if(at<0) return -1;
    i=(uint32_t)at;
    if(i>=end || p[i]!='{') return -1;
    ++i;
    if(copy_text(out->app,sizeof(out->app),app)!=0) return -1;
    for(;;) {
        skip_ws(p,end,&i);
        if(i<end && p[i]=='}') break;
        if(i>=end || p[i]!='"' || read_string(p,end,&i,key,sizeof(key))!=0) return -1;
        skip_ws(p,end,&i); if(i>=end || p[i]!=':') return -1; ++i; skip_ws(p,end,&i);
        if(i<end && p[i]=='"') {
            if(read_string(p,end,&i,text,sizeof(text))!=0) return -1;
            if(str_eq(key,"label")) { if(copy_text(out->label,sizeof(out->label),text)!=0) return -1; have|=1; }
            else if(str_eq(key,"subtitle")) { if(copy_text(out->subtitle,sizeof(out->subtitle),text)!=0) return -1; have|=2; }
            else if(str_eq(key,"accent")) { if(parse_color(text,&out->accent)!=0) return -1; have|=4; }
            else if(str_eq(key,"accent2")) { if(parse_color(text,&out->accent2)!=0) return -1; have|=8; }
            else if(str_eq(key,"glyph")) { if(text[0]<0x21 || text[0]>0x7E || text[1]!='\0') return -1; out->glyph=text[0]; have|=16; }
            else return -1;
        } else if(i<end && p[i]>='0' && p[i]<='9' && str_eq(key,"order")) {
            order=0u;
            while(i<end && p[i]>='0' && p[i]<='9') { order=order*10u+(uint32_t)(p[i]-'0'); if(order>9999u) return -1; ++i; }
            have|=32;
        } else return -1;
        skip_ws(p,end,&i);
        if(i<end && p[i]==',') ++i;
        else if(!(i<end && p[i]=='}')) return -1;
    }
    if(have!=63) return -1;
    *order_out=order; return 0;
}

#define DISK_LAUNCHER_MAX 4u
static struct sand_app_launcher_info disk_launchers[DISK_LAUNCHER_MAX];
static uint32_t disk_launcher_count;
void diskapps_launcher_scan(void) {
    struct { struct sand_app_launcher_info info; uint32_t order; } found[DISK_LAUNCHER_MAX];
    uint32_t n=0u;
    disk_launcher_count=0u;
    for(uint32_t i=0u;n<DISK_LAUNCHER_MAX;++i) {
        char name[SANDFS_NAME_MAX]; const uint8_t *image; uint32_t size, order;
        if(diskapps_name_at(i,name,sizeof(name))!=0) break;
        if(diskapps_find(name,&image,&size)!=0) continue;
        if(diskapps_manifest_launcher(image,size,name,&found[n].info,&order)!=0) continue;
        found[n].order=order; ++n;
    }
    for(uint32_t i=1u;i<n;++i) for(uint32_t j=i;j>0u && found[j].order<found[j-1u].order;--j) { __typeof__(found[0]) t=found[j]; found[j]=found[j-1u]; found[j-1u]=t; }
    for(uint32_t i=0u;i<n;++i) disk_launchers[i]=found[i].info;
    disk_launcher_count=n;
}
uint32_t diskapps_launcher_count(void) { return disk_launcher_count; }
int diskapps_launcher_at(uint32_t index,struct sand_app_launcher_info *out) {
    if(index>=disk_launcher_count || out==0) return -1;
    *out=disk_launchers[index]; return 0;
}
