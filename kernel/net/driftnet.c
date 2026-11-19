#include "sand/net/driftnet.h"
#include "sand/veyra/status.h"
#include "sand/arch/x86/timer.h"

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static void debug_text(const char *s) { while(*s) __asm__ volatile("outb %0,$0xE9"::"a"(*s++)); }

/* ---- PCI (configuration mechanism #1) ---- */
static uint32_t pci_read(uint32_t bus, uint32_t slot, uint32_t fn, uint32_t reg) {
    outl(0xCF8u, 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8) | (reg & 0xFCu));
    return inl(0xCFCu);
}
static void pci_write(uint32_t bus, uint32_t slot, uint32_t fn, uint32_t reg, uint32_t v) {
    outl(0xCF8u, 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8) | (reg & 0xFCu));
    outl(0xCFCu, v);
}

/* ---- RTL8139 ---- */
#define RX_LEN 8192u
#define RX_BUF (RX_LEN + 16u + 1536u)
#define TX_LEN 1536u
static uint8_t rx_buf[RX_BUF] __attribute__((aligned(4)));
static uint8_t tx_buf[TX_LEN] __attribute__((aligned(4)));
static uint16_t io;                  /* 0 = no NIC */
static uint32_t rx_off, tx_idx;
static uint8_t mac[6];
static uint8_t my_ip[4] = { 10, 0, 2, 15 }, gw_ip[4] = { 10, 0, 2, 2 }, netmask[4] = { 255, 255, 255, 0 };   /* defaults until DHCP answers */
static uint8_t dhcp_done, dhcp_active; static uint32_t dhcp_xid; static uint8_t dhcp_stage;   /* stage 1 = waiting for OFFER, 2 = waiting for ACK */
static uint8_t dhcp_offer[4], dhcp_server[4], dhcp_mask[4], dhcp_router[4];
static uint32_t rx_frames, tx_frames;

static int nic_send(const uint8_t *frame, uint32_t len) {
    if(!io || len == 0u || len > TX_LEN) return -1;
    for(uint32_t i = 0; i < len; ++i) tx_buf[i] = frame[i];
    uint32_t n = len < 60u ? 60u : len;                       /* pad to the Ethernet minimum */
    for(uint32_t i = len; i < n; ++i) tx_buf[i] = 0;
    /* The chip sends from its four descriptors strictly in rotation, so every frame uses the next TSAD/TSD pair. */
    uint16_t sad = (uint16_t)(io + 0x20u + 4u * tx_idx), sd = (uint16_t)(io + 0x10u + 4u * tx_idx);
    tx_idx = (tx_idx + 1u) & 3u;
    outl(sad, (uint32_t)(uintptr_t)tx_buf);
    outl(sd, n);                                              /* writing the size clears OWN and starts the DMA */
    for(uint32_t spin = 0; spin < 200000u; ++spin) if(inl(sd) & 0x8000u) { ++tx_frames; return 0; }   /* TOK */
    return -1;
}

static void dhcp_run(void);
sand_veyra_status_t driftnet_init(void) {
    for(uint32_t slot = 0; slot < 32u && !io; ++slot) {
        if(pci_read(0, slot, 0, 0) != 0x813910ECu) continue;
        uint32_t bar = pci_read(0, slot, 0, 0x10);
        if(!(bar & 1u)) continue;
        uint32_t cmd = pci_read(0, slot, 0, 0x04);
        pci_write(0, slot, 0, 0x04, cmd | 0x5u);              /* I/O space + bus master (DMA) */
        io = (uint16_t)(bar & 0xFFFCu);
    }
    if(!io) return SAND_VEYRA_E_NOT_FOUND;
    outb((uint16_t)(io + 0x52u), 0);                           /* power on */
    outb((uint16_t)(io + 0x37u), 0x10u);                       /* software reset */
    for(uint32_t spin = 0; (inb((uint16_t)(io + 0x37u)) & 0x10u) && spin < 1000000u; ++spin) {}
    for(uint32_t i = 0; i < 6u; ++i) mac[i] = inb((uint16_t)(io + i));
    outl((uint16_t)(io + 0x30u), (uint32_t)(uintptr_t)rx_buf);
    outw((uint16_t)(io + 0x3Cu), 0);                           /* no interrupts: the stack is polled */
    outl((uint16_t)(io + 0x44u), 0x8Fu);                       /* accept broadcast/multicast/physical/all, WRAP */
    outb((uint16_t)(io + 0x37u), 0x0Cu);                       /* receiver + transmitter on */
    rx_off = 0; tx_idx = 0;
    debug_text("SAND_TEST:DRIFTNET_NIC_UP\n");
    dhcp_run();
    return SAND_VEYRA_OK;
}
sand_veyra_status_t driftnet_info(struct driftnet_info *o) {
    if(!io) return SAND_VEYRA_E_NOT_FOUND;
    for(uint32_t i = 0; i < 6u; ++i) o->mac[i] = mac[i];
    for(uint32_t i = 0; i < 4u; ++i) { o->ip[i] = my_ip[i]; o->gateway[i] = gw_ip[i]; o->mask[i] = netmask[i]; }
    o->flags = dhcp_done ? 1u : 0u;
    o->rx_frames = rx_frames; o->tx_frames = tx_frames; return SAND_VEYRA_OK;
}

/* ---- protocol helpers ---- */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t csum(const uint8_t *d, uint32_t n, uint32_t seed) {
    uint32_t s = seed;
    for(uint32_t i = 0; i + 1u < n; i += 2u) s += (uint32_t)((d[i] << 8) | d[i + 1u]);
    if(n & 1u) s += (uint32_t)(d[n - 1u] << 8);
    while(s >> 16) s = (s & 0xFFFFu) + (s >> 16);
    return (uint16_t)~s;
}
static int ip_eq(const uint8_t *a, const uint8_t *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]; }
static void mcopy(uint8_t *d, const uint8_t *s, uint32_t n) { for(uint32_t i = 0; i < n; ++i) d[i] = s[i]; }

/* ARP cache: 4 entries, newest overwrites oldest */
static struct { uint8_t ip[4]; uint8_t mac[6]; uint8_t valid; } arp[4];
static uint32_t arp_next;
static void arp_learn(const uint8_t *ip, const uint8_t *m) {
    for(uint32_t i = 0; i < 4u; ++i) if(arp[i].valid && ip_eq(arp[i].ip, ip)) { mcopy(arp[i].mac, m, 6); return; }
    mcopy(arp[arp_next].ip, ip, 4); mcopy(arp[arp_next].mac, m, 6); arp[arp_next].valid = 1; arp_next = (arp_next + 1u) & 3u;
}
static const uint8_t *arp_lookup(const uint8_t *ip) {
    for(uint32_t i = 0; i < 4u; ++i) if(arp[i].valid && ip_eq(arp[i].ip, ip)) return arp[i].mac;
    return 0;
}
static void send_arp(uint16_t op, const uint8_t *dst_mac, const uint8_t *dst_ip) {
    uint8_t f[42]; static const uint8_t bc[6] = { 255, 255, 255, 255, 255, 255 }; static const uint8_t zero[6] = { 0 };
    mcopy(f, op == 1u ? bc : dst_mac, 6); mcopy(f + 6, mac, 6); put16(f + 12, 0x0806u);
    put16(f + 14, 1); put16(f + 16, 0x0800u); f[18] = 6; f[19] = 4; put16(f + 20, op);
    mcopy(f + 22, mac, 6); mcopy(f + 28, my_ip, 4); mcopy(f + 32, op == 1u ? zero : dst_mac, 6); mcopy(f + 38, dst_ip, 4);
    (void)nic_send(f, 42);
}
static const uint8_t *next_hop(const uint8_t *dst) {      /* same /24 goes direct, everything else to the gateway */
    return (((dst[0] ^ my_ip[0]) & netmask[0]) | ((dst[1] ^ my_ip[1]) & netmask[1]) | ((dst[2] ^ my_ip[2]) & netmask[2]) | ((dst[3] ^ my_ip[3]) & netmask[3])) == 0 ? dst : gw_ip;
}
/* Builds Ethernet+IPv4 around payload (already placed at f+34) and sends; false when the next hop's MAC is unknown (and asks for it). */
static int send_ip(uint8_t *f, const uint8_t *dst, uint8_t proto, uint32_t payload_len) {
    const uint8_t *hop = next_hop(dst), *m = arp_lookup(hop);
    if(!m) { send_arp(1, 0, hop); return 0; }
    mcopy(f, m, 6); mcopy(f + 6, mac, 6); put16(f + 12, 0x0800u);
    f[14] = 0x45; f[15] = 0; put16(f + 16, (uint16_t)(20u + payload_len)); put16(f + 18, 0); put16(f + 20, 0x4000u);
    f[22] = 64; f[23] = proto; put16(f + 24, 0); mcopy(f + 26, my_ip, 4); mcopy(f + 30, dst, 4);
    put16(f + 24, csum(f + 14, 20, 0));
    return nic_send(f, 34u + payload_len) == 0;
}

/* ---- ping and UDP state ---- */
static struct { uint8_t state; uint32_t owner; uint8_t ip[4]; uint32_t deadline; uint32_t sent; uint16_t seq; uint32_t rtt; uint32_t last_try; } ping;
#define UDP_PORTS 4u
static struct { uint16_t port; uint8_t full; struct driftnet_udp d; } sock[UDP_PORTS];

static void dhcp_input(const uint8_t *bootp, uint32_t len);
static void tcp_input(const uint8_t *f, uint32_t ihl, const uint8_t *p, uint32_t plen);
static void on_ip(const uint8_t *f, uint32_t len) {
    if(len < 34u || (f[14] >> 4) != 4u) return;
    uint32_t ihl = (uint32_t)(f[14] & 15u) * 4u, total = be16(f + 16);
    if(ihl < 20u || 14u + total > len || total < ihl) return;
    const uint8_t *p = f + 14 + ihl; uint32_t plen = total - ihl;
    if(dhcp_active && f[23] == 17u && plen >= 8u && be16(p + 2) == 68u) { dhcp_input(p + 8, plen - 8u); return; }   /* before we own an address */
    if(!ip_eq(f + 30, my_ip) && !(f[30] == 255 && f[31] == 255)) return;
    if(f[23] == 6u) { tcp_input(f, ihl, p, plen); return; }
    if(f[23] == 1u && plen >= 8u) {                                        /* ICMP */
        if(p[0] == 0u && ping.state == 2u && ip_eq(f + 26, ping.ip) && be16(p + 6) == ping.seq) {   /* echo reply to our request */
            ping.rtt = (timer_ticks() - ping.sent) * (1000u / SAND_TICK_HZ); ping.state = 3u;
        } else if(p[0] == 8u && plen <= 1000u) {                           /* echo request: answer it */
            static uint8_t out[34 + 1000]; mcopy(out + 34, p, plen); out[34] = 0; put16(out + 36, 0); put16(out + 36, csum(out + 34, plen, 0));
            arp_learn(f + 26, f + 6); (void)send_ip(out, f + 26, 1, plen);
        }
    } else if(f[23] == 17u && plen >= 8u) {                                /* UDP */
        uint16_t dport = be16(p + 2), ulen = be16(p + 4);
        if(ulen < 8u || ulen > plen) return;
        for(uint32_t i = 0; i < UDP_PORTS; ++i) {
            if(sock[i].port != dport || sock[i].full) continue;
            uint32_t n = ulen - 8u; if(n > DRIFTNET_UDP_MAX) n = DRIFTNET_UDP_MAX;
            sock[i].d.ip = (uint32_t)f[26] | ((uint32_t)f[27] << 8) | ((uint32_t)f[28] << 16) | ((uint32_t)f[29] << 24);
            sock[i].d.src_port = be16(p); sock[i].d.dst_port = dport; sock[i].d.length = n; mcopy(sock[i].d.data, p + 8, n);
            arp_learn(f + 26, f + 6); sock[i].full = 1; return;
        }
    }
}
static void on_frame(const uint8_t *f, uint32_t len) {
    if(len < 14u) return;
    uint16_t type = be16(f + 12);
    if(type == 0x0806u && len >= 42u && be16(f + 20) == 1u && ip_eq(f + 38, my_ip)) {      /* ARP request for us */
        arp_learn(f + 28, f + 22); send_arp(2, f + 22, f + 28);
    } else if(type == 0x0806u && len >= 42u && be16(f + 20) == 2u) arp_learn(f + 28, f + 22);
    else if(type == 0x0800u) on_ip(f, len);
}
static void poll_rx(void) {
    if(!io) return;
    for(uint32_t guard = 0; guard < 16u && !(inb((uint16_t)(io + 0x37u)) & 1u); ++guard) {
        uint32_t st = (uint32_t)rx_buf[rx_off] | ((uint32_t)rx_buf[rx_off + 1u] << 8);
        uint32_t len = (uint32_t)rx_buf[rx_off + 2u] | ((uint32_t)rx_buf[rx_off + 3u] << 8);
        if(!(st & 1u) || len < 18u || len > 1600u) { outb((uint16_t)(io + 0x37u), 0x04u); outb((uint16_t)(io + 0x37u), 0x0Cu); rx_off = 0; break; }   /* bad ring state: restart the receiver */
        ++rx_frames; on_frame(rx_buf + rx_off + 4u, len - 4u);
        rx_off = (rx_off + len + 4u + 3u) & ~3u; rx_off %= RX_LEN;
        outw((uint16_t)(io + 0x38u), (uint16_t)(rx_off - 16u));
    }
}

sand_veyra_status_t driftnet_ping_step(uint32_t owner, uint32_t ip, uint32_t timeout_ms) {
    if(!io) return SAND_VEYRA_E_NOT_FOUND;
    uint32_t now = timer_ticks();
    if(ping.state == 0u) {
        ping.owner = owner; ping.ip[0] = (uint8_t)ip; ping.ip[1] = (uint8_t)(ip >> 8); ping.ip[2] = (uint8_t)(ip >> 16); ping.ip[3] = (uint8_t)(ip >> 24);
        ping.deadline = now + (timeout_ms * SAND_TICK_HZ + 999u) / 1000u + 1u; ++ping.seq; ping.state = 1u; ping.last_try = 0;
    } else if(ping.owner != owner) {                                        /* another process is pinging (a dead owner's ping times out) */
        if((int32_t)(now - ping.deadline) >= 0) ping.state = 0u;
        return SAND_VEYRA_E_BUSY;
    }
    poll_rx();
    if(ping.state == 3u) { int32_t r = (int32_t)ping.rtt; ping.state = 0u; return r; }
    if((int32_t)(now - ping.deadline) >= 0) { ping.state = 0u; return SAND_VEYRA_E_TIMEOUT; }
    if(ping.state == 1u && now - ping.last_try >= 20u) {                    /* (re)try sending the echo request until the ARP answer is known */
        static uint8_t f[34 + 8 + 16]; ping.last_try = now; if(!now) ping.last_try = 1;
        f[34] = 8; f[35] = 0; put16(f + 36, 0); put16(f + 38, 0x5344u); put16(f + 40, ping.seq);
        for(uint32_t i = 0; i < 16u; ++i) f[42 + i] = (uint8_t)('a' + i);
        put16(f + 36, csum(f + 34, 24, 0));
        ping.sent = now; if(send_ip(f, ping.ip, 1, 24)) ping.state = 2u;
    }
    return DRIFTNET_PING_PENDING;
}
sand_veyra_status_t driftnet_udp_send(const struct driftnet_udp *d) {
    if(!io) return SAND_VEYRA_E_NOT_FOUND;
    if(d->length == 0u || d->length > DRIFTNET_UDP_MAX) return SAND_VEYRA_E_INVALID;
    static uint8_t f[34 + 8 + DRIFTNET_UDP_MAX];
    uint8_t dst[4] = { (uint8_t)d->ip, (uint8_t)(d->ip >> 8), (uint8_t)(d->ip >> 16), (uint8_t)(d->ip >> 24) };
    uint32_t ulen = 8u + d->length;
    put16(f + 34, d->src_port); put16(f + 36, d->dst_port); put16(f + 38, (uint16_t)ulen); put16(f + 40, 0);   /* checksum 0 = none (legal for IPv4) */
    mcopy(f + 42, d->data, d->length);
    for(uint32_t tries = 0; tries < 3u; ++tries) {
        if(send_ip(f, dst, 17, ulen)) return SAND_VEYRA_OK;
        uint32_t t0 = timer_ticks();                                         /* wait briefly for the ARP answer, then try again */
        while(timer_ticks() - t0 < 2u) poll_rx();
    }
    return SAND_VEYRA_E_TIMEOUT;
}
int32_t driftnet_udp_recv(uint16_t port, struct driftnet_udp *out) {
    if(!io) return SAND_VEYRA_E_NOT_FOUND;
    poll_rx();
    uint32_t free_slot = UDP_PORTS;
    for(uint32_t i = 0; i < UDP_PORTS; ++i) {
        if(sock[i].port == port) { if(!sock[i].full) return 0; *out = sock[i].d; sock[i].full = 0; return 1; }
        if(sock[i].port == 0u && free_slot == UDP_PORTS) free_slot = i;
    }
    if(port == 0u || free_slot == UDP_PORTS) return SAND_VEYRA_E_INVALID;
    sock[free_slot].port = port; sock[free_slot].full = 0; return 0;
}

/* ---- DHCP client (RFC 2131, the four-message exchange; one try, no renewal, no lease expiry) ---- */
static void dhcp_send(uint8_t type) {
    static uint8_t f[14 + 20 + 8 + 300]; uint32_t o, n = 14u + 20u + 8u;
    for(uint32_t i = 0; i < sizeof(f); ++i) f[i] = 0;
    for(uint32_t i = 0; i < 6u; ++i) f[i] = 255;
    mcopy(f + 6, mac, 6); put16(f + 12, 0x0800u);
    uint8_t *b = f + 42; b[0] = 1; b[1] = 1; b[2] = 6; b[3] = 0;
    b[4] = (uint8_t)(dhcp_xid >> 24); b[5] = (uint8_t)(dhcp_xid >> 16); b[6] = (uint8_t)(dhcp_xid >> 8); b[7] = (uint8_t)dhcp_xid;
    put16(b + 10, 0x8000u);                                   /* ask for a broadcast reply: we have no address yet */
    mcopy(b + 28, mac, 6);
    b[236] = 0x63; b[237] = 0x82; b[238] = 0x53; b[239] = 0x63;
    o = 240u; b[o++] = 53; b[o++] = 1; b[o++] = type;
    if(type == 3u) { b[o++] = 50; b[o++] = 4; mcopy(b + o, dhcp_offer, 4); o += 4; b[o++] = 54; b[o++] = 4; mcopy(b + o, dhcp_server, 4); o += 4; }
    b[o++] = 55; b[o++] = 3; b[o++] = 1; b[o++] = 3; b[o++] = 6;   /* parameter request: subnet mask, router, DNS */
    b[o++] = 255; if(o < 300u) o = 300u;
    uint32_t ulen = 8u + o;
    put16(f + 34, 68u); put16(f + 36, 67u); put16(f + 38, (uint16_t)ulen);
    f[14] = 0x45; put16(f + 16, (uint16_t)(20u + ulen)); put16(f + 20, 0x4000u); f[22] = 64; f[23] = 17;
    for(uint32_t i = 0; i < 4u; ++i) f[30 + i] = 255;     /* 0.0.0.0 -> 255.255.255.255 */
    put16(f + 24, csum(f + 14, 20, 0));
    (void)nic_send(f, n + o);
}
static void dhcp_input(const uint8_t *b, uint32_t len) {
    if(len < 241u || b[0] != 2u || b[236] != 0x63 || b[237] != 0x82 || b[238] != 0x53 || b[239] != 0x63) return;
    uint32_t xid = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) | b[7];
    if(xid != dhcp_xid) return;
    for(uint32_t i = 0; i < 6u; ++i) if(b[28 + i] != mac[i]) return;
    uint8_t type = 0, srv[4] = {0}, mk[4] = {255, 255, 255, 0}, rt[4] = {0};
    for(uint32_t i = 240u; i < len;) {
        uint8_t code = b[i++];
        if(code == 255u) break;
        if(code == 0u) continue;
        if(i >= len) break;
        uint8_t l = b[i++];
        if(i + l > len) break;
        if(code == 53u && l == 1u) type = b[i]; else if(code == 54u && l == 4u) mcopy(srv, b + i, 4);
        else if(code == 1u && l == 4u) mcopy(mk, b + i, 4); else if(code == 3u && l >= 4u) mcopy(rt, b + i, 4);
        i += l;
    }
    if(dhcp_stage == 1u && type == 2u) { mcopy(dhcp_offer, b + 16, 4); mcopy(dhcp_server, srv, 4); mcopy(dhcp_mask, mk, 4); mcopy(dhcp_router, rt, 4); dhcp_stage = 2u; }
    else if(dhcp_stage == 2u && type == 5u) { mcopy(my_ip, b + 16, 4); mcopy(netmask, mk, 4); mcopy(gw_ip, rt, 4); dhcp_stage = 3u; }
}
static void dhcp_run(void) {
    dhcp_active = 1u; dhcp_xid = 0x53414E44u ^ ((uint32_t)mac[4] << 8) ^ mac[5]; dhcp_stage = 1u;
    for(uint32_t i = 0; i < 3u && dhcp_stage == 1u; ++i) { dhcp_send(1); for(uint32_t s = 0; s < 400000u && dhcp_stage == 1u; ++s) poll_rx(); }
    if(dhcp_stage == 2u) for(uint32_t i = 0; i < 3u && dhcp_stage == 2u; ++i) { dhcp_send(3); for(uint32_t s = 0; s < 400000u && dhcp_stage == 2u; ++s) poll_rx(); }
    dhcp_active = 0u;
    if(dhcp_stage == 3u) { dhcp_done = 1u; debug_text("SAND_TEST:DRIFTNET_DHCP_BOUND\n"); }
    else debug_text("SAND_TEST:DRIFTNET_DHCP_FALLBACK\n");      /* keep the static QEMU defaults */
}

/* ---- TCP client ---- */
#define TCP_RING 2048u
static struct {
    uint8_t state;          /* 0 closed, 1 SYN sent, 2 established, 3 FIN sent */
    uint8_t peer_closed, refused, send_state;
    uint32_t owner, port_local; uint8_t ip[4]; uint16_t rport;
    uint32_t snd_nxt, snd_una, rcv_nxt, deadline, sent_at, retries, seglen;
    uint8_t seg[DRIFTNET_TCP_SEG_MAX];
    uint8_t ring[TCP_RING]; uint32_t head, count;
} tcp;
static uint16_t tcp_port_next = 49152u;
static void tcp_send(uint8_t flags, uint32_t seq, uint32_t ack, const uint8_t *data, uint32_t len) {
    static uint8_t f[34 + 24 + DRIFTNET_TCP_SEG_MAX];
    uint32_t hl = (flags & 2u) ? 24u : 20u, tl = hl + len;
    uint8_t *t = f + 34;
    put16(t, (uint16_t)tcp.port_local); put16(t + 2, tcp.rport);
    for(uint32_t i = 0; i < 4u; ++i) { t[4 + i] = (uint8_t)(seq >> (24 - 8 * i)); t[8 + i] = (uint8_t)(ack >> (24 - 8 * i)); }
    t[12] = (uint8_t)((hl / 4u) << 4); t[13] = flags; put16(t + 14, (uint16_t)(TCP_RING - tcp.count)); put16(t + 16, 0); put16(t + 18, 0);
    if(flags & 2u) { t[20] = 2; t[21] = 4; put16(t + 22, 512u); }              /* MSS option on SYN */
    mcopy(t + hl, data, len);
    uint32_t sum = 0;                                                         /* pseudo header */
    for(uint32_t i = 0; i < 4u; i += 2u) sum += (uint32_t)((my_ip[i] << 8) | my_ip[i + 1u]) + (uint32_t)((tcp.ip[i] << 8) | tcp.ip[i + 1u]);
    sum += 6u + tl;
    put16(t + 16, csum(t, tl, sum));
    (void)send_ip(f, tcp.ip, 6, tl);
}
static void tcp_reset_state(void) { tcp.state = 0; tcp.send_state = 0; tcp.count = 0; tcp.head = 0; }
static void tcp_input(const uint8_t *f, uint32_t ihl, const uint8_t *p, uint32_t plen) {
    (void)ihl;
    if(tcp.state == 0u || plen < 20u || !ip_eq(f + 26, tcp.ip) || be16(p) != tcp.rport || be16(p + 2) != tcp.port_local) return;
    uint32_t seq = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7];
    uint32_t ack = ((uint32_t)p[8] << 24) | ((uint32_t)p[9] << 16) | ((uint32_t)p[10] << 8) | p[11];
    uint32_t hl = (uint32_t)(p[12] >> 4) * 4u; uint8_t fl = p[13];
    if(hl < 20u || hl > plen) return;
    uint32_t dlen = plen - hl;
    if(fl & 4u) { if(tcp.state == 1u) tcp.refused = 1u; tcp.peer_closed = 1u; if(tcp.state == 1u) tcp.state = 0u; else tcp.state = 0u; return; }   /* RST */
    if(tcp.state == 1u) {
        if((fl & 0x12u) == 0x12u && ack == tcp.snd_nxt) { tcp.rcv_nxt = seq + 1u; tcp.snd_una = ack; tcp.state = 2u; tcp_send(0x10u, tcp.snd_nxt, tcp.rcv_nxt, p, 0); }
        return;
    }
    if(fl & 0x10u) { if((int32_t)(ack - tcp.snd_una) > 0 && (int32_t)(ack - tcp.snd_nxt) <= 0) tcp.snd_una = ack; }
    if(dlen > 0u) {
        if(seq == tcp.rcv_nxt) {
            uint32_t room = TCP_RING - tcp.count, n = dlen < room ? dlen : room;
            for(uint32_t i = 0; i < n; ++i) tcp.ring[(tcp.head + tcp.count + i) % TCP_RING] = p[hl + i];
            tcp.count += n; tcp.rcv_nxt += n;
        }
        tcp_send(0x10u, tcp.snd_nxt, tcp.rcv_nxt, p, 0);               /* ACK (also repeats the expected number for out-of-order data) */
    }
    if((fl & 1u) && seq + dlen == tcp.rcv_nxt) { ++tcp.rcv_nxt; tcp.peer_closed = 1u; tcp_send(0x10u, tcp.snd_nxt, tcp.rcv_nxt, p, 0); }
}
sand_veyra_status_t driftnet_tcp_connect_step(uint32_t owner, uint32_t ip, uint32_t port, uint32_t timeout_ms) {
    if(!io) return SAND_VEYRA_E_NOT_FOUND;
    uint32_t now = timer_ticks();
    if(tcp.state == 0u && tcp.owner != owner + 0x80000000u) {            /* fresh connect (the high bit marks "this owner is being retried") */
        tcp.owner = owner + 0x80000000u; tcp.refused = 0; tcp.peer_closed = 0; tcp.count = 0; tcp.head = 0; tcp.send_state = 0;
        for(uint32_t i = 0; i < 4u; ++i) tcp.ip[i] = (uint8_t)(ip >> (8u * i));
        tcp.rport = (uint16_t)port; tcp.port_local = tcp_port_next++; if(tcp_port_next == 0u) tcp_port_next = 49152u;
        tcp.snd_nxt = 0x1000u + now * 64000u; tcp.snd_una = tcp.snd_nxt; tcp.state = 1u; tcp.sent_at = 0;
        tcp.deadline = now + (timeout_ms * SAND_TICK_HZ + 999u) / 1000u + 1u; tcp.retries = 0;
    } else if(tcp.owner != owner + 0x80000000u && tcp.state != 0u) {
        if((tcp.state == 1u) && (int32_t)(now - tcp.deadline) >= 0) tcp_reset_state();
        return SAND_VEYRA_E_BUSY;
    }
    poll_rx();
    if(tcp.state == 2u) { tcp.owner = owner; return SAND_VEYRA_OK; }
    if(tcp.state == 0u) { tcp.owner = 0; return tcp.refused ? SAND_VEYRA_E_IO : SAND_VEYRA_E_NOT_FOUND; }
    if((int32_t)(now - tcp.deadline) >= 0) { tcp_reset_state(); tcp.owner = 0; return SAND_VEYRA_E_TIMEOUT; }
    if(tcp.state == 1u && (tcp.sent_at == 0u || now - tcp.sent_at >= 50u)) {   /* SYN, repeated until ARP and the server answer */
        tcp.sent_at = now ? now : 1u; static const uint8_t none[1] = {0};
        tcp_send(0x02u, tcp.snd_nxt, 0, none, 0); if(tcp.retries == 0u) { tcp.snd_nxt++; } ++tcp.retries;
    }
    return (sand_veyra_status_t)DRIFTNET_TCP_PENDING;
}
int32_t driftnet_tcp_send_step(uint32_t owner, const uint8_t *data, uint32_t len) {
    if(tcp.state != 2u || tcp.owner != owner) return DRIFTNET_TCP_CLOSED;
    if(len == 0u || len > DRIFTNET_TCP_SEG_MAX) return SAND_VEYRA_E_INVALID;
    uint32_t now = timer_ticks();
    poll_rx();
    if(tcp.state != 2u) return DRIFTNET_TCP_CLOSED;
    if(tcp.send_state == 0u) {
        mcopy(tcp.seg, data, len); tcp.seglen = len; tcp.sent_at = now ? now : 1u; tcp.retries = 0;
        tcp_send(0x18u, tcp.snd_nxt, tcp.rcv_nxt, tcp.seg, len); tcp.snd_nxt += len; tcp.send_state = 1u;
        return DRIFTNET_TCP_PENDING;
    }
    if(tcp.snd_una == tcp.snd_nxt) { tcp.send_state = 0u; return (int32_t)tcp.seglen; }
    if(now - tcp.sent_at >= 50u) {                                          /* stop-and-wait retransmit every 0.5 s, five times */
        if(++tcp.retries > 5u) { tcp.send_state = 0u; tcp_reset_state(); return SAND_VEYRA_E_TIMEOUT; }
        tcp.sent_at = now; tcp_send(0x18u, tcp.snd_nxt - tcp.seglen, tcp.rcv_nxt, tcp.seg, tcp.seglen);
    }
    return DRIFTNET_TCP_PENDING;
}
int32_t driftnet_tcp_recv(uint32_t owner, uint8_t *out, uint32_t cap) {
    if(tcp.owner != owner || tcp.state == 0u) return DRIFTNET_TCP_CLOSED;
    poll_rx();
    if(tcp.count == 0u) return (tcp.peer_closed || tcp.state == 0u) ? DRIFTNET_TCP_CLOSED : 0;
    uint32_t n = tcp.count < cap ? tcp.count : cap;
    for(uint32_t i = 0; i < n; ++i) out[i] = tcp.ring[(tcp.head + i) % TCP_RING];
    tcp.head = (tcp.head + n) % TCP_RING; tcp.count -= n; return (int32_t)n;
}
void driftnet_tcp_close(uint32_t owner) {
    if(tcp.owner != owner || tcp.state == 0u) return;
    if(tcp.state == 2u) { static const uint8_t none[1] = {0}; tcp_send(0x11u, tcp.snd_nxt, tcp.rcv_nxt, none, 0); for(uint32_t i = 0; i < 20000u; ++i) poll_rx(); }
    tcp_reset_state(); tcp.owner = 0;
}
