#include "net_ipv6.h"

#define IPV6_HEADER_LENGTH 40U
#define IPV6_NH_HOP_BY_HOP 0U
#define IPV6_NH_ROUTING 43U
#define IPV6_NH_FRAGMENT 44U
#define IPV6_NH_ESP 50U
#define IPV6_NH_AH 51U
#define IPV6_NH_DESTINATION 60U

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int is_extension(uint8_t next_header) {
    return next_header == IPV6_NH_HOP_BY_HOP ||
           next_header == IPV6_NH_ROUTING ||
           next_header == IPV6_NH_FRAGMENT ||
           next_header == IPV6_NH_AH ||
           next_header == IPV6_NH_DESTINATION;
}

int net_ipv6_parse(const uint8_t *packet, uint32_t length,
                   struct net_ipv6_view *out) {
    if (!packet || !out || length < IPV6_HEADER_LENGTH ||
        (packet[0] >> 4) != 6U)
        return -1;

    uint32_t payload_length = read_be16(packet + 4);
    if (payload_length > length - IPV6_HEADER_LENGTH)
        return -1;

    out->traffic_class = (uint8_t)(((packet[0] & 15U) << 4) |
                                   (packet[1] >> 4));
    out->flow_label = ((uint32_t)(packet[1] & 15U) << 16) |
                      ((uint32_t)packet[2] << 8) | packet[3];
    out->payload_length = payload_length;
    out->upper_layer_length = payload_length;
    out->next_header = packet[6];
    out->hop_limit = packet[7];
    for (uint32_t i = 0; i < 16U; ++i) {
        out->source[i] = packet[8U + i];
        out->destination[i] = packet[24U + i];
    }
    out->extension_length = 0;
    out->fragment_offset = 0;
    out->fragment_id = 0;
    out->is_fragment = 0;
    out->more_fragments = 0;
    out->payload = packet + IPV6_HEADER_LENGTH;

    uint32_t offset = IPV6_HEADER_LENGTH;
    uint32_t remaining = payload_length;
    uint32_t ext_bytes = 0;
    uint32_t ext_count = 0;
    int saw_fragment = 0;
    uint8_t next = out->next_header;

    while (is_extension(next)) {
        if (++ext_count > NET_IPV6_MAX_EXT_HEADERS ||
            offset > length || remaining == 0)
            return -1;

        uint32_t ext_length;
        uint8_t following = packet[offset];
        if (next == IPV6_NH_FRAGMENT) {
            if (saw_fragment || remaining < 8U)
                return -1;
            saw_fragment = 1;
            ext_length = 8U;
            uint16_t fragment = read_be16(packet + offset + 2U);
            /* The low bit is the reserved flag and must be zero. */
            if (fragment & 0x0006U)
                return -1;
            out->fragment_offset = (uint16_t)((fragment >> 3) & 0x1fffU);
            out->more_fragments = (uint8_t)(fragment & 1U);
            out->fragment_id = read_be32(packet + offset + 4U);
            out->is_fragment = (uint8_t)(out->fragment_offset != 0 ||
                                         out->more_fragments != 0);
        } else if (next == IPV6_NH_AH) {
            if (remaining < 2U)
                return -1;
            ext_length = ((uint32_t)packet[offset + 1U] + 2U) * 4U;
            if (ext_length < 12U)
                return -1;
        } else {
            if (remaining < 2U)
                return -1;
            ext_length = ((uint32_t)packet[offset + 1U] + 1U) * 8U;
            /* Hop-by-hop options are only valid immediately after the base
             * header; reject rather than ambiguously accepting a later one. */
            if (next == IPV6_NH_HOP_BY_HOP && offset != IPV6_HEADER_LENGTH)
                return -1;
        }

        if (ext_length > remaining ||
            ext_length > NET_IPV6_MAX_EXT_BYTES - ext_bytes ||
            offset > length - ext_length)
            return -1;
        offset += ext_length;
        remaining -= ext_length;
        ext_bytes += ext_length;
        next = following;
    }

    /* Encrypted payload framing is not parsed here. */
    if (next == IPV6_NH_ESP)
        return -1;

    out->next_header = next;
    out->extension_length = (uint16_t)ext_bytes;
    out->upper_layer_length = remaining;
    out->payload = packet + offset;
    return 0;
}
