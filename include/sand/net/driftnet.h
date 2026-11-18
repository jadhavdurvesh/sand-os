#ifndef SAND_NET_DRIFTNET_H
#define SAND_NET_DRIFTNET_H
#include <stdint.h>
#include "sand/veyra/status.h"
/* DRIFTNET: SAND's own network stack, written from scratch: PCI scan, an RTL8139 driver, ARP, IPv4,
 * ICMP echo and UDP. Polled (no interrupts), one NIC, static addressing for QEMU's user network
 * (10.0.2.15, gateway 10.0.2.2). No TCP, no DHCP, no DNS, no IPv6, no fragmentation. */
#define DRIFTNET_PING_PENDING (-100)
#define DRIFTNET_UDP_MAX 512u
struct driftnet_info { uint8_t mac[6]; uint8_t ip[4]; uint8_t gateway[4]; uint32_t rx_frames; uint32_t tx_frames; };
struct driftnet_udp { uint32_t ip; uint16_t src_port; uint16_t dst_port; uint32_t length; uint8_t data[DRIFTNET_UDP_MAX]; };
sand_veyra_status_t driftnet_init(void);                   /* OK when an RTL8139 was found and started, else NOT_FOUND */
sand_veyra_status_t driftnet_info(struct driftnet_info *out);   /* OK, or NOT_FOUND without a NIC */
/* Ping step: call repeatedly (the syscall retries it). Returns the round trip in ms, DRIFTNET_PING_PENDING while waiting,
 * or a Veyra status (TIMEOUT, BUSY when another process is pinging, NOT_FOUND without a NIC). ip is in network byte order as a little-endian u32 of a.b.c.d bytes. */
sand_veyra_status_t driftnet_ping_step(uint32_t owner, uint32_t ip, uint32_t timeout_ms);
sand_veyra_status_t driftnet_udp_send(const struct driftnet_udp *d);   /* OK, INVALID (length), TIMEOUT (next hop never answered ARP), NOT_FOUND */
int32_t driftnet_udp_recv(uint16_t port, struct driftnet_udp *out);   /* binds the port on first use; 1 datagram, 0 none, or a Veyra status */
#endif
