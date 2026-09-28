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

static void test_null_and_bounds(void) {
    uint8_t packet[64];
    struct net_ipv6_view view;

    base_header(packet, 0, 17);
    assert(net_ipv6_parse(0, 40, &view) == -1);
    assert(net_ipv6_parse(packet, 40, 0) == -1);
    assert(net_ipv6_parse(packet, 39, &view) == -1);
    assert(net_ipv6_parse(packet, 0, &view) == -1);

    /* Versions other than 6 are refused even at full header length. */
    for (uint8_t v = 0; v < 16; ++v) {
        packet[0] = (uint8_t)((v << 4) | ((packet[0] & 15U)));
        if (v == 6)
            assert(net_ipv6_parse(packet, 40, &view) == 0);
        else
            assert(net_ipv6_parse(packet, 40, &view) == -1);
    }
    packet[0] = 0x60;
}

static void test_base_header_fields(void) {
    uint8_t packet[64];
    struct net_ipv6_view view;

    memset(packet, 0, sizeof(packet));
    packet[0] = 0x6c;  /* version 6, traffic class high nibble 0xc */
    packet[1] = 0x5a;  /* traffic class low nibble 0x5, flow label 0xa.... */
    packet[2] = 0xbc;
    packet[3] = 0xde;
    packet[4] = 0;
    packet[5] = 8;
    packet[6] = 6;
    packet[7] = 1;
    for (uint32_t i = 0; i < 16; ++i) {
        packet[8 + i] = (uint8_t)(0x20 + i);
        packet[24 + i] = (uint8_t)(0x40 + i);
    }

    memset(&view, 0, sizeof(view));
    assert(net_ipv6_parse(packet, 48, &view) == 0);
    assert(view.traffic_class == 0xc5);
    assert(view.flow_label == 0xabcdeU);
    assert(view.next_header == 6);
    assert(view.hop_limit == 1);
    assert(view.payload_length == 8);
    assert(view.upper_layer_length == 8);
    assert(view.extension_length == 0);
    assert(view.is_fragment == 0);
    for (uint32_t i = 0; i < 16; ++i) {
        assert(view.source[i] == (uint8_t)(0x20 + i));
        assert(view.destination[i] == (uint8_t)(0x40 + i));
    }
    assert(view.payload == packet + 40);

    /* A capture longer than the declared payload is tolerated; the parser
     * never reads past payload_length. */
    packet[5] = 4;
    assert(net_ipv6_parse(packet, 64, &view) == 0);
    assert(view.payload_length == 4);
    assert(view.upper_layer_length == 4);
}

static void test_extension_chain(void) {
    uint8_t packet[320];
    struct net_ipv6_view view;

    /* Routing header (43) uses the same 8-byte-multiple framing. */
    base_header(packet, 24, 43);
    packet[40] = 17;
    packet[41] = 1; /* 16-byte routing header */
    packet[56] = 0xaa;
    memset(&view, 0, sizeof(view));
    assert(net_ipv6_parse(packet, 64, &view) == 0);
    assert(view.next_header == 17);
    assert(view.extension_length == 16);
    assert(view.upper_layer_length == 8);
    assert(view.payload == packet + 56);

    /* An AH header is measured in 4-byte units minus two words. */
    base_header(packet, 32, 51);
    packet[40] = 17;
    packet[41] = 2; /* (2 + 2) * 4 = 16 bytes */
    memset(&view, 0, sizeof(view));
    assert(net_ipv6_parse(packet, 72, &view) == 0);
    assert(view.extension_length == 16);
    assert(view.payload == packet + 56);

    /* An AH shorter than the 12-byte minimum is rejected. */
    base_header(packet, 32, 51);
    packet[40] = 17;
    packet[41] = 0; /* (0 + 2) * 4 = 8 bytes, below the floor */
    assert(net_ipv6_parse(packet, 72, &view) == -1);

    /* A chain that ends in ESP is refused rather than half-parsed. */
    base_header(packet, 16, 60);
    packet[40] = 50;
    packet[41] = 0;
    assert(net_ipv6_parse(packet, 56, &view) == -1);

    /* An extension claiming more bytes than the payload holds is rejected. */
    base_header(packet, 8, 60);
    packet[40] = 17;
    packet[41] = 1; /* 16 bytes > 8-byte payload */
    assert(net_ipv6_parse(packet, 48, &view) == -1);

    /* An extension that would run past the captured buffer is rejected. */
    base_header(packet, 64, 60);
    packet[40] = 17;
    packet[41] = 7; /* 64 bytes, but only 56 captured */
    assert(net_ipv6_parse(packet, 56, &view) == -1);
}

static void test_extension_bounds(void) {
    uint8_t packet[512];
    struct net_ipv6_view view;

    /* The cumulative extension budget is NET_IPV6_MAX_EXT_BYTES (256).
     * Thirty-two 8-byte extension headers fit exactly in that budget but
     * exceed the header-count bound, so the count fires first. */
    base_header(packet, 260, 60);
    for (uint32_t i = 0; i < 33U; ++i) {
        packet[40U + i * 8U] = 60;
        packet[41U + i * 8U] = 0;
    }
    packet[40U + 33U * 8U] = 17;
    assert(net_ipv6_parse(packet, 320, &view) == -1);

    /* Eight extension headers is the maximum; the ninth is refused. The last
     * header in the chain terminates it with an upper-layer protocol. */
    base_header(packet, 72, 60);
    for (uint32_t i = 0; i < 8U; ++i) {
        packet[40U + i * 8U] = (i == 7U) ? 17U : 60U;
        packet[41U + i * 8U] = 0;
    }
    assert(net_ipv6_parse(packet, 128, &view) == 0);
    assert(view.next_header == 17);
    assert(view.extension_length == 64);
    assert(view.upper_layer_length == 8);

    /* Chaining a ninth is refused by the header-count bound. */
    packet[40U + 7U * 8U] = 60;
    packet[40U + 8U * 8U] = 17;
    assert(net_ipv6_parse(packet, 128, &view) == -1);
}

static void test_fragment_edge_cases(void) {
    uint8_t packet[128];
    struct net_ipv6_view view;

    /* A fragment header with offset 0 and MF clear is not a fragment. */
    base_header(packet, 12, 44);
    packet[40] = 17;
    packet[42] = 0;
    packet[43] = 0;
    memset(&view, 0, sizeof(view));
    assert(net_ipv6_parse(packet, 52, &view) == 0);
    assert(view.is_fragment == 0);
    assert(view.fragment_offset == 0);
    assert(view.more_fragments == 0);

    /* A non-zero offset with MF clear still counts as a fragment. */
    packet[42] = (uint8_t)((0x1000U >> 8) & 0xffU);
    packet[43] = (uint8_t)(0x1000U & 0xffU); /* offset = 0x1000 >> 3 = 512 */
    assert(net_ipv6_parse(packet, 52, &view) == 0);
    assert(view.is_fragment == 1);
    assert(view.fragment_offset == 512);
    assert(view.more_fragments == 0);

    /* Reserved bits (0x0006) must be zero. */
    packet[42] = 0;
    packet[43] = 0x02;
    assert(net_ipv6_parse(packet, 52, &view) == -1);
    packet[43] = 0x04;
    assert(net_ipv6_parse(packet, 52, &view) == -1);
    packet[43] = 0x08; /* part of the offset field: legal */
    assert(net_ipv6_parse(packet, 52, &view) == 0);

    /* A fragment header shorter than its fixed 8 bytes is rejected. */
    base_header(packet, 7, 44);
    packet[40] = 17;
    assert(net_ipv6_parse(packet, 47, &view) == -1);

    /* A second fragment header in the same chain is rejected. */
    base_header(packet, 16, 44);
    packet[40] = 44;
    packet[48] = 17;
    assert(net_ipv6_parse(packet, 56, &view) == -1);
}

int main(void) {
    uint8_t packet[320];
    struct net_ipv6_view view;

    test_null_and_bounds();
    test_base_header_fields();
    test_extension_chain();
    test_extension_bounds();
    test_fragment_edge_cases();

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
