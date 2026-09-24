#ifndef ZEROOS_NET_IPV6_H
#define ZEROOS_NET_IPV6_H
#include "types.h"
struct net_ipv6_view { uint8_t source[16],destination[16],traffic_class,hop_limit,next_header; uint32_t flow_label,payload_length; const uint8_t *payload; };
int net_ipv6_parse(const uint8_t *packet,uint32_t length,struct net_ipv6_view *out);
#endif
