/* Host test for kernel/net_core.c: the strict IPv4 header parser and the
 * first-match-wins firewall table. */
#include <assert.h>
#include <string.h>
#include "../kernel/net_core.h"

/* ------------------------------------------------------------------ */
/* IPv4 header helpers                                                 */
/* ------------------------------------------------------------------ */

/* Writes a 20-byte (or longer, with options) IPv4 header. ihl is in words. */
static uint32_t build_ipv4(uint8_t *p, uint8_t ihl, uint16_t total_length,
                           uint16_t fragment, uint8_t protocol) {
    uint32_t h = (uint32_t)ihl * 4U;
    uint32_t sum = 0;
    uint16_t checksum;

    memset(p, 0, h);
    p[0] = (uint8_t)((4U << 4) | ihl);
    p[2] = (uint8_t)(total_length >> 8);
    p[3] = (uint8_t)total_length;
    p[6] = (uint8_t)(fragment >> 8);
    p[7] = (uint8_t)fragment;
    p[8] = 64; /* TTL */
    p[9] = protocol;
    p[12] = 10; p[13] = 0; p[14] = 0; p[15] = 1;   /* 10.0.0.1 */
    p[16] = 8;  p[17] = 8; p[18] = 8; p[19] = 8;   /* 8.8.8.8 */

    for (uint32_t i = 0; i < h; i += 2)
        sum += (uint32_t)(((uint16_t)p[i] << 8) | p[i + 1]);
    while (sum >> 16)
        sum = (sum & 0xffffU) + (sum >> 16);
    checksum = (uint16_t)(~sum & 0xffffU);
    p[10] = (uint8_t)(checksum >> 8);
    p[11] = (uint8_t)checksum;
    return h;
}

static void test_ipv4_parse_null_and_bounds(void) {
    uint8_t p[64];
    struct net_ipv4_view v;

    build_ipv4(p, 5, 20, 0, NET_PROTO_UDP);

    assert(net_ipv4_parse(0, 20, &v) == -1);
    assert(net_ipv4_parse(p, 20, 0) == -1);
    /* A truncated packet cannot even hold the fixed header. */
    assert(net_ipv4_parse(p, 19, &v) == -1);
    assert(net_ipv4_parse(p, 0, &v) == -1);
}

static void test_ipv4_parse_header_fields(void) {
    uint8_t p[64];
    struct net_ipv4_view v;

    build_ipv4(p, 5, 20, 0, NET_PROTO_UDP);
    memset(&v, 0, sizeof(v));
    assert(net_ipv4_parse(p, 20, &v) == 0);
    assert(v.header_length == 20);
    assert(v.total_length == 20);
    assert(v.protocol == NET_PROTO_UDP);
    assert(v.source == 0x0a000001U);
    assert(v.destination == 0x08080808U);
    assert(v.fragment_offset == 0);
    assert(v.more_fragments == 0);
    assert(v.is_fragment == 0);

    /* Version must be 4. */
    p[0] = (uint8_t)((5U << 4) | 5U);
    assert(net_ipv4_parse(p, 20, &v) == -1);
    p[0] = (uint8_t)((4U << 4) | 5U);
    assert(net_ipv4_parse(p, 20, &v) == 0);

    /* IHL below 5 or above 15 is rejected. */
    p[0] = (uint8_t)((4U << 4) | 4U);
    assert(net_ipv4_parse(p, 20, &v) == -1);
    p[0] = (uint8_t)((4U << 4) | 0U);
    assert(net_ipv4_parse(p, 20, &v) == -1);
    p[0] = (uint8_t)((4U << 4) | 15U);
    assert(net_ipv4_parse(p, 60, &v) == -1); /* 60-byte header, bad checksum */

    /* Options (ihl 6) parse and report the real header length. */
    build_ipv4(p, 6, 24, 0, NET_PROTO_TCP);
    memset(&v, 0, sizeof(v));
    assert(net_ipv4_parse(p, 24, &v) == 0);
    assert(v.header_length == 24);
    assert(v.protocol == NET_PROTO_TCP);

    /* A header longer than the captured bytes is rejected. */
    build_ipv4(p, 6, 24, 0, NET_PROTO_TCP);
    assert(net_ipv4_parse(p, 23, &v) == -1);
}

static void test_ipv4_parse_length_relations(void) {
    uint8_t p[64];
    struct net_ipv4_view v;

    /* total_length below the header length is contradictory. */
    build_ipv4(p, 5, 19, 0, NET_PROTO_UDP);
    assert(net_ipv4_parse(p, 20, &v) == -1);

    /* total_length above the captured length over-reads the buffer. */
    build_ipv4(p, 5, 21, 0, NET_PROTO_UDP);
    assert(net_ipv4_parse(p, 20, &v) == -1);

    /* Exactly the header with no payload is fine. */
    build_ipv4(p, 5, 20, 0, NET_PROTO_UDP);
    assert(net_ipv4_parse(p, 20, &v) == 0);

    /* Payload present: total_length may be smaller than the capture. */
    build_ipv4(p, 5, 40, 0, NET_PROTO_UDP);
    assert(net_ipv4_parse(p, 64, &v) == 0);
    assert(v.total_length == 40);
}

static void test_ipv4_parse_checksum(void) {
    uint8_t p[64];
    struct net_ipv4_view v;

    build_ipv4(p, 5, 20, 0, NET_PROTO_UDP);
    assert(net_ipv4_parse(p, 20, &v) == 0);

    /* Any single-bit corruption of the header must be caught. */
    p[8] ^= 0x01; /* TTL */
    assert(net_ipv4_parse(p, 20, &v) == -1);
    p[8] ^= 0x01;
    assert(net_ipv4_parse(p, 20, &v) == 0);

    /* Corrupting the checksum field itself is caught. */
    p[10] ^= 0x80;
    assert(net_ipv4_parse(p, 20, &v) == -1);
    p[10] ^= 0x80;
    assert(net_ipv4_parse(p, 20, &v) == 0);

    /* A zero checksum (never valid for a correct header) is rejected. */
    p[10] = 0;
    p[11] = 0;
    assert(net_ipv4_parse(p, 20, &v) == -1);
}

static void test_ipv4_parse_fragments(void) {
    uint8_t p[64];
    struct net_ipv4_view v;

    /* The reserved (evil) bit must be rejected outright. */
    build_ipv4(p, 5, 20, 0x8000U, NET_PROTO_UDP);
    assert(net_ipv4_parse(p, 20, &v) == -1);

    /* More-fragments set: recorded, not reassembled. */
    build_ipv4(p, 5, 20, 0x2000U, NET_PROTO_UDP);
    memset(&v, 0, sizeof(v));
    assert(net_ipv4_parse(p, 20, &v) == 0);
    assert(v.more_fragments == 1);
    assert(v.fragment_offset == 0);
    assert(v.is_fragment == 1);

    /* Non-zero offset with MF clear: still a fragment. */
    build_ipv4(p, 5, 20, 185U, NET_PROTO_UDP); /* 185 * 8 = 1480 bytes */
    memset(&v, 0, sizeof(v));
    assert(net_ipv4_parse(p, 20, &v) == 0);
    assert(v.fragment_offset == 185);
    assert(v.more_fragments == 0);
    assert(v.is_fragment == 1);

    /* Maximum offset with both flags clear except MF. */
    build_ipv4(p, 5, 20, 0x3fffU, NET_PROTO_UDP);
    memset(&v, 0, sizeof(v));
    assert(net_ipv4_parse(p, 20, &v) == 0);
    assert(v.fragment_offset == 0x1fffU);
    assert(v.more_fragments == 1);
}

/* ------------------------------------------------------------------ */
/* Firewall                                                            */
/* ------------------------------------------------------------------ */

static void test_firewall_init_and_add(void) {
    struct net_firewall fw;
    struct net_firewall_rule r;

    net_firewall_init(0); /* must tolerate NULL */

    net_firewall_init(&fw);
    assert(fw.count == 0);
    assert(fw.accepted == 0);
    assert(fw.denied == 0);
    assert(fw.malformed == 0);

    memset(&r, 0, sizeof(r));
    r.action = NET_ACTION_ALLOW;

    assert(net_firewall_add(0, &r) == -1);
    assert(net_firewall_add(&fw, 0) == -1);

    /* An action outside the known set is rejected. */
    r.action = 2;
    assert(net_firewall_add(&fw, &r) == -1);
    r.action = 0xFF;
    assert(net_firewall_add(&fw, &r) == -1);

    /* DENY (0) and ALLOW (1) are both accepted. */
    r.action = NET_ACTION_DENY;
    assert(net_firewall_add(&fw, &r) == 0);
    r.action = NET_ACTION_ALLOW;
    assert(net_firewall_add(&fw, &r) == 0);
    assert(fw.count == 2);
}

static void test_firewall_capacity(void) {
    struct net_firewall fw;
    struct net_firewall_rule r;

    net_firewall_init(&fw);
    memset(&r, 0, sizeof(r));
    r.action = NET_ACTION_ALLOW;

    for (uint32_t i = 0; i < NET_MAX_FIREWALL_RULES; ++i) {
        r.protocol = (uint8_t)(i + 1);
        assert(net_firewall_add(&fw, &r) == 0);
    }
    assert(fw.count == NET_MAX_FIREWALL_RULES);

    /* One rule too many. */
    r.protocol = 200;
    assert(net_firewall_add(&fw, &r) == -1);
    assert(fw.count == NET_MAX_FIREWALL_RULES);
}

static void test_firewall_remove(void) {
    struct net_firewall fw;
    struct net_firewall_rule r;

    net_firewall_init(&fw);
    memset(&r, 0, sizeof(r));
    r.action = NET_ACTION_ALLOW;

    assert(net_firewall_remove(&fw, 0) == -1); /* empty table */
    assert(net_firewall_remove(0, 0) == -1);

    r.protocol = NET_PROTO_TCP;
    assert(net_firewall_add(&fw, &r) == 0);
    r.protocol = NET_PROTO_UDP;
    assert(net_firewall_add(&fw, &r) == 0);
    r.protocol = NET_PROTO_ICMP;
    assert(net_firewall_add(&fw, &r) == 0);
    assert(fw.count == 3);

    /* Out-of-range index. */
    assert(net_firewall_remove(&fw, 3) == -1);
    assert(fw.count == 3);

    /* Removing the head shifts the tail down. */
    assert(net_firewall_remove(&fw, 0) == 0);
    assert(fw.count == 2);
    assert(fw.rules[0].protocol == NET_PROTO_UDP);
    assert(fw.rules[1].protocol == NET_PROTO_ICMP);

    /* Removing the tail. */
    assert(net_firewall_remove(&fw, 1) == 0);
    assert(fw.count == 1);
    assert(fw.rules[0].protocol == NET_PROTO_UDP);

    assert(net_firewall_remove(&fw, 0) == 0);
    assert(fw.count == 0);
}

static void test_firewall_matching(void) {
    struct net_firewall fw;
    struct net_firewall_rule r;
    struct net_ipv4_view p;

    net_firewall_init(&fw);
    memset(&p, 0, sizeof(p));
    p.source = 0x0a000001U;
    p.destination = 0x08080808U;
    p.protocol = NET_PROTO_UDP;

    /* NULL arguments deny rather than fault. */
    assert(net_firewall_check(0, &p) == NET_ACTION_DENY);
    assert(net_firewall_check(&fw, 0) == NET_ACTION_DENY);

    /* An empty table is default-deny and counts the denial. */
    assert(net_firewall_check(&fw, &p) == NET_ACTION_DENY);
    assert(fw.denied == 1);
    assert(fw.accepted == 0);

    /* NET_PROTO_ANY matches every protocol. */
    memset(&r, 0, sizeof(r));
    r.protocol = NET_PROTO_ANY;
    r.action = NET_ACTION_ALLOW;
    assert(net_firewall_add(&fw, &r) == 0);
    p.protocol = NET_PROTO_TCP;
    assert(net_firewall_check(&fw, &p) == NET_ACTION_ALLOW);
    assert(fw.accepted == 1);
    p.protocol = NET_PROTO_UDP;

    /* Protocol must match when it is not ANY. */
    net_firewall_init(&fw);
    r.protocol = NET_PROTO_TCP;
    assert(net_firewall_add(&fw, &r) == 0);
    assert(net_firewall_check(&fw, &p) == NET_ACTION_DENY);

    /* Source mask: /24 match and non-match. */
    net_firewall_init(&fw);
    memset(&r, 0, sizeof(r));
    r.source = 0x0a000000U;
    r.source_mask = 0xffffff00U;
    r.protocol = NET_PROTO_ANY;
    r.action = NET_ACTION_ALLOW;
    assert(net_firewall_add(&fw, &r) == 0);
    p.source = 0x0a0000ffU;
    assert(net_firewall_check(&fw, &p) == NET_ACTION_ALLOW);
    p.source = 0x0b000001U;
    assert(net_firewall_check(&fw, &p) == NET_ACTION_DENY);
    p.source = 0x0a000001U;

    /* Destination mask. */
    net_firewall_init(&fw);
    memset(&r, 0, sizeof(r));
    r.destination = 0x08080800U;
    r.destination_mask = 0xffffff00U;
    r.protocol = NET_PROTO_ANY;
    r.action = NET_ACTION_ALLOW;
    assert(net_firewall_add(&fw, &r) == 0);
    assert(net_firewall_check(&fw, &p) == NET_ACTION_ALLOW);
    p.destination = 0x08080908U;
    assert(net_firewall_check(&fw, &p) == NET_ACTION_DENY);
    p.destination = 0x08080808U;

    /* An exact-host rule (mask 0xffffffff) is the tightest form. */
    net_firewall_init(&fw);
    memset(&r, 0, sizeof(r));
    r.source = 0x0a000001U;
    r.source_mask = 0xffffffffU;
    r.destination = 0x08080808U;
    r.destination_mask = 0xffffffffU;
    r.protocol = NET_PROTO_UDP;
    r.action = NET_ACTION_ALLOW;
    assert(net_firewall_add(&fw, &r) == 0);
    assert(net_firewall_check(&fw, &p) == NET_ACTION_ALLOW);
    p.source = 0x0a000002U;
    assert(net_firewall_check(&fw, &p) == NET_ACTION_DENY);
}

static void test_firewall_first_match_wins(void) {
    struct net_firewall fw;
    struct net_firewall_rule r;
    struct net_ipv4_view p;

    net_firewall_init(&fw);
    memset(&p, 0, sizeof(p));
    p.source = 0x0a000001U;
    p.destination = 0x08080808U;
    p.protocol = NET_PROTO_UDP;

    /* An explicit DENY installed before a broad ALLOW must win. */
    memset(&r, 0, sizeof(r));
    r.protocol = NET_PROTO_UDP;
    r.action = NET_ACTION_DENY;
    assert(net_firewall_add(&fw, &r) == 0);
    r.protocol = NET_PROTO_ANY;
    r.action = NET_ACTION_ALLOW;
    assert(net_firewall_add(&fw, &r) == 0);

    assert(net_firewall_check(&fw, &p) == NET_ACTION_DENY);
    assert(fw.denied == 1);
    assert(fw.accepted == 0);

    /* Reversing the order flips the verdict: the ALLOW now wins. */
    net_firewall_init(&fw);
    r.protocol = NET_PROTO_ANY;
    r.action = NET_ACTION_ALLOW;
    assert(net_firewall_add(&fw, &r) == 0);
    r.protocol = NET_PROTO_UDP;
    r.action = NET_ACTION_DENY;
    assert(net_firewall_add(&fw, &r) == 0);
    assert(net_firewall_check(&fw, &p) == NET_ACTION_ALLOW);
    assert(fw.accepted == 1);
    assert(fw.denied == 0);

    /* Counters accumulate across calls. */
    assert(net_firewall_check(&fw, &p) == NET_ACTION_ALLOW);
    assert(fw.accepted == 2);
    p.protocol = NET_PROTO_TCP;
    assert(net_firewall_check(&fw, &p) == NET_ACTION_ALLOW);
    assert(fw.accepted == 3);
}

int main(void) {
    test_ipv4_parse_null_and_bounds();
    test_ipv4_parse_header_fields();
    test_ipv4_parse_length_relations();
    test_ipv4_parse_checksum();
    test_ipv4_parse_fragments();
    test_firewall_init_and_add();
    test_firewall_capacity();
    test_firewall_remove();
    test_firewall_matching();
    test_firewall_first_match_wins();
    return 0;
}
