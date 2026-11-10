#include "sand/arch/x86/input.h"
#define PS2_STATUS 0x64u
#define PS2_DATA 0x60u
static uint8_t mouse_packet[3];
static uint8_t mouse_index;
static uint8_t mouse_buttons;
static int wait_input_clear(void) {
    for(uint32_t i=0;i<100000u;++i) { uint8_t s; __asm__ volatile("inb %1,%0":"=a"(s):"Nd"((uint16_t)PS2_STATUS)); if((s&2u)==0u) return 0; }
    return -1;
}
static int wait_output_full(void) {
    for(uint32_t i=0;i<100000u;++i) { uint8_t s; __asm__ volatile("inb %1,%0":"=a"(s):"Nd"((uint16_t)PS2_STATUS)); if((s&1u)!=0u) return 0; }
    return -1;
}
static void out_port(uint16_t port,uint8_t value) { __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port)); }
static uint8_t in_port(uint16_t port) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }
static int controller_command(uint8_t command) { if(wait_input_clear()!=0) return -1; out_port(PS2_STATUS,command); return 0; }
static int device_write(uint8_t value,int mouse) {
    if(mouse && controller_command(0xD4u)!=0) return -1;
    if(wait_input_clear()!=0) return -1;
    out_port(PS2_DATA,value);
    if(wait_output_full()!=0) return -1;
    return in_port(PS2_DATA)==0xFAu?0:-1;
}
int ps2_input_init(void) {
    mouse_index=0u; mouse_buttons=0u;
    if(controller_command(0xA8u)!=0) return -1;
    if(controller_command(0x20u)!=0 || wait_output_full()!=0) return -1;
    uint8_t config=in_port(PS2_DATA);
    config=(uint8_t)((config|0x02u)&~0x20u);
    if(controller_command(0x60u)!=0 || wait_input_clear()!=0) return -1;
    out_port(PS2_DATA,config);
    if(device_write(0xF4u,0)!=0) return -1;
    if(device_write(0xF4u,1)!=0) return -1;
    return 0;
}
int ps2_input_poll(sand_input_event_t *event) {
    if(event==0) return -1;
    event->type=SAND_INPUT_NONE; event->key=0u; event->mouse_dx=0; event->mouse_dy=0; event->buttons=mouse_buttons;
    uint8_t status=in_port(PS2_STATUS);
    if((status&1u)==0u) return 0;
    uint8_t data=in_port(PS2_DATA);
    if((status&0x20u)!=0u) {
        if(mouse_index==0u && (data&0x08u)==0u) return 0;
        mouse_packet[mouse_index++]=data;
        if(mouse_index==3u) {
            mouse_index=0u; mouse_buttons=(uint8_t)(mouse_packet[0]&7u);
            event->type=SAND_INPUT_POINTER;
            event->mouse_dx=(mouse_packet[0]&0x10u)?(int16_t)((int16_t)mouse_packet[1]-256):(int16_t)mouse_packet[1];
            event->mouse_dy=(mouse_packet[0]&0x20u)?(int16_t)((int16_t)mouse_packet[2]-256):(int16_t)mouse_packet[2];
            event->buttons=mouse_buttons;
        }
    } else {
        /* Raw set-1 scancodes; 0xE0 prefix is retained in the high byte. */
        static uint8_t extended;
        if(data==0xE0u) { extended=1u; return 0; }
        event->type=SAND_INPUT_KEY;
        event->key=(uint16_t)data | (extended?0x0100u:0u);
        extended=0u;
    }
    return event->type==SAND_INPUT_NONE?0:1;
}
