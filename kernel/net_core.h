#ifndef ZEROOS_NET_CORE_H
#define ZEROOS_NET_CORE_H
#include "types.h"
#define NET_MAX_FIREWALL_RULES 32
#define NET_ACTION_DENY 0
#define NET_ACTION_ALLOW 1
#define NET_PROTO_ANY 0
#define NET_PROTO_ICMP 1
#define NET_PROTO_TCP 6
#define NET_PROTO_UDP 17
struct net_ipv4_view { uint32_t source, destination; uint16_t total_length; uint8_t protocol, header_length; };
struct net_firewall_rule { uint32_t source, source_mask, destination, destination_mask; uint8_t protocol, action; };
struct net_firewall { struct net_firewall_rule rules[NET_MAX_FIREWALL_RULES]; uint32_t count, accepted, denied, malformed; };
/* Strict parser: packet includes the IPv4 header; fragments are identified but not reassembled. */
int net_ipv4_parse(const uint8_t *packet, uint32_t length, struct net_ipv4_view *out);
void net_firewall_init(struct net_firewall *fw);
int net_firewall_add(struct net_firewall *fw, const struct net_firewall_rule *rule);
int net_firewall_remove(struct net_firewall *fw, uint32_t index);
int net_firewall_check(struct net_firewall *fw, const struct net_ipv4_view *packet);
#endif
