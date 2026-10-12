#ifndef ZEROOS_NET_NIC_H
#define ZEROOS_NET_NIC_H
#include "types.h"

/*
 * Realtek RTL8139 Fast Ethernet driver and IPv4 bring-up.
 *
 * Contract:
 *  - The device is discovered through PCI enumeration (vendor 0x10EC,
 *    device 0x8139); BAR1 is mapped as the register window. Registers are
 *    polled, so this driver never claims an interrupt vector and cannot
 *    perturb the certified interrupt layer.
 *  - RX/TX buffers come from the page allocator, whose kernel addresses are
 *    the identity/direct map of physical memory, so they are valid DMA
 *    addresses without translation.
 *  - Frames flow through the netif layer; ARP and DHCP reuse the
 *    host-tested net_arp and dhcp_core state machines. IPv4 is obtained by
 *    a real DHCP exchange, never hardcoded.
 *  - Every entry point degrades to a diagnostic when no supported NIC is
 *    present. Networking is never allowed to fail or stall the boot.
 */
int net_nic_probe_all(void);
void net_start(void);
int net_nic_attached(void);
uint32_t net_nic_ipv4_address(void);

#endif
