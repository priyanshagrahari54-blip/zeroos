#ifndef ZEROOS_NET_H
#define ZEROOS_NET_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_NET_MAX_INTERFACES 8U
#define ZEROOS_NET_MAX_SOCKETS 64U
#define ZEROOS_NET_MAX_PACKET_SIZE 1500U
#define ZEROOS_NET_MAX_NAME 16U

enum zeroos_net_interface_state {
    ZEROOS_NET_IF_STOPPED = 0,
    ZEROOS_NET_IF_DORMANT,
    ZEROOS_NET_IF_WARM,
    ZEROOS_NET_IF_ACTIVE,
    ZEROOS_NET_IF_THROTTLED,
    ZEROOS_NET_IF_SUSPENDED
};

enum zeroos_net_socket_type {
    ZEROOS_NET_SOCK_UNKNOWN = 0,
    ZEROOS_NET_SOCK_STREAM, /* TCP */
    ZEROOS_NET_SOCK_DGRAM,  /* UDP */
    ZEROOS_NET_SOCK_RAW
};

enum zeroos_net_socket_state {
    ZEROOS_NET_SOCK_CLOSED = 0,
    ZEROOS_NET_SOCK_LISTEN,
    ZEROOS_NET_SOCK_SYN_SENT,
    ZEROOS_NET_SOCK_SYN_RECEIVED,
    ZEROOS_NET_SOCK_ESTABLISHED,
    ZEROOS_NET_SOCK_FIN_WAIT,
    ZEROOS_NET_SOCK_CLOSE_WAIT,
    ZEROOS_NET_SOCK_CLOSING,
    ZEROOS_NET_SOCK_LAST_ACK,
    ZEROOS_NET_SOCK_TIME_WAIT
};

struct zeroos_net_interface {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_net_interface_state state;
    char name[ZEROOS_NET_MAX_NAME];
    uint8_t mac[6];
    uint32_t ipv4_addr; /* network order */
    uint32_t ipv4_netmask;
    uint32_t ipv4_gateway;
    uint8_t ipv6_addr[16];
    uint8_t ipv6_prefix_len;
    uint64_t mtu;
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_errors;
    uint64_t rx_errors;
    uint64_t tx_dropped;
    uint64_t rx_dropped;
    struct spinlock lock;
    struct wait_queue packet_waiters;
};

struct zeroos_net_socket {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_net_socket_type type;
    enum zeroos_net_socket_state state;
    uint64_t interface_id;
    uint32_t local_ipv4;
    uint16_t local_port;
    uint32_t remote_ipv4;
    uint16_t remote_port;
    uint64_t timeout_ticks;
    uint64_t error;
    struct spinlock lock;
    struct wait_queue recv_waiters;
    struct wait_queue send_waiters;
    uint8_t recv_buffer[ZEROOS_NET_MAX_PACKET_SIZE*4];
    uint16_t recv_head;
    uint16_t recv_tail;
    uint16_t recv_count;
};

int net_system_init(void);
int net_interface_register(const char *name, const uint8_t mac[6], uint64_t mtu, uint64_t *if_id_out);
int net_interface_set_ipv4(uint64_t if_id, uint32_t addr, uint32_t netmask, uint32_t gateway);
int net_interface_set_state(uint64_t if_id, enum zeroos_net_interface_state state);
struct zeroos_net_interface *net_interface_lookup(uint64_t if_id);
int net_socket_create(enum zeroos_net_socket_type type, uint64_t *sock_id_out);
int net_socket_bind(uint64_t sock_id, uint32_t addr, uint16_t port);
int net_socket_connect(uint64_t sock_id, uint32_t addr, uint16_t port, uint64_t timeout_ticks);
int net_socket_listen(uint64_t sock_id, uint32_t backlog);
int net_socket_accept(uint64_t sock_id, uint64_t *new_sock_id_out, uint64_t timeout_ticks);
int net_socket_send(uint64_t sock_id, const void *data, uint64_t len, uint64_t flags, uint64_t timeout_ticks);
int net_socket_recv(uint64_t sock_id, void *data, uint64_t cap, uint64_t flags, uint64_t *len_out, uint64_t timeout_ticks);
int net_socket_close(uint64_t sock_id);
int net_debug_validate(void);

#endif
