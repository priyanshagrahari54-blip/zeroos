#include "net_stack.h"
#include "net_ipv6.h"
#include "net_l2.h"
#include "net_transport.h"

static int ipv4_unicast_address(uint32_t address) {
    uint8_t first = (uint8_t)(address >> 24);
    return address != 0 && address != 0xffffffffU && first != 0 &&
           first != 127 && first < 224;
}

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t fold_checksum(uint32_t sum) {
    while (sum >> 16)
        sum = (sum & 0xffffU) + (sum >> 16);
    return sum;
}

static uint32_t checksum_add(uint32_t sum, const uint8_t *bytes,
                             uint32_t length) {
    uint32_t i = 0;
    while (i + 1U < length) {
        sum += ((uint32_t)bytes[i] << 8) | bytes[i + 1U];
        i += 2U;
    }
    if (i < length)
        sum += (uint32_t)bytes[i] << 8;
    return fold_checksum(sum);
}

static int udp_checksum_ipv4_valid(const uint8_t *ip, const uint8_t *udp,
                                   uint16_t udp_length) {
    if (read_be16(udp + 6U) == 0)
        return 1; /* IPv4 UDP checksum is optional. */
    uint32_t sum = checksum_add(0, ip + 12U, 8U);
    uint8_t pseudo_tail[4] = { 0, ip[9],
                               (uint8_t)(udp_length >> 8),
                               (uint8_t)udp_length };
    sum = checksum_add(sum, pseudo_tail, sizeof(pseudo_tail));
    sum = checksum_add(sum, udp, udp_length);
    return fold_checksum(sum) == 0xffffU;
}

static int ethernet_destination_allowed(const struct netif *interface,
                                        const uint8_t destination[6]) {
    uint8_t difference = 0;
    for (uint32_t i = 0; i < 6U; ++i)
        difference |= destination[i] ^ interface->address[i];
    if (difference == 0)
        return 1;
    if (destination[0] & 1U)
        return 1; /* Broadcast and multicast; IP policy still applies. */
    return 0;
}

void net_stack_init(struct net_stack *stack, net_stack_udp_fn udp_receive,
                    void *context) {
    if (!stack)
        return;
    uint8_t *bytes = (uint8_t *)stack;
    for (uint32_t i = 0; i < sizeof(*stack); ++i)
        bytes[i] = 0;
    net_firewall_init(&stack->ipv4_firewall);
    stack->udp_receive = udp_receive;
    stack->context = context;
}

int net_stack_set_ipv4_address(struct net_stack *stack, uint32_t address) {
    if (!stack || !ipv4_unicast_address(address))
        return -1;
    stack->ipv4_local_address = address;
    stack->ipv4_configured = 1;
    return 0;
}

int net_stack_input(struct net_stack *stack, const struct netif *interface,
                    const uint8_t *frame, uint32_t length) {
    struct net_eth_view ethernet;
    if (!stack || !interface || !frame)
        return -1;
    stack->stats.frames++;
    if (!(interface->flags & NETIF_FLAG_LINK) || length < 14U ||
        length > NETIF_FRAME_MAX || length > (uint32_t)interface->mtu + 18U ||
        net_ethernet_parse(frame, length, &ethernet) != 0) {
        stack->stats.malformed++;
        return -1;
    }
    uint8_t source_is_zero = 0;
    for (uint32_t i = 0; i < 6U; ++i)
        source_is_zero |= ethernet.source[i];
    if (!source_is_zero || (ethernet.source[0] & 1U)) {
        stack->stats.malformed++;
        return -1;
    }
    if (!ethernet_destination_allowed(interface, ethernet.destination)) {
        stack->stats.policy_drops++;
        return 1;
    }

    if (ethernet.ethertype == NET_STACK_ETHERTYPE_IPV4) {
        struct net_ipv4_view ipv4;
        struct net_udp_view udp;
        uint8_t source[16] = { 0 }, destination[16] = { 0 };
        if (net_ipv4_parse(ethernet.payload, ethernet.payload_length,
                           &ipv4) != 0) {
            stack->stats.malformed++;
            return -1;
        }
        if (!stack->ipv4_configured ||
            (ipv4.destination != stack->ipv4_local_address &&
             ipv4.destination != 0xffffffffU)) {
            stack->stats.policy_drops++;
            return 1;
        }
        if (net_firewall_check(&stack->ipv4_firewall, &ipv4) !=
            NET_ACTION_ALLOW) {
            stack->stats.policy_drops++;
            return 1;
        }
        if (ipv4.is_fragment) {
            stack->stats.fragments++;
            return 1; /* No fragment reassembly or partial UDP delivery. */
        }
        if (ipv4.protocol != NET_STACK_PROTOCOL_UDP || !stack->udp_receive) {
            stack->stats.unsupported++;
            return 1;
        }
        uint32_t ip_length = ipv4.total_length;
        if (ip_length < ipv4.header_length ||
            net_udp_parse(ethernet.payload + ipv4.header_length,
                          ip_length - ipv4.header_length, &udp) != 0 ||
            udp.length != ip_length - ipv4.header_length) {
            stack->stats.malformed++;
            return -1;
        }
        if (!udp_checksum_ipv4_valid(ethernet.payload,
                                     ethernet.payload + ipv4.header_length,
                                     udp.length)) {
            stack->stats.udp_checksum_errors++;
            return -1;
        }
        source[10] = source[11] = 0xff;
        destination[10] = destination[11] = 0xff;
        source[12] = (uint8_t)(ipv4.source >> 24);
        source[13] = (uint8_t)(ipv4.source >> 16);
        source[14] = (uint8_t)(ipv4.source >> 8);
        source[15] = (uint8_t)ipv4.source;
        destination[12] = (uint8_t)(ipv4.destination >> 24);
        destination[13] = (uint8_t)(ipv4.destination >> 16);
        destination[14] = (uint8_t)(ipv4.destination >> 8);
        destination[15] = (uint8_t)ipv4.destination;
        if (stack->udp_receive(stack->context, NET_STACK_FAMILY_IPV4,
                               interface->index, source, destination,
                               udp.source_port, udp.destination_port,
                               udp.payload, udp.payload_length) != 0) {
            stack->stats.callback_errors++;
            return -1;
        }
        stack->stats.udp_delivered++;
        return 0;
    }

    if (ethernet.ethertype == NET_STACK_ETHERTYPE_IPV6) {
        struct net_ipv6_view ipv6;
        if (net_ipv6_parse(ethernet.payload, ethernet.payload_length,
                           &ipv6) != 0) {
            stack->stats.malformed++;
            return -1;
        }
        /* IPv6 delivery stays fail-closed until firewall/routing policy and
         * fragment reassembly are implemented. Parsing remains useful for
         * diagnostics and safe future policy integration. */
        stack->stats.unsupported++;
        return 1;
    }

    stack->stats.unsupported++;
    return 1;
}

int net_stack_poll(struct net_stack *stack, struct netif *interface,
                   uint32_t frame_budget) {
    uint8_t frame[NETIF_FRAME_MAX];
    uint16_t length;
    uint32_t processed = 0;
    if (!stack || !interface || frame_budget == 0)
        return -1;
    while (processed < frame_budget) {
        int result = netif_dequeue(interface, frame, sizeof(frame), &length);
        if (result == 1)
            break;
        if (result != 0)
            return processed ? (int)processed : -2;
        (void)net_stack_input(stack, interface, frame, length);
        ++processed;
    }
    return (int)processed;
}

static void write_be16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static uint16_t checksum_finish(uint32_t sum) {
    sum = fold_checksum(sum);
    return (uint16_t)~sum;
}

static int mac_is_usable_unicast(const uint8_t mac[6]) {
    uint8_t any = 0;
    for (uint32_t i = 0; i < 6U; ++i)
        any |= mac[i];
    return any && !(mac[0] & 1U);
}

int net_stack_send_udp_ipv4(struct net_stack *stack, struct netif *interface,
                            const uint8_t destination_mac[6],
                            uint32_t destination_address,
                            uint16_t source_port, uint16_t destination_port,
                            const uint8_t *payload, uint16_t payload_length) {
    uint8_t frame[NETIF_FRAME_MAX];
    uint8_t *ip = frame + 14U;
    uint8_t *udp = ip + 20U;
    uint32_t udp_length = 8U + payload_length;
    uint32_t ip_length = 20U + udp_length;
    uint32_t frame_length = 14U + ip_length;
    if (!stack || !interface || !destination_mac ||
        !stack->ipv4_configured || !interface->transmit ||
        !interface->lock || !interface->unlock ||
        !mac_is_usable_unicast(destination_mac) ||
        !mac_is_usable_unicast(interface->address) ||
        !ipv4_unicast_address(destination_address) ||
        destination_address == stack->ipv4_local_address ||
        !source_port || !destination_port ||
        (payload_length && !payload)) {
        if (stack)
            stack->stats.transmit_errors++;
        return -1;
    }
    if (ip_length > interface->mtu || frame_length > sizeof(frame) ||
        ip_length > 0xffffU) {
        stack->stats.transmit_errors++;
        return -2;
    }

    for (uint32_t i = 0; i < 6U; ++i) {
        frame[i] = destination_mac[i];
        frame[6U + i] = interface->address[i];
    }
    write_be16(frame + 12U, NET_STACK_ETHERTYPE_IPV4);
    for (uint32_t i = 0; i < 20U; ++i)
        ip[i] = 0;
    ip[0] = 0x45U;
    write_be16(ip + 2U, (uint16_t)ip_length);
    write_be16(ip + 6U, 0x4000U); /* DF: this primitive never fragments. */
    ip[8] = 64U;
    ip[9] = NET_STACK_PROTOCOL_UDP;
    ip[12] = (uint8_t)(stack->ipv4_local_address >> 24);
    ip[13] = (uint8_t)(stack->ipv4_local_address >> 16);
    ip[14] = (uint8_t)(stack->ipv4_local_address >> 8);
    ip[15] = (uint8_t)stack->ipv4_local_address;
    ip[16] = (uint8_t)(destination_address >> 24);
    ip[17] = (uint8_t)(destination_address >> 16);
    ip[18] = (uint8_t)(destination_address >> 8);
    ip[19] = (uint8_t)destination_address;
    write_be16(ip + 10U, checksum_finish(checksum_add(0, ip, 20U)));

    write_be16(udp, source_port);
    write_be16(udp + 2U, destination_port);
    write_be16(udp + 4U, (uint16_t)udp_length);
    udp[6] = udp[7] = 0;
    for (uint32_t i = 0; i < payload_length; ++i)
        udp[8U + i] = payload[i];
    uint32_t sum = checksum_add(0, ip + 12U, 8U);
    uint8_t pseudo_tail[4] = { 0, NET_STACK_PROTOCOL_UDP,
                               (uint8_t)(udp_length >> 8),
                               (uint8_t)udp_length };
    sum = checksum_add(sum, pseudo_tail, sizeof(pseudo_tail));
    sum = checksum_add(sum, udp, udp_length);
    uint16_t udp_checksum = checksum_finish(sum);
    write_be16(udp + 6U, udp_checksum ? udp_checksum : 0xffffU);

    int result = netif_send(interface, frame, frame_length);
    if (result != 0) {
        stack->stats.transmit_errors++;
        return result;
    }
    stack->stats.udp_transmitted++;
    return 0;
}
