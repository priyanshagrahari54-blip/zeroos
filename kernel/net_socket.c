#include "net_socket.h"

static uint32_t address_is_multicast(uint32_t address) {
    return (address & 0xf0000000U) == 0xe0000000U;
}

static uint64_t make_handle(uint32_t generation, uint32_t slot) {
    return ((uint64_t)generation << 32) | (uint64_t)(slot + 1U);
}

static struct net_udp_socket_slot *lookup(struct net_udp_socket_table *table,
                                           uint32_t owner, uint64_t handle) {
    uint32_t slot_id = (uint32_t)handle;
    uint32_t generation = (uint32_t)(handle >> 32);
    if (!table || !owner || slot_id == 0 ||
        slot_id > NET_UDP_SOCKET_CAPACITY || generation == 0)
        return 0;
    struct net_udp_socket_slot *slot = &table->slots[slot_id - 1U];
    if (!slot->active || slot->owner != owner ||
        slot->generation != generation)
        return 0;
    return slot;
}

static void clear_slot(struct net_udp_socket_slot *slot) {
    uint8_t *bytes = (uint8_t *)slot;
    for (uint32_t i = 0; i < sizeof(*slot); ++i)
        bytes[i] = 0;
}

void net_udp_socket_table_init(struct net_udp_socket_table *table) {
    if (!table)
        return;
    uint8_t *bytes = (uint8_t *)table;
    for (uint32_t i = 0; i < sizeof(*table); ++i)
        bytes[i] = 0;
    table->next_generation = 1U;
}

int net_udp_socket_bind(struct net_udp_socket_table *table, uint32_t owner,
                        uint32_t local_address, uint16_t local_port,
                        uint64_t *handle_out) {
    if (!table || !owner || !local_port || !handle_out ||
        local_address == 0xffffffffU || address_is_multicast(local_address))
        return -1;
    if (!table->next_generation)
        return -2; /* Never recycle a generation after wraparound. */

    uint32_t free_slot = NET_UDP_SOCKET_CAPACITY;
    for (uint32_t i = 0; i < NET_UDP_SOCKET_CAPACITY; ++i) {
        struct net_udp_socket_slot *slot = &table->slots[i];
        if (!slot->active) {
            if (free_slot == NET_UDP_SOCKET_CAPACITY)
                free_slot = i;
            continue;
        }
        if (slot->local_port == local_port &&
            (!local_address || !slot->local_address ||
             local_address == slot->local_address))
            return -3; /* No reuse/stealing or ambiguous wildcard bind. */
    }
    if (free_slot == NET_UDP_SOCKET_CAPACITY)
        return -4;

    struct net_udp_socket_slot *slot = &table->slots[free_slot];
    clear_slot(slot);
    slot->owner = owner;
    slot->generation = table->next_generation++;
    slot->local_address = local_address;
    slot->local_port = local_port;
    slot->active = 1;
    *handle_out = make_handle(slot->generation, free_slot);
    return 0;
}

int net_udp_socket_close(struct net_udp_socket_table *table, uint32_t owner,
                         uint64_t handle) {
    struct net_udp_socket_slot *slot = lookup(table, owner, handle);
    if (!slot) {
        if (table)
            table->invalid++;
        return -1;
    }
    clear_slot(slot);
    return 0;
}

int net_udp_socket_receive(struct net_udp_socket_table *table, uint32_t owner,
                           uint64_t handle, uint8_t *payload,
                           uint16_t capacity, struct net_udp_receive_info *info) {
    struct net_udp_socket_slot *slot = lookup(table, owner, handle);
    if (!slot || !payload || !info) {
        if (table)
            table->invalid++;
        return -1;
    }
    if (!slot->count)
        return 1;
    struct net_udp_datagram *datagram = &slot->queue[slot->head];
    if (capacity < datagram->length)
        return -2;
    for (uint32_t i = 0; i < datagram->length; ++i)
        payload[i] = datagram->payload[i];
    info->source_address = datagram->source_address;
    info->source_port = datagram->source_port;
    info->length = datagram->length;
    slot->head = (uint8_t)((slot->head + 1U) % NET_UDP_SOCKET_QUEUE_DEPTH);
    slot->count--;
    return 0;
}

static int ipv4_mapped(const uint8_t address[16]) {
    uint8_t prefix = 0;
    for (uint32_t i = 0; i < 10U; ++i)
        prefix |= address[i];
    return prefix == 0 && address[10] == 0xffU && address[11] == 0xffU;
}

int net_udp_socket_dispatch(void *context, uint8_t family,
                            uint32_t interface_index,
                            const uint8_t source[16],
                            const uint8_t destination[16],
                            uint16_t source_port, uint16_t destination_port,
                            const uint8_t *payload, uint16_t length) {
    struct net_udp_socket_table *table =
        (struct net_udp_socket_table *)context;
    if (!table || family != NET_UDP_SOCKET_FAMILY_IPV4 ||
        !interface_index || !source || !destination || !source_port ||
        !destination_port || (length && !payload) ||
        length > NET_UDP_SOCKET_PAYLOAD_MAX ||
        !ipv4_mapped(source) || !ipv4_mapped(destination)) {
        if (table)
            table->invalid++;
        return -1;
    }

    uint32_t source_address = ((uint32_t)source[12] << 24) |
                              ((uint32_t)source[13] << 16) |
                              ((uint32_t)source[14] << 8) | source[15];
    uint32_t destination_address = ((uint32_t)destination[12] << 24) |
                                  ((uint32_t)destination[13] << 16) |
                                  ((uint32_t)destination[14] << 8) |
                                  destination[15];
    for (uint32_t i = 0; i < NET_UDP_SOCKET_CAPACITY; ++i) {
        struct net_udp_socket_slot *slot = &table->slots[i];
        if (!slot->active || slot->local_port != destination_port ||
            (slot->local_address &&
             slot->local_address != destination_address))
            continue;
        if (slot->count == NET_UDP_SOCKET_QUEUE_DEPTH) {
            slot->dropped++;
            table->dropped++;
            return 0; /* Best-effort UDP drops newest; never block RX. */
        }
        struct net_udp_datagram *datagram = &slot->queue[slot->tail];
        datagram->source_address = source_address;
        datagram->source_port = source_port;
        datagram->length = length;
        for (uint32_t j = 0; j < length; ++j)
            datagram->payload[j] = payload[j];
        slot->tail = (uint8_t)((slot->tail + 1U) % NET_UDP_SOCKET_QUEUE_DEPTH);
        slot->count++;
        table->delivered++;
        return 0;
    }
    table->unmatched++;
    return 0;
}
