#include "sand/graphics/desktop.h"

/* Tiny built-in 5x7 bitmap font. The kernel desktop deliberately owns its
 * first font so boot visuals do not depend on an application or disk asset. */
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
static uint32_t glyph_index(char c) {
    if(c>='a'&&c<='z') c=(char)(c-'a'+'A');
    if(c>='0'&&c<='9') return (uint32_t)(c-'0');
    if(c>='A'&&c<='Z') return 10u+(uint32_t)(c-'A');
    return 36u;
}
static void pixel(sand_framebuffer_t *fb,uint32_t x,uint32_t y,uint32_t color) {
    if(x<fb->width&&y<fb->height) (void)graphics_put_pixel(fb,(int32_t)x,(int32_t)y,color);
}
static void text(sand_framebuffer_t *fb,uint32_t x,uint32_t y,const char *s,uint32_t color,uint32_t scale) {
    while(*s) {
        uint32_t g=glyph_index(*s++);
        if(g<36u) for(uint32_t col=0;col<5u;++col)
            for(uint32_t row=0;row<7u;++row) if((font[g][col]&(1u<<row))!=0u)
                for(uint32_t sy=0;sy<scale;++sy) for(uint32_t sx=0;sx<scale;++sx)
                    pixel(fb,x+col*scale+sx,y+row*scale+sy,color);
        x+=6u*scale;
    }
}
static void gradient(sand_framebuffer_t *fb) {
    for(uint32_t y=0;y<fb->height;++y) {
        uint32_t t=(y*255u)/fb->height;
        uint32_t r=9u+(t*8u)/255u, g=17u+(t*19u)/255u, b=34u+(t*34u)/255u;
        uint32_t color=(r<<16)|(g<<8)|b;
        (void)graphics_fill_rect(fb,0,(int32_t)y,fb->width,1u,color);
    }
}
static void card(sand_framebuffer_t *fb,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t fill) {
    (void)graphics_fill_rect(fb,(int32_t)x,(int32_t)y,w,h,0x0B1422u);
    if(w>4u&&h>4u) (void)graphics_fill_rect(fb,(int32_t)x+1,(int32_t)y+1,w-2u,h-2u,fill);
    (void)graphics_fill_rect(fb,(int32_t)x+12,(int32_t)y+12,w-24u,1u,0x34445Bu);
}
static void icon(sand_framebuffer_t *fb,uint32_t x,uint32_t y,uint32_t color,uint32_t kind) {
    (void)graphics_fill_rect(fb,(int32_t)x,(int32_t)y,42u,42u,0x263A50u);
    if(kind==0u) {
        (void)graphics_fill_rect(fb,(int32_t)x+10,(int32_t)y+10,22u,22u,color);
        (void)graphics_fill_rect(fb,(int32_t)x+15,(int32_t)y+15,12u,12u,0x263A50u);
    } else if(kind==1u) {
        (void)graphics_fill_rect(fb,(int32_t)x+9,(int32_t)y+9,24u,24u,color);
        (void)graphics_fill_rect(fb,(int32_t)x+19,(int32_t)y+5,4u,32u,0x263A50u);
        (void)graphics_fill_rect(fb,(int32_t)x+5,(int32_t)y+19,32u,4u,0x263A50u);
    } else {
        (void)graphics_draw_line(fb,(int32_t)x+9,(int32_t)y+30,(int32_t)x+21,(int32_t)y+9,color);
        (void)graphics_draw_line(fb,(int32_t)x+21,(int32_t)y+9,(int32_t)x+33,(int32_t)y+30,color);
        (void)graphics_draw_line(fb,(int32_t)x+9,(int32_t)y+30,(int32_t)x+33,(int32_t)y+30,color);
    }
}
int sand_desktop_render(sand_framebuffer_t *fb) {
    if(!graphics_framebuffer_valid(fb)||fb->width<640u||fb->height<400u) return -1;
    gradient(fb);
    /* Soft geometric wallpaper accents. */
    for(uint32_t i=0;i<8u;++i) {
        uint32_t x=fb->width*2u/3u+i*18u, y=fb->height/5u+i*13u;
        (void)graphics_draw_line(fb,(int32_t)x,(int32_t)y,(int32_t)(x+210u),(int32_t)(y+150u),0x1B3550u);
    }
    /* Top system bar. */
    (void)graphics_fill_rect(fb,0,0,fb->width,38u,0x09111Eu);
    (void)graphics_fill_rect(fb,0,37,fb->width,1u,0x26394Fu);
    text(fb,22,14,"SAND",0x75E6D0u,1u);
    text(fb,88,14,"SYSTEM",0xC4D0DEu,1u);
    text(fb,fb->width-190u,14,"WORKSPACE  01",0x91A6BCu,1u);
    /* Desktop heading and status. */
    text(fb,48,78,"GOOD DAY",0xF0F5FAu,3u);
    text(fb,50,110,"YOUR SYSTEM IS READY",0x8FA7BFu,1u);
    uint32_t left=48u, top=160u, gap=18u;
    uint32_t content=fb->width-96u;
    uint32_t col=(content-gap*2u)/3u;
    card(fb,left,top,col,126u,0x17283Cu);
    card(fb,left+col+gap,top,col,126u,0x17283Cu);
    card(fb,left+(col+gap)*2u,top,col,126u,0x17283Cu);
    icon(fb,left+20u,top+24u,0x59DCC5u,0u);
    icon(fb,left+col+gap+20u,top+24u,0x8DB7F0u,1u);
    icon(fb,left+(col+gap)*2u+20u,top+24u,0xD7B5F4u,2u);
    text(fb,left+76u,top+36u,"MEMORY",0xEDF4FBu,1u);
    text(fb,left+76u,top+58u,"PAGING",0x91A7BEu,1u);
    text(fb,left+col+gap+76u,top+36u,"PROCESSES",0xEDF4FBu,1u);
    text(fb,left+col+gap+76u,top+58u,"SCHEDULER",0x91A7BEu,1u);
    text(fb,left+(col+gap)*2u+76u,top+36u,"STORAGE",0xEDF4FBu,1u);
    text(fb,left+(col+gap)*2u+76u,top+58u,"SANDFS",0x91A7BEu,1u);
    /* System overview panel. */
    uint32_t panel_y=top+150u, panel_h=fb->height-panel_y-88u;
    card(fb,left,panel_y,content,panel_h,0x111F31u);
    text(fb,left+22u,panel_y+22u,"SYSTEM OVERVIEW",0xE4EDF6u,1u);
    text(fb,left+22u,panel_y+52u,"KERNEL",0x7F9AB5u,1u);
    text(fb,left+22u,panel_y+76u,"32 BIT X86",0xD2DFEBu,2u);
    text(fb,left+content/3u,panel_y+52u,"DISPLAY",0x7F9AB5u,1u);
    text(fb,left+content/3u,panel_y+76u,"VBE FRAMEBUFFER",0xD2DFEBu,1u);
    text(fb,left+content*2u/3u,panel_y+52u,"SESSION",0x7F9AB5u,1u);
    text(fb,left+content*2u/3u,panel_y+76u,"LOCAL CONSOLE",0xD2DFEBu,1u);
    /* Bottom dock with a deliberate visual hierarchy. */
    uint32_t dock_y=fb->height-58u;
    (void)graphics_fill_rect(fb,0,(int32_t)dock_y,fb->width,58u,0x080F1Au);
    (void)graphics_fill_rect(fb,0,(int32_t)dock_y,fb->width,1u,0x31445A);
    uint32_t dock_x=fb->width/2u-86u;
    icon(fb,dock_x,dock_y+8u,0x59DCC5u,0u);
    icon(fb,dock_x+54u,dock_y+8u,0x8DB7F0u,1u);
    icon(fb,dock_x+108u,dock_y+8u,0xD7B5F4u,2u);
    text(fb,18u,dock_y+24u,"SAND OS",0x91A6BCu,1u);
    text(fb,fb->width-130u,dock_y+24u,"READY",0x75E6D0u,1u);
    return 0;
}
