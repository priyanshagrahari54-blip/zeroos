#ifndef ZEROOS_NET_TRANSPORT_H
#define ZEROOS_NET_TRANSPORT_H
#include "types.h"
#define NET_TCP_MAX_CONNECTIONS 64
enum net_tcp_state { TCP_CLOSED,TCP_LISTEN,TCP_SYN_SENT,TCP_SYN_RECEIVED,TCP_ESTABLISHED,TCP_FIN_WAIT_1,TCP_FIN_WAIT_2,TCP_CLOSE_WAIT,TCP_CLOSING,TCP_LAST_ACK,TCP_TIME_WAIT };
struct net_udp_view { uint16_t source_port,destination_port,length; const uint8_t *payload; uint16_t payload_length; };
struct net_tcp_conn { enum net_tcp_state state; uint32_t snd_una,snd_nxt,rcv_nxt,retries,deadline; };
int net_udp_parse(const uint8_t *segment,uint32_t available,struct net_udp_view *out);
int net_tcp_input(struct net_tcp_conn *c,uint8_t flags,uint32_t seq,uint32_t ack,uint32_t now,uint32_t timeout);
int net_tcp_timeout(struct net_tcp_conn *c,uint32_t now,uint32_t retry_limit,uint32_t next_timeout);
#endif
