#ifndef ZEROOS_NET_SOCKET_H
#define ZEROOS_NET_SOCKET_H
#include "net_stack.h"

#define NET_UDP_SOCKET_CAPACITY 16U
#define NET_UDP_SOCKET_QUEUE_DEPTH 4U
#define NET_UDP_SOCKET_PAYLOAD_MAX 1472U
#define NET_UDP_SOCKET_FAMILY_IPV4 4U

struct net_udp_datagram {
    uint32_t source_address;
    uint16_t source_port, length;
    uint8_t payload[NET_UDP_SOCKET_PAYLOAD_MAX];
};
struct net_udp_socket_slot {
    uint32_t owner, generation, local_address;
    uint16_t local_port;
    uint8_t active, head, tail, count;
    uint64_t dropped;
    struct net_udp_datagram queue[NET_UDP_SOCKET_QUEUE_DEPTH];
};
struct net_udp_socket_table {
    struct net_udp_socket_slot slots[NET_UDP_SOCKET_CAPACITY];
    uint32_t next_generation;
    uint64_t delivered, dropped, unmatched, invalid;
};
struct net_udp_receive_info {
    uint32_t source_address;
    uint16_t source_port, length;
};

/* Caller serializes the table. Handles are generation-tagged and owner-bound.
 * IPv4 address fields use network-byte-order host integers (0 means wildcard). */
void net_udp_socket_table_init(struct net_udp_socket_table *table);
int net_udp_socket_bind(struct net_udp_socket_table *table, uint32_t owner,
                       uint32_t local_address, uint16_t local_port,
                       uint64_t *handle_out);
int net_udp_socket_close(struct net_udp_socket_table *table, uint32_t owner,
                        uint64_t handle);
/* Returns 0 with a datagram, 1 if empty, -1 for stale/foreign handles,
 * and -2 when the output buffer is too small (datagram remains queued). */
int net_udp_socket_receive(struct net_udp_socket_table *table, uint32_t owner,
                           uint64_t handle, uint8_t *payload,
                           uint16_t capacity, struct net_udp_receive_info *info);
/* Can be installed directly as net_stack's UDP callback. Unknown ports are
 * counted and dropped; a full per-socket queue drops newest without blocking. */
int net_udp_socket_dispatch(void *context, uint8_t family,
                            uint32_t interface_index,
                            const uint8_t source[16],
                            const uint8_t destination[16],
                            uint16_t source_port, uint16_t destination_port,
                            const uint8_t *payload, uint16_t length);
#endif
