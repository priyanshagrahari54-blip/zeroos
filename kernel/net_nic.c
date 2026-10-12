/*
 * Realtek RTL8139 Fast Ethernet driver: real PCI discovery, real MMIO
 * register programming, real DMA, and a real IPv4 address obtained over
 * DHCP. This is the layer that turns the netif/net_arp/dhcp_core logic
 * cores into working network connectivity.
 */
#include "net_nic.h"

#include "dhcp_core.h"
#include "kstring.h"
#include "memory.h"
#include "net_arp.h"
#include "netif.h"
#include "pci.h"
#include "sync.h"
#include "task.h"
#include "timer.h"

#define RTL8139_VENDOR 0x10ECU
#define RTL8139_DEVICE 0x8139U

/* Register offsets (memory-mapped window, BAR1). */
#define RTL_MAC0       0x00U
#define RTL_TXSTATUS0  0x10U
#define RTL_TXADDR0    0x20U
#define RTL_RXBUF      0x30U
#define RTL_CHIPCMD    0x37U
#define RTL_RXBUFPTR   0x38U
#define RTL_INTRMASK   0x3CU
#define RTL_INTRSTATUS 0x3EU
#define RTL_TXCONFIG   0x40U
#define RTL_RXCONFIG   0x44U
#define RTL_RXMISSED   0x4CU
#define RTL_CFG9346    0x50U
#define RTL_CONFIG1    0x52U
#define RTL_MEDIASTAT  0x58U

/* ChipCmd bits. */
#define RTL_CMD_RST      0x10U
#define RTL_CMD_RE       0x08U
#define RTL_CMD_TE       0x04U
#define RTL_CMD_BUFEMPTY 0x01U

/* Interrupt status bits (polled, never unmasked). */
#define RTL_ISR_ROK   0x0001U
#define RTL_ISR_RER   0x0002U
#define RTL_ISR_RXOVW 0x0010U

/* RxConfig accept masks. */
#define RTL_RCR_AAP 0x01U
#define RTL_RCR_APM 0x02U
#define RTL_RCR_AM  0x04U
#define RTL_RCR_AB  0x08U

/* TxStatus bits. */
#define RTL_TX_TOK  0x00002000U
#define RTL_TX_TUN  0x00004000U
#define RTL_TX_TABT 0x00008000U

/* The ring wraps at the size programmed into RxConfig (8 KiB); the extra 16
 * bytes are allocation slack for a packet that straddles the wrap, not part
 * of the modulo. The buffer itself is three pages so that slack is real. */
#define RTL_RX_BUF_SZ 8192U
#define RTL_RX_PAGES  3U
#define RTL_TX_BUF_SZ 1536U
#define RTL_TX_DESCS  4U

#define NET_DHCP_PORT_CLIENT 68U
#define NET_DHCP_PORT_SERVER 67U
#define NET_ETHERTYPE_IPV4   0x0800U
#define NET_ETHERTYPE_ARP    0x0806U

struct rtl8139 {
    volatile uint8_t *regs;
    struct pci_device *pci;
    uint8_t mac[6];
    uint8_t *rx_buf;
    uint8_t *tx_buf[RTL_TX_DESCS];
    uint32_t rx_offset;
    uint32_t tx_next;
    int present;
};

static struct rtl8139 rtl_device;
static struct netif net_interface;
static struct net_arp_cache arp_cache;
static struct dhcp_client dhcp;
static struct spinlock net_lock;
static uint64_t net_task_id;
static int net_started;
static uint32_t net_local_ipv4;
static uint32_t net_gateway_ipv4;
static uint8_t net_gateway_mac[6];
static int net_gateway_resolved;
static uint64_t net_stats_frames;
static uint64_t net_stats_tx;
static uint32_t net_arp_sent;
static uint32_t net_arp_seen;
static uint32_t net_frame_logged;
static uint32_t net_arp_parse_fail;

static uint8_t rtl_read8(struct rtl8139 *device, uint32_t offset) {
    return *(volatile uint8_t *)(device->regs + offset);
}

static uint16_t rtl_read16(struct rtl8139 *device, uint32_t offset) {
    return *(volatile uint16_t *)(device->regs + offset);
}

static uint32_t rtl_read32(struct rtl8139 *device, uint32_t offset) {
    return *(volatile uint32_t *)(device->regs + offset);
}

static void rtl_write8(struct rtl8139 *device, uint32_t offset, uint8_t value) {
    *(volatile uint8_t *)(device->regs + offset) = value;
}

static void rtl_write16(struct rtl8139 *device, uint32_t offset,
                        uint16_t value) {
    *(volatile uint16_t *)(device->regs + offset) = value;
}

static void rtl_write32(struct rtl8139 *device, uint32_t offset,
                        uint32_t value) {
    *(volatile uint32_t *)(device->regs + offset) = value;
}

static uint64_t net_lock_fn(void *context) {
    (void)context;
    return spin_lock_irqsave(&net_lock);
}

static void net_unlock_fn(void *context, uint64_t state) {
    (void)context;
    spin_unlock_irqrestore(&net_lock, state);
}

/* ---- byte order and checksums ---- */

static void put_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t checksum_finish(uint32_t sum) {
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFU);
}

static uint16_t ip_checksum(const uint8_t *data, uint32_t length) {
    uint32_t sum = 0;
    uint32_t i = 0;

    for (; i + 1U < length; i += 2U)
        sum += ((uint32_t)data[i] << 8) | data[i + 1U];
    if (i < length)
        sum += (uint32_t)data[i] << 8;
    return checksum_finish(sum);
}

/* Addresses are host integers holding network-order bytes, so splitting them
 * into 16-bit halves yields exactly the on-wire pseudo-header order. */
static uint16_t udp_checksum(uint32_t src, uint32_t dst, const uint8_t *udp,
                             uint16_t length) {
    uint32_t sum = 0;
    uint32_t i = 0;
    uint16_t value;

    sum += (src >> 16) & 0xFFFFU;
    sum += src & 0xFFFFU;
    sum += (dst >> 16) & 0xFFFFU;
    sum += dst & 0xFFFFU;
    sum += 17U;
    sum += length;
    for (; i + 1U < length; i += 2U)
        sum += ((uint32_t)udp[i] << 8) | udp[i + 1U];
    if (i < length)
        sum += (uint32_t)udp[i] << 8;
    value = checksum_finish(sum);
    return value ? value : 0xFFFFU;
}

/* ---- transmit ---- */

static int rtl_transmit(void *context, const uint8_t *frame, uint16_t length) {
    struct rtl8139 *device = (struct rtl8139 *)context;
    uint32_t slot;
    uint32_t tx_length = length;
    uint32_t i;

    if (!device->present || !frame || length < 14U || length > RTL_TX_BUF_SZ)
        return -1;
    slot = device->tx_next % RTL_TX_DESCS;
    memcpy(device->tx_buf[slot], frame, length);
    /* The 8139 does not pad short frames: ARP at 42 bytes must be zero-filled
     * to the 60-byte Ethernet minimum before the length is committed. */
    if (tx_length < 60U) {
        memset(device->tx_buf[slot] + tx_length, 0, 60U - tx_length);
        tx_length = 60U;
    }
    rtl_write32(device, RTL_TXADDR0 + slot * 4U,
                (uint32_t)(uint64_t)(uintptr_t)device->tx_buf[slot]);
    /* Writing the length with the ownership bits clear starts the transfer. */
    rtl_write32(device, RTL_TXSTATUS0 + slot * 4U, tx_length & 0x1FFFU);
    for (i = 0; i < 200000U; ++i) {
        uint32_t status = rtl_read32(device, RTL_TXSTATUS0 + slot * 4U);

        if (status & (RTL_TX_TABT | RTL_TX_TUN))
            return -2;
        if (status & RTL_TX_TOK) {
            device->tx_next++;
            ++net_stats_tx;
            return 0;
        }
    }
    return -3;
}

/* ---- receive ---- */

/* The ring wraps mid-packet, so every access goes through the modulo. */
static uint8_t rx_byte(struct rtl8139 *device, uint32_t offset) {
    return device->rx_buf[offset % RTL_RX_BUF_SZ];
}

static int rtl_receive(struct rtl8139 *device) {
    int received = 0;
    uint16_t status;

    if (!device->present)
        return 0;
    if (rtl_read8(device, RTL_CHIPCMD) & RTL_CMD_BUFEMPTY)
        return 0;
    status = rtl_read16(device, RTL_INTRSTATUS);
    rtl_write16(device, RTL_INTRSTATUS,
                status & (uint16_t)(RTL_ISR_ROK | RTL_ISR_RER | RTL_ISR_RXOVW));

    while (!(rtl_read8(device, RTL_CHIPCMD) & RTL_CMD_BUFEMPTY) &&
           received < 8) {
        uint32_t offset = device->rx_offset % RTL_RX_BUF_SZ;
        uint32_t header = (uint32_t)rx_byte(device, offset) |
                          ((uint32_t)rx_byte(device, offset + 1U) << 8);
        uint32_t length = (uint32_t)rx_byte(device, offset + 2U) |
                          ((uint32_t)rx_byte(device, offset + 3U) << 8);
        uint32_t frame_length = length > 4U ? length - 4U : 0U;
        uint8_t frame[NETIF_FRAME_MAX];

        if ((header & 1U) && frame_length >= 14U &&
            frame_length <= sizeof(frame)) {
            uint32_t i;

            for (i = 0; i < frame_length; ++i)
                frame[i] = rx_byte(device, offset + 4U + i);
            if (netif_receive(&net_interface, frame, frame_length) == 0)
                ++received;
            ++net_stats_frames;
        }
        device->rx_offset = (offset + ((length + 4U + 3U) & ~3U)) % RTL_RX_BUF_SZ;
        rtl_write16(device, RTL_RXBUFPTR,
                    (uint16_t)((device->rx_offset + RTL_RX_BUF_SZ - 16U) %
                               RTL_RX_BUF_SZ));
    }
    return received;
}

/* ---- IPv4/UDP transmit built directly on netif ---- */

static int net_tx_udp(uint32_t src_ip, uint32_t dst_ip,
                      const uint8_t dst_mac[6], uint16_t sport,
                      uint16_t dport, const uint8_t *payload,
                      uint16_t payload_length) {
    uint8_t frame[1514];
    uint8_t *ip = frame + 14U;
    uint8_t *udp = ip + 20U;
    uint16_t udp_length = (uint16_t)(8U + payload_length);
    uint16_t ip_length = (uint16_t)(20U + udp_length);
    uint32_t i;

    if (14U + ip_length > sizeof(frame))
        return -1;
    for (i = 0; i < 6U; ++i) {
        frame[i] = dst_mac[i];
        frame[6U + i] = net_interface.address[i];
    }
    put_be16(frame + 12U, NET_ETHERTYPE_IPV4);
    memset(ip, 0, 20U);
    ip[0] = 0x45U;
    put_be16(ip + 2U, ip_length);
    put_be16(ip + 6U, 0x4000U);
    ip[8] = 64U;
    ip[9] = 17U;
    put_be32(ip + 12U, src_ip);
    put_be32(ip + 16U, dst_ip);
    put_be16(ip + 10U, ip_checksum(ip, 20U));
    put_be16(udp + 0U, sport);
    put_be16(udp + 2U, dport);
    put_be16(udp + 4U, udp_length);
    put_be16(udp + 6U, 0U);
    for (i = 0; i < payload_length; ++i)
        udp[8U + i] = payload[i];
    put_be16(udp + 6U, udp_checksum(src_ip, dst_ip, udp, udp_length));
    return netif_send(&net_interface, frame, (uint16_t)(14U + ip_length));
}

/* ---- DHCP ---- */

static uint16_t dhcp_build_request(uint8_t *packet, uint8_t message_type) {
    uint32_t offset = 236U;
    uint32_t i;

    memset(packet, 0, 240U);
    packet[0] = 1U;   /* BOOTREQUEST */
    packet[1] = 1U;   /* hardware type: Ethernet */
    packet[2] = 6U;   /* hardware address length */
    put_be32(packet + 4U, dhcp.xid);
    put_be16(packet + 10U, 0x8000U);
    for (i = 0; i < 6U; ++i)
        packet[28U + i] = net_interface.address[i];
    /* A RENEW is a unicast to the server and must carry the bound address. */
    if (message_type == 3U && dhcp.state == DHCP_RENEWING)
        put_be32(packet + 12U, dhcp.address);

    packet[offset++] = 99U;
    packet[offset++] = 130U;
    packet[offset++] = 83U;
    packet[offset++] = 99U;
    packet[offset++] = 53U;
    packet[offset++] = 1U;
    packet[offset++] = message_type;
    if (message_type == 3U) {
        packet[offset++] = 50U;
        packet[offset++] = 4U;
        put_be32(packet + offset, dhcp.address);
        offset += 4U;
        packet[offset++] = 54U;
        packet[offset++] = 4U;
        put_be32(packet + offset, dhcp.server);
        offset += 4U;
    }
    packet[offset++] = 55U;
    packet[offset++] = 4U;
    packet[offset++] = 1U;   /* subnet mask */
    packet[offset++] = 3U;   /* router */
    packet[offset++] = 6U;   /* DNS */
    packet[offset++] = 51U;  /* lease time */
    packet[offset++] = 255U;
    return (uint16_t)offset;
}

static void dhcp_transmit(uint8_t message_type) {
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t packet[300];
    uint16_t length = dhcp_build_request(packet, message_type);

    (void)net_tx_udp(net_local_ipv4, 0xFFFFFFFFU, broadcast,
                     NET_DHCP_PORT_CLIENT, NET_DHCP_PORT_SERVER, packet,
                     length);
}

static void net_apply_lease(void) {
    uint32_t mask = 0xFFFFFF00U;

    net_local_ipv4 = dhcp.address;
    klog("ZEROOS: net0: dhcp bound address=%u.%u.%u.%u lease=%us.",
         (net_local_ipv4 >> 24) & 0xFFU, (net_local_ipv4 >> 16) & 0xFFU,
         (net_local_ipv4 >> 8) & 0xFFU, net_local_ipv4 & 0xFFU,
         dhcp.lease_seconds);
    /* dhcp_view exposes no router option, so the gateway is derived from the
     * leased subnet (.2 is the conventional first-hop on a /24). */
    net_gateway_ipv4 = (net_local_ipv4 & mask) | 0x02U;
    net_gateway_resolved = 0;
}

/* ---- inbound frame dispatch ---- */

static void net_handle_arp(const uint8_t *frame, uint32_t length) {
    struct net_arp_ipv4 arp;

    if (net_arp_parse_ipv4(frame, length, &arp) != 0) {
        if (net_arp_parse_fail < 3U) {
            ++net_arp_parse_fail;
            klog("ZEROOS: net0: arp parse rejected len=%u type=0x%x.", length,
                 length >= 14U ? get_be16(frame + 12U) : 0U);
        }
        return;
    }
    if (arp.operation == NET_ARP_REQUEST && net_local_ipv4 &&
        arp.target_ipv4 == net_local_ipv4) {
        uint8_t reply[NET_ARP_ETHERNET_FRAME_SIZE];
        /* The builder returns the frame length on success, negative on error. */
        int built = net_arp_build_reply(net_interface.address, net_local_ipv4,
                                        arp.sender_mac, arp.sender_ipv4, reply);

        if (built > 0)
            (void)netif_send(&net_interface, reply, (uint32_t)built);
        return;
    }
    if (arp.operation == NET_ARP_REPLY && net_local_ipv4) {
        int rc = net_arp_cache_accept_reply(&arp_cache, frame, length,
                                            net_local_ipv4,
                                            net_interface.address,
                                            timer_ticks(),
                                            timer_ticks() + 6000U);

        if (net_arp_seen < 4U)
            klog("ZEROOS: net0: arp reply from %u.%u.%u.%u admit=%d "
                 "ethdst=%x:%x:%x:%x:%x:%x.",
                 (arp.sender_ipv4 >> 24) & 0xFFU, (arp.sender_ipv4 >> 16) & 0xFFU,
                 (arp.sender_ipv4 >> 8) & 0xFFU, arp.sender_ipv4 & 0xFFU, rc,
                 frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]);
        ++net_arp_seen;
    }
}

static void net_handle_ipv4(const uint8_t *frame, uint32_t length) {
    const uint8_t *ip = frame + 14U;
    uint32_t header_length = (uint32_t)(ip[0] & 0x0FU) * 4U;
    const uint8_t *udp;
    uint16_t dport, udp_length;
    uint32_t destination_ip;

    if (length < 34U || header_length < 20U)
        return;
    if (ip[9] != 17U)
        return;
    udp = ip + header_length;
    if (14U + header_length + 8U > length)
        return;
    /* Accept broadcast (pre-lease DHCP) or our own address; drop the rest
     * rather than feeding unaddressed datagrams to the DHCP client. */
    destination_ip = get_be32(ip + 16U);
    if (destination_ip != 0xFFFFFFFFU &&
        (!net_local_ipv4 || destination_ip != net_local_ipv4))
        return;
    dport = get_be16(udp + 2U);
    udp_length = get_be16(udp + 4U);
    if (dport != NET_DHCP_PORT_CLIENT || udp_length < 8U ||
        14U + header_length + udp_length > length)
        return;
    if (dhcp_client_receive(&dhcp, udp + 8U, (uint32_t)udp_length - 8U,
                            (uint32_t)timer_ticks()) == 3)
        net_apply_lease();
}

static void net_handle_frame(const uint8_t *frame, uint16_t length) {
    if (length < 14U)
        return;
    if (net_frame_logged < 6U) {
        ++net_frame_logged;
        klog("ZEROOS: net0: rx frame %u len=%u type=0x%x dst=%x:%x:%x:%x:%x:%x.",
             net_frame_logged, length, get_be16(frame + 12U), frame[0],
             frame[1], frame[2], frame[3], frame[4], frame[5]);
    }
    switch (get_be16(frame + 12U)) {
    case NET_ETHERTYPE_ARP:
        net_handle_arp(frame, length);
        break;
    case NET_ETHERTYPE_IPV4:
        net_handle_ipv4(frame, length);
        break;
    default:
        break;
    }
}

/* ---- ARP resolution of the first hop ---- */

static void net_resolve_gateway(void) {
    uint8_t request[NET_ARP_ETHERNET_FRAME_SIZE];

    if (net_arp_cache_lookup(&arp_cache, net_gateway_ipv4, timer_ticks(),
                             net_gateway_mac) == 0) {
        net_gateway_resolved = 1;
        klog("ZEROOS: net0: first hop %u.%u.%u.%u resolved to "
             "%x:%x:%x:%x:%x:%x.",
             (net_gateway_ipv4 >> 24) & 0xFFU, (net_gateway_ipv4 >> 16) & 0xFFU,
             (net_gateway_ipv4 >> 8) & 0xFFU, net_gateway_ipv4 & 0xFFU,
             net_gateway_mac[0], net_gateway_mac[1], net_gateway_mac[2],
             net_gateway_mac[3], net_gateway_mac[4], net_gateway_mac[5]);
        return;
    }
    if (net_arp_cache_begin_resolution(&arp_cache, net_local_ipv4,
                                       net_interface.address,
                                       net_gateway_ipv4, timer_ticks(),
                                       timer_ticks() + 300U) != 0)
        return;
    /* The builder returns the frame length on success, negative on error. */
    int built = net_arp_build_request(net_interface.address, net_local_ipv4,
                                      net_gateway_ipv4, request);

    if (built <= 0) {
        if (net_arp_sent < 3U)
            klog("ZEROOS: net0: arp request build failed.");
        ++net_arp_sent;
        return;
    }
    ++net_arp_sent;
    if (net_arp_sent <= 3U || net_arp_sent % 20U == 0U)
        klog("ZEROOS: net0: arp request %u for %u.%u.%u.%u sent; "
             "cache learned=%llu rejected=%llu unsolicited=%llu "
             "pending_expired=%llu.",
             net_arp_sent, (net_gateway_ipv4 >> 24) & 0xFFU,
             (net_gateway_ipv4 >> 16) & 0xFFU, (net_gateway_ipv4 >> 8) & 0xFFU,
             net_gateway_ipv4 & 0xFFU,
             (unsigned long long)arp_cache.learned,
             (unsigned long long)arp_cache.rejected,
             (unsigned long long)arp_cache.unsolicited_replies,
             (unsigned long long)arp_cache.pending_expired);
    (void)netif_send(&net_interface, request, (uint32_t)built);
}

/* ---- device attach ---- */

static int rtl8139_attach(struct pci_device *pci) {
    uint64_t base;
    uint32_t i;

    pci_enable_device(pci, 0);
    base = pci_map_bar(pci, 1U, 0);
    if (!base) {
        klog("ZEROOS: net0: rtl8139 BAR1 mapping failed.");
        return -1;
    }
    rtl_device.regs = (volatile uint8_t *)base;
    rtl_device.pci = pci;

    rtl_write8(&rtl_device, RTL_CHIPCMD, RTL_CMD_RST);
    for (i = 0; i < 2000000U &&
                (rtl_read8(&rtl_device, RTL_CHIPCMD) & RTL_CMD_RST);
         ++i)
        ;
    if (rtl_read8(&rtl_device, RTL_CHIPCMD) & RTL_CMD_RST) {
        klog("ZEROOS: net0: rtl8139 reset timed out.");
        return -1;
    }
    for (i = 0; i < 6U; ++i)
        rtl_device.mac[i] = rtl_read8(&rtl_device, RTL_MAC0 + i);
    /* A zero MAC means the register window is not answering; bringing the
     * interface up would produce frames nobody can reply to. */
    if (rtl_device.mac[0] == 0U && rtl_device.mac[1] == 0U &&
        rtl_device.mac[2] == 0U && rtl_device.mac[3] == 0U &&
        rtl_device.mac[4] == 0U && rtl_device.mac[5] == 0U) {
        klog("ZEROOS: net0: rtl8139 MAC read back as all zeros; "
             "register window not responding.");
        rtl_device.present = 0;
        return -1;
    }

    rtl_device.rx_buf = (uint8_t *)page_alloc_contiguous(RTL_RX_PAGES);
    if (!rtl_device.rx_buf) {
        klog("ZEROOS: net0: rx ring allocation failed.");
        return -1;
    }
    memset(rtl_device.rx_buf, 0, RTL_RX_PAGES * ZEROOS_PAGE_SIZE);
    for (i = 0; i < RTL_TX_DESCS; ++i) {
        rtl_device.tx_buf[i] = (uint8_t *)page_alloc_zero();
        if (!rtl_device.tx_buf[i]) {
            klog("ZEROOS: net0: tx buffer allocation failed.");
            return -1;
        }
    }

    rtl_write8(&rtl_device, RTL_CFG9346, 0xC0U);
    rtl_write8(&rtl_device, RTL_CONFIG1, 0x00U);
    rtl_write32(&rtl_device, RTL_RXBUF,
                (uint32_t)(uint64_t)(uintptr_t)rtl_device.rx_buf);
    rtl_write16(&rtl_device, RTL_RXBUFPTR, 0U);
    for (i = 0; i < RTL_TX_DESCS; ++i) {
        rtl_write32(&rtl_device, RTL_TXADDR0 + i * 4U,
                    (uint32_t)(uint64_t)(uintptr_t)rtl_device.tx_buf[i]);
        rtl_write32(&rtl_device, RTL_TXSTATUS0 + i * 4U, 0U);
    }
    rtl_write32(&rtl_device, RTL_TXCONFIG, 0x03000700U);
    rtl_write32(&rtl_device, RTL_RXCONFIG,
                RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB);
    rtl_write16(&rtl_device, RTL_INTRMASK, 0U);
    rtl_write16(&rtl_device, RTL_INTRSTATUS, 0xFFFFU);
    rtl_write8(&rtl_device, RTL_CHIPCMD, RTL_CMD_RE | RTL_CMD_TE);
    rtl_write8(&rtl_device, RTL_CFG9346, 0x00U);

    rtl_device.rx_offset = 0;
    rtl_device.tx_next = 0;
    rtl_device.present = 1;
    return 0;
}

int net_nic_probe_all(void) {
    uint32_t i;

    if (rtl_device.present)
        return 1;
    for (i = 0; i < pci_device_count(); ++i) {
        struct pci_device *pci = pci_device_at(i);

        if (!pci || pci->vendor != RTL8139_VENDOR ||
            pci->device != RTL8139_DEVICE)
            continue;
        if (rtl8139_attach(pci) != 0)
            continue;
        if (netif_init(&net_interface, "net0", 1U, rtl_device.mac, 1500U,
                       &rtl_device, rtl_transmit, net_lock_fn,
                       net_unlock_fn) != 0) {
            rtl_device.present = 0;
            continue;
        }
        net_arp_cache_init(&arp_cache);
        netif_set_link(&net_interface, 1);
        klog("ZEROOS: net0: rtl8139 attached at %u:%u.%u mac=%x:%x:%x:%x:%x:%x "
             "media=0x%x.",
             pci->bus, pci->slot, pci->function, rtl_device.mac[0],
             rtl_device.mac[1], rtl_device.mac[2], rtl_device.mac[3],
             rtl_device.mac[4], rtl_device.mac[5],
             rtl_read8(&rtl_device, RTL_MEDIASTAT));
        return 1;
    }
    return 0;
}

int net_nic_attached(void) {
    return rtl_device.present;
}

uint32_t net_nic_ipv4_address(void) {
    return net_local_ipv4;
}

/* ---- driver task ---- */

static void net_task(void *argument) {
    uint64_t arp_deadline = 0;
    uint32_t probe_attempts = 0;

    (void)argument;
    for (;;) {
        uint64_t now = timer_ticks();
        uint8_t buffer[NETIF_FRAME_MAX];
        uint16_t length = 0;
        uint32_t drained = 0;

        if (!rtl_device.present) {
            /* PCI enumeration runs in the storage task, so retry until it has
             * published its device table before concluding there is no NIC. */
            if (net_nic_probe_all() == 0) {
                if (++probe_attempts >= 400U) {
                    klog("ZEROOS: net0: no supported network device present; "
                         "network unavailable.");
                    task_exit();
                    return;
                }
                task_sleep_ticks(5U);
                continue;
            }
            dhcp_client_start(&dhcp, 0x5A05C0DEU, (uint32_t)now);
            arp_deadline = now;
            klog("ZEROOS: net0: link up, starting dhcp.");
        }

        (void)rtl_receive(&rtl_device);
        while (drained < 8U &&
               netif_dequeue(&net_interface, buffer, sizeof(buffer),
                             &length) == 0) {
            net_handle_frame(buffer, length);
            ++drained;
        }

        switch (dhcp_client_tick(&dhcp, (uint32_t)now, 300U, 6U)) {
        case DHCP_ACTION_DISCOVER:
            dhcp_transmit(1U);
            break;
        case DHCP_ACTION_REQUEST:
        case DHCP_ACTION_RENEW:
        case DHCP_ACTION_REBIND:
            dhcp_transmit(3U);
            break;
        case DHCP_ACTION_EXPIRED:
            net_local_ipv4 = 0;
            net_gateway_resolved = 0;
            dhcp_client_start(&dhcp, dhcp.xid, (uint32_t)now);
            break;
        default:
            break;
        }

        if (net_local_ipv4 && !net_gateway_resolved &&
            (int64_t)(now - arp_deadline) >= 0) {
            arp_deadline = now + 50U;
            net_resolve_gateway();
        }
        task_sleep_ticks(2U);
    }
}

void net_start(void) {
    if (net_started)
        return;
    net_started = 1;
    if (task_create(net_task, 0, &net_task_id) != 0)
        klog("ZEROOS: net0: driver task creation failed; network unavailable.");
}
