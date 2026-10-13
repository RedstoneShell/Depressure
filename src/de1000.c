// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "ddriver.h"
#include "duefi.h"
#include "dkrnl.h"
#include "de1000.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

#define ETH_TYPE_ARP   0x0806
#define ETH_TYPE_IP    0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2
#define IP_PROTO_ICMP  1
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

#define NET_OUR_IP     0x0A00020F   /* 10.0.2.15 */
#define NET_GATEWAY_IP 0x0A000202   /* 10.0.2.2  */
#define NET_NETMASK    0xFFFFFF00

#define PKT_MAX 1600

static inline void uart_outb(u16 port, u8 v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline u8 uart_inb(u16 port) {
    u8 r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static void uart_putc(char c) {
    while ((uart_inb(0x3FD) & 0x20) == 0) {}
    uart_outb(0x3F8, (u8)c);
}
static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
static void uart_put_hex32(u32 v) {
    static const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) uart_putc(hex[(v >> i) & 0xF]);
}
static void uart_put_hex64(u64 v) {
    static const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) uart_putc(hex[(v >> i) & 0xF]);
}
static void uart_put_dec(u64 v) {
    char buf[24];
    int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) uart_putc(buf[--i]);
}
static void uart_put_mac(const u8 *mac) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        uart_putc(hex[(mac[i] >> 4) & 0xF]);
        uart_putc(hex[mac[i] & 0xF]);
        if (i < 5) uart_putc(':');
    }
}

static inline void outl(u16 p, u32 v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(p));
}
static inline u32 inl(u16 p) {
    u32 r;
    __asm__ volatile ("inl %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}
static u32 pci_read32(u8 bus, u8 slot, u8 func, u8 off) {
    u32 a = 0x80000000u | (bus << 16) | (slot << 11) | (func << 8) | (off & 0xFC);
    outl(PCI_ADDR, a);
    return inl(PCI_DATA);
}
static void pci_write32(u8 bus, u8 slot, u8 func, u8 off, u32 v) {
    u32 a = 0x80000000u | (bus << 16) | (slot << 11) | (func << 8) | (off & 0xFC);
    outl(PCI_ADDR, a);
    outl(PCI_DATA, v);
}

static u64 g_mmio_base = 0;
static u8  g_mac[6];
static u8  g_pci_bus = 0;
static u8  g_pci_slot = 0;
static u8  g_pci_func = 0;
static u16 g_pci_vendor = 0;
static u16 g_pci_device = 0;
static u32 g_pci_bar0 = 0;

static e1000_tx_desc_t g_tx_desc[E1000_TX_DESC_COUNT] __attribute__((aligned(16)));
static u8 g_tx_buf[E1000_TX_DESC_COUNT][E1000_PKT_BUF_SIZE] __attribute__((aligned(16)));
static u32 g_tx_head = 0;

static e1000_rx_desc_t g_rx_desc[E1000_RX_DESC_COUNT] __attribute__((aligned(16)));
static u8 g_rx_buf[E1000_RX_DESC_COUNT][E1000_PKT_BUF_SIZE] __attribute__((aligned(16)));
static u32 g_rx_cur = 0;

static u8  g_gw_mac[6];
static int g_gw_mac_valid = 0;
static u8  g_our_mac[6];
static volatile int g_ping_done = 0;
static volatile int g_ping_ok   = 0;
static volatile u32 g_ping_rtt_lo = 0;
static volatile u32 g_ping_rtt_hi = 0;

static inline u32 mmio_read(u64 base, u32 reg) {
    return *(volatile u32 *)(base + reg);
}
static inline void mmio_write(u64 base, u32 reg, u32 val) {
    *(volatile u32 *)(base + reg) = val;
}

int e1000_send(const void *buf, u16 len) {
    if (len > E1000_PKT_BUF_SIZE) return -1;

    u32 idx = g_tx_head;
    e1000_tx_desc_t *d = &g_tx_desc[idx];

    if (!(d->status & 1)) return -1;

    u8 *src = (u8 *)buf;
    u8 *dst = g_tx_buf[idx];
    for (u16 i = 0; i < len; i++) dst[i] = src[i];

    d->length = len;
    d->cmd = (1 << 0) | (1 << 1) | (1 << 3);
    d->status = 0;

    g_tx_head = (g_tx_head + 1) % E1000_TX_DESC_COUNT;
    mmio_write(g_mmio_base, E1000_TDT, g_tx_head);

    return 0;
}

int e1000_recv(void *buf, u16 *len) {
    e1000_rx_desc_t *d = &g_rx_desc[g_rx_cur];

    if (!(d->status & 1)) return -1;
    if (!(d->status & 2)) {
        d->status = 0;
        g_rx_cur = (g_rx_cur + 1) % E1000_RX_DESC_COUNT;
        return -1;
    }

    u16 n = d->length;
    if (n > E1000_PKT_BUF_SIZE) n = E1000_PKT_BUF_SIZE;

    u8 *src = g_rx_buf[g_rx_cur];
    u8 *dst = (u8 *)buf;
    for (u16 i = 0; i < n; i++) dst[i] = src[i];
    *len = n;

    d->status = 0;
    g_rx_cur = (g_rx_cur + 1) % E1000_RX_DESC_COUNT;

    mmio_write(g_mmio_base, E1000_RDT,
               (g_rx_cur + E1000_RX_DESC_COUNT - 1) % E1000_RX_DESC_COUNT);

    return 0;
}

static u32 e1000_get_base(void) { return (u32)g_mmio_base; }

static void e1000_dump_regs(void) {
    uart_puts("[de1000] --- E1000 registers ---\n");
    uart_puts("[de1000] CTRL     = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_CTRL));   uart_puts("\n");
    uart_puts("[de1000] STATUS   = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_STATUS)); uart_puts("\n");
    uart_puts("[de1000] RCTL     = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_RCTL));   uart_puts("\n");
    uart_puts("[de1000] TCTL     = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_TCTL));   uart_puts("\n");
    uart_puts("[de1000] RDBAL    = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_RDBAL));  uart_puts("\n");
    uart_puts("[de1000] RDBAH    = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_RDBAH));  uart_puts("\n");
    uart_puts("[de1000] RDLEN    = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_RDLEN));  uart_puts("\n");
    uart_puts("[de1000] RDH      = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_RDH));    uart_puts("\n");
    uart_puts("[de1000] RDT      = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_RDT));    uart_puts("\n");
    uart_puts("[de1000] TDBAL    = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_TDBAL));  uart_puts("\n");
    uart_puts("[de1000] TDBAH    = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_TDBAH));  uart_puts("\n");
    uart_puts("[de1000] TDLEN    = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_TDLEN));  uart_puts("\n");
    uart_puts("[de1000] TDH      = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_TDH));    uart_puts("\n");
    uart_puts("[de1000] TDT      = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_TDT));    uart_puts("\n");
    uart_puts("[de1000] ICR      = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_ICR));    uart_puts("\n");
    uart_puts("[de1000] IMS      = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_IMS));    uart_puts("\n");
    uart_puts("[de1000] RAL      = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_RAL));    uart_puts("\n");
    uart_puts("[de1000] RAH      = "); uart_put_hex32(mmio_read(g_mmio_base, E1000_RAH));    uart_puts("\n");
}

static void e1000_dump_state(void) {
    uart_puts("[de1000] --- Driver state ---\n");
    uart_puts("[de1000] PCI bus:slot.func = ");
    uart_put_dec(g_pci_bus);  uart_putc(':');
    uart_put_dec(g_pci_slot); uart_putc('.');
    uart_put_dec(g_pci_func); uart_puts("\n");

    uart_puts("[de1000] Vendor:Device    = ");
    uart_put_hex32(g_pci_vendor); uart_putc(':');
    uart_put_hex32(g_pci_device); uart_puts("\n");

    uart_puts("[de1000] BAR0 (raw)       = "); uart_put_hex32(g_pci_bar0);  uart_puts("\n");
    uart_puts("[de1000] MMIO base       = "); uart_put_hex64(g_mmio_base); uart_puts("\n");
    uart_puts("[de1000] MAC              = "); uart_put_mac(g_mac);         uart_puts("\n");

    uart_puts("[de1000] TX desc addr    = "); uart_put_hex64((u64)g_tx_desc); uart_puts("\n");
    uart_puts("[de1000] TX buf addr     = "); uart_put_hex64((u64)g_tx_buf);  uart_puts("\n");
    uart_puts("[de1000] TX count        = "); uart_put_dec(E1000_TX_DESC_COUNT); uart_puts("\n");
    uart_puts("[de1000] TX head         = "); uart_put_dec(g_tx_head);     uart_puts("\n");

    uart_puts("[de1000] RX desc addr    = "); uart_put_hex64((u64)g_rx_desc); uart_puts("\n");
    uart_puts("[de1000] RX buf addr     = "); uart_put_hex64((u64)g_rx_buf);  uart_puts("\n");
    uart_puts("[de1000] RX count        = "); uart_put_dec(E1000_RX_DESC_COUNT); uart_puts("\n");
    uart_puts("[de1000] RX cur          = "); uart_put_dec(g_rx_cur);      uart_puts("\n");
}

typedef struct __attribute__((packed)) eth_hdr {
    u8  dst[6];
    u8  src[6];
    u16 type;
} eth_hdr_t;

typedef struct __attribute__((packed)) arp_pkt {
    u16 hw_type;
    u16 proto;
    u8  hw_len;
    u8  proto_len;
    u16 opcode;
    u8  sender_mac[6];
    u32 sender_ip;
    u8  target_mac[6];
    u32 target_ip;
} arp_pkt_t;

typedef struct __attribute__((packed)) ip_hdr {
    u8  version_ihl;
    u8  tos;
    u16 total_len;
    u16 id;
    u16 frag_off;
    u8  ttl;
    u8  proto;
    u16 checksum;
    u32 src_ip;
    u32 dst_ip;
} ip_hdr_t;

typedef struct __attribute__((packed)) icmp_hdr {
    u8  type;
    u8  code;
    u16 checksum;
    u16 id;
    u16 seq;
} icmp_hdr_t;

static u16 htons16(u16 v) { return (u16)((v >> 8) | (v << 8)); }
static u32 hton32(u32 v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

static u16 ip_checksum(const void *data, u32 len) {
    const u16 *p = (const u16 *)data;
    u32 sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const u8 *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

static u32 rdtsc_lo(void) {
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return lo;
}

static void arp_send_request(u32 target_ip) {
    u8  pkt[64];
    eth_hdr_t *eth = (eth_hdr_t *)pkt;
    arp_pkt_t *arp = (arp_pkt_t *)(pkt + sizeof(eth_hdr_t));

    for (int i = 0; i < 6; i++) eth->dst[i] = 0xFF;
    for (int i = 0; i < 6; i++) eth->src[i] = g_our_mac[i];
    eth->type = htons16(ETH_TYPE_ARP);

    arp->hw_type    = htons16(1);
    arp->proto      = htons16(ETH_TYPE_IP);
    arp->hw_len     = 6;
    arp->proto_len  = 4;
    arp->opcode     = htons16(ARP_OP_REQUEST);
    for (int i = 0; i < 6; i++) arp->sender_mac[i] = g_our_mac[i];
    arp->sender_ip  = hton32(NET_OUR_IP);
    for (int i = 0; i < 6; i++) arp->target_mac[i] = 0;
    arp->target_ip  = hton32(target_ip);

    e1000_send(pkt, sizeof(eth_hdr_t) + sizeof(arp_pkt_t));

    uart_puts("[de1000] ARP request -> ");
    uart_put_hex32(target_ip);
    uart_puts("\n");
}

static void icmp_send_echo(u32 dst_ip, u16 id, u16 seq) {
    u8 pkt[PKT_MAX];
    eth_hdr_t  *eth  = (eth_hdr_t *)pkt;
    ip_hdr_t   *ip   = (ip_hdr_t *)(pkt + sizeof(eth_hdr_t));
    icmp_hdr_t *icmp = (icmp_hdr_t *)((u8 *)ip + sizeof(ip_hdr_t));

    for (int i = 0; i < 6; i++) eth->dst[i] = g_gw_mac[i];
    for (int i = 0; i < 6; i++) eth->src[i] = g_our_mac[i];
    eth->type = htons16(ETH_TYPE_IP);

    static const char payload[] = "Depressure-ping-payload-1234567890";
    u32 plen = sizeof(payload) - 1;

    ip->version_ihl = 0x45;
    ip->tos         = 0;
    ip->total_len   = htons16((u16)(sizeof(ip_hdr_t) + sizeof(icmp_hdr_t) + plen));
    ip->id          = htons16(0x1234);
    ip->frag_off    = 0;
    ip->ttl         = 64;
    ip->proto       = IP_PROTO_ICMP;
    ip->checksum    = 0;
    ip->src_ip      = hton32(NET_OUR_IP);
    ip->dst_ip      = hton32(dst_ip);
    ip->checksum    = ip_checksum(ip, sizeof(ip_hdr_t));

    icmp->type     = ICMP_ECHO_REQUEST;
    icmp->code     = 0;
    icmp->checksum = 0;
    icmp->id       = htons16(id);
    icmp->seq      = htons16(seq);

    u8 *pl = (u8 *)icmp + sizeof(icmp_hdr_t);
    for (u32 i = 0; i < plen; i++) pl[i] = (u8)payload[i];

    u32 icmp_len = sizeof(icmp_hdr_t) + plen;
    icmp->checksum = ip_checksum(icmp, icmp_len);

    u32 total = sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + icmp_len;
    e1000_send(pkt, (u16)total);

    uart_puts("[de1000] ICMP echo -> ");
    uart_put_hex32(dst_ip);
    uart_puts(" id=");
    uart_put_dec(id);
    uart_puts(" seq=");
    uart_put_dec(seq);
    uart_puts("\n");
}

static void eth_handle_packet(const u8 *pkt, u16 len) {
    if (len < sizeof(eth_hdr_t)) return;

    const eth_hdr_t *eth = (const eth_hdr_t *)pkt;
    u16 type = htons16(eth->type);

    if (type == ETH_TYPE_ARP && len >= sizeof(eth_hdr_t) + sizeof(arp_pkt_t)) {
        const arp_pkt_t *arp = (const arp_pkt_t *)(pkt + sizeof(eth_hdr_t));
        u16 op = htons16(arp->opcode);

        if (op == ARP_OP_REPLY) {
            u32 sender_ip = hton32(arp->sender_ip);
            uart_puts("[de1000] ARP reply from ");
            uart_put_hex32(sender_ip);
            uart_puts(" mac=");
            uart_put_mac(arp->sender_mac);
            uart_puts("\n");

            for (int i = 0; i < 6; i++) g_gw_mac[i] = arp->sender_mac[i];
            g_gw_mac_valid = 1;
        } else if (op == ARP_OP_REQUEST) {
            uart_puts("[de1000] ARP request from ");
            uart_put_hex32(hton32(arp->sender_ip));
            uart_puts("\n");
        }
    }
    else if (type == ETH_TYPE_IP && len >= sizeof(eth_hdr_t) + sizeof(ip_hdr_t)) {
        const ip_hdr_t *ip = (const ip_hdr_t *)(pkt + sizeof(eth_hdr_t));
        u8 proto = ip->proto;
        u32 src  = hton32(ip->src_ip);

        if (proto == IP_PROTO_ICMP &&
            len >= sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(icmp_hdr_t)) {
            const icmp_hdr_t *icmp = (const icmp_hdr_t *)((const u8 *)ip + sizeof(ip_hdr_t));

            if (icmp->type == ICMP_ECHO_REPLY) {
                u32 now = rdtsc_lo();
                u32 rtt = now - g_ping_rtt_lo;

                uart_puts("[de1000] ICMP echo reply from ");
                uart_put_hex32(src);
                uart_puts(" id=");
                uart_put_dec(htons16(icmp->id));
                uart_puts(" seq=");
                uart_put_dec(htons16(icmp->seq));
                uart_puts(" rtt~");
                uart_put_dec(rtt);
                uart_puts(" cycles\n");

                g_ping_rtt_hi = now;
                g_ping_done = 1;
                g_ping_ok = 1;
            } else {
                uart_puts("[de1000] ICMP type ");
                uart_put_dec(icmp->type);
                uart_puts(" from ");
                uart_put_hex32(src);
                uart_puts("\n");
            }
        }
    }
}

static void e1000_test_network(void) {
    uart_puts("[de1000] === network test ===\n");

    for (int i = 0; i < 6; i++) g_our_mac[i] = g_mac[i];

    g_gw_mac_valid = 0;
    arp_send_request(NET_GATEWAY_IP);

    u8  pkt[PKT_MAX];
    u16 plen;

    for (int t = 0; t < 500; t++) {
        if (e1000_recv(pkt, &plen) == 0) eth_handle_packet(pkt, plen);
        if (g_gw_mac_valid) break;
        uefi_stall(1000);
    }

    if (!g_gw_mac_valid) {
        uart_puts("[de1000] ARP timeout, no reply\n");
        return;
    }

    uart_puts("[de1000] gateway MAC resolved\n");

    g_ping_done = 0;
    g_ping_ok   = 0;
    g_ping_rtt_lo = rdtsc_lo();
    icmp_send_echo(NET_GATEWAY_IP, 0xAA55, 1);

    for (int t = 0; t < 500; t++) {
        if (e1000_recv(pkt, &plen) == 0) eth_handle_packet(pkt, plen);
        if (g_ping_done) break;
        uefi_stall(1000);
    }

    uart_puts(g_ping_ok ? "[de1000] PING OK\n"
                        : "[de1000] PING timeout\n");
    uart_puts("[de1000] === network test done ===\n");
}

static int e1000_init(void) {
    uart_puts("[de1000] init start\n");

    int found = 0;
    for (u8 slot = 0; slot < 32 && !found; slot++) {
        u32 id = pci_read32(0, slot, 0, 0x00);
        if ((id & 0xFFFF) != DE1000_VENDOR_ID) continue;
        if (((id >> 16) & 0xFFFF) != DE1000_DEVICE_ID) continue;

        g_pci_bus = 0;
        g_pci_slot = slot;
        g_pci_func = 0;
        g_pci_vendor = id & 0xFFFF;
        g_pci_device = (id >> 16) & 0xFFFF;

        uart_puts("[de1000] found device at slot ");
        uart_put_dec(slot);
        uart_puts("\n");

        u32 bar0 = pci_read32(0, slot, 0, 0x10);
        g_pci_bar0 = bar0;

        uart_puts("[de1000] BAR0 = "); uart_put_hex32(bar0); uart_puts("\n");

        if (bar0 & 0x1) {
            uart_puts("[de1000] BAR0 is IO, not MMIO — abort\n");
            return -1;
        }
        g_mmio_base = bar0 & 0xFFFFFFF0;

        u32 cmd = pci_read32(0, slot, 0, 0x04);
        uart_puts("[de1000] PCI CMD before = "); uart_put_hex32(cmd); uart_puts("\n");
        cmd |= (1 << 1) | (1 << 2);
        pci_write32(0, slot, 0, 0x04, cmd);
        uart_puts("[de1000] PCI CMD after  = "); uart_put_hex32(cmd); uart_puts("\n");

        found = 1;
    }

    if (!found || !g_mmio_base) {
        uart_puts("[de1000] device not found\n");
        return -1;
    }

    uart_puts("[de1000] MMIO base = "); uart_put_hex64(g_mmio_base); uart_puts("\n");

    uart_puts("[de1000] reset...\n");
    mmio_write(g_mmio_base, E1000_CTRL, 0x04000000);
    while (mmio_read(g_mmio_base, E1000_CTRL) & 0x04000000) {}

    u32 ral = mmio_read(g_mmio_base, E1000_RAL);
    u32 rah = mmio_read(g_mmio_base, E1000_RAH);
    g_mac[0] = ral & 0xFF;
    g_mac[1] = (ral >> 8) & 0xFF;
    g_mac[2] = (ral >> 16) & 0xFF;
    g_mac[3] = (ral >> 24) & 0xFF;
    g_mac[4] = rah & 0xFF;
    g_mac[5] = (rah >> 8) & 0xFF;

    uart_puts("[de1000] MAC = "); uart_put_mac(g_mac); uart_puts("\n");

    for (int i = 0; i < E1000_TX_DESC_COUNT; i++) {
        g_tx_desc[i].addr = (u64)&g_tx_buf[i][0];
        g_tx_desc[i].status = 1;
    }
    mmio_write(g_mmio_base, E1000_TDBAL, (u32)(u64)g_tx_desc);
    mmio_write(g_mmio_base, E1000_TDBAH, (u32)((u64)g_tx_desc >> 32));
    mmio_write(g_mmio_base, E1000_TDLEN, sizeof(g_tx_desc));
    mmio_write(g_mmio_base, E1000_TDH, 0);
    mmio_write(g_mmio_base, E1000_TDT, 0);
    mmio_write(g_mmio_base, E1000_TCTL,
                (1 << 1) | (1 << 3) | (0x10 << 4) | (0x40 << 12));

    for (int i = 0; i < E1000_RX_DESC_COUNT; i++) {
        g_rx_desc[i].addr = (u64)&g_rx_buf[i][0];
        g_rx_desc[i].status = 0;
    }
    mmio_write(g_mmio_base, E1000_RDBAL, (u32)(u64)g_rx_desc);
    mmio_write(g_mmio_base, E1000_RDBAH, (u32)((u64)g_rx_desc >> 32));
    mmio_write(g_mmio_base, E1000_RDLEN, sizeof(g_rx_desc));
    mmio_write(g_mmio_base, E1000_RDH, 0);
    mmio_write(g_mmio_base, E1000_RDT, E1000_RX_DESC_COUNT - 1);
    mmio_write(g_mmio_base, E1000_RCTL,
                (1 << 1) | (1 << 2) | (1 << 15) | (1 << 26));

    e1000_dump_state();
    e1000_dump_regs();

    uart_puts("[de1000] init done\n");
    return 0;
}

static de1000_api_t g_e1000_api;

static ddriver_t e1000_driver = {
    .pattern = DDRIVER_PATTERN,
    .version = DDRIVER_VERSION,
    .next    = 0,
    .type    = DDRIVER_TYPE_NET,
    .name    = "de1000",
    .entry   = 0,
    .api     = 0,
    .data    = 0,
};

void de1000_main(void *api, void *fb_info) {
    (void)fb_info;

    if (e1000_init() != 0) {
        uart_puts("[de1000] init FAILED\n");
        return;
    }

    for (int i = 0; i < 6; i++) g_e1000_api.mac[i] = g_mac[i];
    g_e1000_api.version  = 0x00010000;
    g_e1000_api._pad     = 0;
    g_e1000_api.send     = e1000_send;
    g_e1000_api.recv     = e1000_recv;
    g_e1000_api.get_base = e1000_get_base;

    if (api && ((dkernel_api_t *)api)->register_driver) {
        e1000_driver.api = (u64)&g_e1000_api;
        ((dkernel_api_t *)api)->register_driver(&e1000_driver);
        uart_puts("[de1000] registered as 'de1000'\n");
    }

    e1000_test_network();
}