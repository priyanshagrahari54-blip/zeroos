#include "net_arp.h"
#include "net_l2.h"

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int mac_is_unicast(const uint8_t mac[6]) {
    uint8_t any = 0;
    uint8_t all_ff = 0xffU;
    for (uint32_t i = 0; i < 6; ++i) {
        any |= mac[i];
        all_ff &= mac[i];
    }
    return any != 0 && all_ff != 0xffU && (mac[0] & 1U) == 0;
}

static int ipv4_is_unicast(uint32_t address) {
    uint8_t first = (uint8_t)(address >> 24);
    return address != 0 && address != 0xffffffffU &&
           (first < 224U || first > 239U) && first != 0U;
}

static int ipv4_sender_is_valid(uint32_t address, uint8_t operation) {
    /* RFC 5227 address-conflict probes use 0.0.0.0 as sender address. */
    if (address == 0 && operation == NET_ARP_REQUEST)
        return 1;
    return ipv4_is_unicast(address);
}

static int mac_equal(const uint8_t a[6], const uint8_t b[6]) {
    uint8_t difference = 0;
    for (uint32_t i = 0; i < 6; ++i)
        difference |= (uint8_t)(a[i] ^ b[i]);
    return difference == 0;
}

int net_arp_parse_ipv4(const uint8_t *frame, uint32_t length,
                       struct net_arp_ipv4 *out) {
    struct net_eth_view eth;
    struct net_arp_view arp;
    uint32_t sender_ip;
    uint32_t target_ip;

    if (!frame || !out || net_ethernet_parse(frame, length, &eth) != 0 ||
        eth.ethertype != 0x0806U || eth.payload_length < 28U ||
        net_arp_parse(eth.payload, eth.payload_length, &arp) != 0 ||
        arp.hardware_type != 1U || arp.protocol_type != 0x0800U ||
        arp.hardware_length != 6U || arp.protocol_length != 4U ||
        (arp.operation != NET_ARP_REQUEST && arp.operation != NET_ARP_REPLY) ||
        !mac_is_unicast(arp.sender_hardware) ||
        !mac_is_unicast(eth.source) ||
        !ipv4_sender_is_valid(read_be32(arp.sender_protocol), arp.operation))
        return -1;

    for (uint32_t i = 0; i < 6; ++i)
        if (eth.source[i] != arp.sender_hardware[i])
            return -1;

    sender_ip = read_be32(arp.sender_protocol);
    target_ip = read_be32(arp.target_protocol);
    if (!ipv4_is_unicast(target_ip))
        return -1;
    if (arp.operation == NET_ARP_REPLY && !mac_is_unicast(arp.target_hardware))
        return -1;
    if (arp.operation == NET_ARP_REQUEST) {
        uint8_t target_any = 0;
        for (uint32_t i = 0; i < 6; ++i)
            target_any |= arp.target_hardware[i];
        if (target_any != 0 && !mac_is_unicast(arp.target_hardware))
            return -1;
    }

    out->operation = arp.operation;
    out->sender_ipv4 = sender_ip;
    out->target_ipv4 = target_ip;
    for (uint32_t i = 0; i < 6; ++i) {
        out->sender_mac[i] = arp.sender_hardware[i];
        out->target_mac[i] = arp.target_hardware[i];
    }
    return 0;
}

static int arp_build(uint8_t operation, const uint8_t source_mac[6],
                     uint32_t source_ipv4, const uint8_t target_mac[6],
                     uint32_t target_ipv4,
                     uint8_t frame[NET_ARP_ETHERNET_FRAME_SIZE]) {
    static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t *ethernet_destination;

    if (!source_mac || !frame || !mac_is_unicast(source_mac) ||
        !ipv4_sender_is_valid(source_ipv4, operation) ||
        !ipv4_is_unicast(target_ipv4) ||
        (operation != NET_ARP_REQUEST && operation != NET_ARP_REPLY))
        return -1;

    if (operation == NET_ARP_REPLY) {
        if (!target_mac || !mac_is_unicast(target_mac) || source_ipv4 == 0)
            return -1;
        ethernet_destination = target_mac;
    } else {
        ethernet_destination = broadcast;
    }

    for (uint32_t i = 0; i < 6; ++i) {
        frame[i] = ethernet_destination[i];
        frame[6U + i] = source_mac[i];
    }
    write_be16(frame + 12, 0x0806U);
    write_be16(frame + 14, 1U);
    write_be16(frame + 16, 0x0800U);
    frame[18] = 6;
    frame[19] = 4;
    write_be16(frame + 20, operation);
    for (uint32_t i = 0; i < 6; ++i) {
        frame[22U + i] = source_mac[i];
        frame[32U + i] = operation == NET_ARP_REPLY ? target_mac[i] : 0;
    }
    write_be32(frame + 28, source_ipv4);
    write_be32(frame + 38, target_ipv4);
    return (int)NET_ARP_ETHERNET_FRAME_SIZE;
}

int net_arp_build_request(const uint8_t source_mac[6], uint32_t source_ipv4,
                          uint32_t target_ipv4,
                          uint8_t frame[NET_ARP_ETHERNET_FRAME_SIZE]) {
    return arp_build(NET_ARP_REQUEST, source_mac, source_ipv4, 0,
                     target_ipv4, frame);
}

int net_arp_build_reply(const uint8_t source_mac[6], uint32_t source_ipv4,
                        const uint8_t target_mac[6], uint32_t target_ipv4,
                        uint8_t frame[NET_ARP_ETHERNET_FRAME_SIZE]) {
    return arp_build(NET_ARP_REPLY, source_mac, source_ipv4, target_mac,
                     target_ipv4, frame);
}

void net_arp_cache_init(struct net_arp_cache *cache) {
    if (cache) {
        for (uint32_t i = 0; i < NET_ARP_CACHE_CAPACITY; ++i)
            cache->entries[i].active = 0;
        for (uint32_t i = 0; i < NET_ARP_PENDING_CAPACITY; ++i)
            cache->pending[i].active = 0;
        cache->count = 0;
        cache->learned = 0;
        cache->refreshed = 0;
        cache->expired = 0;
        cache->rejected = 0;
        cache->full = 0;
        cache->pending_started = 0;
        cache->pending_expired = 0;
        cache->unsolicited_replies = 0;
    }
}

void net_arp_cache_expire(struct net_arp_cache *cache, uint64_t now) {
    if (!cache)
        return;
    for (uint32_t i = 0; i < NET_ARP_CACHE_CAPACITY; ++i) {
        struct net_arp_entry *entry = &cache->entries[i];
        if (entry->active && entry->expires_at <= now) {
            entry->active = 0;
            if (cache->count)
                --cache->count;
            ++cache->expired;
        }
    }
    for (uint32_t i = 0; i < NET_ARP_PENDING_CAPACITY; ++i) {
        struct net_arp_pending *pending = &cache->pending[i];
        if (pending->active && pending->expires_at <= now) {
            pending->active = 0;
            ++cache->pending_expired;
        }
    }
}

int net_arp_cache_learn(struct net_arp_cache *cache, uint32_t ipv4,
                        const uint8_t mac[6], uint64_t now,
                        uint64_t expires_at) {
    uint32_t free_slot = NET_ARP_CACHE_CAPACITY;
    if (!cache || !mac || !ipv4_is_unicast(ipv4) || !mac_is_unicast(mac) ||
        expires_at <= now) {
        if (cache)
            ++cache->rejected;
        return -1;
    }
    net_arp_cache_expire(cache, now);
    for (uint32_t i = 0; i < NET_ARP_CACHE_CAPACITY; ++i) {
        struct net_arp_entry *entry = &cache->entries[i];
        if (entry->active && entry->ipv4 == ipv4) {
            for (uint32_t j = 0; j < 6; ++j)
                entry->mac[j] = mac[j];
            entry->expires_at = expires_at;
            ++cache->refreshed;
            return 0;
        }
        if (!entry->active && free_slot == NET_ARP_CACHE_CAPACITY)
            free_slot = i;
    }
    if (free_slot == NET_ARP_CACHE_CAPACITY) {
        ++cache->full;
        return -2;
    }
    struct net_arp_entry *entry = &cache->entries[free_slot];
    entry->ipv4 = ipv4;
    for (uint32_t i = 0; i < 6; ++i)
        entry->mac[i] = mac[i];
    entry->expires_at = expires_at;
    entry->active = 1;
    ++cache->count;
    ++cache->learned;
    return 0;
}

int net_arp_cache_lookup(struct net_arp_cache *cache, uint32_t ipv4,
                         uint64_t now, uint8_t mac_out[6]) {
    if (!cache || !mac_out || !ipv4_is_unicast(ipv4))
        return -1;
    net_arp_cache_expire(cache, now);
    for (uint32_t i = 0; i < NET_ARP_CACHE_CAPACITY; ++i) {
        struct net_arp_entry *entry = &cache->entries[i];
        if (entry->active && entry->ipv4 == ipv4) {
            for (uint32_t j = 0; j < 6; ++j)
                mac_out[j] = entry->mac[j];
            return 0;
        }
    }
    return -2;
}

int net_arp_cache_remove(struct net_arp_cache *cache, uint32_t ipv4) {
    if (!cache)
        return -1;
    for (uint32_t i = 0; i < NET_ARP_CACHE_CAPACITY; ++i) {
        struct net_arp_entry *entry = &cache->entries[i];
        if (entry->active && entry->ipv4 == ipv4) {
            entry->active = 0;
            if (cache->count)
                --cache->count;
            return 0;
        }
    }
    return -2;
}

int net_arp_cache_begin_resolution(struct net_arp_cache *cache,
                                   uint32_t local_ipv4,
                                   const uint8_t local_mac[6],
                                   uint32_t target_ipv4, uint64_t now,
                                   uint64_t timeout_at) {
    uint32_t free_slot = NET_ARP_PENDING_CAPACITY;
    if (!cache || !local_mac || !ipv4_is_unicast(local_ipv4) ||
        !mac_is_unicast(local_mac) || !ipv4_is_unicast(target_ipv4) ||
        target_ipv4 == local_ipv4 || timeout_at <= now) {
        if (cache)
            ++cache->rejected;
        return -1;
    }
    net_arp_cache_expire(cache, now);
    for (uint32_t i = 0; i < NET_ARP_PENDING_CAPACITY; ++i) {
        struct net_arp_pending *pending = &cache->pending[i];
        if (pending->active && pending->target_ipv4 == target_ipv4 &&
            pending->local_ipv4 == local_ipv4 &&
            mac_equal(pending->local_mac, local_mac)) {
            pending->expires_at = timeout_at;
            return 0;
        }
        if (!pending->active && free_slot == NET_ARP_PENDING_CAPACITY)
            free_slot = i;
    }
    if (free_slot == NET_ARP_PENDING_CAPACITY) {
        ++cache->full;
        return -2;
    }
    struct net_arp_pending *pending = &cache->pending[free_slot];
    pending->target_ipv4 = target_ipv4;
    pending->local_ipv4 = local_ipv4;
    for (uint32_t j = 0; j < 6; ++j)
        pending->local_mac[j] = local_mac[j];
    pending->expires_at = timeout_at;
    pending->active = 1;
    ++cache->pending_started;
    return 0;
}

int net_arp_cache_accept_reply(struct net_arp_cache *cache,
                               const uint8_t *frame, uint32_t length,
                               uint32_t local_ipv4,
                               const uint8_t local_mac[6], uint64_t now,
                               uint64_t lease_until) {
    struct net_arp_ipv4 reply;
    struct net_eth_view eth;
    if (!cache || !frame || !local_mac || !ipv4_is_unicast(local_ipv4) ||
        !mac_is_unicast(local_mac) || lease_until <= now ||
        net_arp_parse_ipv4(frame, length, &reply) != 0 ||
        reply.operation != NET_ARP_REPLY) {
        if (cache)
            ++cache->rejected;
        return -1;
    }
    if (net_ethernet_parse(frame, length, &eth) != 0 ||
        reply.target_ipv4 != local_ipv4 ||
        !mac_equal(reply.target_mac, local_mac) ||
        !mac_equal(eth.destination, local_mac)) {
        ++cache->rejected;
        ++cache->unsolicited_replies;
        return -2;
    }
    net_arp_cache_expire(cache, now);
    for (uint32_t i = 0; i < NET_ARP_PENDING_CAPACITY; ++i) {
        struct net_arp_pending *pending = &cache->pending[i];
        if (!pending->active || pending->target_ipv4 != reply.sender_ipv4 ||
            pending->local_ipv4 != local_ipv4 ||
            !mac_equal(pending->local_mac, local_mac))
            continue;
        if (net_arp_cache_learn(cache, reply.sender_ipv4, reply.sender_mac,
                                now, lease_until) != 0)
            return -1;
        pending->active = 0;
        return 0;
    }
    ++cache->rejected;
    ++cache->unsolicited_replies;
    return -2;
}
