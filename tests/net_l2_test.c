/* Host test for kernel/net_l2.c: Ethernet framing (including VLAN tags) and
 * ARP packet framing. */
#include <assert.h>
#include <string.h>
#include "../kernel/net_l2.h"

/* ------------------------------------------------------------------ */
/* Ethernet                                                            */
/* ------------------------------------------------------------------ */

/* Builds a frame: [dst(6)][src(6)][tags...][ethertype(2)][payload]. */
static uint32_t build_eth(uint8_t *f, uint16_t type, uint32_t payload) {
    uint32_t at = 0;

    memset(f, 0, 64);
    for (uint32_t i = 0; i < 6; ++i) {
        f[i] = 0x02;                 /* destination */
        f[6 + i] = (uint8_t)(i + 1); /* source */
    }
    at = 12;
    if (type == 0x8100U || type == 0x88a8U) {
        f[at++] = 0x81;
        f[at++] = 0x00;
        f[at++] = 0x00; /* PCP/DEI */
        f[at++] = 0x64; /* VLAN 100 */
        f[at++] = 0x08;
        f[at++] = 0x00; /* inner ethertype: IPv4 */
        return at + payload;
    }
    f[at++] = (uint8_t)(type >> 8);
    f[at++] = (uint8_t)type;
    return at + payload;
}

static void test_ethernet_null_and_bounds(void) {
    uint8_t f[64];
    struct net_eth_view e;

    build_eth(f, 0x0800, 0);
    memset(&e, 0, sizeof(e));

    assert(net_ethernet_parse(0, 64, &e) == -1);
    assert(net_ethernet_parse(f, 64, 0) == -1);
    assert(net_ethernet_parse(f, 13, &e) == -1);
    assert(net_ethernet_parse(f, 0, &e) == -1);
}

static void test_ethernet_untagged(void) {
    uint8_t f[64];
    struct net_eth_view e;
    uint32_t length;

    length = build_eth(f, 0x0800, 20);
    memset(&e, 0, sizeof(e));
    assert(net_ethernet_parse(f, length, &e) == 0);
    assert(e.ethertype == 0x0800);
    assert(e.payload_length == 20);
    assert(e.payload == f + 14);
    assert(e.source[0] == 1 && e.source[5] == 6);
    assert(e.destination[0] == 0x02);

    /* ARP and IPv6 are carried the same way. */
    length = build_eth(f, 0x0806, 28);
    assert(net_ethernet_parse(f, length, &e) == 0);
    assert(e.ethertype == 0x0806);
    assert(e.payload_length == 28);

    length = build_eth(f, 0x86dd, 40);
    assert(net_ethernet_parse(f, length, &e) == 0);
    assert(e.ethertype == 0x86dd);
    assert(e.payload_length == 40);

    /* A bare header with no payload is legal. */
    length = build_eth(f, 0x0800, 0);
    assert(net_ethernet_parse(f, length, &e) == 0);
    assert(e.payload_length == 0);
    assert(e.payload == f + 14);
}

static void test_ethernet_length_field(void) {
    uint8_t f[64];
    struct net_eth_view e;

    /* Ethertype values below 0x0600 are IEEE 802.3 length fields and are
     * rejected: this parser handles Ethernet II framing only. */
    build_eth(f, 0x05ff, 20);
    memset(&e, 0, sizeof(e));
    assert(net_ethernet_parse(f, 34, &e) == -1);

    build_eth(f, 0x0000, 20);
    assert(net_ethernet_parse(f, 34, &e) == -1);

    /* 0x0600 itself is a valid (if unusual) Ethertype. */
    build_eth(f, 0x0600, 20);
    assert(net_ethernet_parse(f, 34, &e) == 0);
    assert(e.ethertype == 0x0600);
}

static void test_ethernet_vlan(void) {
    uint8_t f[64];
    struct net_eth_view e;
    uint32_t length;

    /* Single 802.1Q tag: payload starts after the 4-byte tag. */
    length = build_eth(f, 0x8100, 20);
    memset(&e, 0, sizeof(e));
    assert(net_ethernet_parse(f, length, &e) == 0);
    assert(e.ethertype == 0x0800);
    assert(e.payload == f + 18);
    assert(e.payload_length == 20);

    /* A tagged frame must still carry the inner ethertype: 17 bytes is one
     * byte short and is rejected. */
    assert(net_ethernet_parse(f, 17, &e) == -1);

    /* 802.1ad (S-tag) is recognised the same way. */
    f[12] = 0x88;
    f[13] = 0xa8;
    assert(net_ethernet_parse(f, length, &e) == 0);
    assert(e.ethertype == 0x0800);
    assert(e.payload == f + 18);

    /* Q-in-Q (two stacked tags) is explicitly rejected rather than
     * mis-parsed: this parser supports a single level of tagging. */
    f[12] = 0x81;
    f[13] = 0x00;
    f[16] = 0x81;
    f[17] = 0x00;
    assert(net_ethernet_parse(f, length, &e) == -1);
    f[16] = 0x88;
    f[17] = 0xa8;
    assert(net_ethernet_parse(f, length, &e) == -1);
    f[16] = 0x08;
    f[17] = 0x00;
}

/* ------------------------------------------------------------------ */
/* ARP                                                                 */
/* ------------------------------------------------------------------ */

static uint32_t build_arp(uint8_t *p, uint16_t operation, uint8_t hl,
                          uint8_t pl) {
    memset(p, 0, 64);
    p[0] = 0;
    p[1] = 1; /* hardware type: Ethernet */
    p[2] = 0x08;
    p[3] = 0x00; /* protocol type: IPv4 */
    p[4] = hl;
    p[5] = pl;
    p[6] = (uint8_t)(operation >> 8);
    p[7] = (uint8_t)operation;
    return 8U + 2U * ((uint32_t)hl + pl);
}

static void test_arp_null_and_bounds(void) {
    uint8_t a[64];
    struct net_arp_view v;
    uint32_t need;

    need = build_arp(a, 1, 6, 4);
    assert(need == 28);
    memset(&v, 0, sizeof(v));

    assert(net_arp_parse(0, need, &v) == -1);
    assert(net_arp_parse(a, need, 0) == -1);
    assert(net_arp_parse(a, 7, &v) == -1);
    assert(net_arp_parse(a, 0, &v) == -1);

    /* One byte short of the addresses. */
    assert(net_arp_parse(a, need - 1, &v) == -1);
    /* Exactly long enough. */
    assert(net_arp_parse(a, need, &v) == 0);
    /* Extra trailing bytes are tolerated (Ethernet padding). */
    assert(net_arp_parse(a, need + 18, &v) == 0);
}

static void test_arp_operations(void) {
    uint8_t a[64];
    struct net_arp_view v;
    uint32_t need;

    /* Request and reply are the only legal operations. */
    need = build_arp(a, 1, 6, 4);
    memset(&v, 0, sizeof(v));
    assert(net_arp_parse(a, need, &v) == 0);
    assert(v.operation == 1);
    assert(v.hardware_type == 1);
    assert(v.protocol_type == 0x0800);
    assert(v.hardware_length == 6);
    assert(v.protocol_length == 4);

    build_arp(a, 2, 6, 4);
    assert(net_arp_parse(a, need, &v) == 0);
    assert(v.operation == 2);

    /* 0 and anything above 2 are rejected. */
    build_arp(a, 0, 6, 4);
    assert(net_arp_parse(a, need, &v) == -1);
    build_arp(a, 3, 6, 4);
    assert(net_arp_parse(a, need, &v) == -1);
    build_arp(a, 4, 6, 4);
    assert(net_arp_parse(a, need, &v) == -1);
    build_arp(a, 0xffff, 6, 4);
    assert(net_arp_parse(a, need, &v) == -1);
}

static void test_arp_address_lengths(void) {
    uint8_t a[600];
    struct net_arp_view v;

    /* Zero-length addresses are nonsensical. */
    build_arp(a, 1, 0, 4);
    assert(net_arp_parse(a, 64, &v) == -1);
    build_arp(a, 1, 6, 0);
    assert(net_arp_parse(a, 64, &v) == -1);
    build_arp(a, 1, 0, 0);
    assert(net_arp_parse(a, 64, &v) == -1);

    /* Non-Ethernet hardware sizes parse and the pointers track them. */
    {
        uint32_t need = build_arp(a, 1, 8, 4);
        memset(&v, 0, sizeof(v));
        assert(net_arp_parse(a, need, &v) == 0);
        assert(v.hardware_length == 8);
        assert(v.sender_hardware == a + 8);
        assert(v.sender_protocol == a + 16);
        assert(v.target_hardware == a + 20);
        assert(v.target_protocol == a + 28);
    }

    /* A length pair whose address block would exceed 512 bytes is refused
     * even when the caller claims a huge buffer. */
    build_arp(a, 1, 250, 250);
    assert(net_arp_parse(a, 600, &v) == -1);
    /* ...but a large yet in-bounds pair is accepted. */
    {
        uint32_t need = build_arp(a, 1, 100, 100);
        assert(need == 408);
        memset(&v, 0, sizeof(v));
        assert(net_arp_parse(a, need, &v) == 0);
        assert(v.sender_hardware == a + 8);
        assert(v.sender_protocol == a + 108);
        assert(v.target_hardware == a + 208);
        assert(v.target_protocol == a + 308);
    }
}

int main(void) {
    test_ethernet_null_and_bounds();
    test_ethernet_untagged();
    test_ethernet_length_field();
    test_ethernet_vlan();
    test_arp_null_and_bounds();
    test_arp_operations();
    test_arp_address_lengths();
    return 0;
}
