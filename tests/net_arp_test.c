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

    CHECK(net_arp_cache_learn(&cache, 0, peer_mac, 1, 10) < 0);
    CHECK(net_arp_cache_learn(&cache, 0xe0000001U, peer_mac, 1, 10) < 0);
    const uint8_t multicast_mac[6] = {0x01, 0, 0, 0, 0, 1};
    CHECK(net_arp_cache_learn(&cache, peer_ip, multicast_mac, 1, 10) < 0);
    CHECK(cache.rejected == 3);
    CHECK(net_arp_cache_remove(&cache, peer_ip) == -2);

    for (uint32_t i = 0; i < NET_ARP_CACHE_CAPACITY; ++i)
        CHECK(net_arp_cache_learn(&cache, 0x0a000001U + i, peer_mac, 1, 100) == 0);
    CHECK(cache.count == NET_ARP_CACHE_CAPACITY);
    CHECK(net_arp_cache_learn(&cache, 0x0a000100U, peer_mac, 1, 100) == -2);
    CHECK(cache.full == 1);
    CHECK(net_arp_cache_remove(&cache, 0x0a000001U) == 0);
    CHECK(net_arp_cache_learn(&cache, 0x0a000100U, peer_mac, 1, 100) == 0);
    CHECK(cache.count == NET_ARP_CACHE_CAPACITY);

    printf("net_arp_test: checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
