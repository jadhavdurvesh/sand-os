#include "sand/arch/x86/ata_pio.h"
#include "sand/drivers/device.h"
#include "sand/storage/block.h"
#define ATA_REG_DATA 0u
#define ATA_REG_SECCOUNT 2u
#define ATA_REG_LBA0 3u
#define ATA_REG_LBA1 4u
#define ATA_REG_LBA2 5u
#define ATA_REG_DRIVE 6u
#define ATA_REG_STATUS 7u
#define ATA_REG_COMMAND 7u
#define ATA_CMD_IDENTIFY 0xECu
#define ATA_CMD_READ 0x20u
#define ATA_CMD_WRITE 0x30u
#define ATA_CMD_FLUSH 0xE7u
#define ATA_STATUS_ERR 0x01u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_DF 0x20u
#define ATA_STATUS_BSY 0x80u
#define ATA_TIMEOUT 1000000u

struct ata_drive { uint16_t io; uint16_t control; uint8_t slave; uint8_t present; uint32_t sectors; uint32_t position; char name[5]; struct sand_block_device block; };
static struct ata_drive drives[SAND_ATA_MAX_DRIVES];
static uint32_t drive_total;

static inline void outb(uint16_t port,uint8_t value) { __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port)); return value; }
static inline uint16_t inw(uint16_t port) { uint16_t value; __asm__ volatile("inw %1,%0":"=a"(value):"Nd"(port)); return value; }
static inline void outw(uint16_t port,uint16_t value) { __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port)); }
static void delay_400ns(const struct ata_drive *d) { for(uint32_t i=0;i<4u;++i) (void)inb(d->control); }
static int wait_not_busy(const struct ata_drive *d) {
    for(uint32_t i=0;i<ATA_TIMEOUT;++i) {
        uint8_t s=inb((uint16_t)(d->io+ATA_REG_STATUS));
        if(s==0u || s==0xFFu) return -1;
        if((s&ATA_STATUS_BSY)==0u) return (s&(ATA_STATUS_ERR|ATA_STATUS_DF))?-1:0;
    }
    return -1;
}
static int wait_data(const struct ata_drive *d) {
    for(uint32_t i=0;i<ATA_TIMEOUT;++i) {
        uint8_t s=inb((uint16_t)(d->io+ATA_REG_STATUS));
        if(s==0u || s==0xFFu || (s&(ATA_STATUS_ERR|ATA_STATUS_DF))) return -1;
        if((s&ATA_STATUS_BSY)==0u && (s&ATA_STATUS_DRQ)!=0u) return 0;
    }
    return -1;
}
static void select_lba(const struct ata_drive *d,uint32_t lba) {
    outb((uint16_t)(d->io+ATA_REG_DRIVE),(uint8_t)(0xE0u|(d->slave?0x10u:0u)|((lba>>24)&0x0Fu)));
    delay_400ns(d);
    outb((uint16_t)(d->io+ATA_REG_SECCOUNT),1u);
    outb((uint16_t)(d->io+ATA_REG_LBA0),(uint8_t)lba);
    outb((uint16_t)(d->io+ATA_REG_LBA1),(uint8_t)(lba>>8));
    outb((uint16_t)(d->io+ATA_REG_LBA2),(uint8_t)(lba>>16));
}
/* Select the target drive and let the bus settle BEFORE sampling its status:
 * a previous probe may have left an absent drive selected on this channel. */
static void select_drive(const struct ata_drive *d) {
    outb((uint16_t)(d->io+ATA_REG_DRIVE),(uint8_t)(0xE0u|(d->slave?0x10u:0u)));
    delay_400ns(d);
}
static int drive_read(const struct ata_drive *d,uint32_t lba,uint8_t *buffer) {
    if(buffer==0 || lba>=d->sectors) return -1;
    select_drive(d);
    if(wait_not_busy(d)!=0) return -1;
    select_lba(d,lba); outb((uint16_t)(d->io+ATA_REG_COMMAND),ATA_CMD_READ); delay_400ns(d);
    if(wait_data(d)!=0) return -1;
    for(uint32_t i=0;i<256u;++i) { uint16_t v=inw((uint16_t)(d->io+ATA_REG_DATA)); buffer[i*2u]=(uint8_t)v; buffer[i*2u+1u]=(uint8_t)(v>>8); }
    return wait_not_busy(d);
}
static int drive_write(const struct ata_drive *d,uint32_t lba,const uint8_t *buffer) {
    if(buffer==0 || lba>=d->sectors) return -1;
    select_drive(d);
    if(wait_not_busy(d)!=0) return -1;
    select_lba(d,lba); outb((uint16_t)(d->io+ATA_REG_COMMAND),ATA_CMD_WRITE); delay_400ns(d);
    if(wait_data(d)!=0) return -1;
    for(uint32_t i=0;i<256u;++i) outw((uint16_t)(d->io+ATA_REG_DATA),(uint16_t)buffer[i*2u]|((uint16_t)buffer[i*2u+1u]<<8));
    outb((uint16_t)(d->io+ATA_REG_COMMAND),ATA_CMD_FLUSH); return wait_not_busy(d);
}
/* The block layer's callbacks carry no context pointer, so each drive slot
 * gets its own tiny trampolines. */
#define TRAMPOLINES(n) \
    static int read_##n(uint32_t lba,uint8_t *b) { return drive_read(&drives[n],lba,b); } \
    static int write_##n(uint32_t lba,const uint8_t *b) { return drive_write(&drives[n],lba,b); }
TRAMPOLINES(0) TRAMPOLINES(1) TRAMPOLINES(2) TRAMPOLINES(3)
static int (*const read_table[SAND_ATA_MAX_DRIVES])(uint32_t,uint8_t *)={read_0,read_1,read_2,read_3};
static int (*const write_table[SAND_ATA_MAX_DRIVES])(uint32_t,const uint8_t *)={write_0,write_1,write_2,write_3};

static int identify(struct ata_drive *d) {
    outb((uint16_t)(d->io+ATA_REG_DRIVE),(uint8_t)(0xA0u|(d->slave?0x10u:0u)));
    delay_400ns(d);
    outb((uint16_t)(d->io+ATA_REG_SECCOUNT),0u); outb((uint16_t)(d->io+ATA_REG_LBA0),0u);
    outb((uint16_t)(d->io+ATA_REG_LBA1),0u); outb((uint16_t)(d->io+ATA_REG_LBA2),0u);
    outb((uint16_t)(d->io+ATA_REG_COMMAND),ATA_CMD_IDENTIFY);
    uint8_t status=inb((uint16_t)(d->io+ATA_REG_STATUS));
    if(status==0u || status==0xFFu) return -1;          /* nothing on this position */
    for(uint32_t i=0;i<ATA_TIMEOUT && (inb((uint16_t)(d->io+ATA_REG_STATUS))&ATA_STATUS_BSY);++i) {}
    if(inb((uint16_t)(d->io+ATA_REG_LBA1))!=0u || inb((uint16_t)(d->io+ATA_REG_LBA2))!=0u) return -1; /* ATAPI/SATA signature: not PATA disk */
    if(wait_data(d)!=0) return -1;
    uint16_t id[256]; for(uint32_t i=0;i<256u;++i) id[i]=inw((uint16_t)(d->io+ATA_REG_DATA));
    uint32_t sectors=(uint32_t)id[60]|((uint32_t)id[61]<<16);
    if(sectors==0u || sectors>0x10000000u) return -1;
    d->sectors=sectors; return 0;
}
int ata_pio_init(void) {
    static const uint16_t io[2]={0x1F0u,0x170u}, ctl[2]={0x3F6u,0x376u};
    drive_total=0u;
    for(uint32_t position=0;position<4u;++position) {
        struct ata_drive probe={0}; probe.io=io[position>>1]; probe.control=ctl[position>>1]; probe.slave=(uint8_t)(position&1u);
        if(identify(&probe)!=0) continue;
        if(drive_total>=SAND_ATA_MAX_DRIVES) break;
        uint32_t n=drive_total; drives[n]=probe; drives[n].position=position; drives[n].present=1u;
        drives[n].name[0]='a'; drives[n].name[1]='t'; drives[n].name[2]='a'; drives[n].name[3]=(char)('0'+n); drives[n].name[4]='\0';
        drives[n].block.name=drives[n].name; drives[n].block.sector_count=probe.sectors;
        drives[n].block.read=read_table[n]; drives[n].block.write=write_table[n];
        if(block_register(&drives[n].block)!=0) continue;
        (void)device_register(drives[n].name,"disk",position<2u?"ata-pio-pri":"ata-pio-sec",SAND_DEVICE_READY);
        ++drive_total;
    }
    return drive_total?0:-1;
}
uint32_t ata_pio_drive_count(void) { return drive_total; }
int ata_pio_drive_position(uint32_t index,uint32_t *position_out) {
    if(index>=drive_total || position_out==0) return -1;
    *position_out=drives[index].position; return 0;
}
int ata_pio_read(uint32_t lba,uint8_t *buffer) { return drive_total?drive_read(&drives[0],lba,buffer):-1; }
int ata_pio_write(uint32_t lba,const uint8_t *buffer) { return drive_total?drive_write(&drives[0],lba,buffer):-1; }
uint32_t ata_pio_sector_count(void) { return drive_total?drives[0].sectors:0u; }
