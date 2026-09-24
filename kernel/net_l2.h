#ifndef ZEROOS_NET_L2_H
#define ZEROOS_NET_L2_H
#include "types.h"
struct net_eth_view { uint8_t destination[6],source[6]; uint16_t ethertype; const uint8_t *payload; uint32_t payload_length; };
struct net_arp_view { uint16_t hardware_type,protocol_type; uint8_t hardware_length,protocol_length,operation; const uint8_t *sender_hardware,*sender_protocol,*target_hardware,*target_protocol; };
int net_ethernet_parse(const uint8_t *frame,uint32_t length,struct net_eth_view *out);
int net_arp_parse(const uint8_t *packet,uint32_t length,struct net_arp_view *out);
#endif
