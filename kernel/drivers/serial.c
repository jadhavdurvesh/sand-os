#include "sand/drivers/serial.h"
#include "sand/drivers/device.h"
#include "sand/arch/x86/irq.h"
#include "sand/console/console.h"
#define COM1 0x3F8u
#define REG_DATA 0u
#define REG_IER 1u
#define REG_FCR 2u
#define REG_LCR 3u
#define REG_MCR 4u
#define REG_LSR 5u
#define LSR_RX_READY 0x01u
#define LSR_TX_EMPTY 0x20u
static int present;
static inline void outb(uint16_t port,uint8_t value) { __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }
static int wait_tx(void) {
    for(uint32_t i=0;i<100000u;++i) if(inb((uint16_t)(COM1+REG_LSR))&LSR_TX_EMPTY) return 0;
    return -1;
}
static void put_raw(uint8_t byte) { if(wait_tx()==0) outb((uint16_t)(COM1+REG_DATA),byte); }
void serial_write(const char *data,uint32_t length) {
    if(!present) return;
    for(uint32_t i=0;i<length;++i) {
        if(data[i]=='\n') put_raw('\r');
        put_raw((uint8_t)data[i]);
    }
}
static void serial_irq(void) {
    while(inb((uint16_t)(COM1+REG_LSR))&LSR_RX_READY) {
        uint8_t byte=inb((uint16_t)(COM1+REG_DATA));
        if(byte=='\r') byte='\n';
        if(byte==0x7Fu) byte=0x08u;
        console_input_push(byte);
    }
}
int serial_loopback_test(void) {
    static const uint8_t pattern[]={0x53,0x41,0x4E,0x44,0x00,0xFF,0x5A};
    outb((uint16_t)(COM1+REG_MCR),0x1Eu);   /* loopback mode, OUT1/OUT2 */
    for(uint32_t i=0;i<sizeof(pattern);++i) {
        if(wait_tx()!=0) return -1;
        outb((uint16_t)(COM1+REG_DATA),pattern[i]);
        uint32_t spin=0u;
        while(!(inb((uint16_t)(COM1+REG_LSR))&LSR_RX_READY)) if(++spin>100000u) return -1;
        if(inb((uint16_t)(COM1+REG_DATA))!=pattern[i]) return -1;
    }
    outb((uint16_t)(COM1+REG_MCR),0x0Bu);   /* normal operation, IRQ gate (OUT2) on */
    return 0;
}
int serial_init(void) {
    outb((uint16_t)(COM1+REG_IER),0x00u);   /* no interrupts while configuring */
    outb((uint16_t)(COM1+REG_LCR),0x80u);   /* DLAB */
    outb((uint16_t)(COM1+REG_DATA),0x01u);  /* divisor 1 = 115200 baud */
    outb((uint16_t)(COM1+REG_IER),0x00u);
    outb((uint16_t)(COM1+REG_LCR),0x03u);   /* 8 data bits, no parity, 1 stop */
    outb((uint16_t)(COM1+REG_FCR),0xC7u);   /* enable and clear FIFOs, 14-byte trigger */
    if(serial_loopback_test()!=0) { (void)device_register("serial0","uart","ns16550",SAND_DEVICE_FAILED); return -1; }
    while(inb((uint16_t)(COM1+REG_LSR))&LSR_RX_READY) (void)inb((uint16_t)(COM1+REG_DATA)); /* drain */
    present=1;
    (void)console_add_sink(serial_write);
    if(irq_install(4u,serial_irq)!=0) return -1;
    outb((uint16_t)(COM1+REG_IER),0x01u);   /* RX data available */
    (void)device_register("serial0","uart","ns16550",SAND_DEVICE_READY);
    return 0;
}
int serial_ready(void) { return present; }
