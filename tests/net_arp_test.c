#include "net_arp.h"
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int main(void) {
    const uint8_t local_mac[6] = {0x02, 0x10, 0x20, 0x30, 0x40, 0x50};
    const uint8_t peer_mac[6] = {0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    const uint32_t local_ip = 0xc000020aU;
    const uint32_t peer_ip = 0xc0000214U;
    uint8_t frame[NET_ARP_ETHERNET_FRAME_SIZE];
    uint8_t found_mac[6] = {0};
    struct net_arp_ipv4 parsed;
    struct net_arp_cache cache;

    CHECK(net_arp_build_request(local_mac, local_ip, peer_ip, frame) == 42);
    CHECK(frame[0] == 0xff && frame[5] == 0xff);
    CHECK(net_arp_parse_ipv4(frame, sizeof(frame), &parsed) == 0);
    CHECK(parsed.operation == NET_ARP_REQUEST);
    CHECK(parsed.sender_ipv4 == local_ip && parsed.target_ipv4 == peer_ip);
    CHECK(memcmp(parsed.sender_mac, local_mac, 6) == 0);
    CHECK(memcmp(parsed.target_mac, (uint8_t[6]){0}, 6) == 0);

    CHECK(net_arp_build_reply(peer_mac, peer_ip, local_mac, local_ip, frame) == 42);
    CHECK(net_arp_parse_ipv4(frame, sizeof(frame), &parsed) == 0);
    CHECK(parsed.operation == NET_ARP_REPLY);
    CHECK(parsed.sender_ipv4 == peer_ip && parsed.target_ipv4 == local_ip);
    CHECK(memcmp(parsed.target_mac, local_mac, 6) == 0);

    CHECK(net_arp_parse_ipv4(frame, 41, &parsed) < 0);
    frame[6] ^= 1U; /* Ethernet source must match ARP sender hardware. */
    CHECK(net_arp_parse_ipv4(frame, sizeof(frame), &parsed) < 0);
    frame[6] ^= 1U;
    frame[20] = 0; /* Operation zero is invalid. */
    frame[21] = 0;
    CHECK(net_arp_parse_ipv4(frame, sizeof(frame), &parsed) < 0);

    net_arp_cache_init(&cache);
    CHECK(net_arp_cache_learn(&cache, peer_ip, peer_mac, 5, 50) == 0);
    CHECK(cache.count == 1 && cache.learned == 1);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 10, found_mac) == 0);
    CHECK(memcmp(found_mac, peer_mac, 6) == 0);
    const uint8_t refreshed_mac[6] = {0x02, 1, 2, 3, 4, 5};
    CHECK(net_arp_cache_learn(&cache, peer_ip, refreshed_mac, 20, 80) == 0);
    CHECK(cache.count == 1 && cache.refreshed == 1);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 79, found_mac) == 0);
    CHECK(memcmp(found_mac, refreshed_mac, 6) == 0);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 80, found_mac) == -2);
    CHECK(cache.count == 0 && cache.expired == 1);

    /* Unsolicited and misdirected ARP replies cannot poison the cache. */
    CHECK(net_arp_build_reply(peer_mac, peer_ip, local_mac, local_ip, frame) == 42);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 90, 200) == -2);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 90, found_mac) == -2);
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac,
                                         peer_ip, 90, 110) == 0);
    CHECK(net_arp_build_reply(peer_mac, peer_ip, local_mac,
                              local_ip + 1U, frame) == 42);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 91, 200) == -2);
    CHECK(net_arp_build_reply(peer_mac, peer_ip, refreshed_mac, local_ip,
                              frame) == 42);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 92, 200) == -2);
    CHECK(net_arp_build_reply(peer_mac, peer_ip, local_mac, local_ip, frame) == 42);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 93, 200) == 0);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 93, found_mac) == 0);
    CHECK(memcmp(found_mac, peer_mac, 6) == 0);
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac,
                                         peer_ip, 100, 110) == 0);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 110, 200) == -2);
    CHECK(cache.pending_started == 2 && cache.pending_expired == 1);
    CHECK(cache.unsolicited_replies == 4);

    CHECK(net_arp_cache_learn(&cache, 0, peer_mac, 1, 10) < 0);
    CHECK(net_arp_cache_learn(&cache, 0xe0000001U, peer_mac, 1, 10) < 0);
    const uint8_t multicast_mac[6] = {0x01, 0, 0, 0, 0, 1};
    CHECK(net_arp_cache_learn(&cache, peer_ip, multicast_mac, 1, 10) < 0);
    CHECK(cache.rejected == 7);
    CHECK(net_arp_cache_remove(&cache, peer_ip) == 0);
    CHECK(net_arp_cache_remove(&cache, peer_ip) == -2);

    for (uint32_t i = 0; i < NET_ARP_CACHE_CAPACITY; ++i)
        CHECK(net_arp_cache_learn(&cache, 0x0a000001U + i, peer_mac, 1, 100) == 0);
    CHECK(cache.count == NET_ARP_CACHE_CAPACITY);
    CHECK(net_arp_cache_learn(&cache, 0x0a000100U, peer_mac, 1, 100) == -2);
    CHECK(cache.full == 1);
    CHECK(net_arp_cache_remove(&cache, 0x0a000001U) == 0);
    CHECK(net_arp_cache_learn(&cache, 0x0a000100U, peer_mac, 1, 100) == 0);
    CHECK(cache.count == NET_ARP_CACHE_CAPACITY);

    /* ---- Extended coverage below; the cache is re-initialised so the
     * counters start from a known state. ---- */
    net_arp_cache_init(&cache);

    /* Every entry point tolerates a NULL cache or output buffer. */
    net_arp_cache_init(0);
    net_arp_cache_expire(0, 10);
    net_arp_cache_expire(&cache, 10);
    CHECK(net_arp_cache_learn(0, peer_ip, peer_mac, 1, 100) == -1);
    CHECK(net_arp_cache_learn(&cache, peer_ip, 0, 1, 100) == -1);
    CHECK(net_arp_cache_lookup(0, peer_ip, 1, found_mac) == -1);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 1, 0) == -1);
    CHECK(net_arp_cache_remove(0, peer_ip) == -1);
    CHECK(net_arp_cache_begin_resolution(0, local_ip, local_mac, peer_ip, 1,
                                         100) == -1);
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, 0, peer_ip, 1,
                                         100) == -1);
    CHECK(net_arp_cache_accept_reply(0, frame, sizeof(frame), local_ip,
                                     local_mac, 1, 100) == -1);
    CHECK(net_arp_cache_accept_reply(&cache, 0, sizeof(frame), local_ip,
                                     local_mac, 1, 100) == -1);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip, 0,
                                     1, 100) == -1);

    /* Learn/lookup argument validation. */
    CHECK(net_arp_cache_learn(&cache, peer_ip, peer_mac, 100, 100) == -1);
    CHECK(net_arp_cache_learn(&cache, peer_ip, peer_mac, 101, 100) == -1);
    CHECK(net_arp_cache_learn(&cache, 0xffffffffU, peer_mac, 1, 100) == -1);
    CHECK(net_arp_cache_learn(&cache, 0xe0000001U, peer_mac, 1, 100) == -1);
    {
        const uint8_t zero_mac[6] = {0};
        CHECK(net_arp_cache_learn(&cache, peer_ip, zero_mac, 1, 100) == -1);
        CHECK(net_arp_cache_learn(&cache, peer_ip,
                                  (const uint8_t[6]){0xff, 0xff, 0xff, 0xff,
                                                     0xff, 0xff},
                                  1, 100) == -1);
    }
    CHECK(net_arp_cache_lookup(&cache, 0, 1, found_mac) == -1);
    CHECK(net_arp_cache_lookup(&cache, 0xffffffffU, 1, found_mac) == -1);
    CHECK(net_arp_cache_lookup(&cache, 0xe0000001U, 1, found_mac) == -1);
    /* An unknown (but valid) unicast address reports "not cached". */
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 1, found_mac) == -2);
    CHECK(net_arp_cache_remove(&cache, peer_ip) == -2);

    /* Expiry is driven by an absolute deadline, inclusive at the boundary. */
    CHECK(net_arp_cache_learn(&cache, peer_ip, peer_mac, 1, 50) == 0);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 49, found_mac) == 0);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 50, found_mac) == -2);
    CHECK(cache.expired == 1);
    CHECK(cache.count == 0);
    net_arp_cache_expire(&cache, 60);
    CHECK(cache.expired == 1); /* already reclaimed */

    /* Builders reject nonsensical identities. */
    CHECK(net_arp_build_request(0, local_ip, peer_ip, frame) == -1);
    CHECK(net_arp_build_request(local_mac, local_ip, peer_ip, 0) == -1);
    CHECK(net_arp_build_request(local_mac, 0xe0000001U, peer_ip, frame) == -1);
    CHECK(net_arp_build_request(local_mac, local_ip, 0, frame) == -1);
    CHECK(net_arp_build_request(local_mac, local_ip, 0xffffffffU, frame) == -1);
    CHECK(net_arp_build_request(local_mac, local_ip, 0xe0000001U, frame) == -1);
    CHECK(net_arp_build_reply(peer_mac, peer_ip, 0, local_ip, frame) == -1);
    CHECK(net_arp_build_reply(peer_mac, 0, local_mac, local_ip, frame) == -1);
    {
        const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        CHECK(net_arp_build_reply(peer_mac, peer_ip, broadcast, local_ip,
                                  frame) == -1);
        CHECK(net_arp_build_request(broadcast, local_ip, peer_ip, frame) == -1);
    }

    /* A request may carry the unspecified sender address (RFC 5227 probe);
     * a reply may not. */
    CHECK(net_arp_build_request(local_mac, 0, peer_ip, frame) ==
          (int)NET_ARP_ETHERNET_FRAME_SIZE);
    CHECK(net_arp_parse_ipv4(frame, sizeof(frame), &parsed) == 0);
    CHECK(parsed.sender_ipv4 == 0);
    CHECK(net_arp_build_reply(peer_mac, 0, local_mac, local_ip, frame) == -1);

    /* A reply addressed to a broadcast target MAC is refused on parse. */
    {
        uint8_t bad[NET_ARP_ETHERNET_FRAME_SIZE];
        CHECK(net_arp_build_reply(peer_mac, peer_ip, local_mac, local_ip,
                                  bad) == (int)NET_ARP_ETHERNET_FRAME_SIZE);
        /* target hardware address at offset 32 */
        bad[32] = 0xff; bad[33] = 0xff; bad[34] = 0xff;
        bad[35] = 0xff; bad[36] = 0xff; bad[37] = 0xff;
        CHECK(net_arp_parse_ipv4(bad, sizeof(bad), &parsed) < 0);
    }

    /* A request whose target hardware address is neither zero nor unicast is
     * refused. */
    {
        uint8_t bad[NET_ARP_ETHERNET_FRAME_SIZE];
        CHECK(net_arp_build_request(local_mac, local_ip, peer_ip,
                                    bad) == (int)NET_ARP_ETHERNET_FRAME_SIZE);
        bad[32] = 0x01;
        CHECK(net_arp_parse_ipv4(bad, sizeof(bad), &parsed) < 0);
        bad[32] = 0x00;
        CHECK(net_arp_parse_ipv4(bad, sizeof(bad), &parsed) == 0);
    }

    /* VLAN-tagged ARP: the tag is skipped and the inner ethertype decides. */
    {
        uint8_t untagged[NET_ARP_ETHERNET_FRAME_SIZE];
        uint8_t tagged[NET_ARP_ETHERNET_FRAME_SIZE + 4];
        CHECK(net_arp_build_request(local_mac, local_ip, peer_ip,
                                    untagged) == (int)NET_ARP_ETHERNET_FRAME_SIZE);
        memset(tagged, 0, sizeof(tagged));
        memcpy(tagged, untagged, 12);
        tagged[12] = 0x81; tagged[13] = 0x00;
        tagged[14] = 0x00; tagged[15] = 0x64;
        tagged[16] = 0x08; tagged[17] = 0x06;
        memcpy(tagged + 18, untagged + 14,
               NET_ARP_ETHERNET_FRAME_SIZE - 14);
        CHECK(net_arp_parse_ipv4(tagged, sizeof(tagged), &parsed) == 0);
        CHECK(parsed.sender_ipv4 == local_ip);
        /* One byte short of the ARP payload inside the tag. */
        CHECK(net_arp_parse_ipv4(tagged, sizeof(tagged) - 1, &parsed) < 0);
    }

    /* Non-IPv4 and non-Ethernet ARP hardware/protocol tuples are refused. */
    {
        uint8_t bad[NET_ARP_ETHERNET_FRAME_SIZE];
        CHECK(net_arp_build_request(local_mac, local_ip, peer_ip,
                                    bad) == (int)NET_ARP_ETHERNET_FRAME_SIZE);
        bad[14] = 0x00; bad[15] = 0x02; /* hardware type 2 */
        CHECK(net_arp_parse_ipv4(bad, sizeof(bad), &parsed) < 0);
        bad[14] = 0x00; bad[15] = 0x01;
        bad[16] = 0x08; bad[17] = 0x06; /* protocol type != IPv4 */
        CHECK(net_arp_parse_ipv4(bad, sizeof(bad), &parsed) < 0);
        bad[16] = 0x08; bad[17] = 0x00;
        bad[18] = 0x08; /* hardware length != 6 */
        CHECK(net_arp_parse_ipv4(bad, sizeof(bad), &parsed) < 0);
        bad[18] = 0x06;
        bad[19] = 0x06; /* protocol length != 4 */
        CHECK(net_arp_parse_ipv4(bad, sizeof(bad), &parsed) < 0);
        bad[19] = 0x04;
        CHECK(net_arp_parse_ipv4(bad, sizeof(bad), &parsed) == 0);
    }

    /* Resolution bookkeeping: argument validation, dedupe and capacity. */
    net_arp_cache_init(&cache);
    CHECK(net_arp_cache_begin_resolution(&cache, 0, local_mac, peer_ip, 1,
                                         100) == -1);
    CHECK(net_arp_cache_begin_resolution(&cache, 0xe0000001U, local_mac,
                                         peer_ip, 1, 100) == -1);
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac, 0, 1,
                                         100) == -1);
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac,
                                         0xe0000001U, 1, 100) == -1);
    /* Resolving your own address makes no sense. */
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac, local_ip,
                                         1, 100) == -1);
    /* A deadline at or before now is already expired. */
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac, peer_ip,
                                         50, 50) == -1);
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac, peer_ip,
                                         51, 50) == -1);

    for (uint32_t i = 0; i < NET_ARP_PENDING_CAPACITY; ++i)
        CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac,
                                             0x0a000001U + i, 1, 100) == 0);
    CHECK(cache.pending_started == NET_ARP_PENDING_CAPACITY);
    /* The table is full. */
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac,
                                         0x0a000101U, 1, 100) == -2);
    CHECK(cache.full == 1);
    /* Re-arming an existing resolution is idempotent, not a new entry. */
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac,
                                         0x0a000001U, 1, 200) == 0);
    CHECK(cache.pending_started == NET_ARP_PENDING_CAPACITY);

    /* Pending entries expire independently of the resolved cache. */
    net_arp_cache_expire(&cache, 100);
    CHECK(cache.pending_expired == NET_ARP_PENDING_CAPACITY - 1U);

    /* A reply is consumed once: the pending entry is cleared, so a second
     * identical reply is unsolicited. */
    net_arp_cache_init(&cache);
    CHECK(net_arp_build_reply(peer_mac, peer_ip, local_mac, local_ip,
                              frame) == (int)NET_ARP_ETHERNET_FRAME_SIZE);
    /* No resolution outstanding. */
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 1, 200) == -2);
    CHECK(cache.unsolicited_replies == 1);
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac, peer_ip,
                                         1, 150) == 0);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 2, 200) == 0);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 3, found_mac) == 0);
    CHECK(memcmp(found_mac, peer_mac, 6) == 0);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 3, 200) == -2);
    CHECK(cache.unsolicited_replies == 2);

    /* A lease that expires at or before now is refused. */
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac, peer_ip,
                                         3, 150) == 0);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 4, 4) == -1);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 4, 3) == -1);

    /* A truncated or corrupt frame is rejected outright. */
    CHECK(net_arp_cache_accept_reply(&cache, frame, 41, local_ip, local_mac, 4,
                                     200) == -1);

    /* An expired resolution window rejects even a well-formed reply. */
    net_arp_cache_init(&cache);
    CHECK(net_arp_cache_begin_resolution(&cache, local_ip, local_mac, peer_ip,
                                         1, 50) == 0);
    net_arp_cache_expire(&cache, 60);
    CHECK(net_arp_cache_accept_reply(&cache, frame, sizeof(frame), local_ip,
                                     local_mac, 61, 200) == -2);
    CHECK(net_arp_cache_lookup(&cache, peer_ip, 61, found_mac) == -2);

    printf("net_arp_test: checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
