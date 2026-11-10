#include "sand/graphics/window.h"
static sand_window_t windows[SAND_WINDOW_MAX];
static sand_window_id_t order[SAND_WINDOW_MAX];
static uint32_t count,next_id;
static int32_t pointer_x,pointer_y,drag_x,drag_y;
static sand_window_id_t focused,dragging;
static uint8_t previous_buttons;
static uint32_t ids[SAND_WINDOW_MAX];
void window_manager_init(void) {
    for(uint32_t i=0;i<SAND_WINDOW_MAX;++i) { windows[i].used=0u; windows[i].visible=0u; windows[i].focused=0u; ids[i]=0u; order[i]=0u; }
    count=0u; next_id=1u; focused=0u; dragging=0u; previous_buttons=0u; pointer_x=pointer_y=0;
}
static int slot_for(sand_window_id_t id) {
    for(uint32_t i=0;i<SAND_WINDOW_MAX;++i) if(windows[i].used && ids[i]==id) return (int)i;
    return -1;
}
sand_window_id_t window_create(const char *title,int32_t x,int32_t y,uint32_t width,uint32_t height) {
    if(title==0||width<120u||height<80u||count>=SAND_WINDOW_MAX) return 0u;
    uint32_t i=0u; while(i<SAND_WINDOW_MAX&&windows[i].used) ++i; if(i==SAND_WINDOW_MAX) return 0u;
    sand_window_t *w=&windows[i]; w->used=1u; w->visible=1u; w->focused=0u;
    w->x=x; w->y=y; w->width=width; w->height=height;
    uint32_t n=0u; while(title[n]&&n<31u) { w->title[n]=title[n]; ++n; } w->title[n]=0;
    ids[i]=next_id++; order[count++]=ids[i]; focused=ids[i]; w->focused=1u;
    return ids[i];
}
int window_close(sand_window_id_t id) {
    int slot=slot_for(id); if(slot<0) return -1;
    windows[slot].used=0u; windows[slot].visible=0u; windows[slot].focused=0u;
    uint32_t n=0u; for(uint32_t i=0;i<count;++i) if(order[i]!=id) order[n++]=order[i]; count=n;
    if(focused==id) focused=count?order[count-1u]:0u;
    dragging=0u; return 0;
}
int window_get(sand_window_id_t id,sand_window_t *out) {
    int slot=slot_for(id); if(slot<0||out==0) return -1; *out=windows[slot]; return 0;
}
static void raise_window(sand_window_id_t id) {
    uint32_t i=0u; while(i<count&&order[i]!=id) ++i; if(i==count) return;
    for(;i+1u<count;++i) { order[i]=order[i+1u]; }
    order[count-1u]=id;
    for(uint32_t j=0;j<SAND_WINDOW_MAX;++j) windows[j].focused=(uint8_t)(ids[j]==id&&windows[j].used);
    focused=id;
}
void window_manager_pointer(int32_t x,int32_t y,uint8_t buttons) {
    pointer_x=x; pointer_y=y;
    if((buttons&1u)!=0u && (previous_buttons&1u)==0u) {
        for(uint32_t n=count;n>0u;--n) {
            sand_window_id_t id=order[n-1u]; int s=slot_for(id); if(s<0) continue;
            sand_window_t *w=&windows[s];
            if(!w->visible||x<w->x||y<w->y||x>=w->x+(int32_t)w->width||y>=w->y+(int32_t)w->height) continue;
            raise_window(id);
            if(y<w->y+28) { dragging=id; drag_x=x-w->x; drag_y=y-w->y; }
            break;
        }
    }
    if((buttons&1u)!=0u && dragging!=0u) {
        int s=slot_for(dragging); if(s>=0) { windows[s].x=x-drag_x; windows[s].y=y-drag_y; }
    }
    if((buttons&1u)==0u) dragging=0u;
    previous_buttons=buttons;
}
void window_manager_render(sand_framebuffer_t *fb) {
    if(!graphics_framebuffer_valid(fb)) return;
    for(uint32_t n=0;n<count;++n) {
        int s=slot_for(order[n]); if(s<0) continue;
        sand_window_t *w=&windows[s]; if(!w->visible) continue;
        uint32_t title=w->focused?0x24566Au:0x253448u;
        (void)graphics_fill_round_rect(fb,w->x,w->y,w->width,w->height,9u,0x0A1420u);
        (void)graphics_fill_round_rect(fb,w->x,w->y,w->width,30u,8u,title);
        (void)graphics_fill_rect(fb,w->x,w->y+22,(int32_t)w->width,8u,title);
        for(uint32_t k=0;k<3u;++k) {
            uint32_t c=k==0u?0xE98282u:(k==1u?0xE5C477u:0x72D5B8u);
            (void)graphics_fill_round_rect(fb,w->x+12+(int32_t)k*15,w->y+10,8u,8u,4u,c);
        }
        (void)graphics_fill_rect(fb,w->x+12,w->y+43,(int32_t)w->width-24,1u,0x26394Bu);
        (void)w->title;
    }
}
