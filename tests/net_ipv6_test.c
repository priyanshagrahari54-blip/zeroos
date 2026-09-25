#include <assert.h>
#include <string.h>
#include "../kernel/net_ipv6.h"

static void base_header(uint8_t *p, uint16_t payload_length, uint8_t next) {
    memset(p, 0, 40U + payload_length);
    p[0] = 0x60;
    p[4] = (uint8_t)(payload_length >> 8);
    p[5] = (uint8_t)payload_length;
    p[6] = next;
    p[7] = 64;
}

int main(void) {
    uint8_t packet[320];
    struct net_ipv6_view view;

    base_header(packet, 0, 17);
    assert(net_ipv6_parse(packet, 40, &view) == 0);
    assert(view.next_header == 17 && view.hop_limit == 64);
    assert(view.upper_layer_length == 0 && view.payload == packet + 40);
    packet[0] = 0x40;
    assert(net_ipv6_parse(packet, 40, &view) == -1);
    assert(net_ipv6_parse(packet, 39, &view) == -1);
    packet[0] = 0x60;
    packet[5] = 1;
    assert(net_ipv6_parse(packet, 40, &view) == -1);

    /* Hop-by-hop then destination options, followed by UDP payload. */
    base_header(packet, 20, 0);
    packet[40] = 60;
    packet[41] = 0;       /* 8-byte extension */
    packet[48] = 17;
    packet[49] = 0;
    packet[56] = 0x12;
    packet[57] = 0x34;
    assert(net_ipv6_parse(packet, 60, &view) == 0);
    assert(view.next_header == 17 && view.extension_length == 16);
    assert(view.payload_length == 20 && view.upper_layer_length == 4);
    assert(view.payload == packet + 56);
    assert(view.payload[0] == 0x12);

    /* Fragment header fields are reported for upper layers to reassemble or
     * reject; this parser never pretends a fragment is a complete datagram. */
    base_header(packet, 12, 44);
    packet[40] = 17;
    packet[42] = 0;
    packet[43] = 9;       /* offset=1, more=1 */
    packet[47] = 0x2a;
    assert(net_ipv6_parse(packet, 52, &view) == 0);
    assert(view.is_fragment && view.fragment_offset == 1 &&
           view.more_fragments && view.fragment_id == 42);
    assert(view.next_header == 17 && view.upper_layer_length == 4);

    /* Reject truncated/oversized chains, duplicate fragments, illegal
     * reserved fragment flags, misplaced hop-by-hop and encrypted ESP. */
    base_header(packet, 7, 44);
    assert(net_ipv6_parse(packet, 47, &view) == -1);
    base_header(packet, 16, 44);
    packet[40] = 44;
    packet[48] = 17;
    assert(net_ipv6_parse(packet, 56, &view) == -1);
    base_header(packet, 8, 44);
    packet[42] = 0;
    packet[43] = 2;
    assert(net_ipv6_parse(packet, 48, &view) == -1);
    base_header(packet, 16, 60);
    packet[40] = 0;
    packet[41] = 0;
    packet[48] = 17;
    packet[49] = 0;
    assert(net_ipv6_parse(packet, 56, &view) == -1);
    base_header(packet, 0, 50);
    assert(net_ipv6_parse(packet, 40, &view) == -1);

    /* Enforce the parser work bound even with a valid chain. */
    base_header(packet, 80, 60);
    for (uint32_t i = 0; i < 10; ++i) {
        packet[40U + i * 8U] = 60;
        packet[41U + i * 8U] = 0;
    }
    packet[40U + 10U * 8U] = 17;
    assert(net_ipv6_parse(packet, 120, &view) == -1);

    /* Deterministic malformed-input sweep; run under ASan/UBSan as well as
     * the normal host test to guard all length/offset arithmetic. */
    uint32_t random = 0x6d2b79f5U;
    for (uint32_t iteration = 0; iteration < 10000U; ++iteration) {
        random = random * 1664525U + 1013904223U;
        uint32_t packet_length = random % sizeof(packet);
        for (uint32_t i = 0; i < sizeof(packet); ++i) {
            random = random * 1664525U + 1013904223U;
            packet[i] = (uint8_t)(random >> 24);
        }
        (void)net_ipv6_parse(packet, packet_length, &view);
    }
    return 0;
}
