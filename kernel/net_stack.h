#ifndef ZEROOS_NET_STACK_H
#define ZEROOS_NET_STACK_H
#include "net_core.h"
#include "netif.h"

#define NET_STACK_FAMILY_IPV4 4U
#define NET_STACK_FAMILY_IPV6 6U
#define NET_STACK_ETHERTYPE_IPV4 0x0800U
#define NET_STACK_ETHERTYPE_IPV6 0x86ddU
#define NET_STACK_PROTOCOL_UDP 17U

typedef int (*net_stack_udp_fn)(void *context, uint8_t family,
                                uint32_t interface_index,
                                const uint8_t source[16],
                                const uint8_t destination[16],
                                uint16_t source_port,
                                uint16_t destination_port,
                                const uint8_t *payload, uint16_t length);

struct net_stack_stats {
    uint64_t frames, malformed, unsupported, policy_drops, fragments;
    uint64_t udp_delivered, udp_checksum_errors, callback_errors;
    uint64_t udp_transmitted, transmit_errors;
};

/* Caller serializes state access. IPv4 is default-deny through firewall;
 * IPv6 is intentionally unsupported until an explicit IPv6 policy exists. */
struct net_stack {
    struct net_firewall ipv4_firewall;
    net_stack_udp_fn udp_receive;
    void *context;
    uint32_t ipv4_local_address;
    uint8_t ipv4_configured;
    struct net_stack_stats stats;
};

void net_stack_init(struct net_stack *stack, net_stack_udp_fn udp_receive,
                    void *context);
/* Address is stored in network byte order as a host integer, e.g. 0xc0000202
 * for 192.0.2.2. Zero, limited broadcast and multicast are not assignable. */
int net_stack_set_ipv4_address(struct net_stack *stack, uint32_t address);
int net_stack_input(struct net_stack *stack, const struct netif *interface,
                    const uint8_t *frame, uint32_t length);
/* Processes at most frame_budget queued frames; never spins on an empty RX
 * queue. Returns processed frame count, or a negative argument/queue error. */
int net_stack_poll(struct net_stack *stack, struct netif *interface,
                   uint32_t frame_budget);
/* Builds and transmits one unfragmented IPv4/UDP Ethernet frame. Caller
 * supplies the resolved next-hop MAC; ARP, routing and retransmission are
 * outside this bounded primitive. Payload is limited by interface MTU. */
int net_stack_send_udp_ipv4(struct net_stack *stack, struct netif *interface,
                            const uint8_t destination_mac[6],
                            uint32_t destination_address,
                            uint16_t source_port, uint16_t destination_port,
                            const uint8_t *payload, uint16_t payload_length);
#endif
