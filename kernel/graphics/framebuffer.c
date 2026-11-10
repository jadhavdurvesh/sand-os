#include "sand/graphics/framebuffer.h"

int graphics_framebuffer_valid(const sand_framebuffer_t *fb) {
    if (fb == 0 || fb->pixels == 0) return 0;
    if (fb->width == 0u || fb->height == 0u) return 0;
    if (fb->bits_per_pixel != 24u && fb->bits_per_pixel != 32u) return 0;
    uint32_t bytes_per_pixel = fb->bits_per_pixel / 8u;
    if (fb->pitch_bytes < fb->width * bytes_per_pixel) return 0;
    return 1;
}

int graphics_put_pixel(sand_framebuffer_t *fb, int32_t x, int32_t y, uint32_t rgb) {
    if (!graphics_framebuffer_valid(fb)) return -1;
    if (x < 0 || y < 0 || (uint32_t)x >= fb->width || (uint32_t)y >= fb->height) return -1;
    uint32_t bytes_per_pixel = fb->bits_per_pixel / 8u;
    volatile uint8_t *pixel = fb->pixels + (uint32_t)y * fb->pitch_bytes + (uint32_t)x * bytes_per_pixel;
    pixel[0] = (uint8_t)rgb;
    pixel[1] = (uint8_t)(rgb >> 8);
    pixel[2] = (uint8_t)(rgb >> 16);
    if (bytes_per_pixel == 4u) pixel[3] = 0u;
    return 0;
}

int graphics_fill(sand_framebuffer_t *fb, uint32_t rgb) {
    if (!graphics_framebuffer_valid(fb)) return -1;
    for (uint32_t y = 0; y < fb->height; ++y)
        for (uint32_t x = 0; x < fb->width; ++x)
            (void)graphics_put_pixel(fb, (int32_t)x, (int32_t)y, rgb);
    return 0;
}

static void store_pixel(sand_framebuffer_t *fb,uint32_t x,uint32_t y,uint32_t rgb) {
    uint32_t bpp=fb->bits_per_pixel/8u;
    volatile uint8_t *p=fb->pixels+y*fb->pitch_bytes+x*bpp;
    p[0]=(uint8_t)rgb; p[1]=(uint8_t)(rgb>>8); p[2]=(uint8_t)(rgb>>16);
    if(bpp==4u) p[3]=0u;
}
int graphics_fill_rect(sand_framebuffer_t *fb,int32_t x,int32_t y,
                       uint32_t width,uint32_t height,uint32_t rgb) {
    if(!graphics_framebuffer_valid(fb)||width==0u||height==0u||x<0||y<0) return -1;
    if((uint32_t)x>=fb->width||(uint32_t)y>=fb->height) return -1;
    uint32_t x0=(uint32_t)x,y0=(uint32_t)y;
    uint32_t x1=width>fb->width-x0?fb->width:x0+width;
    uint32_t y1=height>fb->height-y0?fb->height:y0+height;
    for(uint32_t row=y0;row<y1;++row) {
        volatile uint8_t *p=fb->pixels+row*fb->pitch_bytes+x0*(fb->bits_per_pixel/8u);
        for(uint32_t col=x0;col<x1;++col) {
            p[0]=(uint8_t)rgb; p[1]=(uint8_t)(rgb>>8); p[2]=(uint8_t)(rgb>>16);
            if(fb->bits_per_pixel==32u) p[3]=0u;
            p+=fb->bits_per_pixel/8u;
        }
    }
    return 0;
}
int graphics_fill_round_rect(sand_framebuffer_t *fb,int32_t x,int32_t y,
                             uint32_t width,uint32_t height,uint32_t radius,uint32_t rgb) {
    if(!graphics_framebuffer_valid(fb)||width==0u||height==0u) return -1;
    uint32_t maxr=(width<height?width:height)/2u;
    if(radius>maxr) radius=maxr;
    if(radius==0u) return graphics_fill_rect(fb,x,y,width,height,rgb);
    for(uint32_t row=0;row<height;++row) {
        uint32_t inset=0u;
        if(row<radius || row>=height-radius) {
            uint32_t dy=row<radius?radius-1u-row:row-(height-radius);
            uint32_t rr=radius*radius, dy2=dy*dy;
            uint32_t dx2=rr>dy2?rr-dy2:0u, dx=0u;
            while((dx+1u)*(dx+1u)<=dx2) ++dx;
            inset=radius-1u-dx;
        }
        if(width>inset*2u)
            (void)graphics_fill_rect(fb,x+(int32_t)inset,y+(int32_t)row,width-inset*2u,1u,rgb);
    }
    return 0;
}
int graphics_blend_pixel(sand_framebuffer_t *fb,int32_t x,int32_t y,uint32_t rgb,uint8_t alpha) {
    if(!graphics_framebuffer_valid(fb)||x<0||y<0||(uint32_t)x>=fb->width||(uint32_t)y>=fb->height) return -1;
    uint32_t old=0u; uint32_t bpp=fb->bits_per_pixel/8u;
    volatile uint8_t *p=fb->pixels+(uint32_t)y*fb->pitch_bytes+(uint32_t)x*bpp;
    old=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);
    uint32_t inv=255u-alpha;
    uint32_t r=((((rgb>>16)&255u)*alpha+((old>>16)&255u)*inv)/255u);
    uint32_t g=((((rgb>>8)&255u)*alpha+((old>>8)&255u)*inv)/255u);
    uint32_t b=(((rgb&255u)*alpha+(old&255u)*inv)/255u);
    store_pixel(fb,(uint32_t)x,(uint32_t)y,(r<<16)|(g<<8)|b);
    return 0;
}

int graphics_draw_line(sand_framebuffer_t *fb, int32_t x0, int32_t y0,
                       int32_t x1, int32_t y1, uint32_t rgb) {
    if (!graphics_framebuffer_valid(fb)) return -1;
    int32_t dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy_abs = y1 >= y0 ? y1 - y0 : y0 - y1;
    int32_t dy = -dy_abs;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && (uint32_t)x0 < fb->width && (uint32_t)y0 < fb->height)
            (void)graphics_put_pixel(fb, x0, y0, rgb);
        if (x0 == x1 && y0 == y1) break;
        int32_t twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
    return 0;
}
