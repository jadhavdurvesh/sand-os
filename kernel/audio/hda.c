#include "sand/audio/hda.h"
#include "sand/arch/x86/paging.h"
#define HDA_VA 0xF0010000u                     /* inside the kernel MMIO window (page table present from paging_init) */
#define GCAP 0x00u
#define GCTL 0x08u
#define STATESTS 0x0Eu
#define ICOI 0x60u
#define ICII 0x64u
#define ICIS 0x68u
#define SD_CTL 0x00u
#define SD_LPIB 0x04u
#define SD_CBL 0x08u
#define SD_LVI 0x0Cu
#define SD_FMT 0x12u
#define SD_BDPL 0x18u
#define SD_BDPU 0x1Cu
#define STREAM_TAG 1u
#define MAX_PATH 8u

static volatile uint8_t *hd;
static uint32_t codec, afg, sd_base;
static uint32_t dacs[4], dac_total;
static uint32_t bdl[64 * 4 + 32] __attribute__((aligned(128)));   /* up to 24 entries of 16 bytes */

static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(port)); }
static inline uint32_t inl(uint16_t port) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(port)); return v; }
static inline void outb(uint16_t port, uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port)); }
static uint32_t pci_read(uint32_t slot, uint32_t reg) { outl(0xCF8, 0x80000000u | (slot << 11) | (reg & 0xFCu)); return inl(0xCFC); }
static void pci_write(uint32_t slot, uint32_t reg, uint32_t v) { outl(0xCF8, 0x80000000u | (slot << 11) | (reg & 0xFCu)); outl(0xCFC, v); }
static inline uint32_t r32(uint32_t o) { return *(volatile uint32_t *)(hd + o); }
static inline uint16_t r16(uint32_t o) { return *(volatile uint16_t *)(hd + o); }
static inline void w32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(hd + o) = v; }
static inline void w16(uint32_t o, uint16_t v) { *(volatile uint16_t *)(hd + o) = v; }
static inline void w8(uint32_t o, uint8_t v) { *(volatile uint8_t *)(hd + o) = v; }
static void settle(uint32_t n) { for (uint32_t i = 0; i < n; ++i) outb(0x80, 0); }   /* ~1 us per port-0x80 write */

/* One command through the immediate command interface. */
static int send(uint32_t cmd, uint32_t *resp) {
    for (uint32_t i = 0; i < 100000u; ++i) { if ((r16(ICIS) & 1u) == 0u) break; settle(1); }
    if (r16(ICIS) & 1u) return -1;
    w16(ICIS, 0x2u);                                     /* clear a stale "result valid" */
    w32(ICOI, cmd);
    w16(ICIS, 0x3u);                                     /* busy + clear valid: go */
    for (uint32_t i = 0; i < 200000u; ++i) {
        uint16_t s = r16(ICIS);
        if ((s & 1u) == 0u && (s & 2u) != 0u) { if (resp) *resp = r32(ICII); w16(ICIS, 0x2u); return 0; }
        settle(1);
    }
    return -1;
}
static int verb12(uint32_t nid, uint32_t verb, uint32_t data, uint32_t *resp) { return send((codec << 28) | (nid << 20) | (verb << 8) | (data & 0xFFu), resp); }
static int verb4(uint32_t nid, uint32_t verb, uint32_t data, uint32_t *resp) { return send((codec << 28) | (nid << 20) | (verb << 16) | (data & 0xFFFFu), resp); }
static uint32_t param(uint32_t nid, uint32_t id) { uint32_t v = 0; return verb12(nid, 0xF00u, id, &v) == 0 ? v : 0u; }

/* Unmute a widget's output amplifier and the input amplifier for connection `in` at 0 dB. */
static void amp_on(uint32_t nid, uint32_t caps, uint32_t in) {
    uint32_t amp_caps = (caps & 8u) ? param(nid, 0x12u) : param(afg, 0x12u);
    uint32_t gain = amp_caps & 0x7Fu;
    if (caps & 4u) (void)verb4(nid, 0x3u, 0xB000u | gain, 0);                       /* output amp: set out, left, right, gain, unmuted */
    if (caps & 2u) {
        uint32_t in_caps = (caps & 8u) ? param(nid, 0x0Du) : param(afg, 0x0Du);
        (void)verb4(nid, 0x3u, 0x7000u | ((in & 0xFu) << 8) | (in_caps & 0x7Fu), 0);   /* input amp (index in) */
    }
}
/* Does this node reach a DAC? On success configures the path (power, amps, connection select) and records the DAC. */
static int route(uint32_t nid, uint32_t depth, uint32_t *dac_out) {
    uint32_t caps = param(nid, 0x09u), type = (caps >> 20) & 0xFu;
    if (type == 0u) {                                                              /* audio output converter */
        (void)verb12(nid, 0x705u, 0u, 0);
        amp_on(nid, caps, 0u);
        *dac_out = nid; return 0;
    }
    if (depth >= MAX_PATH || (type != 2u && type != 3u && type != 4u)) return -1;
    uint32_t cl = param(nid, 0x0Eu), len = cl & 0x7Fu, wide = (cl >> 7) & 1u;
    for (uint32_t i = 0; i < len; ++i) {
        uint32_t entry = 0, per = wide ? 2u : 4u;
        if (verb12(nid, 0xF02u, i - (i % per), &entry) != 0) return -1;
        uint32_t child = wide ? ((entry >> ((i % per) * 16u)) & 0x7FFFu) : ((entry >> ((i % per) * 8u)) & 0x7Fu);
        if (child == 0u) continue;
        if (route(child, depth + 1u, dac_out) == 0) {
            (void)verb12(nid, 0x705u, 0u, 0);
            if (len > 1u && type != 2u) (void)verb12(nid, 0x701u, i, 0);           /* selector / pin: choose this input */
            amp_on(nid, caps, i);
            return 0;
        }
    }
    return -1;
}
static void remember(uint32_t dac) { for (uint32_t i = 0; i < dac_total; ++i) if (dacs[i] == dac) return; if (dac_total < 4u) dacs[dac_total++] = dac; }

int hda_init(void) {
    uint32_t slot;
    for (slot = 0; slot < 32u; ++slot) {
        uint32_t id = pci_read(slot, 0);
        if ((id & 0xFFFFu) == 0xFFFFu) continue;
        if ((pci_read(slot, 0x08) >> 16) == 0x0403u) break;                        /* multimedia / HD audio */
    }
    if (slot == 32u) return -1;
    uint32_t bar = pci_read(slot, 0x10) & 0xFFFFFFF0u;
    if (bar == 0u) return -1;
    pci_write(slot, 0x04, pci_read(slot, 0x04) | 0x6u);                            /* memory space + bus master */
    for (uint32_t p = 0; p < 4u; ++p) if (paging_map_page(HDA_VA + p * 0x1000u, bar + p * 0x1000u, 0x013u) != 0) return -1;
    hd = (volatile uint8_t *)HDA_VA;
    w32(GCTL, 0u);                                                                 /* controller reset */
    for (uint32_t i = 0; i < 100000u && (r32(GCTL) & 1u); ++i) settle(1);
    settle(200);
    w32(GCTL, 1u);
    uint32_t i;
    for (i = 0; i < 100000u && !(r32(GCTL) & 1u); ++i) settle(1);
    if (i == 100000u) return -2;
    for (i = 0; i < 400000u && (r16(STATESTS) & 0x7FFFu) == 0u; ++i) settle(1);   /* codecs announce themselves */
    uint32_t present = r16(STATESTS) & 0x7FFFu;
    if (present == 0u) return -3;
    for (codec = 0; codec < 15u && !(present & (1u << codec)); ++codec) { }
    uint32_t root = param(0u, 0x04u), first = (root >> 16) & 0xFFu, count = root & 0xFFu;
    afg = 0;
    for (uint32_t n = first; n < first + count; ++n) if ((param(n, 0x05u) & 0xFFu) == 1u) { afg = n; break; }
    if (afg == 0u) return -4;
    (void)verb12(afg, 0x705u, 0u, 0);                                              /* function group to D0 */
    settle(2000);
    uint32_t sub = param(afg, 0x04u), wfirst = (sub >> 16) & 0xFFu, wcount = sub & 0xFFu;
    dac_total = 0;
    for (uint32_t n = wfirst; n < wfirst + wcount; ++n) {
        uint32_t caps = param(n, 0x09u);
        if (((caps >> 20) & 0xFu) != 4u) continue;                                 /* pin complex */
        uint32_t pincap = param(n, 0x0Cu), config = 0;
        (void)verb12(n, 0xF1Cu, 0u, &config);
        if (!(pincap & (1u << 4))) continue;                                       /* not output capable */
        if (((config >> 30) & 3u) == 1u) continue;                                 /* no physical connection */
        uint32_t dac;
        if (route(n, 0u, &dac) != 0) continue;
        uint32_t devtype = (config >> 20) & 0xFu;
        (void)verb12(n, 0x707u, devtype == 2u ? 0xC0u : 0x40u, 0);                 /* output (and headphone) enable */
        if (pincap & (1u << 16)) (void)verb12(n, 0x70Cu, 2u, 0);                   /* external amplifier on */
        remember(dac);
    }
    return dac_total > 0u ? 0 : -5;
}
int hda_play(uint32_t rate, uint8_t *ring, uint32_t period_bytes, uint32_t periods) {
    if (hd == 0 || dac_total == 0u || periods == 0u || periods > 64u) return -1;
    uint32_t gcap = r16(GCAP), iss = (gcap >> 8) & 0xFu, oss = (gcap >> 12) & 0xFu;
    if (oss == 0u) return -2;
    sd_base = 0x80u + 0x20u * iss;
    uint32_t fmt = (rate == 44100u ? 0x4000u : 0u) | 0x0011u;                     /* base rate, 16-bit, 2 channels */
    if (rate != 44100u && rate != 48000u) return -3;
    w32(sd_base + SD_CTL, 1u);                                                    /* stream reset */
    for (uint32_t i = 0; i < 100000u && !(r32(sd_base + SD_CTL) & 1u); ++i) settle(1);
    w32(sd_base + SD_CTL, 0u);
    for (uint32_t i = 0; i < 100000u && (r32(sd_base + SD_CTL) & 1u); ++i) settle(1);
    for (uint32_t i = 0; i < periods; ++i) {
        bdl[i * 4u] = (uint32_t)ring + i * period_bytes; bdl[i * 4u + 1u] = 0u; bdl[i * 4u + 2u] = period_bytes; bdl[i * 4u + 3u] = 0u;
    }
    w32(sd_base + SD_BDPL, (uint32_t)bdl); w32(sd_base + SD_BDPU, 0u);
    w32(sd_base + SD_CBL, period_bytes * periods);
    w16(sd_base + SD_LVI, (uint16_t)(periods - 1u));
    w16(sd_base + SD_FMT, (uint16_t)fmt);
    for (uint32_t i = 0; i < dac_total; ++i) {
        (void)verb4(dacs[i], 0x2u, fmt, 0);                                       /* converter stream format */
        (void)verb12(dacs[i], 0x706u, (STREAM_TAG << 4) | 0u, 0);                 /* stream tag, channel 0 */
    }
    w32(sd_base + SD_CTL, (STREAM_TAG << 20) | 0x2u);                             /* stream number + RUN */
    return 0;
}
uint32_t hda_position(void) { return hd && sd_base ? r32(sd_base + SD_LPIB) : 0u; }
void hda_stop(void) { if (hd && sd_base) w32(sd_base + SD_CTL, 0u); }
