#include "sand/graphics/desktop.h"

/* SAND native boot desktop. The font is intentionally embedded so the
 * kernel display never depends on a disk, app package, or userspace service. */
static const uint8_t font[36][5] = {
 {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
 {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
 {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
 {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
 {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
 {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
 {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
 {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
 {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
 {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
 {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
 {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
 {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
 {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
 {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
 {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};
static uint32_t glyph(char c) {
    if(c>='a'&&c<='z') c=(char)(c-'a'+'A');
    if(c>='0'&&c<='9') return (uint32_t)(c-'0');
    if(c>='A'&&c<='Z') return 10u+(uint32_t)(c-'A');
    return 36u;
}
static void txt(sand_framebuffer_t *fb,uint32_t x,uint32_t y,const char *s,uint32_t color,uint32_t scale) {
    while(*s) {
        uint32_t g=glyph(*s++);
        if(g<36u) for(uint32_t col=0;col<5u;++col)
            for(uint32_t row=0;row<7u;++row) if(font[g][col]&(1u<<row))
                (void)graphics_fill_rect(fb,(int32_t)(x+col*scale),(int32_t)(y+row*scale),scale,scale,color);
        x+=6u*scale;
    }
}
static void line(sand_framebuffer_t *fb,int32_t x0,int32_t y0,int32_t x1,int32_t y1,uint32_t c) {
    (void)graphics_draw_line(fb,x0,y0,x1,y1,c);
}
static void panel(sand_framebuffer_t *fb,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t color) {
    (void)graphics_fill_round_rect(fb,(int32_t)x+2,(int32_t)y+4,w,h,12u,0x070E18u);
    (void)graphics_fill_round_rect(fb,(int32_t)x,(int32_t)y,w,h,12u,color);
    (void)graphics_fill_rect(fb,(int32_t)x+14,(int32_t)y+1,w-28u,1u,0x34465Au);
}
static void dot(sand_framebuffer_t *fb,uint32_t x,uint32_t y,uint32_t color,uint32_t r) {
    for(uint32_t yy=0;yy<r*2u+1u;++yy) for(uint32_t xx=0;xx<r*2u+1u;++xx) {
        int32_t dx=(int32_t)xx-(int32_t)r,dy=(int32_t)yy-(int32_t)r;
        if((uint32_t)(dx*dx+dy*dy)<=r*r)
            (void)graphics_put_pixel(fb,(int32_t)(x+xx),(int32_t)(y+yy),color);
    }
}
static void wallpaper(sand_framebuffer_t *fb) {
    for(uint32_t y=0;y<fb->height;++y) {
        uint32_t t=y*255u/fb->height;
        uint32_t r=7u+t*7u/255u,g=14u+t*15u/255u,b=29u+t*34u/255u;
        (void)graphics_fill_rect(fb,0,(int32_t)y,fb->width,1u,(r<<16)|(g<<8)|b);
    }
    /* Low-contrast ambient rings and angled light. */
    uint32_t cx=fb->width*3u/4u,cy=fb->height/3u;
    for(uint32_t ring=0;ring<7u;++ring) {
        uint32_t radius=90u+ring*24u;
        int32_t px=(int32_t)(cx+radius),py=(int32_t)cy;
        for(uint32_t a=1u;a<=48u;++a) {
            uint32_t nx=cx+(radius*(48u-a)*(48u-a))/(48u*48u);
            (void)nx;
            int32_t x=(int32_t)(cx+(radius*(a<24u?a:48u-a))/24u);
            int32_t y=(int32_t)(cy+(a<24u?1:-1)*(int32_t)(radius*a*(48u-a)/576u));
            line(fb,px,py,x,y,0x142B43u); px=x; py=y;
        }
    }
}
static void status_card(sand_framebuffer_t *fb,uint32_t x,uint32_t y,uint32_t w,
                        uint32_t accent,const char *label,const char *value,const char *detail,uint32_t kind) {
    panel(fb,x,y,w,112u,0x111F30u);
    dot(fb,x+25u,y+27u,accent,4u);
    if(kind==0u) {
        (void)graphics_fill_round_rect(fb,(int32_t)x+43,(int32_t)y+17,25u,20u,4u,0x263D52u);
        (void)graphics_fill_rect(fb,(int32_t)x+48,(int32_t)y+22,15u,3u,accent);
        (void)graphics_fill_rect(fb,(int32_t)x+48,(int32_t)y+29,10u,3u,accent);
    } else if(kind==1u) {
        (void)graphics_fill_round_rect(fb,(int32_t)x+45,(int32_t)y+17,20u,20u,5u,0x263D52u);
        line(fb,(int32_t)x+55,(int32_t)y+20,(int32_t)x+55,(int32_t)y+34,accent);
        line(fb,(int32_t)x+48,(int32_t)y+27,(int32_t)x+62,(int32_t)y+27,accent);
    } else {
        (void)graphics_fill_round_rect(fb,(int32_t)x+45,(int32_t)y+17,20u,20u,4u,0x263D52u);
        line(fb,(int32_t)x+49,(int32_t)y+22,(int32_t)x+61,(int32_t)y+22,accent);
        line(fb,(int32_t)x+49,(int32_t)y+27,(int32_t)x+61,(int32_t)y+27,accent);
        line(fb,(int32_t)x+49,(int32_t)y+32,(int32_t)x+58,(int32_t)y+32,accent);
    }
    txt(fb,x+18u,y+53u,label,0x8EA5BCu,1u);
    txt(fb,x+18u,y+70u,value,0xEAF2FAu,2u);
    txt(fb,x+18u,y+96u,detail,accent,1u);
}
int sand_desktop_render(sand_framebuffer_t *fb) {
    if(!graphics_framebuffer_valid(fb)||fb->width<640u||fb->height<480u) return -1;
    wallpaper(fb);
    /* Top panel */
    (void)graphics_fill_rect(fb,0,0,fb->width,48u,0x091321u);
    (void)graphics_fill_rect(fb,0,47,(uint32_t)fb->width,1u,0x26394Fu);
    (void)graphics_fill_round_rect(fb,18,10,29u,28u,8u,0x18394Au);
    txt(fb,25,20,"S",0x69E4CFu,2u);
    txt(fb,59,18,"SAND",0xE7F0F8u,2u);
    txt(fb,127,22,"SYSTEM",0x8FA7BEu,1u);
    dot(fb,fb->width-154u,23u,0x60DCC5u,3u);
    txt(fb,fb->width-140u,19u,"ONLINE",0xA8BACB,1u);
    /* Main heading */
    txt(fb,42u,82u,"YOUR SPACE",0x72E2D0u,1u);
    txt(fb,40u,103u,"A CLEARER WAY TO COMPUTE",0xF0F5FAu,2u);
    txt(fb,42u,129u,"SAND OS KERNEL SESSION",0x91A7BDu,1u);
    uint32_t margin=40u,gap=14u,content=fb->width-margin*2u;
    uint32_t cw=(content-gap*2u)/3u,cy=164u;
    status_card(fb,margin,cy,cw,0x62DCC5u,"MEMORY","PAGING","FRAME MANAGER",0u);
    status_card(fb,margin+cw+gap,cy,cw,0x91BDF0u,"PROCESSES","SCHEDULER","PREEMPTIVE CORE",1u);
    status_card(fb,margin+(cw+gap)*2u,cy,cw,0xC5A8F0u,"STORAGE","SANDFS","NATIVE FILESYSTEM",2u);
    /* Wide system panel with actual architecture information, not empty filler. */
    uint32_t py=cy+132u,ph=fb->height-py-104u;
    panel(fb,margin,py,content,ph,0x101E2Eu);
    txt(fb,margin+22u,py+20u,"SYSTEM STATUS",0xEAF2FAu,1u);
    (void)graphics_fill_rect(fb,(int32_t)margin+22,(int32_t)py+40u,content-44u,1u,0x2A4055u);
    uint32_t col1=margin+24u,col2=margin+content/3u,col3=margin+content*2u/3u;
    txt(fb,col1,py+57u,"ARCHITECTURE",0x7894AEu,1u);
    txt(fb,col1,py+79u,"X86 32 BIT",0xDDE8F2u,2u);
    txt(fb,col2,py+57u,"DISPLAY",0x7894AEu,1u);
    txt(fb,col2,py+79u,"VBE GRAPHICS",0xDDE8F2u,2u);
    txt(fb,col3,py+57u,"SESSION",0x7894AEu,1u);
    txt(fb,col3,py+79u,"KERNEL MODE",0xDDE8F2u,2u);
    /* Small live-state indicators along panel base. */
    dot(fb,col1,py+ph-19u,0x62DCC5u,3u);
    txt(fb,col1+12u,py+ph-23u,"CORE ACTIVE",0x91A7BDu,1u);
    dot(fb,col2,py+ph-19u,0x91BDF0u,3u);
    txt(fb,col2+12u,py+ph-23u,"FRAMEBUFFER",0x91A7BDu,1u);
    dot(fb,col3,py+ph-19u,0xC5A8F0u,3u);
    txt(fb,col3+12u,py+ph-23u,"NATIVE KERNEL",0x91A7BDu,1u);
    /* Dock */
    uint32_t dock_y=fb->height-62u;
    (void)graphics_fill_rect(fb,0,(int32_t)dock_y,fb->width,62u,0x08111Du);
    (void)graphics_fill_rect(fb,0,(int32_t)dock_y,fb->width,1u,0x2B4055u);
    uint32_t dx=fb->width/2u-48u;
    for(uint32_t i=0;i<3u;++i) {
        uint32_t x=dx+i*42u;
        (void)graphics_fill_round_rect(fb,(int32_t)x,(int32_t)dock_y+12,32u,32u,9u,
            i==0u?0x214B4B:(i==1u?0x263D59:0x3A3152));
        if(i==0u) {
            (void)graphics_fill_round_rect(fb,(int32_t)x+9,(int32_t)dock_y+20,14u,14u,4u,0x69E4CFu);
        } else if(i==1u) {
            line(fb,(int32_t)x+8,(int32_t)dock_y+28,(int32_t)x+16,(int32_t)dock_y+17,0x91BDF0u);
            line(fb,(int32_t)x+16,(int32_t)dock_y+17,(int32_t)x+24,(int32_t)dock_y+28,0x91BDF0u);
        } else {
            (void)graphics_fill_rect(fb,(int32_t)x+9,(int32_t)dock_y+20,14u,14u,0xC5A8F0u);
        }
    }
    txt(fb,18u,dock_y+27u,"SAND OS",0x91A7BDu,1u);
    txt(fb,fb->width-104u,dock_y+27u,"READY",0x69E4CFu,1u);
    return 0;
}
