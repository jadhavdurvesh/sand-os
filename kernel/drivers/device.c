#include "sand/drivers/device.h"
static struct sand_device devices[SAND_DEVICE_MAX];
static uint32_t total;
static int same(const char *a,const char *b) { uint32_t i=0u; while(a[i] && a[i]==b[i]) ++i; return a[i]==b[i]; }
int device_register(const char *name,const char *kind,const char *driver,enum sand_device_status status) {
    if(name==0 || kind==0 || driver==0) return -1;
    for(uint32_t i=0;i<total;++i) if(same(devices[i].name,name)) { devices[i].status=status; return 0; }
    if(total>=SAND_DEVICE_MAX) return -1;
    devices[total].name=name; devices[total].kind=kind; devices[total].driver=driver; devices[total].status=status;
    ++total; return 0;
}
int device_set_status(const char *name,enum sand_device_status status) {
    for(uint32_t i=0;i<total;++i) if(same(devices[i].name,name)) { devices[i].status=status; return 0; }
    return -1;
}
uint32_t device_count(void) { return total; }
const struct sand_device *device_get(uint32_t index) { return index<total?&devices[index]:0; }
const struct sand_device *device_find(const char *name) {
    for(uint32_t i=0;i<total;++i) if(same(devices[i].name,name)) return &devices[i];
    return 0;
}
