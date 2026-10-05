#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../kernel/firewall.h"
#include "../kernel/net_stack.h"
#include "../kernel/net_l2.h"

struct capture {
    uint8_t frame[NETIF_FRAME_MAX];
    uint16_t length;
    int fail;
};

struct received_datagram {
    uint32_t count;
    uint16_t source_port, destination_port, length;
    uint8_t payload[32];
};

static int receive_udp(void *context, uint8_t family, uint32_t interface_index,
                       const uint8_t source[16], const uint8_t destination[16],
                       uint16_t source_port, uint16_t destination_port,
                       const uint8_t *payload, uint16_t length) {
    struct received_datagram *received = (struct received_datagram *)context;
    (void)source;
    (void)destination;
    assert(family == NET_STACK_FAMILY_IPV4 && interface_index == 2);
    assert(length <= sizeof(received->payload));
    received->count++;
    received->source_port = source_port;
    received->destination_port = destination_port;
    received->length = length;
    if (length)
        memcpy(received->payload, payload, length);
    return 0;
}

static uint64_t lock_noop(void *context) { (void)context; return 0; }
static void unlock_noop(void *context, uint64_t state) {
    (void)context; (void)state;
}
static int transmit(void *context, const uint8_t *frame, uint16_t length) {
    struct capture *capture = (struct capture *)context;
    if (capture->fail)
        return -1;
    assert(length <= sizeof(capture->frame));
    memcpy(capture->frame, frame, length);
    capture->length = length;
    return 0;
}

static uint32_t add_words(uint32_t sum, const uint8_t *bytes, uint32_t length) {
    uint32_t i = 0;
    while (i + 1U < length) {
        sum += ((uint32_t)bytes[i] << 8) | bytes[i + 1U];
        i += 2U;
    }
    if (i < length)
        sum += (uint32_t)bytes[i] << 8;
    while (sum >> 16)
        sum = (sum & 0xffffU) + (sum >> 16);
    return sum;
}

static int checksum_valid(const uint8_t *bytes, uint32_t length) {
    return add_words(0, bytes, length) == 0xffffU;
}

static int udp_checksum_valid(const uint8_t *ip, const uint8_t *udp,
                              uint16_t length) {
    uint32_t sum = add_words(0, ip + 12U, 8U);
    uint8_t pseudo[4] = { 0, 17, (uint8_t)(length >> 8), (uint8_t)length };
    sum = add_words(sum, pseudo, sizeof(pseudo));
    sum = add_words(sum, udp, length);
    return sum == 0xffffU;
}

int main(void) {
    struct capture capture = {0};
    struct netif interface, receiver_interface;
    struct net_stack stack, receiver_stack;
    struct received_datagram received = {0};
    struct zd_fw_rule allow_udp = {0};
    const uint8_t local_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    const uint8_t remote_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };
    const uint8_t payload[] = { 'z', 'e', 'r', 'o', 'o', 's', '!' };
    const uint32_t local_ip = 0xc0000202U;
    const uint32_t remote_ip = 0xc6336409U;

    struct zd_fw ipv6_firewall;
    struct zd_fw_rule ipv6_allow = {
        .dir = ZD_FW_IN, .proto = ZD_FW_UDP, .action = ZD_FW_ALLOW
    };
    const uint8_t ipv6_source[16] =
        { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    const uint8_t ipv6_destination[16] =
        { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 };
    net_fw_init(&ipv6_firewall);
    ipv6_allow.ip_lo = 0x20010db8U;
    ipv6_allow.ip_hi = 0x20010db8U;
    assert(net_fw_add(&ipv6_firewall, &ipv6_allow, 0) == 0);
    assert(net_fw_check_ipv6(&ipv6_firewall, ipv6_source, ipv6_destination,
                             17, 49152, 53) == ZD_FW_DENY);
    assert(ipv6_firewall.stats.flows == 1 &&
           ipv6_firewall.stats.denied == 1);
    assert(net_fw_remove(&ipv6_firewall,
                         ipv6_firewall.rules[0].id) == 0);
    assert(net_fw_add(&ipv6_firewall, &(struct zd_fw_rule) {
                          .dir = ZD_FW_IN, .proto = ZD_FW_UDP,
                          .action = ZD_FW_ALLOW
                      }, 0) == 0);
    assert(net_fw_check_ipv6(&ipv6_firewall, ipv6_source, ipv6_destination,
                             17, 49152, 53) == ZD_FW_ALLOW);
    assert(net_fw_check_ipv6(&ipv6_firewall, ipv6_source, ipv6_destination,
                             58, 0, 0) == ZD_FW_DENY);
    assert(ipv6_firewall.stats.flows == 3 &&
           ipv6_firewall.stats.allowed == 1 &&
           ipv6_firewall.stats.denied == 2);

    assert(netif_init(&interface, "tx0", 1, local_mac, 1500, &capture,
                      transmit, lock_noop, unlock_noop) == 0);
    assert(netif_set_link(&interface, 1) == 0);
    net_stack_init(&stack, 0, 0);
    assert(net_stack_set_ipv4_address(&stack, local_ip) == 0);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   49152, 53, payload, sizeof(payload)) == -3);
    assert(stack.stats.policy_drops == 1 && stack.stats.transmit_errors == 0);
    allow_udp.dir = ZD_FW_OUT;
    allow_udp.proto = ZD_FW_UDP;
    allow_udp.action = ZD_FW_ALLOW;
    assert(net_fw_add(&stack.ipv4_firewall, &allow_udp, 0) == 0);
    struct zd_fw_rule deny_dns = {
        .dir = ZD_FW_OUT, .proto = ZD_FW_UDP, .action = ZD_FW_DENY,
        .port_lo = 53, .port_hi = 53
    };
    assert(net_fw_add(&stack.ipv4_firewall, &deny_dns, 1) == 0);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   49152, 53, payload, sizeof(payload)) == -3);
    assert(stack.stats.policy_drops == 2 && capture.length == 0);
    assert(net_fw_remove(&stack.ipv4_firewall,
                         stack.ipv4_firewall.rules[0].id) == 0);

    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   49152, 53, payload, sizeof(payload)) == 0);
    assert(stack.stats.udp_transmitted == 1 && stack.stats.transmit_errors == 0);
    assert(interface.stats.tx_packets == 1 && interface.stats.tx_dropped == 0);
    assert(capture.length == 14U + 20U + 8U + sizeof(payload));
    assert(memcmp(capture.frame, remote_mac, 6) == 0);
    assert(memcmp(capture.frame + 6, local_mac, 6) == 0);
    assert(capture.frame[12] == 0x08 && capture.frame[13] == 0x00);

    const uint8_t *ip = capture.frame + 14U;
    const uint8_t *udp = ip + 20U;
    assert(ip[0] == 0x45 && ip[8] == 64 && ip[9] == 17);
    assert(ip[6] == 0x40 && ip[7] == 0x00); /* Don't fragment. */
    assert(ip[2] == 0 && ip[3] == 35);
    assert(ip[12] == 192 && ip[15] == 2);
    assert(ip[16] == 198 && ip[17] == 51 && ip[18] == 100 && ip[19] == 9);
    assert(checksum_valid(ip, 20));
    assert(udp[0] == 0xc0 && udp[1] == 0x00);
    assert(udp[2] == 0 && udp[3] == 53);
    assert(udp[4] == 0 && udp[5] == 15);
    assert((udp[6] || udp[7]) && udp_checksum_valid(ip, udp, 15));
    assert(memcmp(udp + 8, payload, sizeof(payload)) == 0);

    /* Exercise the emitted wire frame through a separately initialized RX
     * stack, including checksum validation and UDP callback delivery. */
    assert(netif_init(&receiver_interface, "rx0", 2, remote_mac, 1500,
                      &capture, transmit, lock_noop, unlock_noop) == 0);
    assert(netif_set_link(&receiver_interface, 1) == 0);
    net_stack_init(&receiver_stack, receive_udp, &received);
    assert(net_stack_set_ipv4_address(&receiver_stack, remote_ip) == 0);
    struct zd_fw_rule allow_inbound = {
        .dir = ZD_FW_IN, .proto = ZD_FW_UDP, .action = ZD_FW_ALLOW
    };
    assert(net_fw_add(&receiver_stack.ipv4_firewall, &allow_inbound, 0) == 0);
    struct zd_fw_rule deny_inbound_dns = {
        .dir = ZD_FW_IN, .proto = ZD_FW_UDP, .action = ZD_FW_DENY,
        .port_lo = 53, .port_hi = 53
    };
    assert(net_fw_add(&receiver_stack.ipv4_firewall, &deny_inbound_dns, 1) == 0);
    assert(net_stack_input(&receiver_stack, &receiver_interface, capture.frame,
                           capture.length) == 1);
    assert(received.count == 0 && receiver_stack.stats.policy_drops == 1);
    assert(net_fw_remove(&receiver_stack.ipv4_firewall,
                         receiver_stack.ipv4_firewall.rules[0].id) == 0);
    assert(net_stack_input(&receiver_stack, &receiver_interface, capture.frame,
                           capture.length) == 0);
    assert(received.count == 1 && received.source_port == 49152 &&
           received.destination_port == 53 && received.length == sizeof(payload));
    assert(memcmp(received.payload, payload, sizeof(payload)) == 0);

    /* Zero-length datagrams are legal, and still get a nonzero checksum. */
    struct zd_fw_rule deny_ntp = {
        .dir = ZD_FW_OUT, .proto = ZD_FW_UDP, .action = ZD_FW_DENY,
        .port_lo = 7, .port_hi = 7
    };
    assert(net_fw_add(&stack.ipv4_firewall, &deny_ntp, 1) == 0);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   49152, 7, 0, 0) == -3);
    assert(net_fw_remove(&stack.ipv4_firewall,
                         stack.ipv4_firewall.rules[0].id) == 0);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   49152, 7, 0, 0) == 0);
    ip = capture.frame + 14U;
    udp = ip + 20U;
    assert(capture.length == 42 && udp[4] == 0 && udp[5] == 8);
    assert((udp[6] || udp[7]) && udp_checksum_valid(ip, udp, 8));
    assert(net_stack_input(&receiver_stack, &receiver_interface, capture.frame,
                           capture.length) == 0);
    assert(received.count == 2 && received.source_port == 49152 &&
           received.destination_port == 7 && received.length == 0);

    uint8_t too_large[1473] = {0};
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   1, 2, too_large, sizeof(too_large)) == -2);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   0, 2, payload, sizeof(payload)) == -1);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, 0xe0000001U,
                                   1, 2, payload, sizeof(payload)) == -1);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, 0x7f000001U,
                                   1, 2, payload, sizeof(payload)) == -1);
    assert(stack.stats.udp_transmitted == 2 && stack.stats.transmit_errors == 4);

    capture.fail = 1;
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   1, 2, payload, sizeof(payload)) == -2);
    assert(interface.stats.tx_dropped == 1);
    assert(stack.stats.udp_transmitted == 2 && stack.stats.transmit_errors == 5);
    capture.fail = 0;
    sandbox_enforce_all(&stack.sandbox);
    uint64_t tx_before_sandbox=stack.stats.udp_transmitted;
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   1, 2, payload, sizeof(payload)) == -3);
    assert(stack.stats.udp_transmitted==tx_before_sandbox);
    struct zd_sandbox_rule allow_interface = {
        .action=ZD_SANDBOX_ALLOW, .resource=ZD_SANDBOX_RES_NETWORK
    };
    memcpy(allow_interface.target,"tx0",4);
    assert(sandbox_add_rule(&stack.sandbox,&allow_interface)==0);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   1, 2, payload, sizeof(payload)) == 0);
    assert(stack.stats.udp_transmitted==tx_before_sandbox+1);
    assert(netif_set_link(&interface, 0) == 0);
    assert(net_stack_send_udp_ipv4(&stack, &interface, remote_mac, remote_ip,
                                   1, 2, payload, sizeof(payload)) == -1);
    assert(stack.stats.transmit_errors == 6);
    return 0;
}
