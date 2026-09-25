#ifndef ZEROOS_NET_IPV6_H
#define ZEROOS_NET_IPV6_H
#include "types.h"

#define NET_IPV6_MAX_EXT_HEADERS 8U
#define NET_IPV6_MAX_EXT_BYTES 256U

struct net_ipv6_view {
    uint8_t source[16], destination[16], traffic_class, hop_limit;
    uint8_t next_header;             /* Upper-layer protocol after extensions. */
    uint32_t flow_label, payload_length, upper_layer_length;
    uint16_t extension_length, fragment_offset;
    uint32_t fragment_id;
    uint8_t is_fragment, more_fragments;
    const uint8_t *payload;           /* First byte after parsed extensions. */
};

/* Validates the fixed header and a bounded chain of supported extension
 * headers. ESP and jumbograms are deliberately not interpreted. */
int net_ipv6_parse(const uint8_t *packet, uint32_t length,
                   struct net_ipv6_view *out);
#endif
