#include "sand/arch/x86/ata_pio.h"
#include "sand/storage/block.h"
#define ATA_DATA 0x1F0u
#define ATA_ERROR 0x1F1u
#define ATA_SECCOUNT 0x1F2u
#define ATA_LBA0 0x1F3u
#define ATA_LBA1 0x1F4u
#define ATA_LBA2 0x1F5u
#define ATA_DRIVE 0x1F6u
#define ATA_STATUS 0x1F7u
#define ATA_COMMAND 0x1F7u
#define ATA_CONTROL 0x3F6u
#define ATA_CMD_IDENTIFY 0xECu
#define ATA_CMD_READ 0x20u
#define ATA_CMD_WRITE 0x30u
#define ATA_STATUS_ERR 0x01u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_DF 0x20u
#define ATA_STATUS_BSY 0x80u
#define ATA_TIMEOUT 1000000u
static uint32_t disk_sectors;
static inline void outb(uint16_t port,uint8_t value) { __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port)); return value; }
static inline uint16_t inw(uint16_t port) { uint16_t value; __asm__ volatile("inw %1,%0":"=a"(value):"Nd"(port)); return value; }
static inline void outw(uint16_t port,uint16_t value) { __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port)); }
static void delay_400ns(void) { (void)inb(ATA_CONTROL); (void)inb(ATA_CONTROL); (void)inb(ATA_CONTROL); (void)inb(ATA_CONTROL); }
static int wait_not_busy(void) {
    for(uint32_t i=0;i<ATA_TIMEOUT;++i) { uint8_t s=inb(ATA_STATUS); if(s==0u || s==0xFFu) return -1; if((s&ATA_STATUS_BSY)==0u) return (s&(ATA_STATUS_ERR|ATA_STATUS_DF))? -1:0; }
    return -1;
}
static int wait_data(void) {
    for(uint32_t i=0;i<ATA_TIMEOUT;++i) { uint8_t s=inb(ATA_STATUS); if(s==0u || s==0xFFu || (s&(ATA_STATUS_ERR|ATA_STATUS_DF))) return -1; if((s&ATA_STATUS_BSY)==0u && (s&ATA_STATUS_DRQ)!=0u) return 0; }
    return -1;
}
static void select_lba(uint32_t lba) {
    outb(ATA_DRIVE,(uint8_t)(0xE0u|((lba>>24)&0x0Fu)));
    delay_400ns(); outb(ATA_SECCOUNT,1u); outb(ATA_LBA0,(uint8_t)lba);
    outb(ATA_LBA1,(uint8_t)(lba>>8)); outb(ATA_LBA2,(uint8_t)(lba>>16));
}
int ata_pio_read(uint32_t lba,uint8_t *buffer) {
    if(buffer==0 || lba>=disk_sectors || wait_not_busy()!=0) return -1;
    select_lba(lba); outb(ATA_COMMAND,ATA_CMD_READ); delay_400ns();
    if(wait_data()!=0) return -1;
    for(uint32_t i=0;i<256u;++i) { uint16_t v=inw(ATA_DATA); buffer[i*2u]=(uint8_t)v; buffer[i*2u+1u]=(uint8_t)(v>>8); }
    return wait_not_busy();
}
int ata_pio_write(uint32_t lba,const uint8_t *buffer) {
    if(buffer==0 || lba>=disk_sectors || wait_not_busy()!=0) return -1;
    select_lba(lba); outb(ATA_COMMAND,ATA_CMD_WRITE); delay_400ns();
    if(wait_data()!=0) return -1;
    for(uint32_t i=0;i<256u;++i) outw(ATA_DATA,(uint16_t)buffer[i*2u]|((uint16_t)buffer[i*2u+1u]<<8));
    outb(ATA_COMMAND,0xE7u); return wait_not_busy();
}
static int device_read(uint32_t lba,uint8_t *buffer) { return ata_pio_read(lba,buffer); }
static int device_write(uint32_t lba,const uint8_t *buffer) { return ata_pio_write(lba,buffer); }
static const struct sand_block_device primary_master={"ata0",0u,device_read,device_write};
int ata_pio_init(void) {
    outb(ATA_DRIVE,0xA0u); delay_400ns(); outb(ATA_SECCOUNT,0u); outb(ATA_LBA0,0u); outb(ATA_LBA1,0u); outb(ATA_LBA2,0u); outb(ATA_COMMAND,ATA_CMD_IDENTIFY);
    uint8_t status=inb(ATA_STATUS); if(status==0u || status==0xFFu) return -1;
    if(wait_data()!=0) return -1;
    uint16_t identify[256]; for(uint32_t i=0;i<256u;++i) identify[i]=inw(ATA_DATA);
    disk_sectors=(uint32_t)identify[60]|((uint32_t)identify[61]<<16);
    if(disk_sectors==0u || disk_sectors>0x10000000u) return -1;
    /* Register a stable descriptor whose capacity is now known. */
    static struct sand_block_device device;
    device.name=primary_master.name; device.sector_count=disk_sectors; device.read=device_read; device.write=device_write;
    return block_register(&device);
}
uint32_t ata_pio_sector_count(void) { return disk_sectors; }
