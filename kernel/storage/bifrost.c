#include "sand/storage/bifrost.h"
#include "sand/storage/block.h"
#include "sand/arch/x86/paging.h"
#include "sand/drivers/device.h"
#define MMIO_VA 0xF0000000u          /* kernel-only window above the user range (directory entries >= 768 are shared) */
#define PORT_MAX 32u
#define H_CAP 0x00u
#define H_GHC 0x04u
#define H_PI 0x0Cu
#define P_BASE(n) (0x100u + 0x80u * (n))
#define P_CLB 0x00u
#define P_CLBU 0x04u
#define P_FB 0x08u
#define P_FBU 0x0Cu
#define P_IS 0x10u
#define P_CMD 0x18u
#define P_TFD 0x20u
#define P_SIG 0x24u
#define P_SSTS 0x28u
#define P_SERR 0x30u
#define P_CI 0x38u
#define CMD_ST 0x1u
#define CMD_FRE 0x10u
#define CMD_FR 0x4000u
#define CMD_CR 0x8000u
#define SPIN 4000000u

struct port_mem {
    uint8_t clb[1024] __attribute__((aligned(1024)));      /* 32 command headers */
    uint8_t fis[256] __attribute__((aligned(256)));        /* received FIS area */
    uint8_t table[256] __attribute__((aligned(128)));      /* CFIS (0x00), PRDT (0x80) */
};
static struct port_mem mem[SAND_BIFROST_MAX_PORTS];
static uint8_t bounce[512] __attribute__((aligned(16)));
static volatile uint8_t *hba;
static uint32_t disk_port[SAND_BIFROST_MAX_PORTS], disk_sectors[SAND_BIFROST_MAX_PORTS], disk_total;
static char disk_name[SAND_BIFROST_MAX_PORTS][6];
static struct sand_block_device blocks[SAND_BIFROST_MAX_PORTS];

static inline uint32_t rd(uint32_t off) { return *(volatile uint32_t *)(hba + off); }
static inline void wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(hba + off) = v; }
static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(port)); }
static inline uint32_t inl(uint16_t port) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(port)); return v; }
static uint32_t pci_read(uint32_t slot, uint32_t reg) { outl(0xCF8, 0x80000000u | (slot << 11) | (reg & 0xFCu)); return inl(0xCFC); }
static void pci_write(uint32_t slot, uint32_t reg, uint32_t v) { outl(0xCF8, 0x80000000u | (slot << 11) | (reg & 0xFCu)); outl(0xCFC, v); }
static void zero(void *p, uint32_t n) { uint8_t *b = (uint8_t *)p; for (uint32_t i = 0; i < n; ++i) b[i] = 0; }

static int port_stop(uint32_t pb) {
    wr(pb + P_CMD, rd(pb + P_CMD) & ~(CMD_ST | CMD_FRE));
    for (uint32_t i = 0; i < SPIN; ++i) if ((rd(pb + P_CMD) & (CMD_FR | CMD_CR)) == 0u) return 0;
    return -1;
}
/* One 512-byte command in slot 0. cmd: 0xEC identify, 0x25 read DMA ext, 0x35 write DMA ext, 0xEA flush. */
static int issue(uint32_t slot, uint8_t cmd, uint32_t lba, int write, int data) {
    uint32_t pb = P_BASE(disk_port[slot]);
    struct port_mem *m = &mem[slot];
    uint32_t *hdr = (uint32_t *)m->clb; uint8_t *t = m->table;
    zero(t, 256);
    t[0] = 0x27; t[1] = 0x80; t[2] = cmd; t[7] = 0x40;                 /* host-to-device FIS, command bit, LBA mode */
    t[4] = (uint8_t)lba; t[5] = (uint8_t)(lba >> 8); t[6] = (uint8_t)(lba >> 16); t[8] = (uint8_t)(lba >> 24);
    t[12] = (cmd == 0x25 || cmd == 0x35) ? 1u : 0u;
    if (data) {
        uint32_t *prd = (uint32_t *)(t + 0x80);
        prd[0] = (uint32_t)bounce; prd[1] = 0u; prd[2] = 0u; prd[3] = 511u;
    }
    hdr[0] = 5u | (write ? 0x40u : 0u) | ((data ? 1u : 0u) << 16);       /* CFL = 5 dwords, W, PRDTL */
    hdr[1] = 0u; hdr[2] = (uint32_t)t; hdr[3] = 0u;
    wr(pb + P_IS, 0xFFFFFFFFu); wr(pb + P_SERR, 0xFFFFFFFFu);
    for (uint32_t i = 0; i < SPIN && (rd(pb + P_TFD) & 0x88u) != 0u; ++i) { }   /* BSY / DRQ clear */
    wr(pb + P_CI, 1u);
    for (uint32_t i = 0; i < SPIN; ++i) {
        if ((rd(pb + P_IS) & 0x40000000u) != 0u) return -1;               /* task file error */
        if ((rd(pb + P_CI) & 1u) == 0u) return (rd(pb + P_TFD) & 0x01u) ? -1 : 0;
    }
    return -1;
}
static int dread(uint32_t s, uint32_t lba, uint8_t *buf) {
    if (lba >= disk_sectors[s]) return -1;
    zero(bounce, 512);                                                         /* a failed DMA must not look like data */
    if (issue(s, 0x25, lba, 0, 1) != 0) return -1;
    for (uint32_t i = 0; i < 512u; ++i) buf[i] = bounce[i];
    return 0;
}
static int dwrite(uint32_t s, uint32_t lba, const uint8_t *buf) {
    if (lba >= disk_sectors[s]) return -1;
    for (uint32_t i = 0; i < 512u; ++i) bounce[i] = buf[i];
    if (issue(s, 0x35, lba, 1, 1) != 0) return -1;
    return issue(s, 0xEA, 0, 0, 0);                                          /* flush cache */
}
#define THUNK(n) static int r##n(uint32_t l, uint8_t *b) { return dread(n, l, b); } static int w##n(uint32_t l, const uint8_t *b) { return dwrite(n, l, b); }
THUNK(0) THUNK(1) THUNK(2) THUNK(3)
static int (*const read_table[4])(uint32_t, uint8_t *) = { r0, r1, r2, r3 };
static int (*const write_table[4])(uint32_t, const uint8_t *) = { w0, w1, w2, w3 };

static int port_up(uint32_t port, uint32_t slot) {
    uint32_t pb = P_BASE(port);
    if ((rd(pb + P_SSTS) & 0x0Fu) != 3u || ((rd(pb + P_SSTS) >> 8) & 0x0Fu) != 1u) return -1;   /* device present, link active */
    if (rd(pb + P_SIG) != 0x00000101u) return -1;                                               /* SATA disk, not ATAPI */
    if (port_stop(pb) != 0) return -1;
    zero(&mem[slot], sizeof(mem[slot]));
    wr(pb + P_CLB, (uint32_t)mem[slot].clb); wr(pb + P_CLBU, 0u);
    wr(pb + P_FB, (uint32_t)mem[slot].fis); wr(pb + P_FBU, 0u);
    wr(pb + P_IS, 0xFFFFFFFFu); wr(pb + P_SERR, 0xFFFFFFFFu);
    wr(pb + P_CMD, rd(pb + P_CMD) | CMD_FRE);
    wr(pb + P_CMD, rd(pb + P_CMD) | CMD_ST);
    disk_port[slot] = port;
    if (issue(slot, 0xEC, 0, 0, 1) != 0) return -1;                          /* IDENTIFY DEVICE */
    const uint16_t *id = (const uint16_t *)bounce;
    uint32_t sectors = ((uint32_t)id[101] << 16) | id[100];                  /* 48-bit count, low 32 bits */
    if (sectors == 0u) sectors = ((uint32_t)id[61] << 16) | id[60];
    if (sectors == 0u) return -1;
    disk_sectors[slot] = sectors;
    return 0;
}
int bifrost_init(void) {
    disk_total = 0u;
    for (uint32_t slot = 0; slot < 32u; ++slot) {
        uint32_t id = pci_read(slot, 0);
        if ((id & 0xFFFFu) == 0xFFFFu) continue;
        if ((pci_read(slot, 0x08) >> 8) != 0x010601u) continue;             /* mass storage / SATA / AHCI 1.0 */
        uint32_t abar = pci_read(slot, 0x24) & 0xFFFFFFF0u;
        if (abar == 0u) continue;
        pci_write(slot, 0x04, pci_read(slot, 0x04) | 0x6u);                  /* memory space + bus master */
        if (paging_reserve_table(MMIO_VA) != 0) return 0;
        for (uint32_t p = 0; p < 2u; ++p) if (paging_map_page(MMIO_VA + p * 0x1000u, abar + p * 0x1000u, 0x013u) != 0) return 0;   /* present, writable, cache-disabled */
        hba = (volatile uint8_t *)MMIO_VA;
        wr(H_GHC, rd(H_GHC) | 0x80000000u);                                  /* AHCI enable */
        uint32_t pi = rd(H_PI);
        for (uint32_t port = 0; port < PORT_MAX && disk_total < SAND_BIFROST_MAX_PORTS; ++port) {
            if ((pi & (1u << port)) == 0u) continue;
            if (port_up(port, disk_total) != 0) continue;
            uint32_t n = disk_total;
            disk_name[n][0] = 's'; disk_name[n][1] = 'a'; disk_name[n][2] = 't'; disk_name[n][3] = 'a'; disk_name[n][4] = (char)('0' + n); disk_name[n][5] = 0;
            blocks[n].name = disk_name[n]; blocks[n].sector_count = disk_sectors[n]; blocks[n].read = read_table[n]; blocks[n].write = write_table[n];
            if (block_register(&blocks[n]) != 0) continue;
            (void)device_register(disk_name[n], "disk", "ahci-bifrost", SAND_DEVICE_READY);
            ++disk_total;
        }
        break;                                                               /* first controller only */
    }
    return (int)disk_total;
}
int bifrost_self_test(uint32_t index) {
    if (index >= disk_total) return -1;
    uint32_t last = disk_sectors[index] - 1u;
    static uint8_t keep[512], pat[512], back[512];
    if (dread(index, last, keep) != 0) return -2;
    for (uint32_t i = 0; i < 512u; ++i) pat[i] = (uint8_t)(i * 7u + 0x5Au);
    if (dwrite(index, last, pat) != 0) return -3;
    zero(back, 512);
    if (dread(index, last, back) != 0) return -4;
    for (uint32_t i = 0; i < 512u; ++i) if (back[i] != pat[i]) { (void)dwrite(index, last, keep); return -5; }
    return dwrite(index, last, keep) == 0 ? 0 : -6;
}
int bifrost_seed_present(uint32_t index) {
    static uint8_t b[512]; static const char seed[] = "BIFROST-SEED";
    if (index >= disk_total || dread(index, 0u, b) != 0) return 0;
    for (uint32_t i = 0; i + 1u < sizeof(seed); ++i) if (b[i] != (uint8_t)seed[i]) return 0;
    return 1;
}
