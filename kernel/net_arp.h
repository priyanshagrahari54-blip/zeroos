#ifndef ZEROOS_NET_ARP_H
#define ZEROOS_NET_ARP_H

#include "types.h"

#define NET_ARP_CACHE_CAPACITY 32U
#define NET_ARP_PENDING_CAPACITY 16U
#define NET_ARP_ETHERNET_FRAME_SIZE 42U
#define NET_ARP_REQUEST 1U
#define NET_ARP_REPLY 2U

struct net_arp_ipv4 {
    uint8_t operation;
    uint8_t sender_mac[6];
    uint8_t target_mac[6];
    uint32_t sender_ipv4; /* host integer in network byte order */
    uint32_t target_ipv4; /* host integer in network byte order */
};

struct net_arp_entry {
    uint32_t ipv4;
    uint8_t mac[6];
    uint64_t expires_at;
    uint8_t active;
};

struct net_arp_pending {
    uint32_t target_ipv4;
    uint32_t local_ipv4;
    uint8_t local_mac[6];
    uint64_t expires_at;
    uint8_t active;
};

struct net_arp_cache {
    struct net_arp_entry entries[NET_ARP_CACHE_CAPACITY];
    struct net_arp_pending pending[NET_ARP_PENDING_CAPACITY];
    uint32_t count;
    uint64_t learned, refreshed, expired, rejected, full;
    uint64_t pending_started, pending_expired, unsolicited_replies;
};

/* Validate an untagged Ethernet/IPv4 ARP request or reply. */
int net_arp_parse_ipv4(const uint8_t *frame, uint32_t length,
                       struct net_arp_ipv4 *out);
int net_arp_build_request(const uint8_t source_mac[6], uint32_t source_ipv4,
                          uint32_t target_ipv4,
                          uint8_t frame[NET_ARP_ETHERNET_FRAME_SIZE]);
int net_arp_build_reply(const uint8_t source_mac[6], uint32_t source_ipv4,
                        const uint8_t target_mac[6], uint32_t target_ipv4,
                        uint8_t frame[NET_ARP_ETHERNET_FRAME_SIZE]);

/* Cache is fixed-size and caller-serialized. `expires_at` is an absolute,
 * monotonic tick deadline. The direct learn API is for trusted/static
 * mappings only; never pass untrusted packet observations to it. Network
 * replies must go through net_arp_cache_accept_reply(), which requires a
 * matching live request and exact local IP/MAC destination. */
void net_arp_cache_init(struct net_arp_cache *cache);
int net_arp_cache_learn(struct net_arp_cache *cache, uint32_t ipv4,
                        const uint8_t mac[6], uint64_t now,
                        uint64_t expires_at);
int net_arp_cache_lookup(struct net_arp_cache *cache, uint32_t ipv4,
                         uint64_t now, uint8_t mac_out[6]);
int net_arp_cache_remove(struct net_arp_cache *cache, uint32_t ipv4);
void net_arp_cache_expire(struct net_arp_cache *cache, uint64_t now);
/* Replies are admitted only when they match a live resolution request and
 * are addressed to the exact local IPv4/MAC identity that initiated it. */
int net_arp_cache_begin_resolution(struct net_arp_cache *cache,
                                   uint32_t local_ipv4,
                                   const uint8_t local_mac[6],
                                   uint32_t target_ipv4, uint64_t now,
                                   uint64_t timeout_at);
int net_arp_cache_accept_reply(struct net_arp_cache *cache,
                               const uint8_t *frame, uint32_t length,
                               uint32_t local_ipv4,
                               const uint8_t local_mac[6], uint64_t now,
                               uint64_t lease_until);

#endif
