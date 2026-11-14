#include "sand/proc/apps.h"

/* SAPP images are converted to linkable objects by the Makefile
 * (objcopy -I binary), which defines these start/end symbols. */
#define APP_SYMBOLS(id) extern const uint8_t _binary_##id##_sapp_start[]; extern const uint8_t _binary_##id##_sapp_end[];
#define APP_ENTRY(id) { #id, _binary_##id##_sapp_start, _binary_##id##_sapp_end }
APP_SYMBOLS(hello) APP_SYMBOLS(abitest) APP_SYMBOLS(spin) APP_SYMBOLS(crash) APP_SYMBOLS(sandsh) APP_SYMBOLS(locus) APP_SYMBOLS(locusc) APP_SYMBOLS(quanta) APP_SYMBOLS(chronos) APP_SYMBOLS(prism)
struct app_entry { const char *name; const uint8_t *start; const uint8_t *end; };
static const struct app_entry table[] = {
    APP_ENTRY(sandsh), APP_ENTRY(hello), APP_ENTRY(abitest), APP_ENTRY(spin), APP_ENTRY(crash), APP_ENTRY(locus), APP_ENTRY(locusc), APP_ENTRY(quanta), APP_ENTRY(chronos), APP_ENTRY(prism)
};
#define APP_COUNT (sizeof(table)/sizeof(table[0]))
int apps_find(const char *name,const uint8_t **image,uint32_t *size) {
    if(name==0 || image==0 || size==0) return -1;
    for(uint32_t i=0;i<APP_COUNT;++i) {
        uint32_t j=0u; while(name[j] && table[i].name[j]==name[j]) ++j;
        if(name[j]=='\0' && table[i].name[j]=='\0') {
            *image=table[i].start; *size=(uint32_t)(table[i].end-table[i].start); return 0;
        }
    }
    return -1;
}
int apps_name_at(uint32_t index,const char **name) {
    if(index>=APP_COUNT || name==0) return -1;
    *name=table[index].name; return 0;
}
